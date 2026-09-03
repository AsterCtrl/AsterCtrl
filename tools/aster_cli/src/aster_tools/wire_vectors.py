"""Language-neutral default-value wire vectors for generated records."""

from __future__ import annotations

import struct

import yaml

from aster_tools.cpp_codegen import record_wire_sizes, topological_records
from aster_tools.interface_model import (
    FieldDefinition,
    InterfaceError,
    InterfaceModel,
    RecordDefinition,
    resolve_symbol,
)


STRUCT_FORMATS = {
    "bool": "<?",
    "int8": "<b",
    "uint8": "<B",
    "int16": "<h",
    "uint16": "<H",
    "int32": "<i",
    "uint32": "<I",
    "int64": "<q",
    "uint64": "<Q",
    "float32": "<f",
    "float64": "<d",
}


def _scalar_value(
    record: RecordDefinition,
    field: FieldDefinition,
    symbol: str,
    model: InterfaceModel,
):
    if symbol in model.enums:
        if field.default is None:
            return 0
        for item in model.enums[symbol].values:
            if item.name == field.default:
                return item.value
        raise InterfaceError(
            f"{record.source}: unknown enum default {field.default!r}"
        )
    if field.default is not None:
        return field.default
    return False if symbol == "bool" else 0


def _encode_field(
    record: RecordDefinition,
    field: FieldDefinition,
    model: InterfaceModel,
    encoded_records: dict[str, bytes],
) -> bytes:
    symbol = resolve_symbol(record, field.type_name, model)
    count = field.array or 1
    if symbol in model.records:
        return encoded_records[symbol] * count
    primitive = (
        model.enums[symbol].underlying_type if symbol in model.enums else symbol
    )
    try:
        value = _scalar_value(record, field, symbol, model)
        return b"".join(struct.pack(STRUCT_FORMATS[primitive], value) for _ in range(count))
    except (KeyError, struct.error, TypeError) as error:
        raise InterfaceError(
            f"{record.source}: cannot encode default for {record.name}.{field.name}: {error}"
        ) from error


def render_test_vectors(model: InterfaceModel) -> str:
    sizes = record_wire_sizes(model)
    encoded_records: dict[str, bytes] = {}
    vectors = []
    for record in topological_records(model):
        encoded = b"".join(
            _encode_field(record, field, model, encoded_records)
            for field in record.fields
        )
        expected_size = sizes[record.full_name]
        if len(encoded) != expected_size:
            raise InterfaceError(
                f"{record.full_name}: vector has {len(encoded)} bytes, expected {expected_size}"
            )
        encoded_records[record.full_name] = encoded
        vectors.append(
            {
                "type": record.full_name,
                "schema_hash": record.schema_hash,
                "max_serialized_size": expected_size,
                "encoded_hex": encoded.hex(),
            }
        )
    document = {
        "api_version": "aster.dev/schema/v1alpha1",
        "kind": "TypeSupportTestVectors",
        "deployment_schema_hash": model.deployment_schema_hash,
        "vectors": vectors,
    }
    return yaml.safe_dump(
        document,
        sort_keys=False,
        allow_unicode=False,
        default_flow_style=False,
    )
