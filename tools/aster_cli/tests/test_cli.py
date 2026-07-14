from xrobot_tools import __version__
from xrobot_tools.cli import main


def test_empty_command_succeeds() -> None:
    assert main([]) == 0


def test_version_is_development_version() -> None:
    assert __version__ == "0.1.0.dev0"


def test_validate_command_accepts_a_known_document(tmp_path) -> None:
    workspace = tmp_path / "workspace.yaml"
    workspace.write_text(
        """\
api_version: xrobot.io/v1alpha1
kind: Workspace
metadata: {name: test}
packages: []
""",
        encoding="utf-8",
    )

    assert main(["config", "validate", str(workspace)]) == 0
