# Build trees go in `build/<name>`

A convention, not a check. Nothing enforces it and nothing should — a build
directory in the wrong place costs disk and tidiness, not correctness, and a
CMake guard that refuses to configure would break whoever is mid-build for a
problem `.gitignore` already contains.

## The rule

```
cmake -S . -B build/<name>
```

Everything else follows: `CMakePresets.json` uses `${sourceDir}/build/${presetName}`,
scikit-build-core uses `build/{wheel_tag}` from `pyproject.toml`, and CI uses
`build/r`, `build/java`, `build/js`, `build/js-asan`. A directory entirely
outside the checkout is equally fine and always was.

Do not add a new top-level `build-something/` or `cmake-build-something/`. They
are ignored so they cannot be committed by accident, but that is a safety net,
not permission.

## Why it is worth keeping to

The root had collected eight build trees — `build`, `build_new`, `build_py312`,
`build-r`, `build-js`, `build-java`, `cmake-build-debug`, `cmake-build-release`
— **1.7 GB**. That alone is only untidy. The part that mattered: `build_*` with
an underscore matched none of the ignore rules that existed, so `git status`
offered **480 MB of object files** as untracked files sitting next to the
sources, one `git add -A` away from being committed.

`.gitignore` now covers `/build/`, `build-*/`, `build_*/` and `cmake-build-*/`,
so the accident is closed whichever name someone picks. The convention is what
keeps the directory listing readable; the ignore rules are what keep the
repository clean.

## What tests assume

Three tests look for a built CLI or plugin and glob `build/*` for it:

- `test/python/misc/test_cli_sm_burst_table.py` — newest `build/*/bin/tttr`
- `test/python/test_pto_names.py` — `build/*/bin/tttr`
- `test/python/plugin/test_plugins.py` — `build/*/examples/plugin/…`

They used to name `build_new/` and `cmake-build-debug/` explicitly, which is how
one developer's layout ended up hard-coded in the suite. A build outside
`build/` will not be found by them, and the tests skip rather than fail — so an
off-convention tree shows up as tests quietly not running, which is the one way
this can cost more than disk.

`TTTRLIB_CLI` and `TTTRLIB_EXAMPLE_PLUGIN` override the search if you need to
point them somewhere else.
