#!/usr/bin/env python
# SPDX-License-Identifier: BSD-3-Clause
"""Dump tttrlib's complete API index to a JSON file.

The index itself is built by :func:`tttrlib.api_index`, which derives it from the
built module — around 110 classes and 2000 methods, with signatures, defaults and
docstrings. This script is only a command-line front end for writing it out, so
the logic lives in the library where every language binding can reach it rather
than in a script only the build can run.

The JSON lets scripting languages discover and call tttrlib without parsing C++
headers. For the curated subset that carries semantics — parameter units, ranges
and meanings, which a signature cannot express — see :func:`tttrlib.registry`.

Usage::

    python tools/generate_api_index.py -o tttrlib_api.json
    python tools/generate_api_index.py --registry -o tttrlib_registry.json

Run it in the environment tttrlib is installed in; it imports the module.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "-o", "--output", type=pathlib.Path, default=None,
        help="write here instead of stdout",
    )
    parser.add_argument(
        "--registry", action="store_true",
        help="dump the curated registry instead of the full API index",
    )
    parser.add_argument(
        "--indent", type=int, default=1,
        help="JSON indentation (0 for the most compact output)",
    )
    args = parser.parse_args(argv)

    import tttrlib

    payload = tttrlib.registry() if args.registry else tttrlib.api_index()
    text = json.dumps(payload, indent=args.indent or None, sort_keys=False)

    if args.output is None:
        sys.stdout.write(text + "\n")
        return 0

    args.output.write_text(text + "\n")
    if args.registry:
        summary = ", ".join(f"{k}: {len(v)}" for k, v in payload.items())
    else:
        summary = (
            f"{payload['n_classes']} classes, {payload['n_methods']} methods, "
            f"{payload['n_functions']} functions"
        )
    print(f"{args.output}: {summary}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
