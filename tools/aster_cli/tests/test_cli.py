from xrobot_tools import __version__
from xrobot_tools.cli import main


def test_empty_command_succeeds() -> None:
    assert main([]) == 0


def test_version_is_development_version() -> None:
    assert __version__ == "0.1.0.dev0"
