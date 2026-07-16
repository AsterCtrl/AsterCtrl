"""Validated in-memory model for Schema First robot interfaces."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal

from aster_tools.validation import ValidationError, load_yaml, validate_mapping


class InterfaceError(ValueError):
    """Raised when interface schemas are invalid or inconsistent."""


PRIMITIVE_CPP_TYPES = {
    "bool": "bool",
    "int8": "std::int8_t",
    "uint8": "std::uint8_t",
    "int16": "std::int16_t",
    "uint16": "std::uint16_t",
    "int32": "std::int32_t",
    "uint32": "std::uint32_t",
    "int64": "std::int64_t",
    "uint64": "std::uint64_t",
    "float32": "float",
    "float64": "double",
}

PRIMITIVE_SIZES = {
    "bool": 1,
    "int8": 1,
    "uint8": 1,
    "int16": 2,
    "uint16": 2,
    "int32": 4,
    "uint32": 4,
    "int64": 8,
    "uint64": 8,
    "float32": 4,
    "float64": 8,
}


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode("ascii")


def hash16(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()[:32]


@dataclass(frozen=True)
class EnumValue:
    name: str
    value: int


@dataclass(frozen=True)
class EnumDefinition:
    name: str
    namespace: str
    underlying_type: str
    values: tuple[EnumValue, ...]
    source: str

    @property
    def full_name(self) -> str:
        return f"{self.namespace}.{self.name}"


@dataclass(frozen=True)
class FieldDefinition:
    name: str
    type_name: str
    array: int | None
    default: Any | None
    unit: str | None


@dataclass(frozen=True)
class RecordDefinition:
    name: str
    namespace: str
    fields: tuple[FieldDefinition, ...]
    source: str
    schema_hash: str

    @property
    def full_name(self) -> str:
        return f"{self.namespace}.{self.name}"


@dataclass(frozen=True)
class InterfaceDefinition:
    kind: Literal["Message", "Service", "Action"]
    name: str
    namespace: str
    source: str
    source_sha256: str
    schema_hash: str
    records: tuple[RecordDefinition, ...]

    @property
    def full_name(self) -> str:
        return f"{self.namespace}.{self.name}"


@dataclass(frozen=True)
class InterfaceModel:
    interfaces: tuple[InterfaceDefinition, ...]
    records: dict[str, RecordDefinition]
    enums: dict[str, EnumDefinition]
    deployment_schema_hash: str


def _extract_enums(
    namespace: str, source: str, record: dict[str, Any]
) -> list[EnumDefinition]:
    result: list[EnumDefinition] = []
    for enum in record.get("enums", []):
        values = tuple(
            EnumValue(name=item["name"], value=item["value"])
            for item in enum["values"]
        )
        names = [item.name for item in values]
        numbers = [item.value for item in values]
        if len(names) != len(set(names)) or len(numbers) != len(set(numbers)):
            raise InterfaceError(f"{source}: enum {enum['name']} has duplicate values")
        result.append(
            EnumDefinition(
                name=enum["name"],
                namespace=namespace,
                underlying_type=enum["underlying_type"],
                values=values,
                source=source,
            )
        )
    return result


def _extract_record(
    namespace: str,
    name: str,
    source: str,
    record: dict[str, Any],
) -> RecordDefinition:
    fields = tuple(
        FieldDefinition(
            name=field["name"],
            type_name=field["type"],
            array=field.get("array"),
            default=field.get("default"),
            unit=field.get("unit"),
        )
        for field in record["fields"]
    )
    names = [field.name for field in fields]
    if len(names) != len(set(names)):
        raise InterfaceError(f"{source}: record {name} has duplicate field names")
    for field in fields:
        if field.array is not None and field.default is not None:
            raise InterfaceError(
                f"{source}: array field {name}.{field.name} cannot have a scalar default"
            )
    canonical = {
        "namespace": namespace,
        "name": name,
        "enums": record.get("enums", []),
        "fields": record["fields"],
    }
    return RecordDefinition(
        name=name,
        namespace=namespace,
        fields=fields,
        source=source,
        schema_hash=hash16(canonical),
    )


def _expected_suffix(kind: str) -> str:
    return {
        "Message": ".msg.yaml",
        "Service": ".srv.yaml",
        "Action": ".action.yaml",
    }[kind]


def _load_interface(path: Path, root: Path) -> tuple[InterfaceDefinition, list[EnumDefinition]]:
    relative = path.relative_to(root).as_posix()
    try:
        document = load_yaml(path)
        validate_mapping(document, "interface.schema.json", relative)
    except ValidationError as error:
        raise InterfaceError(str(error)) from error

    kind = document["kind"]
    if not path.name.endswith(_expected_suffix(kind)):
        raise InterfaceError(
            f"{relative}: {kind} must use the {_expected_suffix(kind)} suffix"
        )
    name = document["metadata"]["name"]
    namespace = document["metadata"]["namespace"]
    spec = document["spec"]
    records: list[RecordDefinition] = []
    enums: list[EnumDefinition] = []

    if kind == "Message":
        records.append(_extract_record(namespace, name, relative, spec))
        enums.extend(_extract_enums(namespace, relative, spec))
    elif kind == "Service":
        for suffix, key in (("Request", "request"), ("Response", "response")):
            records.append(
                _extract_record(namespace, f"{name}{suffix}", relative, spec[key])
            )
            enums.extend(_extract_enums(namespace, relative, spec[key]))
    else:
        for suffix, key in (
            ("Goal", "goal"),
            ("Feedback", "feedback"),
            ("Result", "result"),
        ):
            records.append(
                _extract_record(namespace, f"{name}{suffix}", relative, spec[key])
            )
            enums.extend(_extract_enums(namespace, relative, spec[key]))

    return (
        InterfaceDefinition(
            kind=kind,
            name=name,
            namespace=namespace,
            source=relative,
            source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
            schema_hash=hash16(document),
            records=tuple(records),
        ),
        enums,
    )


def resolve_symbol(
    record: RecordDefinition, type_name: str, model: InterfaceModel
) -> str:
    if type_name in PRIMITIVE_CPP_TYPES:
        return type_name
    full_name = type_name if "." in type_name else f"{record.namespace}.{type_name}"
    if full_name in model.enums or full_name in model.records:
        return full_name
    raise InterfaceError(
        f"{record.source}: {record.name} has unknown or unbounded type {type_name!r}"
    )


def _validate_defaults(model: InterfaceModel) -> None:
    for record in model.records.values():
        for field in record.fields:
            symbol = resolve_symbol(record, field.type_name, model)
            if field.default is None:
                continue
            if symbol in model.enums:
                allowed = {item.name for item in model.enums[symbol].values}
                if field.default not in allowed:
                    raise InterfaceError(
                        f"{record.source}: {record.name}.{field.name} has invalid "
                        f"enum default {field.default!r}"
                    )
            elif symbol in model.records:
                raise InterfaceError(
                    f"{record.source}: nested field {record.name}.{field.name} "
                    "cannot have a scalar default"
                )


def load_interface_model(schema_root: str | Path) -> InterfaceModel:
    root = Path(schema_root)
    paths = sorted(path for path in root.rglob("*.yaml") if path.is_file())
    if not paths:
        raise InterfaceError(f"{root}: no interface schema files found")

    interfaces: list[InterfaceDefinition] = []
    all_enums: list[EnumDefinition] = []
    for path in paths:
        interface, enums = _load_interface(path, root)
        interfaces.append(interface)
        all_enums.extend(enums)

    interface_names = [item.full_name for item in interfaces]
    if len(interface_names) != len(set(interface_names)):
        raise InterfaceError("duplicate fully-qualified interface name")

    records: dict[str, RecordDefinition] = {}
    for interface in interfaces:
        for record in interface.records:
            if record.full_name in records:
                raise InterfaceError(f"duplicate record {record.full_name}")
            records[record.full_name] = record

    enums: dict[str, EnumDefinition] = {}
    for enum in all_enums:
        if enum.full_name in enums or enum.full_name in records:
            raise InterfaceError(f"duplicate type {enum.full_name}")
        enums[enum.full_name] = enum

    deployment_schema_hash = hash16(
        [
            {
                "name": item.full_name,
                "kind": item.kind,
                "hash": item.schema_hash,
            }
            for item in sorted(interfaces, key=lambda value: value.full_name)
        ]
    )
    model = InterfaceModel(
        interfaces=tuple(sorted(interfaces, key=lambda item: item.full_name)),
        records=records,
        enums=enums,
        deployment_schema_hash=deployment_schema_hash,
    )
    _validate_defaults(model)
    return model
