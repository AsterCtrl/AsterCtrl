from __future__ import annotations

import json
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import aster_cli.doctor as doctor
import pytest
from aster_cli.cli import main
from aster_cli.validation import validate_document

REPOSITORY = Path(__file__).resolve().parents[2]


def test_init_creates_a_linux_package_without_a_deployment_graph(tmp_path: Path) -> None:
    target = tmp_path / "starter"

    assert main(["init", str(target)]) == 0
    assert (target / "runtime.yaml").is_file()
    assert not (target / "application.yaml").exists()
    assert not (target / "workspace.yaml").exists()
    assert not (target / "src/main.cpp").exists()
    manifest = validate_document(target / "package.yaml")
    assert manifest["api_version"] == "aster.dev/v1alpha3"
    assert manifest["spec"]["modules"][0]["type"] == "demo.Hello"


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
    build = target / "build"

    assert main(["init", str(target)]) == 0
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
            f"-DPython3_EXECUTABLE={sys.executable}",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    result = subprocess.run(
        [
            str(build / "asterctrl/aster_runtime"),
            "--config",
            str(build / "runtime.yaml"),
            "--duration-ms",
            "20",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    assert "[hello/lifecycle] hello from AsterCtrl" in result.stderr

    # Exercise the shipped service command against the same Runtime as direct launch.
    unit = (build / "asterctrl/aster-node@.service").read_text()
    assert "@CMAKE_INSTALL_FULL_BINDIR@" not in unit
    command = shlex.split(
        next(
            line.removeprefix("ExecStart=")
            for line in unit.splitlines()
            if line.startswith("ExecStart=")
        )
    )
    command[0] = str(build / "asterctrl/aster_runtime")
    command[-1] = str(build / "runtime.yaml")
    subprocess.run([*command, "--duration-ms", "20"], check=True, capture_output=True, text=True)

    # The installed SDK must provide the same helper, without repository paths.
    prefix = tmp_path / "install"
    subprocess.run(
        ["cmake", "--install", str(build / "asterctrl"), "--prefix", str(prefix)], check=True
    )
    consumer = tmp_path / "consumer"
    assert main(["init", str(consumer)]) == 0
    (consumer / "aster_cli.py").write_text("raise RuntimeError('Package-owned Python executed')\n")
    subprocess.run(
        [
            "cmake",
            "-S",
            str(consumer),
            "-B",
            str(consumer / "build"),
            "-G",
            "Ninja",
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DPython3_EXECUTABLE={sys.executable}",
        ],
        check=True,
        cwd=consumer,
    )
    subprocess.run(["cmake", "--build", str(consumer / "build")], check=True)
    package = consumer / "build/demo.so"
    original = package.read_bytes()
    config = consumer / "runtime.yaml"
    config.write_text(config.read_text().replace("hello from AsterCtrl", "configured at runtime"))
    subprocess.run(["cmake", "--build", str(consumer / "build")], check=True)
    assert package.read_bytes() == original
    result = subprocess.run(
        [
            str(prefix / "bin/aster_runtime"),
            "--config",
            str(consumer / "build/runtime.yaml"),
            "--duration-ms",
            "20",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    assert "configured at runtime" in result.stderr


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
