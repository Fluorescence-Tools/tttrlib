# PRD-018 — A stable ABI within a minor series

> **PRD #:** 018 · **Status:** Draft · **Created:** 2026-08-06 · **Updated:** 2026-08-06 · **Owner:** tpeulen
> **Related:** the plugin ABI shipped in `a127eb53`, the module system in `a66d5fa1`, PRD-016 (JS bindings — Node-API is the worked example of a stable ABI paying off), PRD-003 (packaging)
>
> **Sequencing note (2026-08-06).** PRD-019 has already spent the window this PRD
> will close: `DataStore` gained a `groups_` member, `BitMask` lost a constructor
> from the Java surface, and R, Java and JavaScript each gained a slice of public
> API (`DataStore`, `Hdf5Table`, `Registry`, and eleven `Column::get_*_into`
> accessors in Java). Doing that before any ABI promise existed was the right
> ordering and it was free. Once this lands it stops being free — so no further
> members on `DataStore` after the SOVERSION and the export list are in place.

## Summary

Make binary compatibility an explicit, tested promise instead of an accident:

> **Within one minor series `0.MINOR.*`, a tttrlib shared library is a drop-in
> replacement for any earlier patch release in that series.** Upgrading
> `0.27.0 → 0.27.3` never requires a downstream consumer to recompile or relink.
> The promise is voided, deliberately and visibly, at `0.28.0`.

Plus one guarantee that outlives the series: **the C plugin ABI (`tttrlib_plugin_init_v1`)
keeps working across minor and major bumps**, which is the whole reason it is C.

Today none of this is enforced. The conda recipe already *asserts* it —
`run_exports: pin_subpackage(upper_bound="x.x")` tells every downstream package that
`0.27.x` are interchangeable — while the build ships an unversioned `libtttrlib.so`
with every symbol exported and no check that anything survived a patch.

## Problem / motivation

Four separate binary boundaries exist in the tree right now, and they have four
different (mostly unstated) compatibility stories:

| # | Boundary | Artefact | Consumer | Guarantee today |
|---|---|---|---|---|
| 1 | Plugin C ABI | `tttrlib_<name>.so` ↔ host | third-party plugin authors | designed for it; **untested, and one check is backwards** |
| 2 | Installed C++ library | `libtttrlib.so` + `include/tttrlib/*.h` | C++ apps, conda downstreams | **none** — no SOVERSION, no export control |
| 3 | Intra-package modules | `libtttrlib_core.so` … next to `_tttrlib` | tttrlib itself | none needed, none stated |
| 4 | Static archive | `libtttrlib_static.a` | the R package | rebuilt every time; N/A |

The concrete failure modes:

**Nothing stops a patch release from breaking (2).** `cmake/TTTRLibModule.cmake:142`
records the decision not to version the module libraries, and notes that "the versioned
C++ library for system consumers is a separate target and keeps its soname" — but
`CMakeLists.txt:480` sets no `VERSION`/`SOVERSION` on `tttrlibShared` either. So
`libtttrlib.so` has no soname beyond its own filename. A user who upgrades the library
under a binary that was linked against the previous patch gets, in the good case, a
loader error naming a mangled symbol; in the bad case, a struct whose layout moved and a
silent wrong answer.

**The ABI surface is currently "everything".** ELF default visibility is forced ON for
module targets (`CXX_VISIBILITY_PRESET default`, correct, they exist to be linked
against) and `WINDOWS_EXPORT_ALL_SYMBOLS ON` stands in for export macros that were never
written — the comment scopes the missing annotation at ~97 sites. Every non-inline
function in 103 module headers is therefore exported and, absent a policy, notionally
part of the contract. **A promise over an unbounded surface is unaffordable**: it makes
every internal helper rename an ABI break. Bounding the surface is what makes the
guarantee cheap enough to keep.

**The plugin ABI's forward-compatibility check runs the wrong way.**
`PluginHost.cpp:345` (and `:396`, `:435`) rejects a table whose `struct_size` is *smaller*
than the host's:

```cpp
if (c == nullptr || c->struct_size < sizeof(tttrlib_container_v1) || …) return TTTRLIB_INVALID;
```

Contract 2 in `tttrlib_plugin.h` says fields may only be appended and the reader checks
the size before touching a late field. A plugin built against today's header has exactly
today's `sizeof`; the moment a field is appended, **every already-shipped plugin fails to
register on the new host** — the precise outcome the design exists to prevent. The check
should reject only tables too small to contain the fields the host actually requires,
and gate each later field on the reported size.

**Downstreams are being told a promise that is not kept.** `pin_subpackage(upper_bound="x.x")`
means a conda package built against `tttrlib 0.27.0` is considered satisfied by
`0.27.9`. That is the right policy — this PRD adopts it — but it is currently metadata
with nothing behind it.

**Why now.** The module split just landed and the plugin ABI is two commits old. Symbol
visibility, library naming and soname are all being touched anyway. Deciding the policy
after the export macros are written means writing them twice.

## Goals

1. **A written policy** stating what is guaranteed at each of the four boundaries, in
   `doc/abi-policy.rst`, linked from `BUILDING.md` and `doc/plugins.rst`.
2. **A versioned soname.** `libtttrlib.so.0.27` — `SOVERSION` = the minor series, so the
   loader itself enforces the boundary and a minor bump cannot be mistaken for a patch.
3. **A bounded exported surface.** An explicit `TTTRLIB_API` marker; everything unmarked
   is hidden and free to change at any time. This is the load-bearing item.
4. **Mechanical enforcement.** A checked-in ABI baseline per series and a CI job that
   fails a patch-level change removing or altering an exported symbol. Additions pass.
5. **Plugin ABI backward compatibility, fixed and tested** — a plugin binary built
   against the `0.27` headers loads on a later host, verified by a test that keeps an
   old-shaped table around on purpose.
6. **A release checklist item** that regenerates the baseline on a minor bump and records
   the break in `CHANGELOG.md`.

## Non-goals

- **Cross-toolchain C++ ABI.** Nothing here makes a GCC-built `libtttrlib.so` usable from
  MSVC, or survives a `_GLIBCXX_USE_CXX11_ABI` flip. The public headers pass
  `std::vector`, `std::string` and `std::map` by value; that pins the standard library
  and always will. The C++ guarantee is *same toolchain, same flags, later patch*. The
  cross-toolchain boundary is the plugin ABI, and it is C for exactly this reason.
- **Source compatibility.** Already a standing project constraint ("no public API
  removals or signature changes", asserted per-PRD — e.g. PRD-012 criterion 9). ABI
  stability is strictly stronger and does not replace it: adding a data member breaks ABI
  while leaving source untouched.
- **A stable ABI for the language bindings.** The Python extension, the R package and the
  Java native are rebuilt against their own runtimes. Python's limited API (`abi3`) is
  out of reach while SWIG generates the wrappers, and is not worth pursuing here.
- **Freezing the module libraries (boundary 3).** They are private to the install tree
  and must stay free to change; this PRD only asks that they stop *looking* public.
- **Extending the promise across minors.** `0.27 → 0.28` may break anything. That is the
  release valve that keeps the promise credible.
- **Retrofitting a baseline onto released versions.** The first baseline is whatever
  `0.28.0` (or the next minor) exports. Nothing is claimed about `0.27.x` retroactively.

## The policy, stated precisely

**Compatibility unit: the minor series.** For `0.MINOR.PATCH`, the series is
`0.MINOR`. (At `1.0` this becomes the major, with `SOVERSION` following; the parsing
already reads the version from `pyproject.toml`, so the rule can be expressed once in
CMake and stay correct across that transition.)

**Within a series, these are forbidden on anything marked `TTTRLIB_API`:**

| Change | Why it breaks |
|---|---|
| Remove or rename an exported function, class, or global | unresolved symbol at load |
| Change any parameter or return type, or add a parameter | different mangled name; silently "missing" |
| Add, remove, reorder or retype a **data member** | every caller's `sizeof`/offsets are stale |
| Add, remove or reorder a **virtual** function | vtable slots shift; wrong function called, no diagnostic |
| Change a base class, or add a first virtual to a non-polymorphic class | layout moves |
| Change the body of an **inline / header-defined / template** function | the old body is already baked into callers; the two coexist and ODR-violate |
| Change an enum's underlying type, or the value of an existing enumerator | values are baked into callers |
| Narrow behaviour a caller could already rely on | not an ABI break, but a compat break; treat the same |

**Within a series, these are allowed:**

- Adding a new exported function, class or enumerator *appended* to the end.
- Adding a defaulted parameter **only** via an overload — a default argument is a source
  feature, evaluated at the call site; changing one changes what old binaries pass.
- Anything at all to an unmarked (hidden) symbol, or to a module library.
- Bug fixes that change results, provided signatures and layouts hold.

**The plugin ABI is versioned separately and independently of the release version.**
`tttrlib_plugin_init_v1` and the `_v1` structs are permanent. A breaking redesign exports
`tttrlib_plugin_init_v2` and keeps v1 loading. Struct growth inside v1 is append-only and
gated on `struct_size` **in both directions**.

## Proposed approach

### 1. Fix the plugin `struct_size` check first (small, and shipping already)

It is three call sites and a rule: require only the fields the host genuinely dereferences,
and read anything beyond the original v1 footprint only after confirming `struct_size`
covers it. Freeze the original footprint as a named constant per struct
(`TTTRLIB_CONTAINER_V1_MIN_SIZE`) so the minimum cannot drift upward by accident when a
field is appended — a `sizeof` in that position is exactly how this regressed.

### 2. Give the aggregate library a soname

In the top-level `CMakeLists.txt`, next to the existing version parse:

```cmake
set(TTTRLIB_ABI_SERIES "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")  # 1.x → major only
set_target_properties(${PROJECT_NAME}Shared PROPERTIES
        VERSION   "${PROJECT_VERSION}"
        SOVERSION "${TTTRLIB_ABI_SERIES}")
```

Module libraries keep no soname — `cmake/TTTRLibModule.cmake:142` already argues that
correctly (three copies in a wheel), and this PRD adds the second reason: they are not a
public boundary. Make that explicit in the comment, and consider whether their install
name should say so.

### 3. Bound the surface — `TTTRLIB_API`

The real work. A generated export header (`GenerateExportHeader`, or a hand-written one)
providing `TTTRLIB_API` = `__declspec(dllexport/dllimport)` on Windows,
`__attribute__((visibility("default")))` elsewhere. Then, for the aggregate library only,
build with hidden visibility and drop `WINDOWS_EXPORT_ALL_SYMBOLS` — the two are the same
decision expressed per-platform, and holding them apart is what left Windows and ELF with
different surfaces.

Marking is a judgement call per class, not a sweep: **anything a test, a SWIG interface,
an example or a downstream calls is in; everything else starts out**. Recommended order:
mark, then let the link failures in `ext/` and the test suites enumerate what was missed.
That inverts the ~97-site estimate from a guess into a work list. Modules keep default
visibility as they do today (`TTTRLibModule.cmake` explains why: hidden there fails the
split wheel at import).

Expect this step to reveal that some *headers* are internal and should not install at
all. `CMakeLists.txt:499` globs `modules/**/*.h*` into `include/tttrlib/` flat — every
private header in 103 becomes public by glob.

### 4. Baseline and check

Options considered:

- **libabigail (`abidiff`)** — reads DWARF, understands C++ mangling, vtables and member
  layout, and has a suppression file format. Linux-only. **Recommended.**
- **`abi-compliance-checker`** — header-driven, more portable in principle, much noisier
  and effectively unmaintained.
- **A symbol-list diff (`nm -D --defined-only`)** — catches removals and renames only,
  never a layout change, which is the failure mode with no diagnostic. Cheap, and a
  reasonable fallback for a first cut; not sufficient alone.
- **A link-and-run compatibility test** — build a small consumer against the series' first
  release's headers, then run it against the current library. Catches what matters, in
  the way a user meets it, and costs a CI job. **Recommended as well** — it is the only
  one of these that tests the loader rather than a model of it.

So: `abi/tttrlib-0.MINOR.abi` (a `abidw` dump) checked in, regenerated only on a minor
bump; CI runs `abidiff --no-added-syms` against the built library and fails on anything
else. Additions must pass, or every feature patch is red.

The Linux-only limitation is acceptable — struct layout and vtable errors are the same
mistakes on every platform, so catching them on one is enough to catch them.

### 5. Write it down

`doc/abi-policy.rst`: the table above, the release rules, what a downstream may assume.
`doc/plugins.rst` gains the cross-version statement for `_v1`. `CHANGELOG.md` gains an
**ABI** line on every minor bump saying what moved. `BUILDING.md` links the policy from
the options table.

## Acceptance criteria

1. `doc/abi-policy.rst` exists, states the guarantee per boundary, and is linked from
   `BUILDING.md` and `doc/plugins.rst`.
2. `libtttrlib.so` installs with `SOVERSION` = the minor series (`libtttrlib.so.0.28` →
   `libtttrlib.so.0.28.0`); `readelf -d` shows the soname. macOS gets the corresponding
   `compatibility_version`.
3. `TTTRLIB_API` exists; the aggregate library builds with hidden visibility and
   *without* `WINDOWS_EXPORT_ALL_SYMBOLS`; all four bindings and every test still build
   and pass on Linux, macOS and Windows.
4. An `abi/tttrlib-<series>.abi` baseline is checked in and a CI job runs `abidiff`
   against it. A deliberately removed exported method fails the job; a deliberately added
   one passes it. **Both directions must be demonstrated** — a check that never fires is
   indistinguishable from no check.
5. A consumer built against the series' first release runs unmodified against a later
   patch build of the library, in CI, without recompiling.
6. A plugin built against a *frozen copy* of the `0.27` `tttrlib_plugin.h` — kept in the
   test tree precisely so it cannot be updated in step — registers and runs on the current
   host, after a field is appended to `tttrlib_container_v1`.
7. Only headers intended to be public install into `include/tttrlib/`.
8. The release checklist covers baseline regeneration and the `CHANGELOG.md` ABI note.
9. No public API removals or signature changes (standing constraint) — this is additive
   build and doc work.

## Sequencing

1. Fix the three `struct_size` checks + the frozen-header plugin test. Independent of
   everything else and prevents a break that is otherwise coming.
2. Policy document. Cheap, and it settles the arguments in 3–4 before they cost code.
3. `SOVERSION` on the aggregate library.
4. `TTTRLIB_API` + hidden visibility + the public-header audit. The long pole.
5. Baseline + `abidiff` CI job + the link-and-run consumer test.
6. Baseline the first series at the next minor bump; add the release checklist item.

Steps 1–3 are worth doing regardless of whether 4 is ever finished. If 4 stalls, the
fallback is a symbol-list diff over the current unbounded surface — noisier, but it still
catches removals.

## Risks / notes

- **The surface audit is the whole cost, and it is easy to underestimate.** ~97 sites is
  the existing estimate for Windows export macros; the real number is whatever survives
  after deciding which of 103 headers are public. If that lands badly, the honest
  fallback is to scope the guarantee to a curated subset (`TTTR`, `CLSMImage`, the
  correlators, the plugin ABI) and say so explicitly, rather than to promise everything
  and enforce nothing. **A narrow kept promise beats a broad one.**
- **Header-defined code leaks into callers and is the trap nobody sees.** Anything inline
  or templated in a public header is compiled into the consumer's binary; changing its
  body inside a series is an ABI break that `abidiff` will not report and no test will
  catch. It is a review rule, not a tool rule, and belongs in the policy document in bold.
- **Hidden visibility has bitten this build before.** `TTTRLibModule.cmake` records that
  the wheel's global `CXX_VISIBILITY_PRESET=hidden` would have made the first split wheel
  fail at import with undefined symbols. Step 4 must apply to the aggregate library only
  and leave the module targets exactly as they are.
- **`run_exports` is already making the promise.** Until the check exists, a downstream
  conda package can be broken by a tttrlib patch release with no signal anywhere. This is
  the argument for doing steps 3 and 5 ahead of step 4.
- **0.x conventionally means "no promises".** Choosing a stricter reading is fine but must
  be documented, or a downstream will assume SemVer's default and pin `==0.27.0`.
- **A stable ABI can ossify a design.** The escape hatch is the minor bump, and it should
  be used without apology when a design is wrong. The commitment is *"we tell you, and the
  loader tells you"*, not *"we never change"*.
- **Windows is the weakest link.** `abidiff` will not run there and `WINDOWS_EXPORT_ALL_SYMBOLS`
  makes the current DLL surface compiler-determined. Fixing it comes free with step 4;
  until then, treat the Windows guarantee as unverified and say so in the policy.

## References

- `modules/plugin/include/tttrlib_plugin.h` — the five contracts; the model this PRD
  generalises
- `modules/plugin/src/PluginHost.cpp:345,396,435` — the inverted `struct_size` checks
- `cmake/TTTRLibModule.cmake:142` — why module libraries carry no soname
- `CMakeLists.txt:480–530` — the aggregate library targets, header glob and install rules
- `recipes/py/recipe.yaml:52` — `run_exports` already pinning to the minor series
- libabigail: <https://sourceware.org/libabigail/> · KDE's policy is the clearest prior
  art for the rules table: <https://community.kde.org/Policies/Binary_Compatibility_Issues_With_C++>
