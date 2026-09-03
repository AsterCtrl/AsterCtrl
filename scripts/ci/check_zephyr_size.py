#!/usr/bin/env python3
"""Enforce a simple text/data/bss budget for a Zephyr ELF."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--size-command", default="arm-zephyr-eabi-size")
    parser.add_argument("--max-flash", type=int, required=True)
    parser.add_argument("--max-ram", type=int, required=True)
    args = parser.parse_args()
    completed = subprocess.run(
        [args.size_command, str(args.elf)],
        check=True,
        capture_output=True,
        text=True,
    )
    lines = [line.split() for line in completed.stdout.splitlines() if line.strip()]
    if len(lines) < 2 or lines[0][:3] != ["text", "data", "bss"]:
        print(completed.stdout, file=sys.stderr)
        raise RuntimeError(f"unexpected {args.size_command} output")
    text, data, bss = (int(value) for value in lines[-1][:3])
    flash = text + data
    ram = data + bss
    print(f"flash={flash}/{args.max_flash} bytes, ram={ram}/{args.max_ram} bytes")
    if flash > args.max_flash or ram > args.max_ram:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
