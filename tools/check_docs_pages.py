#!/usr/bin/env python3
"""Basic structural checks for generated HTML documentation."""

from __future__ import annotations

import argparse
import sys
from html.parser import HTMLParser
from pathlib import Path


class PageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.in_title = False
        self.title_parts: list[str] = []
        self.body_seen = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        if tag.lower() == "title":
            self.in_title = True
        if tag.lower() == "body":
            self.body_seen = True

    def handle_endtag(self, tag: str) -> None:
        if tag.lower() == "title":
            self.in_title = False

    def handle_data(self, data: str) -> None:
        if self.in_title:
            self.title_parts.append(data)

    @property
    def title(self) -> str:
        return "".join(self.title_parts).strip()


def check_page(path: Path, root: Path) -> list[str]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8", errors="replace")
    lower = text.lower()

    if "</html>" not in lower:
        errors.append("missing closing html tag")

    parser = PageParser()
    parser.feed(text)
    if not parser.title:
        errors.append("missing title")
    if not parser.body_seen:
        errors.append("missing body")

    forbidden = [
        "traceback (most recent call last)",
        "sphinx.errors.",
        "systemerror:",
        "segmentation fault",
    ]
    for marker in forbidden:
        if marker in lower:
            errors.append(f"contains {marker!r}")

    rel = path.relative_to(root)
    return [f"{rel}: {error}" for error in errors]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("html_root", type=Path)
    args = parser.parse_args()

    root = args.html_root.resolve()
    if not root.is_dir():
        print(f"HTML root does not exist: {root}", file=sys.stderr)
        return 2

    pages = sorted(root.rglob("*.html"))
    if not pages:
        print(f"No HTML pages found under {root}", file=sys.stderr)
        return 2

    errors: list[str] = []
    for page in pages:
        errors.extend(check_page(page, root))

    if errors:
        print("Documentation page check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(f"Checked {len(pages)} HTML pages under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
