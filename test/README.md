# tttrlib tests

```bash
TTTRLIB_DATA=/path/to/tttr-data python -m pytest test/ -q
```

`TTTRLIB_DATA` overrides `data_root` in `test/settings.json`, which is the single
management point for file locations and test parameters. Without the data files
most tests skip rather than fail. On this project's usual environment
`pytest-qt` is broken and needs `-p no:pytest-qt`.

## Running only what is relevant

The whole suite is ~18 minutes warm (~33 cold, the first run after a checkout
pays for reading every image file off disk), which is far too slow to run after
every change. Narrow it by **cost**, by **subsystem**, or both.

### By cost -- `--lane`

```bash
pytest test/ --lane fast        # ~1.7 min, 97% of the tests  <- while working
pytest test/ --lane standard    # ~2.8 min, 98% of the tests  <- before pushing
pytest test/                    # ~18 min,  everything        <- before merging
```

Cost is wildly unevenly distributed, so this buys more than it looks like: **29
tests are 85% of the runtime**, and five of them in `clsm/test_ism_psf_model.py`
are a fifth of the suite on their own. Dropping those 29 leaves 98% of the tests
running in a tenth of the time. By group the money goes to `hmm` (46%), `clsm`
(25%) and `simulation` (24%); everything else together is under 6%.

(Figures measured on an 8-core macOS laptop, warm page cache. They are a shape,
not a promise -- what matters is the concentration, which is a property of the
tests, not the machine.)

Every test declares which tier it is in, measured as setup + call + teardown:

| marker | cost | in `fast` | in `standard` |
|---|---|---|---|
| *(none)* | < 1s | yes | yes |
| `slow` | 1-5s | no | yes |
| `heavy` | > 5s | no | no |
| `smoke` | any | **yes** | **yes** |

`smoke` exists because cost and coverage are not aligned. A few files have no
test under a second at all -- they open a large image per test -- so a purely
cost-based fast lane would drop them entirely. One test in each carries `smoke`
to keep the file represented. Any lane that would leave a file with no tests at
all says so, by name, rather than quietly shrinking its coverage.

**The tiers keep themselves honest.** Every run compares what each test actually
cost against the tier it declares and reports anything that has drifted:

```
========================= workload markers out of date =========================
    9.4s  mark `heavy`  test/python/hmm/test_gibbs.py::TestGibbs::test_converges
    0.2s  drop `slow`   test/python/tttr/test_TTTR.py::Tests::test_reading
```

This works from *any* run -- the fast lane catches a test that has crept over a
second just as well as a full run catches one that has grown past five. Add
`--strict-workload` to make it fail rather than report. There is no duration
file to regenerate: the numbers come from the run you just did.

### By subsystem -- `-m` and `--modules`

Every test file lives in a directory named after the subsystem it exercises, and
`conftest.py` turns that directory name into a marker automatically:

```bash
pytest test/ -m clsm                 # imaging only
pytest test/ -m "tttr or correlator" # two groups
```

`--modules` takes the names declared in `modules/` and expands each to the
groups that exercise it -- including through a dependency, because changing
`core` can break anything downstream of it:

```bash
pytest test/ --modules sim           # the simulator and what depends on it
pytest test/ --modules pda,io_pq
pytest test/ --modules core --lane fast    # both axes at once
```

The expansion is deliberately generous: a run that is slightly too wide costs
seconds, one that is too narrow costs a regression. The mapping is **derived
from `modules/**/CMakeLists.txt`** -- the `NAME`, `DEPENDS` and `TEST_DIR` each
module already declares -- so a new module is selectable the moment it is
declared, and nothing has to be kept in step by hand. (It used to be a dict in
`conftest.py`; it had drifted to list five modules that no longer existed while
missing all twelve `io_*` ones.)

A group that no module claims via `TEST_DIR` -- `bva`, `twocde` and
`correlator` today -- runs in *every* `--modules` invocation, and is named in
the output, on the same too-wide-is-cheaper principle.

CI runs `--lane standard` on branch pushes and the full suite on `main`.

## Layout

The module column is whichever modules name that directory in their `TEST_DIR`;
it is derived, not maintained here, so `pytest test/ --modules <name> --co -q`
is the authority if this table ever falls behind.

| directory | what it covers | module(s) |
|---|---|---|
| `tttr/` | photon-stream data model, readers, writers, transcoding | core, and the `io_*` readers |
| `clsm/` | confocal imaging, super-resolution, ISM | imaging |
| `correlator/` | correlation / FCS | *(unclaimed -- always runs)* |
| `decayfit/` | decay fitting, convolution, priors | decay |
| `burstfilter/` | burst search, selection, filtering | burst |
| `bva/`, `twocde/` | burst variance analysis, two-channel KDE | *(unclaimed -- always runs)* |
| `hmm/` | photon-by-photon hidden Markov models | hmm |
| `pda/` | photon distribution analysis | pda |
| `simulation/` | diffusion and photon emission | sim |
| `plugin/` | the drop-in binary plugin ABI and loader | plugin |
| `misc/` | cross-cutting helpers; always run | util, io_image |
| *(files directly in `test/python/`)* | registry, settings, DataStore, table I/O | registry, io_csv, io_hdf5_table, io_store, io_pto |
| `benchmarks/` | standalone perf scripts, not collected by pytest | -- |

`test/java/` and `test/r/` hold the bindings' own tests; see
`tools/check_swig_multilang.sh` after changing any `.i` file.

`test/conformance/` is different from all of the above: it holds **one case
list that all four bindings run**, so the expected values are shared rather than
copied. See `test/conformance/README.md`.

## Adding a feature, and the conformance suite

If the feature is new **public API** — something a caller of the Python, R, Java
or JavaScript binding can reach — add a conformance case for it:

1. Put it in `test/conformance/cases/<area>.json` with an empty `"expect": {}`,
   using ops from `test/conformance/OPS.md`.
2. `python tools/conformance_update.py --id <your.case.id>` fills the
   expectations in. **Read the diff** — every number in it is a claim.
3. Run all four runners. A case only Python can run is not doing its job;
   either make it work everywhere, or declare the gap in the case with a reason.

The one rule: a failing case means a binding is wrong, not that the number needs
updating. Regenerating expectations to make a red test go green destroys the
only thing the suite provides.

Language-specific behaviour — Python sugar, R's S4 dispatch, JavaScript
ergonomics — stays in that language's own tests. The conformance suite is the
*shared* subset.

## Reference arrays

`test/data/reference/*.npz` pin decoded arrays per data file. They regenerate
when deleted -- delete the matching `.npz` after an intentional decoder change,
or after a previously missing data file appears.
