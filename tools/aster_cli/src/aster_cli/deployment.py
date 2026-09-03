"""Verified Deployment Bundles and explicit deployment Adapters."""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

import yaml

from .models import InventoryHost, load_deployment, load_inventory
from .validation import canonical_json, dump_yaml, validate_document, validate_mapping

BUNDLE_NAME = "deployment.bundle.yaml"
STATE_NAME = ".aster-deploy-state.yaml"
_SSH_ADDRESS_PATTERN = re.compile(r"^(?:[A-Za-z0-9][A-Za-z0-9._-]*@)?[A-Za-z0-9][A-Za-z0-9.-]*$")
_REMOTE_PART_PATTERN = re.compile(r"^[A-Za-z0-9._-]+$")


class DeploymentError(ValueError):
    """Raised when an artifact cannot pass the deployment gate."""


@dataclass(frozen=True, slots=True)
class BundleFile:
    path: str
    sha256: str
    size: int

    def as_mapping(self) -> dict[str, str | int]:
        return {"path": self.path, "sha256": self.sha256, "size": self.size}


@dataclass(frozen=True, slots=True)
class DeploymentBundle:
    name: str
    deployment_id: str
    deployment_lock: BundleFile
    files: tuple[BundleFile, ...]
    bundle_digest: str
    source: Path

    @property
    def root(self) -> Path:
        return self.source.parent


@dataclass(frozen=True, slots=True)
class ReleaseState:
    deployment_id: str
    bundle_digest: str
    release: str

    def as_mapping(self) -> dict[str, str]:
        return {
            "deployment_id": self.deployment_id,
            "bundle_digest": self.bundle_digest,
            "release": self.release,
        }


@dataclass(frozen=True, slots=True)
class NodeDeploymentState:
    current: ReleaseState
    previous: ReleaseState | None


@dataclass(frozen=True, slots=True)
class DeploymentState:
    name: str
    deployments: dict[str, NodeDeploymentState]


@dataclass(frozen=True, slots=True)
class DeploymentAction:
    host: str
    node: str
    target: str
    transport: str
    source_root: Path
    destination: str
    address: str | None
    deployment_id: str
    bundle_digest: str
    files: tuple[BundleFile, ...]
    manifest_sha256: str
    serial_number: str | None = None
    apply_supported: bool = True
    blocked_reason: str | None = None

    def as_mapping(self) -> dict[str, Any]:
        result: dict[str, Any] = {
            "host": self.host,
            "node": self.node,
            "target": self.target,
            "transport": self.transport,
            "source": str((self.source_root / "nodes" / self.node).resolve()),
            "destination": self.destination,
            "deployment_id": self.deployment_id,
            "bundle_digest": self.bundle_digest,
            "files": [item.as_mapping() for item in self.files],
            "apply_supported": self.apply_supported,
        }
        if self.blocked_reason is not None:
            result["blocked_reason"] = self.blocked_reason
        if self.serial_number is not None:
            result["serial_number"] = self.serial_number
        if self.transport == "ssh":
            result["adapter"] = {
                "ssh_argv_prefix": list(_ssh_prefix(self.address or "")),
                "scp_argv_prefix": ["scp", "-q", "--"],
                "staging_template": f"{self.destination}/.staging-{self.bundle_digest}.XXXXXX",
                "authentication": "external-ssh-agent-or-config",
            }
        return result


@dataclass(frozen=True, slots=True)
class DeploymentPlan:
    bundle: DeploymentBundle
    actions: tuple[DeploymentAction, ...]

    def as_mapping(self) -> dict[str, Any]:
        return {
            "mode": "plan",
            "deployment_id": self.bundle.deployment_id,
            "bundle_digest": self.bundle.bundle_digest,
            "actions": [item.as_mapping() for item in self.actions],
        }


Runner = Callable[..., subprocess.CompletedProcess[str]]


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _record(root: Path, path: Path) -> BundleFile:
    relative = path.relative_to(root).as_posix()
    return BundleFile(relative, _sha256(path), path.stat().st_size)


def _bundle_payload(document: dict[str, Any]) -> dict[str, Any]:
    payload = dict(document)
    payload.pop("bundle_digest", None)
    return payload


def _bundle_digest(document: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(_bundle_payload(document))).hexdigest()


def _atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    temporary.write_text(content, encoding="utf-8", newline="\n")
    temporary.replace(path)


def _safe_bundle_path(root: Path, relative: str) -> Path:
    pure = PurePosixPath(relative)
    if pure.is_absolute() or not pure.parts or any(part in ("", ".", "..") for part in pure.parts):
        raise DeploymentError(f"unsafe Deployment Bundle path {relative!r}")
    current = root
    for part in pure.parts:
        current = current / part
        if current.is_symlink():
            raise DeploymentError(f"Deployment Bundle path {relative!r} traverses a symlink")
    try:
        current.resolve().relative_to(root.resolve())
    except ValueError as error:
        raise DeploymentError(f"Deployment Bundle path {relative!r} escapes its root") from error
    return current


def _file_matches(root: Path, item: BundleFile) -> None:
    path = _safe_bundle_path(root, item.path)
    if not path.is_file():
        raise DeploymentError(f"Deployment Bundle file is missing: {item.path}")
    size = path.stat().st_size
    if size != item.size:
        raise DeploymentError(
            f"Deployment Bundle size mismatch for {item.path}: expected {item.size}, got {size}"
        )
    digest = _sha256(path)
    if digest != item.sha256:
        raise DeploymentError(
            f"Deployment Bundle digest mismatch for {item.path}: "
            f"expected {item.sha256}, got {digest}"
        )


def _bundle_file(value: dict[str, Any]) -> BundleFile:
    return BundleFile(str(value["path"]), str(value["sha256"]), int(value["size"]))


def write_deployment_bundle(root: str | Path) -> DeploymentBundle:
    """Write a deterministic manifest for every regular artifact below ``root``."""

    bundle_root = Path(root).resolve()
    lock_path = bundle_root / "deployment.lock.yaml"
    if not lock_path.is_file() or lock_path.is_symlink():
        raise DeploymentError(f"Deployment Bundle requires a regular {lock_path.name}")
    lock = validate_document(lock_path)
    if lock["kind"] != "DeploymentLock":
        raise DeploymentError(f"{lock_path}: expected DeploymentLock")

    manifest_path = bundle_root / BUNDLE_NAME
    files: list[BundleFile] = []
    paths = sorted(
        bundle_root.rglob("*"),
        key=lambda item: item.relative_to(bundle_root).as_posix(),
    )
    for path in paths:
        if path == manifest_path:
            continue
        if path.is_symlink():
            raise DeploymentError(
                f"Deployment Bundle cannot contain symlink {path.relative_to(bundle_root)}"
            )
        if path.is_file():
            files.append(_record(bundle_root, path))
    lock_record = next((item for item in files if item.path == "deployment.lock.yaml"), None)
    if lock_record is None:
        raise DeploymentError("Deployment Bundle did not include deployment.lock.yaml")

    document: dict[str, Any] = {
        "api_version": "aster.dev/v1alpha2",
        "kind": "DeploymentBundle",
        "metadata": {"name": lock["metadata"]["name"]},
        "format_version": 1,
        "deployment_id": lock["deployment_id"],
        "deployment_lock": lock_record.as_mapping(),
        "files": [item.as_mapping() for item in files],
    }
    document["bundle_digest"] = _bundle_digest(document)
    validate_mapping(document, "deployment-bundle.schema.json", str(manifest_path))
    _atomic_write(manifest_path, dump_yaml(document))
    return load_deployment_bundle(manifest_path, verify_files=True)


def load_deployment_bundle(path: str | Path, *, verify_files: bool = False) -> DeploymentBundle:
    source = Path(path).resolve()
    document = validate_document(source)
    if document["kind"] != "DeploymentBundle":
        raise DeploymentError(f"{source}: expected DeploymentBundle")
    if _bundle_digest(document) != document["bundle_digest"]:
        raise DeploymentError(f"{source}: bundle_digest does not match the manifest")

    files = tuple(_bundle_file(item) for item in document["files"])
    paths = [item.path for item in files]
    if paths != sorted(paths) or len(paths) != len(set(paths)):
        raise DeploymentError(f"{source}: files must use unique, sorted paths")
    lock = _bundle_file(document["deployment_lock"])
    if lock.path != "deployment.lock.yaml" or files.count(lock) != 1:
        raise DeploymentError(f"{source}: deployment_lock must match the listed lock file")

    bundle = DeploymentBundle(
        document["metadata"]["name"],
        document["deployment_id"],
        lock,
        files,
        document["bundle_digest"],
        source,
    )
    if verify_files:
        _verify_bundle_files(bundle)
    return bundle


def _verify_bundle_files(bundle: DeploymentBundle) -> None:
    root = bundle.root
    expected = {item.path for item in bundle.files}
    actual: set[str] = set()
    for path in root.rglob("*"):
        if path == bundle.source:
            continue
        if path.is_symlink():
            raise DeploymentError(
                f"Deployment Bundle cannot contain symlink {path.relative_to(root)}"
            )
        if path.is_file():
            actual.add(path.relative_to(root).as_posix())
    if actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        details = []
        if missing:
            details.append(f"missing: {', '.join(missing)}")
        if unexpected:
            details.append(f"unexpected: {', '.join(unexpected)}")
        raise DeploymentError(f"Deployment Bundle file set mismatch ({'; '.join(details)})")
    for item in bundle.files:
        _file_matches(root, item)

    lock = validate_document(root / bundle.deployment_lock.path)
    if lock["kind"] != "DeploymentLock" or lock["deployment_id"] != bundle.deployment_id:
        raise DeploymentError(
            "Deployment Bundle and deployment.lock.yaml identify different deployments"
        )


def verify_deployment_bundle(root: str | Path) -> DeploymentBundle:
    return load_deployment_bundle(Path(root) / BUNDLE_NAME, verify_files=True)


def _safe_remote_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if not path.is_absolute() or len(path.parts) < 2:
        raise DeploymentError(f"SSH deploy_root must be a non-root absolute path: {value!r}")
    unsafe = any(
        part in ("", ".", "..") or not _REMOTE_PART_PATTERN.fullmatch(part)
        for part in path.parts[1:]
    )
    if unsafe:
        raise DeploymentError(f"unsafe SSH deployment path {value!r}")
    return path


def _ssh_prefix(address: str) -> tuple[str, ...]:
    if not _SSH_ADDRESS_PATTERN.fullmatch(address) or address.startswith("-"):
        raise DeploymentError(f"unsafe SSH address {address!r}")
    return ("ssh", "-o", "BatchMode=yes", "--", address)


def _ssh_argv(action: DeploymentAction, *remote: str) -> list[str]:
    return [*_ssh_prefix(action.address or ""), *remote]


def _scp_argv(action: DeploymentAction, source: Path, destination: PurePosixPath) -> list[str]:
    _safe_remote_path(str(destination))
    address = action.address or ""
    _ssh_prefix(address)
    return ["scp", "-q", "--", str(source), f"{address}:{destination}"]


def plan_deployment(
    deployment_path: str | Path, inventory_path: str | Path, artifacts: str | Path
) -> DeploymentPlan:
    bundle = verify_deployment_bundle(artifacts)
    deployment = load_deployment(deployment_path)
    inventory = load_inventory(inventory_path)
    lock = validate_document(bundle.root / bundle.deployment_lock.path)
    locked_nodes = lock["nodes"]
    targets = {item.name: item for item in inventory.hosts}
    hosts = {item.name: item for item in deployment.hosts}
    manifest_sha256 = _sha256(bundle.source)
    actions: list[DeploymentAction] = []

    for node in deployment.nodes:
        if node.name not in locked_nodes or locked_nodes[node.name]["host"] != node.host:
            raise DeploymentError(f"node {node.name!r} does not match the Deployment Lock")
        host = hosts[node.host]
        target_name = host.inventory or host.name
        target = targets.get(target_name)
        if target is None:
            raise DeploymentError(f"host {host.name!r} has no inventory target {target_name!r}")
        apply_supported = target.transport in ("local", "ssh")
        blocked_reason = None
        if not apply_supported:
            blocked_reason = (
                f"{target.transport} flashing is not a v0.2 deployment Adapter; "
                "use the emitted firmware with the board runner"
            )
        if apply_supported and not target.deploy_root:
            raise DeploymentError(
                f"inventory target {target.name!r} requires deploy_root for {target.transport}"
            )
        selected = tuple(
            item
            for item in bundle.files
            if item.path == bundle.deployment_lock.path
            or item.path.startswith(f"nodes/{node.name}/")
        )
        if len(selected) < 2:
            raise DeploymentError(f"Deployment Bundle has no artifacts for node {node.name!r}")
        if target.transport == "ssh":
            _ssh_prefix(target.address or "")
            destination = str(_safe_remote_path(target.deploy_root) / node.name)
            for item in selected:
                _safe_remote_path(str(PurePosixPath(destination) / item.path))
            _safe_remote_path(str(PurePosixPath(destination) / BUNDLE_NAME))
        elif target.transport == "local":
            deploy_root = Path(target.deploy_root).resolve()
            if deploy_root == Path(deploy_root.anchor):
                raise DeploymentError("local deploy_root cannot be a filesystem root")
            destination = str(deploy_root / node.name)
        else:
            destination = ""
        actions.append(
            DeploymentAction(
                host=host.name,
                node=node.name,
                target=target_name,
                transport=target.transport,
                source_root=bundle.root,
                destination=destination,
                address=target.address,
                deployment_id=bundle.deployment_id,
                bundle_digest=bundle.bundle_digest,
                files=selected,
                manifest_sha256=manifest_sha256,
                serial_number=target.serial_number,
                apply_supported=apply_supported,
                blocked_reason=blocked_reason,
            )
        )
    return DeploymentPlan(bundle, tuple(actions))


def _release_mapping(state: ReleaseState) -> dict[str, str]:
    return state.as_mapping()


def _state_mapping(state: DeploymentState) -> dict[str, Any]:
    return {
        "api_version": "aster.dev/v1alpha2",
        "kind": "DeploymentState",
        "metadata": {"name": state.name},
        "deployments": {
            name: {
                "current": _release_mapping(item.current),
                "previous": _release_mapping(item.previous) if item.previous else None,
            }
            for name, item in sorted(state.deployments.items())
        },
    }


def _parse_state(document: dict[str, Any], source: str) -> DeploymentState:
    validate_mapping(document, "deployment-state.schema.json", source)
    deployments = {
        name: NodeDeploymentState(
            ReleaseState(**item["current"]),
            ReleaseState(**item["previous"]) if item["previous"] else None,
        )
        for name, item in sorted(document["deployments"].items())
    }
    return DeploymentState(document["metadata"]["name"], deployments)


def _empty_state(name: str) -> DeploymentState:
    return DeploymentState(name, {})


def _load_local_state(path: Path, name: str) -> DeploymentState:
    if not path.exists():
        return _empty_state(name)
    if not path.is_file() or path.is_symlink():
        raise DeploymentError(f"invalid deployment state path {path}")
    try:
        document = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        raise DeploymentError(f"cannot read deployment state {path}: {error}") from error
    if not isinstance(document, dict):
        raise DeploymentError(f"invalid deployment state {path}")
    state = _parse_state(document, str(path))
    if state.name != name:
        raise DeploymentError(f"deployment state {path} belongs to {state.name!r}, not {name!r}")
    return state


def _updated_state(
    state: DeploymentState, action: DeploymentAction
) -> tuple[DeploymentState, ReleaseState | None]:
    existing = state.deployments.get(action.node)
    current = ReleaseState(
        action.deployment_id,
        action.bundle_digest,
        f"releases/{action.bundle_digest}",
    )
    previous = existing.previous if existing else None
    if existing and existing.current.bundle_digest != current.bundle_digest:
        previous = existing.current
    deployments = dict(state.deployments)
    deployments[action.node] = NodeDeploymentState(current, previous)
    return DeploymentState(state.name, deployments), previous


def _copy_payload(action: DeploymentAction, destination: Path) -> None:
    for item in action.files:
        source = _safe_bundle_path(action.source_root, item.path)
        target = destination / Path(*PurePosixPath(item.path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    shutil.copy2(action.source_root / BUNDLE_NAME, destination / BUNDLE_NAME)


def _verify_payload(action: DeploymentAction, root: Path) -> None:
    expected = {item.path for item in action.files} | {BUNDLE_NAME}
    actual: set[str] = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise DeploymentError(f"staged payload cannot contain symlink {path.relative_to(root)}")
        if path.is_file():
            actual.add(path.relative_to(root).as_posix())
    if actual != expected:
        raise DeploymentError("staged Deployment Bundle file set mismatch")
    for item in action.files:
        _file_matches(root, item)
    manifest = root / BUNDLE_NAME
    invalid_manifest = (
        not manifest.is_file()
        or manifest.is_symlink()
        or _sha256(manifest) != action.manifest_sha256
    )
    if invalid_manifest:
        raise DeploymentError("staged Deployment Bundle manifest digest mismatch")


def _read_relative_symlink(path: Path) -> str | None:
    if not path.exists() and not path.is_symlink():
        return None
    if not path.is_symlink():
        raise DeploymentError(f"deployment activation path is not a symlink: {path}")
    target = os.readlink(path)
    if not re.fullmatch(r"releases/[0-9a-f]{64}", target):
        raise DeploymentError(f"deployment activation symlink has unsafe target {target!r}")
    return target


def _atomic_symlink(target: str, path: Path) -> None:
    suffix = hashlib.sha256(target.encode()).hexdigest()[:12]
    temporary = path.with_name(f".{path.name}.{suffix}.tmp")
    if temporary.exists() or temporary.is_symlink():
        temporary.unlink()
    temporary.symlink_to(target)
    os.replace(temporary, path)


def _restore_symlink(path: Path, target: str | None) -> None:
    if target is None:
        if path.is_symlink():
            path.unlink()
        return
    _atomic_symlink(target, path)


def _apply_local(action: DeploymentAction) -> None:
    node_root = Path(action.destination)
    node_root.mkdir(parents=True, exist_ok=True)
    releases = node_root / "releases"
    releases.mkdir(exist_ok=True)
    release = releases / action.bundle_digest
    if release.exists():
        if not release.is_dir() or release.is_symlink():
            raise DeploymentError(f"invalid release path {release}")
        _verify_payload(action, release)
    else:
        stage = Path(
            tempfile.mkdtemp(prefix=f".staging-{action.bundle_digest[:12]}-", dir=node_root)
        )
        try:
            _copy_payload(action, stage)
            _verify_payload(action, stage)
            stage.replace(release)
        finally:
            if stage.exists():
                shutil.rmtree(stage)

    state_path = node_root.parent / STATE_NAME
    state = _load_local_state(state_path, action.target)
    updated, previous = _updated_state(state, action)
    current_link = node_root / "current"
    previous_link = node_root / "previous"
    old_current = _read_relative_symlink(current_link)
    old_previous = _read_relative_symlink(previous_link)
    try:
        if previous is not None:
            _atomic_symlink(previous.release, previous_link)
        _atomic_symlink(f"releases/{action.bundle_digest}", current_link)
        _atomic_write(state_path, dump_yaml(_state_mapping(updated)))
    except Exception:
        _restore_symlink(current_link, old_current)
        _restore_symlink(previous_link, old_previous)
        raise


def _invoke(
    runner: Runner,
    argv: list[str],
    *,
    check: bool = True,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    return runner(
        argv,
        check=check,
        capture_output=capture_output,
        text=True,
        shell=False,
    )


def _require_remote_success(result: subprocess.CompletedProcess[str], argv: list[str]) -> None:
    if result.returncode != 0:
        raise subprocess.CalledProcessError(result.returncode, argv, result.stdout, result.stderr)


def _remote_digest(
    action: DeploymentAction, path: PurePosixPath, expected: str, runner: Runner
) -> None:
    argv = _ssh_argv(action, "sha256sum", "--", str(path))
    result = _invoke(runner, argv, capture_output=True)
    match = re.match(r"^([0-9a-f]{64})(?:\s|$)", result.stdout or "")
    if match is None or match.group(1) != expected:
        actual = match.group(1) if match else "invalid-output"
        raise DeploymentError(
            f"remote Deployment Bundle digest mismatch for {path}: "
            f"expected {expected}, got {actual}"
        )


def _remote_state(action: DeploymentAction, runner: Runner) -> DeploymentState:
    state_path = _safe_remote_path(str(PurePosixPath(action.destination).parent / STATE_NAME))
    argv = _ssh_argv(action, "cat", "--", str(state_path))
    result = _invoke(runner, argv, check=False, capture_output=True)
    if result.returncode == 1:
        return _empty_state(action.target)
    _require_remote_success(result, argv)
    try:
        document = yaml.safe_load(result.stdout)
    except yaml.YAMLError as error:
        raise DeploymentError(f"invalid remote deployment state: {error}") from error
    if not isinstance(document, dict):
        raise DeploymentError("invalid remote deployment state")
    state = _parse_state(document, f"{action.target}:{state_path}")
    if state.name != action.target:
        raise DeploymentError(
            f"remote deployment state belongs to {state.name!r}, not {action.target!r}"
        )
    return state


def _remote_readlink(action: DeploymentAction, path: PurePosixPath, runner: Runner) -> str | None:
    argv = _ssh_argv(action, "readlink", "--", str(path))
    result = _invoke(runner, argv, check=False, capture_output=True)
    if result.returncode == 1:
        return None
    _require_remote_success(result, argv)
    target = (result.stdout or "").strip()
    if not re.fullmatch(r"releases/[0-9a-f]{64}", target):
        raise DeploymentError(f"remote activation symlink has unsafe target {target!r}")
    return target


def _remote_upload(
    action: DeploymentAction,
    source: Path,
    destination: PurePosixPath,
    runner: Runner,
) -> None:
    _invoke(runner, _scp_argv(action, source, destination))


def _apply_ssh(action: DeploymentAction, runner: Runner) -> None:
    node_root = _safe_remote_path(action.destination)
    releases = node_root / "releases"
    release = releases / action.bundle_digest
    _invoke(runner, _ssh_argv(action, "mkdir", "-p", "--", str(releases)))
    exists_argv = _ssh_argv(action, "test", "-d", str(release))
    exists = _invoke(runner, exists_argv, check=False)
    if exists.returncode not in (0, 1):
        _require_remote_success(exists, exists_argv)

    if exists.returncode == 1:
        template = node_root / f".staging-{action.bundle_digest}.XXXXXX"
        stage_argv = _ssh_argv(action, "mktemp", "-d", str(template))
        stage_result = _invoke(runner, stage_argv, capture_output=True)
        stage = _safe_remote_path((stage_result.stdout or "").strip())
        if stage.parent != node_root or not stage.name.startswith(
            f".staging-{action.bundle_digest}."
        ):
            raise DeploymentError(f"SSH mktemp returned unsafe staging path {stage}")
        try:
            parents = {
                stage / PurePosixPath(item.path).parent
                for item in action.files
                if PurePosixPath(item.path).parent != PurePosixPath(".")
            }
            for parent in sorted(parents, key=str):
                _invoke(runner, _ssh_argv(action, "mkdir", "-p", "--", str(parent)))
            for item in action.files:
                destination = stage / item.path
                _remote_upload(
                    action,
                    _safe_bundle_path(action.source_root, item.path),
                    destination,
                    runner,
                )
            _remote_upload(action, action.source_root / BUNDLE_NAME, stage / BUNDLE_NAME, runner)
            for item in action.files:
                _remote_digest(action, stage / item.path, item.sha256, runner)
            _remote_digest(
                action,
                stage / BUNDLE_NAME,
                action.manifest_sha256,
                runner,
            )
            _invoke(runner, _ssh_argv(action, "mv", "--", str(stage), str(release)))
        except Exception:
            _invoke(runner, _ssh_argv(action, "rm", "-rf", "--", str(stage)), check=False)
            raise
    else:
        for item in action.files:
            _remote_digest(action, release / item.path, item.sha256, runner)
        _remote_digest(
            action,
            release / BUNDLE_NAME,
            action.manifest_sha256,
            runner,
        )

    state = _remote_state(action, runner)
    updated, previous = _updated_state(state, action)
    current_path = node_root / "current"
    previous_path = node_root / "previous"
    old_current = _remote_readlink(action, current_path, runner)
    if previous is not None:
        previous_tmp = node_root / f".previous-{action.bundle_digest}.tmp"
        _invoke(
            runner,
            _ssh_argv(action, "ln", "-s", "--", previous.release, str(previous_tmp)),
        )
        _invoke(
            runner,
            _ssh_argv(action, "mv", "-Tf", "--", str(previous_tmp), str(previous_path)),
        )
    elif old_current is not None and old_current != f"releases/{action.bundle_digest}":
        previous_tmp = node_root / f".previous-{action.bundle_digest}.tmp"
        _invoke(runner, _ssh_argv(action, "ln", "-s", "--", old_current, str(previous_tmp)))
        _invoke(
            runner,
            _ssh_argv(action, "mv", "-Tf", "--", str(previous_tmp), str(previous_path)),
        )

    current_tmp = node_root / f".current-{action.bundle_digest}.tmp"
    _invoke(
        runner,
        _ssh_argv(
            action,
            "ln",
            "-s",
            "--",
            f"releases/{action.bundle_digest}",
            str(current_tmp),
        ),
    )
    _invoke(
        runner,
        _ssh_argv(action, "mv", "-Tf", "--", str(current_tmp), str(current_path)),
    )

    remote_state_path = _safe_remote_path(str(node_root.parent / STATE_NAME))
    remote_state_tmp = _safe_remote_path(
        str(node_root.parent / f".{STATE_NAME}.{action.bundle_digest}.tmp")
    )
    with tempfile.TemporaryDirectory(prefix="aster-deploy-state-") as temporary:
        local_state = Path(temporary) / STATE_NAME
        local_state.write_text(dump_yaml(_state_mapping(updated)), encoding="utf-8", newline="\n")
        _remote_upload(action, local_state, remote_state_tmp, runner)
    _invoke(
        runner,
        _ssh_argv(action, "mv", "-f", "--", str(remote_state_tmp), str(remote_state_path)),
    )


def apply_deployment(
    plan: DeploymentPlan,
    *,
    execute: bool = False,
    runner: Runner | None = None,
) -> list[dict[str, str]]:
    """Apply a verified plan. ``execute=False`` is strictly read-only."""

    if not execute:
        return []
    blocked = [action for action in plan.actions if not action.apply_supported]
    if blocked:
        targets = ", ".join(f"{action.target} ({action.transport})" for action in blocked)
        raise DeploymentError(f"deployment apply has unsupported targets: {targets}")
    current_bundle = verify_deployment_bundle(plan.bundle.root)
    if current_bundle.bundle_digest != plan.bundle.bundle_digest:
        raise DeploymentError("Deployment Bundle changed after planning")
    command_runner = runner or subprocess.run
    for action in plan.actions:
        if action.transport == "local":
            _apply_local(action)
        elif action.transport == "ssh":
            _apply_ssh(action, command_runner)
        else:
            raise DeploymentError(f"unsupported deployment Adapter {action.transport!r}")
    return [
        {
            "host": action.host,
            "node": action.node,
            "status": "deployed",
            "deployment_id": action.deployment_id,
            "bundle_digest": action.bundle_digest,
        }
        for action in plan.actions
    ]


def _status_rows(host: InventoryHost, state: DeploymentState) -> list[dict[str, str]]:
    return [
        {
            "host": host.name,
            "transport": host.transport,
            "node": node,
            "status": "deployed",
            "deployment_id": item.current.deployment_id,
            "bundle_digest": item.current.bundle_digest,
            "previous_bundle_digest": item.previous.bundle_digest if item.previous else "",
        }
        for node, item in sorted(state.deployments.items())
    ]


def deployment_status(
    inventory_path: str | Path,
    *,
    execute: bool = False,
    runner: Runner | None = None,
) -> list[dict[str, str]]:
    """Read current Deployment ID and bundle digest for each Inventory target."""

    inventory = load_inventory(inventory_path)
    command_runner = runner or subprocess.run
    rows: list[dict[str, str]] = []
    for host in inventory.hosts:
        if host.transport == "local" and host.deploy_root:
            state = _load_local_state(Path(host.deploy_root).resolve() / STATE_NAME, host.name)
            if state.deployments:
                rows.extend(_status_rows(host, state))
            else:
                rows.append({"host": host.name, "transport": "local", "status": "unknown"})
        elif host.transport == "ssh" and execute:
            _ssh_prefix(host.address or "")
            remote_root = _safe_remote_path(host.deploy_root or "")
            probe = DeploymentAction(
                host=host.name,
                node="status",
                target=host.name,
                transport="ssh",
                source_root=Path(),
                destination=str(remote_root / "status"),
                address=host.address,
                deployment_id="0" * 64,
                bundle_digest="0" * 64,
                files=(),
                manifest_sha256="0" * 64,
                serial_number=host.serial_number,
            )
            state = _remote_state(probe, command_runner)
            if state.deployments:
                rows.extend(_status_rows(host, state))
            else:
                rows.append({"host": host.name, "transport": "ssh", "status": "unknown"})
        elif host.transport == "ssh":
            rows.append(
                {
                    "host": host.name,
                    "transport": "ssh",
                    "status": "query-requires-execute",
                }
            )
        else:
            rows.append(
                {
                    "host": host.name,
                    "transport": host.transport,
                    "status": "unsupported-adapter",
                }
            )
    return rows


__all__ = [
    "BUNDLE_NAME",
    "STATE_NAME",
    "BundleFile",
    "DeploymentAction",
    "DeploymentBundle",
    "DeploymentError",
    "DeploymentPlan",
    "DeploymentState",
    "NodeDeploymentState",
    "ReleaseState",
    "apply_deployment",
    "deployment_status",
    "load_deployment_bundle",
    "plan_deployment",
    "verify_deployment_bundle",
    "write_deployment_bundle",
]
