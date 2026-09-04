#!/usr/bin/env python3
"""Generate a deterministic CycloneDX inventory for packaged release assets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_release_sbom(input_dir: Path, version: str) -> dict[str, object]:
    input_dir = input_dir.resolve()
    if not input_dir.is_dir():
        raise ValueError(f"release input directory does not exist: {input_dir}")
    if not version:
        raise ValueError("release version must not be empty")

    files = sorted(path for path in input_dir.rglob("*") if path.is_file())
    if not files:
        raise ValueError(f"release input directory is empty: {input_dir}")

    components: list[dict[str, object]] = []
    for path in files:
        relative = path.relative_to(input_dir).as_posix()
        digest = _sha256(path)
        components.append(
            {
                "type": "file",
                "bom-ref": f"release-asset:{relative}@sha256:{digest}",
                "name": relative,
                "hashes": [{"alg": "SHA-256", "content": digest}],
                "properties": [
                    {"name": "asterctrl:release-asset:size", "value": str(path.stat().st_size)}
                ],
            }
        )

    source_version = version.removeprefix("v")
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "version": 1,
        "metadata": {
            "component": {
                "type": "application",
                "bom-ref": f"pkg:github/AsterCtrl/AsterCtrl@{source_version}",
                "name": "AsterCtrl",
                "version": source_version,
                "properties": [{"name": "asterctrl:release-tag", "value": version}],
            }
        },
        "components": components,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()

    try:
        document = build_release_sbom(args.input_dir, args.version)
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"wrote {len(document['components'])} release assets to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
