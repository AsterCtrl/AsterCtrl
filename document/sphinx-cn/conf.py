from pathlib import Path


project = "AsterCtrl"
author = "AsterCtrl contributors"
release = "0.2.0-alpha.1"
extensions = ["breathe", "myst_parser", "sphinx.ext.autosectionlabel", "sphinx.ext.extlinks"]
autosectionlabel_prefix_document = True
html_theme = "furo"
exclude_patterns = ["_build"]
language = "zh_CN"
source_suffix = {".md": "markdown"}
myst_enable_extensions = ["deflist"]

repository_root = Path(__file__).resolve().parents[2]
breathe_projects = {"AsterCtrl": str(repository_root / "build" / "document" / "doxygen" / "xml")}
breathe_default_project = "AsterCtrl"
breathe_domain_by_extension = {"h": "c", "hpp": "cpp"}
