from __future__ import annotations

import importlib.util
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts/ci/check_zephyr_size.py"
SPEC = importlib.util.spec_from_file_location("check_zephyr_size", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
CHECK_ZEPHYR_SIZE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_ZEPHYR_SIZE)


def test_finds_tool_recorded_by_zephyr_cmake(tmp_path: Path) -> None:
    build = tmp_path / "build/dev_c"
    elf = build / "zephyr/zephyr.elf"
    tool = tmp_path / "zephyr-sdk/bin/arm-zephyr-eabi-size"
    elf.parent.mkdir(parents=True)
    tool.parent.mkdir(parents=True)
    elf.touch()
    tool.touch()
    (build / "CMakeCache.txt").write_text(f"CMAKE_SIZE:FILEPATH={tool}\n", encoding="utf-8")

    assert CHECK_ZEPHYR_SIZE.find_size_command(elf, None) == str(tool)


def test_reports_and_enforces_budget(monkeypatch, capsys) -> None:
    monkeypatch.setattr(
        CHECK_ZEPHYR_SIZE.subprocess,
        "run",
        lambda *args, **kwargs: subprocess.CompletedProcess(
            args=["fake-size"],
            returncode=0,
            stdout="text data bss dec hex filename\n10 2 3 15 f app\n",
        ),
    )
    monkeypatch.setattr(
        sys,
        "argv",
        [
            str(SCRIPT),
            "app.elf",
            "--size-command",
            "fake-size",
            "--max-flash",
            "12",
            "--max-ram",
            "5",
        ],
    )

    assert CHECK_ZEPHYR_SIZE.main() == 0
    assert capsys.readouterr().out == "flash=12/12 bytes, ram=5/5 bytes\n"
