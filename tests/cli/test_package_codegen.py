from pathlib import Path

from aster_cli.cli import main


def test_package_codegen_rejects_duplicate_yaml_keys(tmp_path: Path, capsys) -> None:
    assert main(["init", str(tmp_path / "app")]) == 0
    manifest = tmp_path / "app/package.yaml"
    manifest.write_text(
        manifest.read_text().replace(
            "class: demo::Hello", "class: demo::Oops\n      class: demo::Hello"
        )
    )
    assert main(["codegen", "--package", str(manifest), "--output", str(tmp_path / "out")]) == 2
    assert "duplicate key" in capsys.readouterr().err
    assert not (tmp_path / "out").exists()
