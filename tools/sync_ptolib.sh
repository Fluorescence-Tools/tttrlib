#!/usr/bin/env sh
# Refresh thirdparty/ptolib from a sibling ptolib checkout: verbatim copies
# (what is committed -- a clone has no checkout beside it), or symlinks for a
# working tree while editing both sides.
#
#   tools/sync_ptolib.sh [path-to-ptolib-checkout]         # copies (default ../ptolib)
#   tools/sync_ptolib.sh --link [path-to-ptolib-checkout]  # symlinks, never committed
#
# ptolib (https://github.com/tpeulen/ptolib) is the source of truth for the PTO
# container and the DataStore; test/python/misc/test_vendored_ptolib.py
# compares the files against the checkout and refuses a symlink.
set -eu
mode=
if [ "${1:-}" = "--link" ]; then mode=--link; shift; fi
if [ "${1:-}" = "--copy" ]; then mode=; shift; fi
here=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-$here/../ptolib}
[ -f "$src/scripts/vendor.sh" ] || { echo "no ptolib checkout at $src" >&2; exit 1; }
sh "$src/scripts/vendor.sh" $mode "$here/thirdparty/ptolib"
