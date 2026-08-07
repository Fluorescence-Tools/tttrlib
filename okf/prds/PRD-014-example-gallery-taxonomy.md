# PRD-014 — Example gallery: one taxonomy, nothing invisible

> **PRD #:** 014 · **Status:** Proposed · **Created:** 2026-08-05 · **Owner:** tpeulen
> **Related:** PRD-003 (docs deploy), PRD-009 (documenting every feature by simulated example)

## Summary

Reorganise `examples/` into an explicitly ordered taxonomy where every file is
reachable from the rendered gallery, every section has a header, and the order a
reader meets the examples in is a decision rather than an alphabetical accident.

## Problem / motivation

`examples/` holds **127 Python files in 14 directories**. The gallery is built by
sphinx-gallery from `doc/conf.py` with `examples_dirs = [examples/]`, no
`subsection_order`, and no `within_subsection_order`. Four things follow.

**1. 21 files never appear in the documentation.** sphinx-gallery 0.19's
`get_subsections()` keeps only subfolders for which `_get_gallery_header(...)`
returns non-`None` — i.e. those with a `README.txt`. Folders without one are
dropped **silently**: no warning, no error, no entry.

| Folder | files | `plot_*` | `README.txt` | in gallery |
|---|--:|--:|:--:|:--:|
| `beginner` | 6 | 6 | yes | yes |
| `correlation` | 10 | 10 | yes | yes |
| `flim` | 18 | 18 | yes | yes |
| `fluorescence_decay` | 11 | 11 | yes | yes |
| `image_correlation` | 4 | 4 | yes | yes |
| `ism` | 6 | 5 | **NO** | **no** |
| `localization` | 4 | 0 | **NO** | **no** |
| `microscopy_flim` | 1 | 1 | **NO** | **no** |
| `microscopy_localization` | 6 | 6 | yes | 1 of 6 (5 blacklisted) |
| `miscellaneous` | 3 | 3 | yes | yes |
| `release_highlights` | 2 | 2 | yes | yes |
| `simulation` | 10 | 0 | **NO** | **no** |
| `single_molecule` | 32 | 32 | yes | yes |
| `tttr` | 10 | 8 | yes | yes |

The whole **`simulation/` section is invisible** — ten examples covering the
feature set PRD-005/008 shipped, including `alex_smfret.py`, `flow_fcs.py` and
`clsm_star_scan.py`. Nothing in the build tells anyone.

**2. The order is alphabetical, so it teaches nothing.** `tttr` — the
fundamentals — renders *last*, after `single_molecule`. `release_highlights`
lands in the middle of the analysis sections. That `beginner` comes first is
luck, not design.

**3. Sections have drifted into overlapping and oversized shapes.**
`localization` vs `microscopy_localization`, `flim` vs `microscopy_flim` are
duplicate topics with different vintages. `single_molecule` alone holds 32 files:
15 burst, 10 HMM, 3 CLSM super-resolution, 4 micro-time selection, 3 PDA — three
of those groups are not single-molecule-burst topics at all, and the CLSM
super-resolution trio belongs next to the other imaging examples.

**4. The stated conventions are not the enforced ones.** `examples/INFO.txt` says
files under a `"broken"` subfolder are ignored; there is no `broken/` anywhere,
while `correlation/not_ready/` and `flim/todo/` exist and are dropped by the
header rule rather than by any documented mechanism. Two loose files sit at the
gallery root (`clsm_memory_usage_example.py`, `microtime_linearization_example.py`)
where they render above every section, unsectioned. 21 of the 127 files do not
match `filename_pattern = plot_.*\.py$`, so they render as source with no output
and no figure — indistinguishable, to a reader, from an example that produced
nothing.

## Goals

1. Every file under `examples/` is either **in the gallery** or **explicitly
   parked** in a location the build is documented to skip. No silent drops.
2. An explicit, pedagogical section order, set in `doc/conf.py`, not inherited
   from `os.listdir()`.
3. An explicit within-section order, so an author controls where a new example
   lands.
4. No duplicate/overlapping sections; no section above ~20 files.
5. A **CI check** that fails when a new example folder has no header, or a file
   is neither `plot_*` nor parked — so this cannot silently rot again.

## Non-goals

- Rewriting example *content*, fixing broken examples, or changing what they
  compute. Files move and are renamed; their bodies are untouched except for the
  title underline when a title changes.
- Executing the gallery in CI (`TTTRLIB_DOCS_EXECUTE_EXAMPLES` stays as-is).
- The blacklist mechanism itself (`doc/gallery_blacklist.txt`) — it keeps working
  and keeps its five `microscopy_localization` entries until those examples are
  fixed.

## Proposed approach

### Target taxonomy

Twelve sections in reading order, from "what is a TTTR file" to specialised
analyses. Numbers are the file counts that land there.

| # | Section | Source of files | n |
|--:|---|---|--:|
| 1 | `01_beginner` | `beginner/` | 6 |
| 2 | `02_tttr` | `tttr/` + the two loose root files | 12 |
| 3 | `03_simulation` | `simulation/` | 10 |
| 4 | `04_decay` | `fluorescence_decay/` | 11 |
| 5 | `05_correlation` | `correlation/` | 10 |
| 6 | `06_image_correlation` | `image_correlation/` | 4 |
| 7 | `07_flim` | `flim/` + `microscopy_flim/` | 19 |
| 8 | `08_superres` | `single_molecule/plot_clsm_superres_*` + `ism/` | 9 |
| 9 | `09_localization` | `localization/` + `microscopy_localization/` | 10 |
| 10 | `10_bursts` | `single_molecule/` burst + micro-time selection | 19 |
| 11 | `11_hmm` | `single_molecule/plot_hmm_*` | 10 |
| 12 | `12_pda` | `single_molecule/*pda*` | 3 |
| — | `miscellaneous`, `release_highlights` | unchanged, pinned last | 5 |

The numeric prefix is on the *directory*, so `ExplicitOrder` and a plain
alphabetical fallback agree — a reader cloning the repo sees the same order as
the website even without Sphinx.

### Ordering

```python
from sphinx_gallery.sorting import ExplicitOrder, FileNameSortKey

sphinx_gallery_conf = {
    ...,
    "subsection_order": ExplicitOrder([...the twelve, then miscellaneous,
                                       release_highlights...]),
    "within_subsection_order": FileNameSortKey,
}
```

Within a section, files are renamed to `plot_NN_<topic>.py` as `beginner/`
already does. `FileNameSortKey` is then a *decision*, not a coincidence.

### Non-`plot_` files

Three cases, decided per file, and every one of the 21 gets a decision recorded
in the migration table:

- **runnable example** → rename to `plot_*` (this is most of `simulation/`;
  they run and produce figures, they simply never got the prefix).
- **helper imported by examples** → prefix `_` (already ignored via
  `ignore_pattern = ^_`), or move into `examples/_example_data.py`.
- **not an example** → `examples/localization/test_localization.py` is a test;
  it moves to `test/python/` or is deleted.

### Parking, explicitly

`correlation/not_ready/`, `flim/todo/` and anything else not ready move to
**`examples/_incubator/<topic>/`**. The leading `_` is already excluded by
`ignore_pattern`, so the exclusion is mechanical rather than a matter of the
folder happening to lack a header. `INFO.txt` is rewritten to describe what the
build actually does: `_`-prefixed paths are ignored, `doc/gallery_blacklist.txt`
drops individual basenames, everything else must be in the gallery.

### The guard

`tools/check_examples_layout.py`, run in the docs CI job and as a pytest:

1. every directory under `examples/` that contains a `.py` file has a
   `README.txt`, **or** its path contains a `_`-prefixed component;
2. every non-`_` `.py` file is `plot_*` **or** listed in the blacklist;
3. every section in `ExplicitOrder` exists and every existing section is in
   `ExplicitOrder` (this is the check that catches a new folder);
4. no section exceeds 24 files.

Failure prints the offending path and the rule. This is the deliverable that
keeps the rest from decaying — the reorganisation is one afternoon, the guard is
what makes it hold.

## Milestones

- **M1 — inventory & guard.** `tools/check_examples_layout.py` written against
  the *current* tree, listing all violations; wired into CI as non-blocking.
  Produces the authoritative migration table (127 rows: old path → new path or
  `_incubator` or delete).
- **M2 — headers first.** Add `README.txt` to `ism`, `localization`,
  `microscopy_flim`, `simulation`. **This alone makes 21 examples visible** and
  is independently shippable; do it first and confirm on the built site.
- **M3 — move & rename.** Execute the migration table with `git mv` (one commit
  per section, so a bad move is revertable). Update `examples/README.rst` quick
  links and any `:ref:` targets in `doc/` that point at the old
  `sphx_glr_auto_examples_*` anchors.
- **M4 — order.** `subsection_order` / `within_subsection_order` in
  `doc/conf.py`; renumber filenames within sections.
- **M5 — enforce.** Guard becomes blocking; `INFO.txt` rewritten; incubator
  documented.

## Risks

- **Stale deep links.** Every renamed file changes its
  `sphx_glr_auto_examples_<section>_<file>` anchor, breaking external links and
  any in-repo `:ref:`. Mitigation: grep `doc/` for `sphx_glr_auto_examples_`
  before M3 and update in the same commit; consider `redirects` in the theme for
  the handful of examples linked from the README.
- **`ExplicitOrder` raises on an unlisted folder.** That is the desired
  behaviour (it is the same check as guard rule 3), but it turns "someone added
  a folder" into a docs-build failure. Acceptable — the failure message names
  the folder.
- **Examples that only *appear* to work.** Making `simulation/` visible exposes
  ten never-rendered examples. `plot_gallery` is off by default, so they will
  not be executed by the docs build; expect a follow-up round of fixes when
  `TTTRLIB_DOCS_EXECUTE_EXAMPLES=1` is next run. Track those in PRD-009, not
  here.

## Acceptance

- `python tools/check_examples_layout.py` exits 0.
- A docs build lists twelve sections in the specified order, and the number of
  rendered examples equals `127 − |incubator| − |blacklist| − |_-prefixed|`,
  asserted by the guard.
- No `.py` file under `examples/` is unaccounted for in the migration table.
