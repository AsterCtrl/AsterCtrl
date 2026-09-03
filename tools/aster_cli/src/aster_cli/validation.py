"""Strict validation for versioned AsterCtrl YAML documents."""

from __future__ import annotations

import json
import re
from importlib.resources import files
from pathlib import Path
from typing import Any, Final

import jsonschema
import yaml
from referencing import Registry, Resource

API_VERSION: Final = "aster.dev/v1alpha2"
SCHEMA_BY_KIND: Final = {
    "Package": "package.schema.json",
    "Module": "module.schema.json",
    "Workspace": "workspace.schema.json",
    "Application": "application.schema.json",
    "Deployment": "deployment.schema.json",
    "Hardware": "hardware.schema.json",
    "Inventory": "inventory.schema.json",
    "PackageLock": "package-lock.schema.json",
    "DeploymentLock": "deployment-lock.schema.json",
    "DeploymentBundle": "deployment-bundle.schema.json",
    "DeploymentState": "deployment-state.schema.json",
}


class ValidationError(ValueError):
    """Raised when a document cannot be parsed or violates its contract."""


def load_yaml(path: str | Path) -> dict[str, Any]:
    source = Path(path)
    try:
        value = yaml.safe_load(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        raise ValidationError(f"{source}: cannot read YAML: {error}") from error
    if not isinstance(value, dict):
        raise ValidationError(f"{source}: document root must be a mapping")
    return value


def _schemas() -> dict[str, dict[str, Any]]:
    root = files("aster_cli.schemas")
    result: dict[str, dict[str, Any]] = {}
    for name in ("common.schema.json", *SCHEMA_BY_KIND.values()):
        schema = json.loads(root.joinpath(name).read_text(encoding="utf-8"))
        result[name] = schema
        result[str(schema["$id"])] = schema
    return result


def validate_mapping(
    document: dict[str, Any], schema_name: str, source_name: str = "<memory>"
) -> None:
    schemas = _schemas()
    schema = schemas[schema_name]
    registry = Registry()
    for candidate in schemas.values():
        registry = registry.with_resource(str(candidate["$id"]), Resource.from_contents(candidate))
    validator = jsonschema.Draft202012Validator(schema, registry=registry)
    errors = sorted(validator.iter_errors(document), key=lambda item: list(item.path))
    if not errors:
        return
    error = errors[0]
    path = ".".join(str(part) for part in error.absolute_path)
    if error.validator == "additionalProperties":
        match = re.search(r"'([^']+)' was unexpected", error.message)
        if match:
            path = ".".join(filter(None, (path, match.group(1))))
    location = f"{source_name}: {path}" if path else source_name
    raise ValidationError(f"{location}: {error.message}")


def validate_document(path: str | Path) -> dict[str, Any]:
    source = Path(path)
    document = load_yaml(source)
    kind = document.get("kind")
    if not isinstance(kind, str) or kind not in SCHEMA_BY_KIND:
        raise ValidationError(f"{source}: unsupported kind {kind!r}")
    validate_mapping(document, SCHEMA_BY_KIND[kind], str(source))
    return document


def dump_yaml(document: dict[str, Any]) -> str:
    return yaml.safe_dump(document, sort_keys=True, allow_unicode=True)


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode(
        "utf-8"
    )
