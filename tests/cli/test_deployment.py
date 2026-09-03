from __future__ import annotations

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest
import yaml
from aster_cli.cli import main
from aster_cli.deployment import (
    BUNDLE_NAME,
    STATE_NAME,
    DeploymentError,
    apply_deployment,
    deployment_status,
    load_deployment_bundle,
    plan_deployment,
    verify_deployment_bundle,
    write_deployment_bundle,
)
from aster_cli.emitters import emit_deployment
from aster_cli.models import load_inventory
from aster_cli.validation import ValidationError
from conftest import create_workspace, write


def _local_inventory(root: Path) -> Path:
    return write(
        root,
        "inventory.yaml",
        f"""api_version: aster.dev/v1alpha2
kind: Inventory
metadata: {{name: test-inventory}}
spec:
  hosts:
    mcu: {{transport: local, serial_number: DEV-C-001, deploy_root: {root / "deploy/mcu"}}}
    soc: {{transport: local, serial_number: SOC-001, deploy_root: {root / "deploy/soc"}}}
""",
    )


def _ssh_inventory(root: Path) -> Path:
    return write(
        root,
        "inventory.ssh.yaml",
        """api_version: aster.dev/v1alpha2
kind: Inventory
metadata: {name: ssh-inventory}
spec:
  hosts:
    mcu: {transport: ssh, address: deploy@robot-mcu, deploy_root: /opt/aster/mcu}
    soc: {transport: ssh, address: deploy@robot-soc, deploy_root: /opt/aster/soc}
""",
    )


def _generated(root: Path) -> tuple[Path, Path, Path]:
    workspace, _, _, deployment = create_workspace(root)
    output = root / "generated"
    emit_deployment(workspace, deployment, output)
    return workspace, deployment, output


def test_codegen_writes_a_deterministic_versioned_bundle(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    first_bytes = (output / BUNDLE_NAME).read_bytes()
    first = verify_deployment_bundle(output)

    workspace = tmp_path / "workspace.yaml"
    emit_deployment(workspace, deployment, output)
    second = load_deployment_bundle(output / BUNDLE_NAME, verify_files=True)

    assert (output / BUNDLE_NAME).read_bytes() == first_bytes
    assert first == second
    assert first.deployment_lock.path == "deployment.lock.yaml"
    assert first.files == tuple(sorted(first.files, key=lambda item: item.path))
    assert all(len(item.sha256) == 64 and item.size >= 0 for item in first.files)
    document = yaml.safe_load(first_bytes)
    assert document["format_version"] == 1
    payload = dict(document)
    expected = payload.pop("bundle_digest")
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode()
    assert hashlib.sha256(encoded).hexdigest() == expected


def test_bundle_verification_rejects_tamper_missing_and_unexpected_files(
    tmp_path: Path,
) -> None:
    _, _, output = _generated(tmp_path)
    target = output / "nodes/controller-node/composition.generated.cpp"
    original = target.read_text(encoding="utf-8")

    target.write_text(original + "// tampered\n", encoding="utf-8")
    with pytest.raises(DeploymentError, match="(size|digest) mismatch"):
        verify_deployment_bundle(output)
    target.write_text(original, encoding="utf-8")

    target.unlink()
    with pytest.raises(DeploymentError, match="file set mismatch"):
        verify_deployment_bundle(output)
    target.write_text(original, encoding="utf-8")

    (output / "unexpected.bin").write_bytes(b"unexpected")
    with pytest.raises(DeploymentError, match="unexpected: unexpected.bin"):
        verify_deployment_bundle(output)


def test_bundle_rejects_symlinks_and_unsafe_manifest_paths(tmp_path: Path) -> None:
    _, _, output = _generated(tmp_path)
    outside = tmp_path / "outside"
    outside.write_text("outside", encoding="utf-8")
    (output / "link").symlink_to(outside)
    with pytest.raises(DeploymentError, match="symlink"):
        write_deployment_bundle(output)
    (output / "link").unlink()

    manifest = yaml.safe_load((output / BUNDLE_NAME).read_text(encoding="utf-8"))
    manifest["files"][0]["path"] = "../deployment.lock.yaml"
    (output / BUNDLE_NAME).write_text(yaml.safe_dump(manifest), encoding="utf-8")
    with pytest.raises(ValidationError, match="does not match"):
        load_deployment_bundle(output / BUNDLE_NAME)


def test_local_apply_is_staged_verified_atomic_and_keeps_previous(
    tmp_path: Path,
) -> None:
    _, deployment, output = _generated(tmp_path)
    inventory = _local_inventory(tmp_path)
    first_plan = plan_deployment(deployment, inventory, output)

    assert apply_deployment(first_plan) == []
    assert not (tmp_path / "deploy").exists()
    results = apply_deployment(first_plan, execute=True)
    assert {item["bundle_digest"] for item in results} == {first_plan.bundle.bundle_digest}

    for action in first_plan.actions:
        node_root = Path(action.destination)
        assert node_root.joinpath("current").is_symlink()
        assert os.readlink(node_root / "current") == f"releases/{first_plan.bundle.bundle_digest}"
        assert not node_root.joinpath("previous").exists()
        release = node_root / os.readlink(node_root / "current")
        assert (release / BUNDLE_NAME).is_file()
        assert not list(node_root.glob(".staging-*"))

    rows = deployment_status(inventory)
    assert {row["deployment_id"] for row in rows} == {first_plan.bundle.deployment_id}
    assert {row["bundle_digest"] for row in rows} == {first_plan.bundle.bundle_digest}

    changed = output / "nodes/controller-node/composition.generated.cpp"
    changed.write_text(changed.read_text(encoding="utf-8") + "// next\n", encoding="utf-8")
    second_bundle = write_deployment_bundle(output)
    assert second_bundle.bundle_digest != first_plan.bundle.bundle_digest
    second_plan = plan_deployment(deployment, inventory, output)
    apply_deployment(second_plan, execute=True)

    for action in second_plan.actions:
        node_root = Path(action.destination)
        assert os.readlink(node_root / "current") == f"releases/{second_bundle.bundle_digest}"
        assert os.readlink(node_root / "previous") == (
            f"releases/{first_plan.bundle.bundle_digest}"
        )
        state = yaml.safe_load((node_root.parent / STATE_NAME).read_text(encoding="utf-8"))
        entry = state["deployments"][action.node]
        assert entry["current"]["bundle_digest"] == second_bundle.bundle_digest
        assert entry["previous"]["bundle_digest"] == first_plan.bundle.bundle_digest
        assert "address" not in state and "password" not in state


def test_apply_rechecks_bundle_after_plan(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    plan = plan_deployment(deployment, _local_inventory(tmp_path), output)
    source = output / "nodes/sensor-node/composition.generated.cpp"
    source.write_text("changed after plan", encoding="utf-8")

    with pytest.raises(DeploymentError, match="(size|digest) mismatch"):
        apply_deployment(plan, execute=True)
    assert not (tmp_path / "deploy").exists()


def test_cli_plan_verifies_bundle_and_apply_defaults_to_read_only(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    _, deployment, output = _generated(tmp_path)
    inventory = _local_inventory(tmp_path)
    arguments = [str(deployment), str(inventory), str(output)]

    assert main(["deploy", "plan", *arguments]) == 0
    plan_output = capsys.readouterr().out
    assert '"bundle_digest"' in plan_output
    assert '"serial_number": "DEV-C-001"' in plan_output
    assert main(["deploy", "apply", *arguments]) == 0
    assert "pass --execute" in capsys.readouterr().out
    assert not (tmp_path / "deploy").exists()


def test_inventory_serial_number_is_loaded_validated_and_planned(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    inventory_path = _local_inventory(tmp_path)

    inventory = load_inventory(inventory_path)
    assert {host.name: host.serial_number for host in inventory.hosts} == {
        "mcu": "DEV-C-001",
        "soc": "SOC-001",
    }
    actions = plan_deployment(deployment, inventory_path, output).as_mapping()["actions"]
    assert {action["target"]: action["serial_number"] for action in actions} == {
        "mcu": "DEV-C-001",
        "soc": "SOC-001",
    }

    document = yaml.safe_load(inventory_path.read_text(encoding="utf-8"))
    document["spec"]["hosts"]["mcu"]["serial_number"] = "contains whitespace"
    inventory_path.write_text(yaml.safe_dump(document, sort_keys=False), encoding="utf-8")
    with pytest.raises(ValidationError, match="serial_number"):
        load_inventory(inventory_path)


def test_serial_and_debug_probe_are_planned_but_apply_is_rejected(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    inventory = write(
        tmp_path,
        "inventory.unsupported.yaml",
        """api_version: aster.dev/v1alpha2
kind: Inventory
metadata: {name: unsupported}
spec:
  hosts:
    mcu: {transport: debug-probe, address: dev_c}
    soc: {transport: serial, address: /dev/ttyACM0}
""",
    )
    plan = plan_deployment(deployment, inventory, output)
    assert {action.transport for action in plan.actions} == {"debug-probe", "serial"}
    assert all(not action.apply_supported for action in plan.actions)
    assert all(action.blocked_reason for action in plan.actions)
    with pytest.raises(DeploymentError, match="unsupported targets"):
        apply_deployment(plan, execute=True)


def test_inventory_forbids_credentials_and_unsafe_ssh_values(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    credentials = write(
        tmp_path,
        "inventory.credentials.yaml",
        """api_version: aster.dev/v1alpha2
kind: Inventory
metadata: {name: unsafe}
spec:
  hosts:
    mcu: {transport: ssh, address: robot, deploy_root: /opt/aster, password: secret}
    soc: {transport: ssh, address: robot, deploy_root: /opt/aster}
""",
    )
    with pytest.raises(ValidationError, match="password"):
        plan_deployment(deployment, credentials, output)

    unsafe = credentials.with_name("inventory.unsafe-address.yaml")
    unsafe.write_text(
        credentials.read_text(encoding="utf-8")
        .replace(", password: secret", "")
        .replace("address: robot", "address: 'robot; reboot'", 1),
        encoding="utf-8",
    )
    with pytest.raises(ValidationError, match="does not match"):
        plan_deployment(deployment, unsafe, output)


class _SshRunner:
    def __init__(self, actions) -> None:
        self.actions = actions
        self.calls: list[tuple[list[str], dict[str, object]]] = []

    def __call__(self, argv, **kwargs):
        assert isinstance(argv, list)
        assert kwargs["shell"] is False
        self.calls.append((argv, kwargs))
        command = argv[5] if argv[0] == "ssh" else "scp"
        stdout = ""
        returncode = 0
        if command in ("test", "cat", "readlink"):
            returncode = 1
        elif command == "mktemp":
            stdout = argv[-1].replace("XXXXXX", "ABC123") + "\n"
        elif command == "sha256sum":
            remote = argv[-1]
            expected = None
            for action in self.actions:
                if remote.endswith(f"/{BUNDLE_NAME}"):
                    expected = action.manifest_sha256
                for item in action.files:
                    if remote.endswith(f"/{item.path}"):
                        expected = item.sha256
            assert expected is not None
            stdout = f"{expected}  {remote}\n"
        return subprocess.CompletedProcess(argv, returncode, stdout=stdout, stderr="")


def test_ssh_adapter_is_argv_only_and_requires_execute(tmp_path: Path) -> None:
    _, deployment, output = _generated(tmp_path)
    inventory = _ssh_inventory(tmp_path)
    plan = plan_deployment(deployment, inventory, output)
    runner = _SshRunner(plan.actions)

    mapping = plan.as_mapping()
    assert all(action["transport"] == "ssh" for action in mapping["actions"])
    assert all(action["adapter"]["ssh_argv_prefix"][0] == "ssh" for action in mapping["actions"])
    assert apply_deployment(plan, runner=runner) == []
    assert deployment_status(inventory, runner=runner)[0]["status"] == "query-requires-execute"
    assert runner.calls == []

    results = apply_deployment(plan, execute=True, runner=runner)
    assert len(results) == 2
    assert {call[0][0] for call in runner.calls} == {"ssh", "scp"}
    assert all(not isinstance(call[0], str) for call in runner.calls)
