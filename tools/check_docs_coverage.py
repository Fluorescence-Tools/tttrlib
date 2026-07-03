#!/usr/bin/env python3
"""Check that documented analysis domains have source, examples, and HTML."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def load_inventory(path: Path) -> dict[str, Any]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ValueError(
            f"{path} must use JSON-compatible YAML syntax: {exc}"
        ) from exc


def read_joined(paths: list[Path]) -> str:
    parts: list[str] = []
    for path in paths:
        if path.exists():
            parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def rendered_example_path(example: str, html_root: Path) -> Path:
    prefix = "examples/"
    suffix = ".py"
    if not example.startswith(prefix) or not example.endswith(suffix):
        return html_root / "__invalid_example_path__"
    rel = example[len(prefix):-len(suffix)] + ".html"
    return html_root / "auto_examples" / rel


def as_list(domain: dict[str, Any], key: str) -> list[str]:
    value = domain.get(key, [])
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise ValueError(f"domain {domain.get('name', '<unnamed>')!r} has invalid {key!r}")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inventory", type=Path)
    parser.add_argument("doc_root", type=Path)
    parser.add_argument("html_root", type=Path)
    args = parser.parse_args()

    inventory_path = args.inventory.resolve()
    doc_root = args.doc_root.resolve()
    html_root = args.html_root.resolve()
    repo_root = doc_root.parent

    if not inventory_path.is_file():
        print(f"Documentation coverage inventory does not exist: {inventory_path}", file=sys.stderr)
        return 2
    if not doc_root.is_dir():
        print(f"Documentation source root does not exist: {doc_root}", file=sys.stderr)
        return 2
    if not html_root.is_dir():
        print(f"Documentation HTML root does not exist: {html_root}", file=sys.stderr)
        return 2

    try:
        inventory = load_inventory(inventory_path)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    domains = inventory.get("domains")
    if not isinstance(domains, list) or not domains:
        print("Documentation coverage inventory must contain a non-empty 'domains' list", file=sys.stderr)
        return 2

    errors: list[str] = []
    reports: list[str] = []
    checked_symbols = 0

    for domain in domains:
        if not isinstance(domain, dict):
            errors.append("domain entry is not an object")
            continue

        name = str(domain.get("name", "<unnamed>"))
        try:
            source_pages = as_list(domain, "source_pages")
            html_pages = as_list(domain, "html_pages")
            examples = as_list(domain, "examples")
            symbols = as_list(domain, "symbols")
        except ValueError as exc:
            errors.append(str(exc))
            continue

        if not source_pages:
            errors.append(f"{name}: no source pages listed")
        if not html_pages:
            errors.append(f"{name}: no HTML pages listed")
        if not examples:
            errors.append(f"{name}: no examples listed")
        if not symbols:
            errors.append(f"{name}: no symbols listed")

        source_paths = [doc_root / path for path in source_pages]
        html_paths = [html_root / path for path in html_pages]
        example_paths = [repo_root / path for path in examples]

        for path in source_paths:
            if not path.is_file():
                errors.append(f"{name}: missing source page {path.relative_to(repo_root)}")
        for path in html_paths:
            if not path.is_file():
                errors.append(f"{name}: missing rendered HTML {path.relative_to(html_root)}")
        for path in example_paths:
            if not path.is_file():
                errors.append(f"{name}: missing example {path.relative_to(repo_root)}")
        for example in examples:
            rendered_path = rendered_example_path(example, html_root)
            if not rendered_path.is_file():
                errors.append(
                    f"{name}: missing rendered gallery page "
                    f"{rendered_path.relative_to(html_root)} for {example}"
                )

        source_text = read_joined(source_paths)
        html_text = read_joined(html_paths)
        for symbol in symbols:
            checked_symbols += 1
            if symbol not in source_text:
                errors.append(f"{name}: symbol {symbol!r} is not covered in listed source pages")
            if symbol not in html_text:
                errors.append(f"{name}: symbol {symbol!r} is not present in listed rendered HTML")

        reports.append(
            f"  - {name}: {len(source_pages)} source page(s), "
            f"{len(html_pages)} rendered page(s), {len(examples)} example(s), "
            f"{len(symbols)} symbol(s)"
        )
        reports.append(f"    pages: {', '.join(html_pages)}")
        reports.append(f"    symbols: {', '.join(symbols)}")

    if errors:
        print("Documentation coverage check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"Checked documentation coverage for {len(domains)} domains "
        f"and {checked_symbols} API symbols"
    )
    for report in reports:
        print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
