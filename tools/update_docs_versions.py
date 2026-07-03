#!/usr/bin/env python3
"""Merge the current docs build into a GitHub Pages versions manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def load_versions(path: Path) -> list[dict[str, str | bool]]:
    if not path.exists():
        return []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return []
    if isinstance(data, dict):
        data = data.get("versions", [])
    if not isinstance(data, list):
        return []
    return [item for item in data if isinstance(item, dict) and "version" in item]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--existing", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--url", required=True)
    parser.add_argument("--stable", action="store_true")
    args = parser.parse_args()

    versions = load_versions(args.existing) if args.existing else []
    versions = [v for v in versions if v.get("version") != args.version]
    if args.stable:
        for item in versions:
            item.pop("stable", None)
    versions.insert(
        0,
        {
            "version": args.version,
            "label": args.label,
            "url": args.url,
            "stable": bool(args.stable),
        },
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps({"versions": versions}, indent=2) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
