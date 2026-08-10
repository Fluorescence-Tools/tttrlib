# Nomenclature — the dictionary is not here

**Moved 2026-08-10.** `mmfdb.dic` used to live in this directory. It does not any
more, and nothing in tttrlib should carry a copy of it again.

## Where the vocabulary lives

**[mmfdb](https://github.com/Fluorescence-Tools/mmfdb) is the naming
repository.** Every controlled term a `.pto` carries is defined in
`mmfdb/src/mmfdb/data/*.dic` and nowhere else:

| category | file |
|---|---|
| `_mmfdb_operation`, `_mmfdb_artifact`, `_mmfdb_edge` | `mmfdb_flr_ext.dic` |
| `_mmfdb_container`, `_mmfdb_column`, `_mmfdb_units` | `mmfdb_workflow_ext.dic` |
| `_mmfdb_burst_column`, `_mmfdb_constant`, `_mmfdb_derived_column` | `mmfdb_workflow_ext.dic` — **migrated from here** |

The three categories in the last row were genuinely tttrlib's: no mmfdb
dictionary defined them, and they name the columns of a burst table
(`Duration (green) (ms)`), the calibration constants (`gG_gR`), and the derived
quantities an ndX equation computes. They are now in mmfdb, because that is
where a name belongs — not because they were duplicates.

## Why the copy is gone

Everything else in the old file *was* a duplicate — `_mmfdb_artifact`,
`_mmfdb_edge`, `_mmfdb_operation` — and a copy of a vocabulary drifts from it.
That one had, by **eighteen terms**: it declared `bva`, `kde_cde`, `mle_green`,
`mle_red`, `burst_fcs`, `hmm_photon_by_photon`, `tcspc_calibration`,
`pda_histogram`, `companion_of`, `histogram_bin` and seven `…4` data formats,
none of which mmfdb has.

The failure mode is worth stating because it is not obvious: the registry agreed
with the local copy, the local copy agreed with the writer, and a conformance
test checked them against each other and passed. Three things in perfect
agreement and all three wrong — a closed loop of agreement that says nothing
about the vocabulary the rest of the world reads these files in.

## How tttrlib gets at it

`test/python/mmfdb_dictionary.py` resolves mmfdb's dictionaries from, in order:
the installed `mmfdb` package, `$MMFDB_DIC_DIR`, or a sibling checkout at
`../mmfdb/src/mmfdb/data`. The tests that use it skip loudly — naming where they
looked — when mmfdb is absent, because a vocabulary that cannot be checked is a
finding rather than a pass.

| test | checks |
|---|---|
| `test/python/test_vocabulary_matches_mmfdb.py` | no term tttrlib publishes is one mmfdb does not declare |
| `test/python/test_registry_matches_mmfdb.py` | the registry and the dictionary describe the same operations, in both directions |
| chiSurf `test/fio/test_container_cross_writer.py` | every term the compiled writer puts in a real container is a dictionary term |

## Adding a term

Add it **to mmfdb**, never here. A term tttrlib needs and mmfdb lacks is a gap
in the shared vocabulary, and filling it locally is how the eighteen got in.
`pda_burst_likelihood`, the `histogram_bin` row grain and
`_mmfdb_operation.algorithm` were added to mmfdb on 2026-08-10 for exactly this
reason.

See [the spec](../specs/mmfdb-is-the-vocabulary.md).
