from aster_cli.cli import main


def test_run_starts_configured_runtime(tmp_path, monkeypatch):
    config = tmp_path / "runtime.yaml"
    config.write_text("api_version: aster.dev/v1alpha3\naster: {}\n")
    calls = []

    def run(command, *, check):
        calls.append(command)
        return type("Result", (), {"returncode": 0})()

    monkeypatch.setattr("aster_cli.cli.shutil.which", lambda name: "/opt/aster/bin/aster_runtime")
    monkeypatch.setattr("aster_cli.cli.subprocess.run", run)
    assert main(["run", "--config", str(config), "--check"]) == 0
    assert calls == [["/opt/aster/bin/aster_runtime", "--config", str(config), "--check"]]


def test_run_reports_missing_runtime(tmp_path, monkeypatch, capsys):
    config = tmp_path / "runtime.yaml"
    config.write_text("api_version: aster.dev/v1alpha3\naster: {}\n")
    monkeypatch.setattr("aster_cli.cli.shutil.which", lambda name: None)
    assert main(["run", "--config", str(config)]) == 2
    assert "--runtime" in capsys.readouterr().err


def test_validate_uses_native_parser_without_loading_modules(tmp_path, monkeypatch):
    config = tmp_path / "runtime.yaml"
    config.write_text("api_version: aster.dev/v1alpha3\naster: {}\n")
    calls = []

    def run(command, *, check):
        calls.append(command)
        return type("Result", (), {"returncode": 0})()

    monkeypatch.setattr("aster_cli.cli.subprocess.run", run)
    assert main(["validate", str(config), "--runtime", "/opt/aster/aster_runtime"]) == 0
    assert calls == [["/opt/aster/aster_runtime", "--config", str(config), "--validate-config"]]
