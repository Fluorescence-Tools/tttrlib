# tttrlib tests

```bash
TTTRLIB_DATA=/path/to/tttr-data python -m pytest test/ -q
```

`TTTRLIB_DATA` overrides `data_root` in `test/settings.json`, which is the single
management point for file locations and test parameters. Without the data files
most tests skip rather than fail. On this project's usual environment
`pytest-qt` is broken and needs `-p no:pytest-qt`.

## Running only what is relevant

The whole suite takes about eleven minutes, which is too slow to run after every
change to one subsystem. Two ways to narrow it, neither of which needs a
decorator on a test or a list that has to be kept in step with the files.

**By test group.** Every test file lives in a directory named after the
subsystem it exercises, and `conftest.py` turns that directory name into a
marker automatically:

```bash
pytest test/ -m clsm                 # imaging only
pytest test/ -m "tttr or correlator" # two groups
pytest test/ -m "not slow"           # skip the long-running ones
```

**Skipping the slow ones.** 16 tests are ~70% of the runtime -- FCS curves
across a diffusion-coefficient sweep, and HMM bootstrap and Gibbs convergence
checks, all of which need a lot of simulated photons to say anything. They carry
`@pytest.mark.slow`:

```bash
pytest test/ -m "not slow"    # 1277 of 1321 tests, ~3 min instead of ~10
```

That is the run to use while working. The full suite goes before a merge and at
the end of a phase.

**By module.** `--modules` takes the names declared in `modules/` and expands
each to the groups that exercise it -- including through a dependency, because
changing `core` can break anything downstream of it:

```bash
pytest test/ --modules sim           # the simulator and what depends on it
pytest test/ --modules pda,io_image
```

The expansion is deliberately generous. A run that is slightly too wide costs
seconds; one that is too narrow costs a regression. The mapping lives in
`MODULE_TEST_GROUPS` in `test/python/conftest.py` and should be kept in step
with the `TEST_DIR` argument in each `modules/*/CMakeLists.txt`.

Run the full suite before merging, and after the last change of a phase.

## Layout

| directory | what it covers | module(s) |
|---|---|---|
| `tttr/` | photon-stream data model, readers, writers, transcoding | core, hist |
| `clsm/` | confocal imaging, super-resolution, ISM | imaging, superres, localization, io_image |
| `correlator/` | correlation / FCS | imaging |
| `decayfit/` | decay fitting, convolution, priors | decay, opt |
| `burstfilter/` | burst search, selection, filtering | burst, core |
| `bva/`, `twocde/` | burst variance analysis, two-channel KDE | burst |
| `hmm/` | photon-by-photon hidden Markov models | hmm, nn, decay, sim |
| `pda/` | photon distribution analysis | pda |
| `simulation/` | diffusion and photon emission | sim |
| `misc/` | cross-cutting helpers; always run | -- |
| `benchmarks/` | performance, not correctness | -- |

`test/java/` and `test/r/` hold the bindings' own tests; see
`tools/check_swig_multilang.sh` after changing any `.i` file.

## Reference arrays

`test/data/reference/*.npz` pin decoded arrays per data file. They regenerate
when deleted -- delete the matching `.npz` after an intentional decoder change,
or after a previously missing data file appears.
