"""Deterministic local toolchain diagnostics for ``aster doctor``."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from typing import Any


def _version(text: str) -> tuple[int, ...] | None:
    match = re.search(r"(?<![0-9])(\d+)\.(\d+)(?:\.(\d+))?", text)
    if match is None:
        return None
    return tuple(int(part) for part in match.groups(default="0"))


def _external_check(
    name: str,
    *,
    required: bool,
    minimum: tuple[int, ...] | None = None,
) -> dict[str, Any]:
    path = shutil.which(name)
    result: dict[str, Any] = {
        "name": name,
        "required": required,
        "minimum": ".".join(map(str, minimum)) if minimum else None,
        "path": path,
        "version": None,
        "ok": False,
        "status": "missing",
    }
    if path is None:
        return result
    try:
        completed = subprocess.run(
            [path, "--version"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        result["status"] = "error"
        return result
    output = (completed.stdout or completed.stderr).strip()
    parsed = _version(output)
    result["version"] = ".".join(map(str, parsed)) if parsed else output
    if minimum is not None and (parsed is None or parsed < minimum):
        result["status"] = "outdated"
        return result
    result["ok"] = True
    result["status"] = "ok"
    return result


def doctor_report() -> dict[str, Any]:
    python_version = tuple(sys.version_info[:3])
    python_supported = python_version[:2] == (3, 12)
    checks = [
        {
            "name": "python",
            "required": True,
            "minimum": "3.12.x",
            "path": sys.executable,
            "version": ".".join(map(str, python_version)),
            "ok": python_supported,
            "status": "ok" if python_supported else "unsupported",
        },
        _external_check("cmake", required=True, minimum=(3, 28)),
        _external_check("protoc", required=True),
        _external_check("ninja", required=False),
        _external_check("west", required=False),
    ]
    return {
        "ok": all(item["ok"] for item in checks if item["required"]),
        "checks": checks,
    }


def format_doctor_text(report: dict[str, Any]) -> str:
    lines = []
    for check in report["checks"]:
        role = "required" if check["required"] else "optional"
        detail = check["version"] or check["status"]
        if not check["minimum"]:
            minimum = ""
        elif check["name"] == "python":
            minimum = f", supported {check['minimum']}"
        else:
            minimum = f", need >= {check['minimum']}"
        lines.append(f"[{check['status']}] {check['name']}: {detail} ({role}{minimum})")
    lines.append("doctor: ok" if report["ok"] else "doctor: required tools unavailable")
    return "\n".join(lines) + "\n"
