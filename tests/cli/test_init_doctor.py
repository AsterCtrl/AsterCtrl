from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from types import SimpleNamespace

import aster_cli.doctor as doctor
import pytest
from aster_cli.cli import main
from aster_cli.graph import resolve_deployment
from aster_cli.validation import validate_document

REPOSITORY = Path(__file__).resolve().parents[2]


def test_init_creates_valid_resolvable_template(tmp_path: Path) -> None:
    target = tmp_path / "starter"

    assert main(["init", str(target)]) == 0
    assert (target / "application.yaml").is_file()
    assert (target / "deployment.sim.yaml").is_file()
    for path in target.rglob("*.yaml"):
        if path.name == "bounds.yaml":
            continue
        validate_document(path)
    lock = resolve_deployment(target / "workspace.yaml", target / "deployment.sim.yaml")
    assert lock["nodes"]["app"]["id"] == 1
    assert lock["nodes"]["app"]["instances"] == ["source", "sink"]


def test_init_refuses_nonempty_directory(tmp_path: Path, capsys) -> None:
    target = tmp_path / "occupied"
    target.mkdir()
    (target / "keep.txt").write_text("keep", encoding="utf-8")

    assert main(["init", str(target)]) == 2
    assert "absent or empty" in capsys.readouterr().err
    assert (target / "keep.txt").read_text(encoding="utf-8") == "keep"


@pytest.mark.skipif(
    shutil.which("cmake") is None or shutil.which("ninja") is None,
    reason="CMake and Ninja are required for the starter build contract",
)
def test_init_template_builds_and_runs(tmp_path: Path) -> None:
    target = tmp_path / "starter"
    generated = target / "build" / "generated"
    build = target / "build" / "host"

    assert main(["init", str(target)]) == 0
    assert (
        main(
            [
                "codegen",
                str(target / "workspace.yaml"),
                str(target / "deployment.sim.yaml"),
                str(generated),
            ]
        )
        == 0
    )
    assert (generated / "types" / "state.pb.hpp").is_file()
    subprocess.run(
        [
            "cmake",
            "-S",
            str(target),
            "-B",
            str(build),
            "-G",
            "Ninja",
            f"-DASTERCTRL_SOURCE_DIR={REPOSITORY}",
            f"-DASTER_GENERATED_DIR={generated / 'nodes' / 'app'}",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    subprocess.run([str(build / "aster_app")], check=True)


def test_doctor_json_fails_when_required_cmake_is_missing(monkeypatch, capsys) -> None:
    monkeypatch.setattr(doctor.shutil, "which", lambda _name: None)

    assert main(["doctor", "--format", "json"]) == 1
    report = json.loads(capsys.readouterr().out)
    assert report["ok"] is False
    assert next(item for item in report["checks"] if item["name"] == "cmake")["required"]


def test_doctor_allows_missing_optional_tools(monkeypatch, capsys) -> None:
    monkeypatch.setattr(
        doctor.shutil,
        "which",
        lambda name: f"/tool/{name}" if name in ("cmake", "protoc") else None,
    )
    monkeypatch.setattr(
        doctor.subprocess,
        "run",
        lambda args, **_kwargs: SimpleNamespace(
            stdout="cmake version 3.28.1\n" if args[0].endswith("cmake") else "libprotoc 29.3\n",
            stderr="",
        ),
    )

    assert main(["doctor"]) == 0
    output = capsys.readouterr().out
    assert "[ok] cmake: 3.28.1" in output
    assert "[ok] protoc: 29.3.0" in output
    assert "[missing] ninja" in output
    assert "doctor: ok" in output


def test_doctor_requires_the_pinned_python_minor(monkeypatch) -> None:
    monkeypatch.setattr(doctor.sys, "version_info", (3, 13, 0))
    monkeypatch.setattr(doctor.shutil, "which", lambda _name: None)

    report = doctor.doctor_report()

    python = next(item for item in report["checks"] if item["name"] == "python")
    assert python["status"] == "unsupported"
    assert report["ok"] is False
