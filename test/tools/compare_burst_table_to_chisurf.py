"""Diff a `tttr sm` burst table against ChiSurf's own writer, cell for cell.

`tttr sm --setup ... --csv table.tsv` claims to write the same table ChiSurf's
`generate_burst_dataframe` writes. `test/python/misc/test_cli_sm_burst_table.py`
checks that against a NumPy reimplementation of ChiSurf's documented rules — a
reimplementation, because ChiSurf is not a tttrlib dependency and a test that
needed it would be skipped everywhere it matters.

That leaves the blind spot this program does not share: a rule I read wrongly is
read the same wrong way in the test. So this runs the **actual** ChiSurf
function over the same photons and diffs the two tables. It is deliberately not
part of the suite, for the same reason `pto_ebml_check.cpp` is not part of the
build: the check is worth having, the dependency is not.

If the two disagree, tttrlib is wrong. ChiSurf's `burst.py` is the format.

Running it
----------

Both sides must use **one** tttrlib, and it must match the binary under test::

    R=/path/to/tttrlib
    $R/build/bin/tttr sim $R/examples/simulation/configs/mfd_2col_2pol.json \
        --channels 4 --routing-channels 0,8,1,9 -o mfd_sim.spc
    $R/build/bin/tttr sm mfd_sim.spc \
        --setup $R/examples/simulation/configs/mfd_detector_setups.json \
        --output bursts.mmfdb.pto --csv bursts.tsv

    PYTHONPATH=$R/build/ext:/path/to/chisurf python \
        $R/test/tools/compare_burst_table_to_chisurf.py \
        mfd_sim.spc $R/examples/simulation/configs/mfd_detector_setups.json \
        bursts.tsv [SETUP_NAME]

Two traps that cost real time the first time:

* a ChiSurf **conda env** may be built for a different architecture or Python
  version than the tttrlib build tree, and then `import tttrlib` silently
  resolves to the *installed* one. Import ChiSurf from source with `PYTHONPATH`
  into an interpreter that can load the build instead.
* an editable install puts a **meta path finder** in `site-packages`, and meta
  path finders run before `sys.path` — so `PYTHONPATH` does not override one.
  Check `tttrlib.__file__` before trusting a run.

Both sides must also agree on the burst list, or the row counts differ and
nothing below means anything; the search is run here rather than read out of the
table so that a disagreement shows up as a row-count mismatch rather than as a
silent misalignment.
"""

import csv
import json
import sys

import numpy as np
import tttrlib

from chisurf.core.datastore import column_names
from chisurf.core.fio.fluorescence.burst import generate_burst_dataframe
from chisurf.core.fio.fluorescence.burst_container import deinterleave_bursts

#: Must match the search `tttr sm` was run with.
L, M, T = 20, 10, 5e-4


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 2
    sim, setup_path, tsv = argv[1], argv[2], argv[3]
    setup_name = argv[4] if len(argv) > 4 else None

    print("tttrlib:", tttrlib.__file__)

    setups = json.load(open(setup_path))
    setup = setups["setups"][setup_name or setups["last_used"]]
    detectors = {
        name: {"chs": d["chs"],
               "micro_time_ranges": d.get("micro_time_ranges", [])}
        for name, d in setup["detectors"].items()
    }
    windows = {k: tuple(v) for k, v in setup.get("windows", {}).items()}

    full = tttrlib.TTTR(sim)
    chans = sorted({c for d in detectors.values() for c in d["chs"]})
    data = full.get_tttr_by_channel(np.array(chans, dtype=np.int8))

    sel = np.asarray(data.burst_search(L, M, T, "sliding_window", 0.05, 0.05))
    start_stop = list(zip(sel[0::2], sel[1::2]))
    print(f"{len(start_stop)} bursts from the search")

    ref = deinterleave_bursts(
        generate_burst_dataframe(start_stop, sim, data, windows, detectors))
    ref_cols = [c for c in column_names(ref) if c]

    with open(tsv) as fh:
        rows = list(csv.reader(fh, delimiter="\t"))
    got_cols = rows[0]
    got = {name: [r[i] for r in rows[1:]] for i, name in enumerate(got_cols)}

    print(f"ChiSurf columns : {len(ref_cols)}")
    print(f"tttr sm columns : {len(got_cols)}")
    missing = [c for c in ref_cols if c not in got_cols]
    extra = [c for c in got_cols if c not in ref_cols]
    if missing:
        print("MISSING in tttr sm:", missing)
    if extra:
        print("EXTRA in tttr sm :", extra)

    n_ref = len(np.asarray(ref[ref_cols[0]]))
    n_got = len(rows) - 1
    print(f"rows: chisurf {n_ref}  tttr sm {n_got}")
    if n_ref != n_got:
        print("ROW COUNT MISMATCH -- the two are not describing the same bursts")
        return 1

    bad = 0
    for c in ref_cols:
        if c not in got:
            continue
        a = np.asarray(ref[c])
        if a.dtype.kind in "OU":
            b = np.asarray(got[c], dtype=object)
            same = np.array([str(x) == str(y) for x, y in zip(a, b)])
        else:
            b = np.asarray(got[c], dtype=float)
            a = a.astype(float)
            same = np.isclose(a, b, rtol=1e-9, atol=1e-9, equal_nan=True)
        if not same.all():
            bad += 1
            i = int(np.flatnonzero(~same)[0])
            print(f"  DIFF {c!r}: {int((~same).sum())}/{len(same)} rows, "
                  f"first at row {i}: chisurf={a[i]!r} tttr={b[i]!r}")

    if bad or missing or extra:
        print(f"{bad} columns differ")
        return 1
    print("MATCH")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
