#!/usr/bin/env python3
"""Generate a pydata-sphinx-theme ``switcher.json`` from the docs versions manifest.

The version selector in the docs header reads this file; it lists every
published version (development + releases) with an absolute URL into the
GitHub Pages site. Keep it in sync with ``versions.json`` (produced by
``tools/update_docs_versions.py``).
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

DEFAULT_BASE_URL = "https://fluorescence-tools.github.io/tttrlib"


def build_entries(versions: list, base_url: str) -> list:
    base = base_url.rstrip("/")
    entries = []
    for v in versions:
        if not isinstance(v, dict):
            continue
        ver = str(v.get("version", "")).strip()
        if not ver:
            continue
        entries.append(
            {
                "name": "development" if ver == "dev" else ver,
                "version": ver,
                "url": f"{base}/{ver}/",
                "preferred": bool(v.get("stable", False)),
            }
        )
    return entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--versions", type=Path, required=True,
                        help="Path to versions.json")
    parser.add_argument("--output", type=Path, required=True,
                        help="Path to write switcher.json")
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    args = parser.parse_args()

    versions = []
    if args.versions.exists():
        try:
            data = json.loads(args.versions.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                versions = data.get("versions", [])
        except json.JSONDecodeError:
            versions = []

    entries = build_entries(versions, args.base_url)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
