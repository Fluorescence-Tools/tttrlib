# PRD-003 — Documentation deploy via rattler

> **PRD #:** 003 · **Status:** In Progress · **Created:** 2026-07-03 · **Owner:** tpeulen

## Summary

Make the gh-pages documentation deployment consume the docs artifact produced by
the rattler `recipes/docs` build, so the docs are *built once* (reproducibly, via
rattler) and simply *published* by the deploy job.

## Problem / motivation

Docs are now built two ways:

- `recipes/docs/recipe.yaml` + the `build_conda_docs` CI job build the HTML
  reproducibly via rattler-build (this PRD's foundation, already in place).
- The legacy `build_docs` job *also* builds the HTML with an ad-hoc pip+sphinx
  setup and then deploys it to gh-pages.

That is a duplicate build. The deploy job was intentionally left on the proven
path because extracting HTML from a `.conda` and wiring it into the intricate
versioned gh-pages deploy was too risky to do blind. This PRD finishes the
unification.

## Goals

- Single source of truth for the doc build: `recipes/docs`.
- `build_docs` deploy steps consume the rattler-built HTML (extracted from the
  `tttrlib-docs` package or a direct artifact), preserving the existing
  version-selector and release/stable/dev deploy targets.
- No regression in what is published.

## Non-goals

- Changing the Sphinx sources, theme, or the gh-pages layout.

## Proposed approach

1. Have `recipes/docs/build.sh` (or the CI job) expose the built HTML as a plain
   artifact in addition to packaging it (avoids fragile `.conda` extraction).
2. In `build_docs`, replace the pip-install + `sphinx-build` steps with:
   download the docs artifact → place at `doc/_build/html/stable` → keep the
   existing checks, version-selector, and `peaceiris/actions-gh-pages` deploys.
3. Delete the duplicate build once parity is confirmed on a dev deploy.

## Milestones

- **M1** — `build_conda_docs` uploads the HTML as a directory artifact.
- **M2** — `build_docs` consumes it for the `dev` deploy target; verify output.
- **M3** — switch `stable`/release targets; remove the ad-hoc sphinx build.

## Risks

- The rattler doc build downloads reference data and runs the full gallery; it is
  heavier and slower than the ad-hoc build. Cache reference data; keep
  `TTTRLIB_DOCS_EXECUTE_EXAMPLES=0`.
- First rattler docs build may need a missing conda-forge doc dependency name or a
  `BUILD_TIER` tweak (isolated to `build_conda_docs`).

## References

- `recipes/docs/recipe.yaml`, `recipes/docs/build.sh`
- `.github/workflows/ci.yml` jobs `build_conda_docs`, `build_docs`
