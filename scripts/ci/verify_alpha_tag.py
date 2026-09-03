#!/usr/bin/env python3
"""Guard the prerelease workflow against publishing a final release."""

from __future__ import annotations

import argparse
import os
import re

ALPHA_TAG = re.compile(r"v0\.2\.0-alpha\.[1-9][0-9]*")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("tag", nargs="?", default=os.environ.get("GITHUB_REF_NAME", ""))
    args = parser.parse_args()
    if ALPHA_TAG.fullmatch(args.tag) is None:
        raise SystemExit(f"refusing release for {args.tag!r}; only v0.2.0-alpha.N is allowed")
    print(f"verified prerelease tag {args.tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
