"""Versioned YAML document validation."""

from __future__ import annotations

import json
import re
from importlib.resources import files
from pathlib import Path
from typing import Any

import jsonschema
import yaml


class ValidationError(ValueError):
    """Raised when a framework document does not satisfy its schema."""


SCHEMA_BY_KIND = {
    "Package": "package.schema.json",
    "Module": "module.schema.json",
    "Workspace": "workspace.schema.json",
    "Robot": "robot.schema.json",
    "HardwareProfile": "hardware.schema.json",
    "Deployment": "deployment.schema.json",
    "PackageLock": "package-lock.schema.json",
    "DeploymentLock": "deployment-lock.schema.json",
}


def load_yaml(path: Path) -> dict[str, Any]:
    try:
        document = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        raise ValidationError(f"{path.name}: cannot read YAML: {error}") from error
    if not isinstance(document, dict):
        raise ValidationError(f"{path.name}: document root must be a mapping")
    return document


def load_schema(name: str) -> dict[str, Any]:
    resource = files("xrobot_tools.schemas").joinpath(name)
    return json.loads(resource.read_text(encoding="utf-8"))


def validate_mapping(
    document: dict[str, Any], schema_name: str, source_name: str
) -> None:
    validator = jsonschema.Draft202012Validator(load_schema(schema_name))
    errors = sorted(validator.iter_errors(document), key=lambda item: list(item.path))
    if not errors:
        return
    error = errors[0]
    path = ".".join(str(part) for part in error.absolute_path)
    if error.validator == "additionalProperties":
        match = re.search(r"\('([^']+)' was unexpected\)", error.message)
        if match:
            path = ".".join(filter(None, (path, match.group(1))))
    location = f"{source_name}: {path}" if path else source_name
    raise ValidationError(f"{location}: {error.message}")


def validate_document(path: str | Path) -> dict[str, Any]:
    source = Path(path)
    document = load_yaml(source)
    kind = document.get("kind")
    if not isinstance(kind, str) or kind not in SCHEMA_BY_KIND:
        raise ValidationError(f"{source.name}: unsupported kind {kind!r}")
    validate_mapping(document, SCHEMA_BY_KIND[kind], source.name)
    return document
