"""Command-line entry point for the framework tooling."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from xrobot_tools import __version__
from xrobot_tools.validation import ValidationError, validate_document


def validate_config(args: argparse.Namespace) -> int:
    validate_document(args.path)
    print(f"validated {args.path}")
    return 0


def generate_schema(args: argparse.Namespace) -> int:
    from xrobot_tools.interfaces import generate_interfaces

    result = generate_interfaces(args.schema_root, args.output)
    print(
        f"generated {result.interface_count} interfaces "
        f"({result.record_count} records) into {args.output}"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="xrctl")
    parser.add_argument("--version", action="version", version=__version__)
    commands = parser.add_subparsers(dest="command")

    config = commands.add_parser("config", help="validate framework YAML")
    config_commands = config.add_subparsers(dest="config_command")
    validate = config_commands.add_parser("validate", help="validate one YAML file")
    validate.add_argument("path", type=Path)
    validate.set_defaults(handler=validate_config)

    schema = commands.add_parser("schema", help="compile message contracts")
    schema_commands = schema.add_subparsers(dest="schema_command")
    generate = schema_commands.add_parser("generate", help="generate C++ TypeSupport")
    generate.add_argument("schema_root", type=Path)
    generate.add_argument("output", type=Path)
    generate.set_defaults(handler=generate_schema)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    handler = getattr(args, "handler", None)
    if handler is None:
        return 0
    try:
        return int(handler(args))
    except (ValidationError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
