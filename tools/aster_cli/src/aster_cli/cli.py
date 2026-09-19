"""The single public ``aster`` command."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from . import __version__
from .deployment import (
    DeploymentError,
    apply_deployment,
    deployment_status,
    plan_deployment,
)
from .doctor import doctor_report, format_doctor_text
from .emitters import emit_deployment
from .graph import GraphError, compile_application, resolve_deployment, to_dot
from .models import load_deployment
from .package_codegen import generate_package
from .project import ProjectError, initialize_project
from .protobuf import ProtobufProfileError, generate_bounded_cpp, generate_from_proto
from .validation import ValidationError, dump_yaml, load_yaml, validate_document, validate_mapping


def _print(value: Any, output: Path | None = None) -> None:
    content = (
        json.dumps(value, sort_keys=True, indent=2) + "\n"
        if isinstance(value, dict | list)
        else str(value)
    )
    if output is None:
        print(content, end="")
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="utf-8", newline="\n")


def _validate(args: argparse.Namespace) -> int:
    for path in args.paths:
        document = load_yaml(path)
        if "aster" in document or (
            document.get("api_version") == "aster.dev/v1alpha3" and "kind" not in document
        ):
            status = _invoke_runtime(path, args.runtime, ["--validate-config"])
            if status != 0:
                return status
            continue
        document = validate_document(path)
        print(f"validated {document['kind']} {path}")
    return 0


def _init(args: argparse.Namespace) -> int:
    target = initialize_project(args.directory)
    print(f"initialized {target}")
    return 0


def _doctor(args: argparse.Namespace) -> int:
    report = doctor_report()
    if args.format == "json":
        _print(report)
    else:
        print(format_doctor_text(report), end="")
    return 0 if report["ok"] else 1


def _graph(args: argparse.Namespace) -> int:
    graph = compile_application(args.workspace, args.application, release=args.release)
    content: Any = to_dot(graph) if args.format == "dot" else graph
    _print(content, args.output)
    return 0


def _resolve(args: argparse.Namespace) -> int:
    lock = resolve_deployment(args.workspace, args.deployment, release=args.release)
    validate_mapping(lock, "deployment-lock.schema.json", str(args.output or args.deployment))
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(dump_yaml(lock), encoding="utf-8", newline="\n")
    else:
        print(dump_yaml(lock), end="")
    return 0


def _codegen(args: argparse.Namespace) -> int:
    if args.package_manifest:
        if any(value is not None for value in (args.workspace, args.deployment, args.graph_output)):
            raise ValueError("Package codegen cannot be combined with deployment paths")
        if args.proto_output is None:
            raise ValueError("Package codegen requires --output")
        generate_package(args.package_manifest, args.proto_output)
        print(f"generated Package entry in {args.proto_output}")
        return 0
    if args.descriptor or args.proto:
        if any(value is not None for value in (args.workspace, args.deployment, args.graph_output)):
            raise ValueError("bounded protobuf codegen cannot be combined with graph paths")
        if args.proto_output is None:
            raise ValueError("bounded protobuf codegen requires --output")
        if args.descriptor:
            result = generate_bounded_cpp(args.descriptor, args.proto_output, args.bounds)
        else:
            result = generate_from_proto(
                args.proto, args.proto_output, args.bounds, args.include, args.protoc
            )
        print(f"generated {result.message_count} bounded messages ({result.schema_hash})")
        return 0

    if args.workspace is None or args.deployment is None or args.graph_output is None:
        raise ValueError(
            "deployment codegen requires WORKSPACE DEPLOYMENT OUTPUT, or use --proto/--descriptor"
        )
    lock = emit_deployment(
        args.workspace,
        args.deployment,
        args.graph_output,
        release=args.release,
    )
    print(f"generated {len(lock['nodes'])} nodes in {args.graph_output} ({lock['content_hash']})")
    return 0


def _build_commands(
    workspace: Path, deployment: Path, build_root: Path, generated_root: Path
) -> list[list[str]]:
    model = load_deployment(deployment)
    hosts = {item.name: item for item in model.hosts}
    commands: list[list[str]] = []
    for node in model.nodes:
        host = hosts[node.host]
        build_dir = build_root / node.name
        generated = generated_root / "nodes" / node.name
        if host.os == "zephyr":
            commands.append(
                [
                    "west",
                    "build",
                    "-b",
                    host.board or "",
                    "-d",
                    str(build_dir),
                    str(workspace.parent),
                    "--",
                    "-DASTER_ZEPHYR_BUILD=ON",
                    f"-DASTER_GENERATED_DIR={generated}",
                    f"-DEXTRA_CONF_FILE={generated / 'aster.generated.conf'}",
                    f"-DDTC_OVERLAY_FILE={generated / 'aster.generated.overlay'}",
                ]
            )
        else:
            commands.extend(
                [
                    [
                        "cmake",
                        "-S",
                        str(workspace.parent),
                        "-B",
                        str(build_dir),
                        f"-DASTER_GENERATED_DIR={generated}",
                    ],
                    ["cmake", "--build", str(build_dir)],
                ]
            )
    return commands


def _build(args: argparse.Namespace) -> int:
    resolve_deployment(args.workspace, args.deployment, release=args.release)
    commands = _build_commands(
        args.workspace.resolve(),
        args.deployment.resolve(),
        args.output.resolve(),
        args.generated.resolve(),
    )
    if not args.execute:
        _print({"mode": "plan", "commands": commands})
        return 0
    emit_deployment(
        args.workspace,
        args.deployment,
        args.generated,
        release=args.release,
    )
    for command in commands:
        subprocess.run(command, check=True)
    return 0


def _invoke_runtime(config: Path, executable: Path | None, flags: Sequence[str]) -> int:
    if not config.is_file():
        raise ValueError(f"runtime config does not exist: {config}")
    runtime = str(executable.resolve()) if executable else shutil.which("aster_runtime")
    if runtime is None:
        raise ValueError(
            "aster_runtime is not on PATH; install the Host Runtime or pass --runtime PATH"
        )
    command = [runtime, "--config", str(config.resolve()), *flags]
    try:
        return subprocess.run(command, check=False).returncode
    except OSError as error:
        raise ValueError(f"cannot start Runtime: {error}") from error


def _run(args: argparse.Namespace) -> int:
    flags = ["--check"] if args.check else []
    if args.duration_ms is not None:
        flags.extend(["--duration-ms", str(args.duration_ms)])
    return _invoke_runtime(args.config, args.runtime, flags)


def _deploy_plan(args: argparse.Namespace) -> int:
    plan = plan_deployment(args.deployment, args.inventory, args.artifacts)
    _print(plan.as_mapping())
    return 0


def _deploy_apply(args: argparse.Namespace) -> int:
    plan = plan_deployment(args.deployment, args.inventory, args.artifacts)
    if not args.execute:
        document = plan.as_mapping()
        document["note"] = "pass --execute to stage, verify, and atomically activate"
        _print(document)
        return 0
    _print(apply_deployment(plan, execute=True))
    return 0


def _deploy_status(args: argparse.Namespace) -> int:
    _print(deployment_status(args.inventory, execute=args.execute))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="aster")
    parser.add_argument("--version", action="version", version=__version__)
    commands = parser.add_subparsers(dest="command")

    init = commands.add_parser("init", help="create a minimal Linux Module Package")
    init.add_argument("directory", type=Path)
    init.set_defaults(handler=_init)

    doctor = commands.add_parser("doctor", help="check the local toolchain")
    doctor.add_argument("--format", choices=("text", "json"), default="text")
    doctor.set_defaults(handler=_doctor)

    validate = commands.add_parser("validate", help="validate versioned framework documents")
    validate.add_argument("paths", nargs="+", type=Path)
    validate.add_argument("--runtime", type=Path, help="native parser for runtime.yaml validation")
    validate.set_defaults(handler=_validate)

    graph = commands.add_parser("graph", help="compile a legacy v1alpha2 Application graph")
    graph.add_argument("workspace", type=Path)
    graph.add_argument("application", type=Path)
    graph.add_argument("--format", choices=("json", "dot"), default="json")
    graph.add_argument("--output", type=Path)
    graph.add_argument(
        "--release",
        action="store_true",
        help="require declared bounded protobuf schema hashes",
    )
    graph.set_defaults(handler=_graph)

    resolve = commands.add_parser("resolve", help="resolve a legacy v1alpha2 Deployment graph")
    resolve.add_argument("workspace", type=Path)
    resolve.add_argument("deployment", type=Path)
    resolve.add_argument("--output", type=Path)
    resolve.add_argument(
        "--release",
        action="store_true",
        help="require declared bounded protobuf schema hashes",
    )
    resolve.set_defaults(handler=_resolve)

    codegen = commands.add_parser(
        "codegen", help="emit deployment inputs or bounded C++ protobuf types"
    )
    codegen.add_argument("workspace", type=Path, nargs="?")
    codegen.add_argument("deployment", type=Path, nargs="?")
    codegen.add_argument("graph_output", type=Path, nargs="?")
    source = codegen.add_mutually_exclusive_group()
    source.add_argument("--package", dest="package_manifest", type=Path)
    source.add_argument("--descriptor", type=Path)
    source.add_argument("--proto", type=Path, nargs="+")
    codegen.add_argument("--include", type=Path, action="append", default=[])
    codegen.add_argument("--bounds", type=Path)
    codegen.add_argument("--protoc", default="protoc")
    codegen.add_argument("--output", dest="proto_output", type=Path)
    codegen.add_argument(
        "--release",
        action="store_true",
        help="require declared bounded protobuf schema hashes",
    )
    codegen.set_defaults(handler=_codegen)

    build = commands.add_parser("build", help="plan or execute native build tools")
    build.add_argument("workspace", type=Path)
    build.add_argument("deployment", type=Path)
    build.add_argument("--output", type=Path, default=Path("build/aster"))
    build.add_argument("--generated", type=Path, default=Path("build/generated"))
    build.add_argument("--execute", action="store_true")
    build.add_argument(
        "--release",
        action="store_true",
        help="require declared bounded protobuf schema hashes",
    )
    build.set_defaults(handler=_build)

    run = commands.add_parser("run", help="start the configuration-driven Host Runtime")
    run.add_argument("--config", type=Path, required=True)
    run.add_argument("--runtime", type=Path, help="explicit aster_runtime executable")
    run.add_argument(
        "--check",
        action="store_true",
        help="initialize and validate registrations without starting",
    )
    run.add_argument("--duration-ms", type=int, help="stop after a bounded smoke run")
    run.set_defaults(handler=_run)

    deploy = commands.add_parser("deploy", help="plan, apply, or inspect deployment")
    deploy_commands = deploy.add_subparsers(dest="deploy_command")
    for name, handler in (("plan", _deploy_plan), ("apply", _deploy_apply)):
        item = deploy_commands.add_parser(name)
        item.add_argument("deployment", type=Path)
        item.add_argument("inventory", type=Path)
        item.add_argument("artifacts", type=Path)
        if name == "apply":
            item.add_argument("--execute", action="store_true")
        item.set_defaults(handler=handler)
    status = deploy_commands.add_parser("status")
    status.add_argument("inventory", type=Path)
    status.add_argument(
        "--execute",
        action="store_true",
        help="query SSH targets; local state is always read directly",
    )
    status.set_defaults(handler=_deploy_status)

    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments and arguments[0] == "package":
        print(
            "error: 'aster package' was removed; use CMake, west and uv for dependencies",
            file=sys.stderr,
        )
        return 2
    args = build_parser().parse_args(arguments)
    if args.command in ("graph", "resolve", "build") or (
        args.command == "codegen" and args.workspace is not None
    ):
        print(
            "warning: this is the legacy v1alpha2 deployment path; ordinary Linux uses "
            "runtime.yaml and 'aster run --config'. The v1alpha3 deployment compiler "
            "is not implemented yet.",
            file=sys.stderr,
        )
    handler = getattr(args, "handler", None)
    if handler is None:
        return 0
    try:
        return int(handler(args))
    except (
        ValidationError,
        GraphError,
        DeploymentError,
        ProjectError,
        ProtobufProfileError,
        ValueError,
        subprocess.CalledProcessError,
    ) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
