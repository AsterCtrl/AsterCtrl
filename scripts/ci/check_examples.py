#!/usr/bin/env python3
"""Validate examples and prove graph/code generation is deterministic."""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

EXAMPLES = (
    ("linux_local", "workspace.yaml", "deployment.yaml"),
    ("linux_zephyr_can", "workspace.yaml", "deployment.yaml"),
    ("linux_zephyr_usb_cdc", "workspace.yaml", "deployment.yaml"),
    ("provider_swap_sim", "workspace.sim.yaml", "deployment.sim.yaml"),
    ("provider_swap_real", "workspace.real.yaml", "deployment.real.yaml"),
)

SCHEMA_HASH = re.compile(r'kSchemaSha256\s*=\s*"([0-9a-f]{64})"')
RUNNABLE_NODES = {
    "linux_local": ("app",),
    "linux_zephyr_can": ("controller-node",),
    "linux_zephyr_usb_cdc": ("gateway-node",),
    "provider_swap_sim": ("simulation",),
}


def run(*arguments: object, cwd: Path) -> None:
    command = [
        sys.executable,
        "-m",
        "aster_cli.cli",
        *(str(item) for item in arguments),
    ]
    subprocess.run(command, cwd=cwd, check=True)


def tree_digest(root: Path) -> dict[str, str]:
    return {
        str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def validate_example(root: Path, example: Path) -> None:
    documents = [
        path
        for path in sorted(example.glob("*.yaml"))
        if path.name != "bounds.yaml" and not path.name.endswith("lock.yaml")
    ]
    run("validate", *documents, cwd=root)


def compile_generated_header(
    header: Path, compiler: str, directory: Path, project_root: Path
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "check.cpp"
    source.write_text(f'#include "{header.name}"\nint main() {{ return 0; }}\n', encoding="utf-8")
    subprocess.run(
        [
            compiler,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(header.parent),
            "-I",
            str(project_root / "include"),
            str(source),
            "-o",
            str(directory / "check"),
        ],
        check=True,
    )


def schema_hash(header: Path) -> str:
    match = SCHEMA_HASH.search(header.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError(f"{header}: generated header has no kSchemaSha256")
    return match.group(1)


def compile_typed_compositions(
    generated: Path,
    compiler: str,
    directory: Path,
    project_root: Path,
    example: Path,
) -> None:
    headers = sorted(generated.glob("nodes/*/composition.generated.hpp"))
    if not headers:
        raise RuntimeError(f"{generated}: codegen emitted no node compositions")
    for header in headers:
        node = header.parent.name
        output = directory / node
        output.mkdir(parents=True, exist_ok=True)
        source = output / "typed_composition.cpp"
        source.write_text(
            '#include "composition.generated.hpp"\n'
            "static_assert(aster::generated::kTypedComposition, "
            '"module implementation headers must not fall back");\n'
            "int main() {\n"
            "  aster::generated::Composition composition;\n"
            "  return composition.Modules().empty() ? 1 : 0;\n"
            "}\n",
            encoding="utf-8",
        )
        executable = output / "typed_composition"
        subprocess.run(
            [
                compiler,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fno-exceptions",
                "-fno-rtti",
                "-I",
                str(header.parent),
                "-I",
                str(example),
                "-I",
                str(generated / "types"),
                "-I",
                str(project_root / "include"),
                str(source),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def build_runnable_example(
    example: Path,
    project_root: Path,
    generated: Path,
    node: str,
    build: Path,
) -> None:
    command = [
        "cmake",
        "-S",
        str(example),
        "-B",
        str(build),
        f"-DASTERCTRL_SOURCE_DIR={project_root}",
        f"-DASTER_GENERATED_DIR={generated / 'nodes' / node}",
    ]
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    subprocess.run(["ctest", "--test-dir", str(build), "--output-on-failure"], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    compiler = shutil.which("c++")
    if compiler is None:
        raise RuntimeError("a C++20 compiler is required to validate typed examples")
    if shutil.which("cmake") is None:
        raise RuntimeError("CMake is required to validate runnable Linux examples")

    with tempfile.TemporaryDirectory(prefix="aster-example-check-") as temporary:
        output_root = Path(temporary)
        for output_name, workspace_name, deployment_name in EXAMPLES:
            example_name = (
                "provider_swap" if output_name.startswith("provider_swap_") else output_name
            )
            example = root / "examples" / example_name
            validate_example(root, example)
            workspace = example / workspace_name
            deployment = example / deployment_name

            proto_dir = example / "proto"
            proto_files = sorted(proto_dir.glob("*.proto"))
            bounded_hashes: set[str] = set()
            bounded_include = output_root / f"{output_name}-proto"
            for proto_file in proto_files:
                bounded = [
                    bounded_include / f"{proto_file.stem}-{index}.pb.hpp" for index in (1, 2)
                ]
                for header in bounded:
                    run(
                        "codegen",
                        "--proto",
                        proto_file,
                        "--include",
                        proto_dir,
                        "--bounds",
                        proto_dir / "bounds.yaml",
                        "--output",
                        header,
                        cwd=root,
                    )
                if bounded[0].read_bytes() != bounded[1].read_bytes():
                    raise RuntimeError(f"{output_name}: bounded protobuf output changed")
                canonical = bounded_include / f"{proto_file.stem}.pb.hpp"
                shutil.copyfile(bounded[0], canonical)
                bounded_hashes.add(schema_hash(canonical))
                compile_generated_header(
                    canonical,
                    compiler,
                    output_root / f"{output_name}-protobuf-check",
                    root,
                )

            locks = [output_root / f"{output_name}-{index}.lock.yaml" for index in (1, 2)]
            for lock in locks:
                run(
                    "resolve",
                    workspace,
                    deployment,
                    "--release",
                    "--output",
                    lock,
                    cwd=root,
                )
            if locks[0].read_bytes() != locks[1].read_bytes():
                raise RuntimeError(f"{output_name}: deployment lock is not deterministic")
            lock_document = yaml.safe_load(locks[0].read_text(encoding="utf-8"))
            routes = lock_document["routes"]
            if routes and len(bounded_hashes) != 1:
                raise RuntimeError(
                    f"{output_name}: route-to-schema verification requires one bounded schema"
                )
            for route in routes:
                if route["schema_hash_source"] != "descriptor_bounds":
                    raise RuntimeError(
                        f"{output_name}: release route {route['id']} did not derive its hash "
                        "from descriptors and bounds"
                    )
                if route["schema_hash"] not in bounded_hashes:
                    raise RuntimeError(
                        f"{output_name}: route {route['id']} schema hash does not match "
                        "the generated bounded protobuf hash"
                    )

            generated = [output_root / f"{output_name}-{index}" for index in (1, 2)]
            for destination in generated:
                run(
                    "codegen",
                    workspace,
                    deployment,
                    destination,
                    "--release",
                    cwd=root,
                )
            if tree_digest(generated[0]) != tree_digest(generated[1]):
                raise RuntimeError(f"{output_name}: generated tree is not deterministic")

            compile_typed_compositions(
                generated[0],
                compiler,
                output_root / f"{output_name}-composition-check",
                root,
                example,
            )
            for runnable_node in RUNNABLE_NODES.get(output_name, ()):
                build_runnable_example(
                    example,
                    root,
                    generated[0],
                    runnable_node,
                    output_root / f"{output_name}-{runnable_node}-build",
                )

    print("all release locks, bounded schemas, typed compositions, and Linux examples passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
