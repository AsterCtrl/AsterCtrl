"""Generate a Linux Package entry from declarative export metadata."""

from __future__ import annotations

import json
from pathlib import Path

from .validation import ValidationError, load_yaml, validate_mapping


def generate_package(manifest: Path, output: Path) -> None:
    manifest = manifest.resolve()
    document = load_yaml(manifest)
    if document.get("api_version") != "aster.dev/v1alpha3":
        raise ValidationError("Package entry generation requires a v1alpha3 Package Manifest")
    validate_mapping(document, "package-v3.schema.json", str(manifest))
    modules = document["spec"]["modules"]
    names = [module["type"] for module in modules]
    if len(set(names)) != len(names):
        raise ValidationError(f"{manifest}: duplicate exported Module type")

    headers: set[str] = set()
    sources: set[Path] = set()
    registrations: list[str] = []
    for module in modules:
        if "linux" not in module["platforms"]:
            continue
        for name in [module["header"], *module.get("sources", [])]:
            path = (manifest.parent / name).resolve()
            if Path(name).is_absolute() or not path.is_relative_to(manifest.parent):
                raise ValidationError(f"{manifest}: source must stay inside the Package: {name}")
            if not path.is_file():
                raise ValidationError(f"{manifest}: source does not exist: {name}")
            sources.add(path)
        headers.add(module["header"])
        registrations.append(
            f"  {{{json.dumps(module['type'])}, &aster::CreateModule<{module['class']}>}},"
        )
    if not registrations:
        raise ValidationError(f"{manifest}: Package exports no Linux Module types")
    cpp = '#include "aster_pkg_c_interface/pkg_macro.hpp"\n'
    cpp += "".join(f"#include {json.dumps(header)}\n" for header in sorted(headers))
    cpp += "\nnamespace {\nconstexpr aster::ModuleRegistration kModules[]{\n"
    cpp += "\n".join(registrations) + "\n};\n}  // namespace\n\n"
    metadata = document["metadata"]
    cpp += (
        f"ASTER_PKG_MAIN(kModules, {json.dumps(metadata['name'])}, "
        f"{json.dumps(metadata['version'])})\n"
    )
    output = output.resolve()
    sources.add(output / "package.generated.cpp")
    # Bracket arguments are literal CMake strings (no variable expansion).
    if any("]==]" in str(path) or ";" in str(path) or "\n" in str(path) for path in sources):
        raise ValidationError("Package source paths cannot contain CMake list delimiters")
    cmake = "set(ASTER_PACKAGE_SOURCE_FILES\n"
    cmake += "".join(f"  [==[{path}]==]\n" for path in sorted(sources)) + ")\n"
    output.mkdir(parents=True, exist_ok=True)
    for name, content in (("package.generated.cpp", cpp), ("package.generated.cmake", cmake)):
        destination = output / name
        if not destination.exists() or destination.read_text(encoding="utf-8") != content:
            destination.write_text(content, encoding="utf-8", newline="\n")
