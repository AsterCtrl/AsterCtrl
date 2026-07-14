"""Command-line entry point for the framework tooling."""

from __future__ import annotations

import argparse
from collections.abc import Sequence

from xrobot_tools import __version__


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="xrctl")
    parser.add_argument("--version", action="version", version=__version__)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    build_parser().parse_args(argv)
    return 0
