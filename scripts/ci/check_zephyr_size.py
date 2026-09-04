#!/usr/bin/env python3
"""Report and optionally enforce a text/data/bss budget for a Zephyr ELF."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def find_size_command(elf: Path, requested: str | None) -> str:
    if requested is not None:
        return requested

    for directory in elf.resolve().parents:
        cache = directory / "CMakeCache.txt"
        if not cache.is_file():
            continue
        for line in cache.read_text(encoding="utf-8").splitlines():
            if line.startswith("CMAKE_SIZE:") and "=" in line:
                candidate = Path(line.split("=", 1)[1])
                if candidate.is_file():
                    return str(candidate)

    for name in ("arm-zephyr-eabi-size", "llvm-size", "size"):
        candidate = shutil.which(name)
        if candidate is not None:
            return candidate
    raise FileNotFoundError("no size tool found in CMakeCache.txt or PATH")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--size-command")
    parser.add_argument("--max-flash", type=int)
    parser.add_argument("--max-ram", type=int)
    args = parser.parse_args()
    if (args.max_flash is None) != (args.max_ram is None):
        parser.error("--max-flash and --max-ram must be provided together")
    size_command = find_size_command(args.elf, args.size_command)
    completed = subprocess.run(
        [size_command, str(args.elf)],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [line.split() for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) < 2 or lines[0][:3] != ["text", "data", "bss"]:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(f"unexpected {size_command} output")
    text, data, bss = (int(value) for value in lines[-1][:3])
    flash = text + data
    ram = data + bss
    if args.max_flash is None:
        print(f"text={text} bytes, data={data} bytes, bss={bss} bytes")
        print(f"flash={flash} bytes, ram={ram} bytes")
        return 0
    print(f"flash={flash}/{args.max_flash} bytes, ram={ram}/{args.max_ram} bytes")
    if flash > args.max_flash or ram > args.max_ram:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
