"""``tttr sm`` writes a ChiSurf-conformant, detector-named burst table.

The whole point of the ``.pto`` burst pipeline is that a table the compiled CLI
writes is the table ChiSurf's ``generate_burst_dataframe`` writes — same column
names, same sentinels, same arithmetic — so ndX opens either without knowing
which produced it. ChiSurf is not a tttrlib dependency, so the reference is
reimplemented here from its documented rules rather than imported:

* ``stop`` is the burst's **last** photon, inclusive, so a burst holds
  ``stop - start + 1`` photons and its duration is ``macro[stop] - macro[start]``.
* the aggregate column is ``Mean Macro Time (ms)`` and the per-detector one is
  ``Mean Macrotime (<d>) (ms)`` — two spellings, both deliberate — and both are
  the **midpoint** of the first and last photon, not a mean.
* ``Duration (ms)`` is milliseconds, so photons-per-duration is already kHz.
* a detector with no photons in a burst gets ``-1`` / ``-1.0``, except its
  photon count, which gets ``0``.
* a detector's micro-time ranges are half-open, and **no ranges accepts every
  micro time** — a detector defined by routing channels alone is ungated.

The input is simulated, so the test needs no data download and the ground truth
is known: two species differing in FRET efficiency, which must come out as two
separated proximity-ratio populations.
"""

import csv
import json
import os
import shutil
import subprocess

import numpy as np
import pytest

_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)
_CONFIGS = os.path.join(_REPO_ROOT, "examples", "simulation", "configs")
_SIM_CONFIG = os.path.join(_CONFIGS, "mfd_2col_2pol.json")
_SETUP = os.path.join(_CONFIGS, "mfd_detector_setups.json")

_SEARCH = ("--min-photons", "20", "--rate-window", "10",
           "--time-separation", "0.0005")


def _find_tttr():
    """The binary under test, with the directory its shared library sits in.

    The newest of the build trees rather than the first one that exists: a
    checkout commonly carries more than one (`build/dev`, `build/release`), and
    taking whichever comes first alphabetically silently tests a stale binary —
    which shows up as "Option ... does not exist" for a flag that was added
    minutes ago. `TTTRLIB_CLI` overrides.

    Every build tree is under `build/` (see the placement check at the top of
    CMakeLists.txt), so that is the only directory worth walking.
    """
    override = os.environ.get("TTTRLIB_CLI")
    if override and os.access(override, os.X_OK):
        return override, os.path.dirname(os.path.dirname(os.path.abspath(override)))
    found = []
    build_root = os.path.join(_REPO_ROOT, "build")
    # the top-level tree (`build/bin/tttr`, what test_cli.py uses) and every
    # sub-tree (`build/<name>/bin/tttr`)
    candidates = [os.path.join(build_root, "bin", "tttr")]
    candidates += [os.path.join(build_root, entry, "bin", "tttr")
                   for entry in sorted(os.listdir(build_root) if os.path.isdir(build_root) else [])]
    for candidate in candidates:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            found.append(candidate)
    if found:
        newest = max(found, key=os.path.getmtime)
        return newest, os.path.dirname(os.path.dirname(newest))
    on_path = shutil.which("tttr")
    return (on_path, None) if on_path else (None, None)


TTTR_BIN, _LIB_DIR = _find_tttr()

pytestmark = pytest.mark.skipif(
    TTTR_BIN is None,
    reason="tttr binary not found (not on PATH and not in build/bin)",
)


def _run(*args):
    env = dict(os.environ)
    if _LIB_DIR:
        for var in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
            env[var] = _LIB_DIR + os.pathsep + env.get(var, "")
    result = subprocess.run([TTTR_BIN] + list(args), capture_output=True,
                            text=True, env=env)
    assert result.returncode == 0, (
        f"tttr {' '.join(args)} exited {result.returncode}\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}"
    )
    return result


def _read_tsv(path):
    with open(path) as fh:
        rows = list(csv.reader(fh, delimiter="\t"))
    header = rows[0]
    return {name: [r[i] for r in rows[1:]] for i, name in enumerate(header)}, header


def test_the_binary_finds_its_own_library_without_a_search_path():
    """Run it with the loader path stripped, which is what every other test hides.

    Every other test here sets `DYLD_LIBRARY_PATH`/`LD_LIBRARY_PATH` for the
    subprocess, so none of them exercises the binary's rpath — and a wrong rpath
    is not merely a failure to start. A prefix on the rpath may hold a
    `libtttrlib` symlink into *another* build tree, and then the tool starts
    successfully on a stale library: the wrong subcommands, no error, and
    `tttr sm --output x.pto` writing the old format over a container. This asks
    for a flag that exists only in the current source, so a stale library fails
    it rather than passing quietly.
    """
    env = {k: v for k, v in os.environ.items()
           if k not in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH")}
    result = subprocess.run([TTTR_BIN, "sm", "--help"], capture_output=True,
                            text=True, env=env)
    assert result.returncode == 0, (
        f"the binary does not run without a library search path:\n{result.stderr}"
    )
    assert "--donor" in result.stdout + result.stderr, (
        "ran, but not against this source tree's library"
    )


@pytest.fixture(scope="module")
def sim_file(tmp_path_factory):
    """A four-channel MFD photon stream on routing channels 0, 8, 1, 9."""
    out = tmp_path_factory.mktemp("sm") / "mfd_sim.spc"
    _run("sim", _SIM_CONFIG, "--channels", "4",
         "--routing-channels", "0,8,1,9", "-o", str(out))
    assert out.is_file()
    return out


@pytest.fixture(scope="module")
def two_detector(sim_file, tmp_path_factory):
    """`tttr sm` over the green/red setup: the .pto, the .tsv and the header."""
    d = tmp_path_factory.mktemp("two")
    pto, tsv = d / "bursts.mmfdb.pto", d / "bursts.tsv"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
         "--output", str(pto), "--csv", str(tsv))
    table, header = _read_tsv(tsv)
    return pto, table, header


def _expected_columns(detector_names):
    cols = ["First Photon", "Last Photon", "Duration (ms)",
            "Mean Macro Time (ms)", "Number of Photons", "Count Rate (KHz)",
            "Confidence (sigma)", "First File", "Last File"]
    for d in detector_names:
        cols += [f"First Photon ({d})", f"Last Photon ({d})",
                 f"Duration ({d}) (ms)", f"Mean Macrotime ({d}) (ms)",
                 f"Number of Photons ({d})", f"{d.capitalize()} Count Rate (KHz)"]
    cols += [f"Mean Microtime ({d}) (ns)" for d in detector_names]
    return cols


# The module-scoped `sim_file` fixture is ~23 s of Monte Carlo and lands on
# whichever test runs first. Shrinking the run would make it fast and make it
# test less -- 3778 bursts is what separates the two populations -- so it is
# tiered instead, which is what the tiers are for.
@pytest.mark.heavy
def test_columns_are_named_after_the_setups_detectors(two_detector):
    _, _, header = two_detector
    assert header == _expected_columns(["green", "red"])


def test_a_four_detector_setup_names_four_sets_of_columns(sim_file, tmp_path):
    """No green/red anywhere: the names come from the file, whatever they are."""
    tsv = tmp_path / "b4.tsv"
    _run("sm", str(sim_file), "--setup", _SETUP,
         "--setup-name", "MFD 4-detector", *_SEARCH,
         "--output", str(tmp_path / "b4.mmfdb.pto"), "--csv", str(tsv))
    _, header = _read_tsv(tsv)
    assert header == _expected_columns(
        ["green_par", "green_perp", "red_par", "red_perp"])


def test_without_a_setup_only_the_aggregate_columns_are_written(sim_file, tmp_path):
    tsv = tmp_path / "plain.tsv"
    _run("sm", str(sim_file), *_SEARCH,
         "--output", str(tmp_path / "plain.mmfdb.pto"), "--csv", str(tsv))
    _, header = _read_tsv(tsv)
    assert header == _expected_columns([])


def test_values_match_the_reference_arithmetic(sim_file, two_detector):
    """Every cell, against NumPy — this is the ChiSurf conformance check."""
    import tttrlib

    _, table, _ = two_detector
    setups = json.load(open(_SETUP))
    detectors = setups["setups"][setups["last_used"]]["detectors"]

    full = tttrlib.TTTR(str(sim_file))
    chans = sorted({c for d in detectors.values() for c in d["chs"]})
    data = full.get_tttr_by_channel(np.array(chans, dtype=np.int8))

    macro = np.asarray(data.macro_times)
    micro = np.asarray(data.micro_times)
    rout = np.asarray(data.routing_channel)
    res = data.header.macro_time_resolution
    micro_ns = data.header.micro_time_resolution * 1e9

    sel = np.asarray(data.burst_search(20, 10, 5e-4, "sliding_window", 0.05, 0.05))
    pairs = [(int(a), int(b)) for a, b in zip(sel[0::2], sel[1::2])
             if b > a and b < len(macro) and a >= 0]
    assert len(pairs) == len(table["First Photon"])

    got = {k: np.asarray(v, dtype=float) for k, v in table.items()
           if k not in ("First File", "Last File")}

    start = np.array([p[0] for p in pairs])
    stop = np.array([p[1] for p in pairs])
    dur = (macro[stop].astype(float) - macro[start]) * res * 1e3
    npix = stop - start + 1

    assert np.array_equal(got["First Photon"], start)
    assert np.array_equal(got["Last Photon"], stop)
    assert np.allclose(got["Duration (ms)"], dur)
    assert np.allclose(
        got["Mean Macro Time (ms)"],
        (macro[stop].astype(float) + macro[start]) / 2 * res * 1e3)
    assert np.array_equal(got["Number of Photons"], npix)
    assert np.allclose(got["Count Rate (KHz)"],
                       np.where(dur > 0, npix / np.where(dur > 0, dur, 1), np.nan),
                       equal_nan=True)

    for name, info in detectors.items():
        member = np.isin(rout, info["chs"])
        n = np.empty(len(pairs), dtype=np.int64)
        d_ms = np.full(len(pairs), -1.0)
        mid = np.full(len(pairs), -1.0)
        first = np.full(len(pairs), -1, dtype=np.int64)
        last = np.full(len(pairs), -1, dtype=np.int64)
        mt = np.full(len(pairs), -1.0)
        rate = np.full(len(pairs), -1.0)
        for i, (a, b) in enumerate(pairs):
            idx = np.flatnonzero(member[a:b + 1])
            n[i] = idx.size
            if idx.size == 0:
                continue
            i0, i1 = a + idx[0], a + idx[-1]
            first[i], last[i] = i0, i1
            d_ms[i] = (float(macro[i1]) - float(macro[i0])) * res * 1e3
            mid[i] = (float(macro[i1]) + float(macro[i0])) / 2 * res * 1e3
            rate[i] = idx.size / d_ms[i] if d_ms[i] > 0 else np.nan
            mt[i] = float(np.mean(micro[a:b + 1][idx])) * micro_ns

        assert np.array_equal(got[f"First Photon ({name})"], first)
        assert np.array_equal(got[f"Last Photon ({name})"], last)
        assert np.allclose(got[f"Duration ({name}) (ms)"], d_ms)
        assert np.allclose(got[f"Mean Macrotime ({name}) (ms)"], mid)
        assert np.array_equal(got[f"Number of Photons ({name})"], n)
        assert np.allclose(got[f"{name.capitalize()} Count Rate (KHz)"], rate,
                           equal_nan=True)
        assert np.allclose(got[f"Mean Microtime ({name}) (ns)"], mt)


def test_micro_time_gates_and_window_columns(sim_file, tmp_path):
    """A gated detector counts only its window, and a window adds S-columns."""
    import tttrlib

    setup = tmp_path / "pie.json"
    setup.write_text(json.dumps({
        "last_used": "PIE",
        "setups": {"PIE": {
            "detectors": {"green": {"chs": [8, 0],
                                    "micro_time_ranges": [[0, 2048]]},
                          "red": {"chs": [1, 9]}},
            "windows": {"prompt": [0, 2048], "delayed": [2048, 4096]},
        }},
    }))
    tsv = tmp_path / "pie.tsv"
    _run("sm", str(sim_file), "--setup", str(setup), *_SEARCH,
         "--output", str(tmp_path / "pie.mmfdb.pto"), "--csv", str(tsv))
    table, header = _read_tsv(tsv)

    for w, (lo, hi) in (("prompt", (0, 2048)), ("delayed", (2048, 4096))):
        for d in ("green", "red"):
            assert f"S {w} {d} (kHz) | {lo}-{hi}" in header

    # The gate is real: no green photon outside [0, 2048) is counted, so the
    # gated green count is at most the ungated one and strictly less somewhere.
    full = tttrlib.TTTR(str(sim_file))
    data = full.get_tttr_by_channel(np.array([0, 1, 8, 9], dtype=np.int8))
    micro = np.asarray(data.micro_times)
    rout = np.asarray(data.routing_channel)
    gated = np.asarray(table["Number of Photons (green)"], dtype=np.int64)
    first = np.asarray(table["First Photon"], dtype=np.int64)
    last = np.asarray(table["Last Photon"], dtype=np.int64)
    ungated = np.array([np.count_nonzero(np.isin(rout[a:b + 1], [8, 0]))
                        for a, b in zip(first, last)])
    assert np.all(gated <= ungated)
    assert np.any(gated < ungated)


def test_the_container_carries_the_profile_the_reader_looks_for(two_detector):
    pto, table, _ = two_detector
    tags = _run("pto", "tags", str(pto)).stdout
    assert "_mmfdb_container.profile=PTO.MFDB" in tags
    assert "_mmfdb_artifact.row_grain=burst" in tags
    # dstore, not `bur`: the extension names a file layout and this is not one.
    assert "_mmfdb_artifact.data_format=dstore" in tags
    assert "_mmfdb_operation.operation_type=burst_selection" in tags
    assert "_mmfdb_operation.settings_hash=" in tags
    assert "_mmfdb_edge.relationship_type=derived_from" in tags

    listing = _run("pto", "ls", str(pto)).stdout
    assert "tttr_photon_stream" in listing
    assert f"rows={len(table['First Photon'])}" in listing
    # The instrument file goes in verbatim, not as a re-encoded `.sm` of the
    # channel-filtered stream: the profile promises it comes back byte for byte.
    assert "encoding=spc" in listing
    assert "_mmfdb_artifact.checksum_algorithm=sha256" in tags


def test_the_instrument_file_comes_back_byte_for_byte(sim_file, tmp_path):
    import hashlib

    pto = tmp_path / "verbatim.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--output", str(pto))
    out = tmp_path / "restored.spc"
    _run("pto", "extract", str(pto), sim_file.name, str(out))

    def digest(p):
        return hashlib.sha256(open(p, "rb").read()).hexdigest()

    assert digest(out) == digest(sim_file)
    # ...and the container says so itself, so a reader can check without the
    # original beside it.
    assert digest(sim_file) in _run("pto", "tags", str(pto)).stdout


def test_a_rerun_replaces_its_artifact_and_a_new_search_does_not(sim_file, tmp_path):
    """Run identity is the settings hash, so a container does not accumulate."""
    pto = tmp_path / "rerun.mmfdb.pto"

    def n_burst_tables():
        return _run("pto", "ls", str(pto)).stdout.count("kind=burst_table")

    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--output", str(pto))
    after_first = n_burst_tables()
    assert after_first >= 1

    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--output", str(pto))
    assert n_burst_tables() == after_first, "same settings must replace in place"

    _run("sm", str(sim_file), "--setup", _SETUP, "--min-photons", "50",
         "--rate-window", "10", "--time-separation", "0.0005",
         "--output", str(pto))
    assert n_burst_tables() == 2 * after_first, "a different search is a new run"


def test_companions_are_computed_not_asserted(sim_file, tmp_path):
    """BVA and 2CDE hold measurements, and are absent when they cannot be."""
    import tttrlib

    pto = tmp_path / "comp.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--output", str(pto))
    tags = _run("pto", "tags", str(pto)).stdout
    assert "_mmfdb_operation.operation_type=burst_variance_analysis" in tags
    assert "_mmfdb_operation.operation_type=burst_2cde" in tags
    # `companion_of` is not an `_mmfdb_edge.relationship_type` term; a
    # companion is derived from its burst table, and that the two share a
    # grain is what `row_grain` says.
    assert "companion_of" not in tags

    f = tttrlib.PtoFile()
    assert f.open(str(pto))
    values = {}
    for obj in f.objects():
        if obj.encoding != "dstore":
            continue
        store = tttrlib.DataStore()
        tttrlib.pto_read_store(f, obj.uid, store)
        for name in store.column_names():
            values[name] = np.asarray(store[name])
    f.close()

    # A constant column is what a placeholder looks like; these must vary, and
    # sit where the definitions say they should for static bursts. NaN is a
    # legitimate value here — 2CDE is undefined for a burst missing one of its
    # two streams — so most rows, not all, have to be finite.
    std = values["Proximity Ratio Std"]
    cde = values["FRET 2CDE"]
    assert np.nanstd(std) > 0.0 and np.nanstd(cde) > 0.0
    assert np.isfinite(cde).mean() > 0.9
    assert 0.0 <= np.nanmedian(std) <= 1.0
    assert 5.0 < np.nanmedian(cde) < 20.0, "FRET-2CDE is ~10 for static bursts"

    # No detector named donor/acceptor and none given -> no companions at all,
    # rather than companions computed over an arbitrary pair.
    setup = tmp_path / "anon.json"
    setup.write_text(json.dumps({
        "last_used": "anon",
        "setups": {"anon": {"detectors": {"a": {"chs": [8, 0]},
                                          "b": {"chs": [1, 9]}}}},
    }))
    bare = tmp_path / "bare.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", str(setup), *_SEARCH,
         "--output", str(bare))
    assert "burst_variance_analysis" not in _run("pto", "tags", str(bare)).stdout


def _columns_and_units(pto_path):
    import tttrlib

    f = tttrlib.PtoFile()
    assert f.open(str(pto_path))
    out = {}
    try:
        for obj in f.objects():
            if obj.encoding != "dstore":
                continue
            store = tttrlib.DataStore()
            tttrlib.pto_read_store(f, obj.uid, store)
            for i in range(store.n_columns()):
                column = store[i]
                out[column.name()] = str(column.attribute("units") or "")
    finally:
        f.close()
    return out


def test_columns_carry_their_unit(two_detector):
    """A name and a dtype are enough to read a column, not to understand it.

    The unit is an `_mmfdb_column.units` term on the column itself, so it
    survives a column-subset read. Absent means *unknown*, not dimensionless —
    a photon index has no unit and must not claim one.
    """
    pto, _, _ = two_detector
    units = _columns_and_units(pto)

    assert units["Duration (ms)"] == "milliseconds"
    assert units["Mean Macro Time (ms)"] == "milliseconds"
    assert units["Duration (green) (ms)"] == "milliseconds"
    assert units["Mean Macrotime (red) (ms)"] == "milliseconds"
    assert units["Mean Microtime (green) (ns)"] == "nanoseconds"
    assert units["Count Rate (KHz)"] == "kilohertz"
    assert units["Green Count Rate (KHz)"] == "kilohertz"
    assert units["Confidence (sigma)"] == "dimensionless"
    assert units["Proximity Ratio Std"] == "dimensionless"
    assert units["FRET 2CDE"] == "dimensionless"

    # The same quantity, qualified by a detector, keeps its unit. Keying the
    # fallback table on the exact name made `Number of Photons` photons and
    # `Number of Photons (green)`, in the same row, unitless.
    assert units["Number of Photons"] == "photons"
    assert units["Number of Photons (green)"] == "photons"
    assert units["Number of Photons (red)"] == "photons"

    # An index into the photon stream is not measured in anything.
    for name in ("First Photon", "Last Photon", "First Photon (green)",
                 "First File", "Last File"):
        assert units[name] == "", name


def test_a_window_rate_carries_its_unit(sim_file, tmp_path):
    """`S prompt green (kHz) | 0-2048` says kHz, and the bar is a range."""
    setup = tmp_path / "pie.json"
    setup.write_text(json.dumps({
        "last_used": "PIE",
        "setups": {"PIE": {
            "detectors": {"green": {"chs": [8, 0]}, "red": {"chs": [1, 9]}},
            "windows": {"prompt": [0, 2048]},
        }},
    }))
    pto = tmp_path / "win.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", str(setup), *_SEARCH,
         "--output", str(pto))
    units = _columns_and_units(pto)
    assert units["S prompt green (kHz) | 0-2048"] == "kilohertz"
    assert units["S prompt red (kHz) | 0-2048"] == "kilohertz"


def test_extending_a_container_does_not_add_a_second_primary(sim_file, tmp_path):
    """A container this did not create already holds its primary.

    Adding a second copy would give the graph two roots and leave the burst
    table hanging off the one nothing else references — which reads as intact
    until somebody walks the lineage of an artifact another tool wrote. The
    match is on the checksum, so a byte-identical file under a different name
    still resolves to the one already there.
    """
    first = tmp_path / "run.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--output", str(first))

    renamed = tmp_path / "same_photons_other_name.spc"
    renamed.write_bytes(sim_file.read_bytes())
    _run("sm", str(renamed), "--setup", _SETUP, "--min-photons", "50",
         "--rate-window", "10", "--time-separation", "0.0005",
         "--output", str(first))

    listing = _run("pto", "ls", str(first)).stdout
    assert listing.count("kind=tttr_photon_stream") == 1, listing


def test_mle_refuses_to_invent_an_instrument_response(sim_file, tmp_path):
    """`--mle` without `--irf` must fail, not pick a default.

    A prompt is a claim about the instrument. A lifetime fitted against the
    wrong one is wrong by roughly its width, and nothing in the output says so —
    so the one thing this must not do is choose quietly.
    """
    env = dict(os.environ)
    if _LIB_DIR:
        for var in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
            env[var] = _LIB_DIR + os.pathsep + env.get(var, "")
    result = subprocess.run(
        [TTTR_BIN, "sm", str(sim_file), "--setup", _SETUP, *_SEARCH, "--mle",
         "--output", str(tmp_path / "no_irf.mmfdb.pto")],
        capture_output=True, text=True, env=env)
    assert result.returncode != 0
    assert "--irf" in result.stderr


def test_mle_recovers_the_simulated_lifetimes(sim_file, tmp_path):
    """The ground truth the simulation was built to carry.

    Two species, 3.8 ns and 1.6 ns, separated by their proximity ratio. The fit
    has to put each population back where it came from — which is the only check
    that distinguishes a working estimator from one that returns plausible
    numbers. It caught three things that a "does it produce columns" test would
    have passed: the `DecayFitProblem(n_channels, n_bins, ...)` argument order,
    an un-subtracted background biasing every lifetime *up* by half, and a
    four-parameter anisotropy fit on a hundred photons that moved the answer by
    a factor of two on a change of start value while its 2I* still looked fine.
    """
    import tttrlib

    pto = tmp_path / "mle.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
         "--mle", "--irf", "delta", "--tau-init", "2.5", "--output", str(pto))

    f = tttrlib.PtoFile()
    assert f.open(str(pto))
    cols = {}
    try:
        for obj in f.objects():
            if obj.encoding != "dstore":
                continue
            store = tttrlib.DataStore()
            tttrlib.pto_read_store(f, obj.uid, store)
            for name in store.column_names():
                cols[name] = np.asarray(store[name])
    finally:
        f.close()

    tau = cols["Tau (green)"]
    ng = cols["Number of Photons (green)"].astype(float)
    nr = cols["Number of Photons (red)"].astype(float)
    pr = nr / np.maximum(ng + nr, 1)

    fitted = np.isfinite(tau)
    assert fitted.sum() > 500, "almost nothing was fitted"

    low = tau[fitted & (pr < 0.5)]
    high = tau[fitted & (pr >= 0.5)]
    assert low.size > 200 and high.size > 100
    # 10%: the estimator is biased slightly high on a hundred photons over a
    # flat background, and pinning it tighter would be pinning the noise.
    assert np.median(low) == pytest.approx(3.8, rel=0.10), np.median(low)
    assert np.median(high) == pytest.approx(1.6, rel=0.15), np.median(high)
    # The two populations must be resolved, not merely averaged.
    assert np.median(low) - np.median(high) > 1.5


def _median_tau(pto_path, column="Tau (green)"):
    import tttrlib

    f = tttrlib.PtoFile()
    assert f.open(str(pto_path))
    cols = {}
    try:
        for obj in f.objects():
            if obj.encoding != "dstore":
                continue
            store = tttrlib.DataStore()
            tttrlib.pto_read_store(f, obj.uid, store)
            for name in store.column_names():
                cols[name] = np.asarray(store[name])
    finally:
        f.close()
    tau = cols[column]
    ng = cols["Number of Photons (green)"].astype(float)
    ratio = cols["Number of Photons (red)"].astype(float) / np.maximum(
        ng + cols["Number of Photons (red)"].astype(float), 1)
    low = tau[np.isfinite(tau) & (ratio < 0.5)]
    assert low.size > 200
    return float(np.median(low))


@pytest.mark.parametrize("spec", [
    "delta",
    "gauss:0.3", "gauss:0.3,0.1",
    "gaussian:0.3",                       # the spelling this shipped with first
    "sgauss:0.3", "sgauss:0.3,0.1,2.0",
    "skewed:0.3",
])
def test_every_irf_spelling_is_accepted(sim_file, tmp_path, spec):
    pto = tmp_path / f"irf_{abs(hash(spec))}.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
         "--mle", "--irf", spec, "--output", str(pto))
    assert "_mmfdb_operation.operation_type=burst_lifetime_fitting" in _run(
        "pto", "tags", str(pto)).stdout


@pytest.mark.parametrize("spec", [
    "gauss:",             # no number
    "gauss:abc",          # not a number
    "gauss:-1",           # a width must be positive
    "gauss:0.3,0,1",      # a symmetric gaussian has no skew
    "sgauss:0.3,0,1,2",   # one value too many
    "no_such_irf_file",
])
def test_a_malformed_irf_is_refused(sim_file, tmp_path, spec):
    env = dict(os.environ)
    if _LIB_DIR:
        for var in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
            env[var] = _LIB_DIR + os.pathsep + env.get(var, "")
    result = subprocess.run(
        [TTTR_BIN, "sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
         "--mle", "--irf", spec, "--output", str(tmp_path / "bad.mmfdb.pto")],
        capture_output=True, text=True, env=env)
    assert result.returncode != 0, f"{spec!r} was accepted"
    assert "--irf" in result.stderr


def test_the_instrument_response_changes_the_answer(sim_file, tmp_path):
    """An IRF that is not used is worse than one that is wrong.

    The simulation convolves with nothing, so `delta` is the *correct* model and
    must recover the truth best. A wider prompt takes more out of the decay, so
    the recovered lifetime has to fall monotonically with the prompt's width —
    and a skewed prompt of the same width, which carries extra weight at later
    times, has to differ from the symmetric one. Together these say the response
    reached the fit, in the right place, with the right sign; a spec that parsed
    and was then ignored passes none of them.
    """
    taus = {}
    for spec in ("delta", "gauss:0.5", "gauss:2.0", "sgauss:2.0,0,3.0"):
        pto = tmp_path / (spec.replace(":", "_").replace(",", "_") + ".mmfdb.pto")
        _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
             "--mle", "--irf", spec, "--tau-init", "2.5", "--output", str(pto))
        taus[spec] = _median_tau(pto)

    assert taus["delta"] == pytest.approx(3.8, rel=0.10), taus
    assert taus["delta"] > taus["gauss:0.5"] > taus["gauss:2.0"], taus
    assert abs(taus["sgauss:2.0,0,3.0"] - taus["gauss:2.0"]) > 0.01, taus


def test_mle_columns_are_the_bg4_set(sim_file, tmp_path):
    import tttrlib

    pto = tmp_path / "mlecols.mmfdb.pto"
    _run("sm", str(sim_file), "--setup", _SETUP, *_SEARCH,
         "--mle", "--irf", "gaussian:0.3", "--output", str(pto))

    f = tttrlib.PtoFile()
    assert f.open(str(pto))
    names = set()
    try:
        for obj in f.objects():
            if obj.encoding != "dstore":
                continue
            store = tttrlib.DataStore()
            tttrlib.pto_read_store(f, obj.uid, store)
            names.update(store.column_names())
    finally:
        f.close()

    for det in ("green", "red"):
        for col in (f"Number of Photons (fit window) ({det})",
                    # two spaces after the star: part of the .b?4 format
                    f"2I*  ({det})", f"Tau ({det})", f"gamma ({det})",
                    f"r0 ({det})", f"rho ({det})",
                    f"BIFL scatter? ({det})", f"2I*: P+2S? ({det})",
                    f"r Scatter ({det})", f"r Experimental ({det})"):
            assert col in names, col
    assert "Ng-p-all" in names and "Ng-s-all" in names

    tags = _run("pto", "tags", str(pto)).stdout
    # Dictionary terms, not informal ones: one `burst_lifetime_fitting`
    # per detector, told apart by the detector in their settings.
    assert tags.count("_mmfdb_operation.operation_type=burst_lifetime_fitting") == 2


def test_the_simulated_populations_come_out_separated(two_detector):
    """Ground truth: two species, one green-dominant and one red-dominant."""
    _, table, _ = two_detector
    ng = np.asarray(table["Number of Photons (green)"], dtype=float)
    nr = np.asarray(table["Number of Photons (red)"], dtype=float)
    pr = nr / np.maximum(ng + nr, 1)
    low = pr[pr < 0.5]
    high = pr[pr >= 0.5]
    assert low.size > 100 and high.size > 100
    assert np.median(high) - np.median(low) > 0.4


# ------------------------------------------------------------------- stdin

def _run_piped(stdin_path, *args):
    """`tttr` with a file on stdin, which is what `-` reads."""
    env = dict(os.environ)
    if _LIB_DIR:
        for var in ("DYLD_LIBRARY_PATH", "LD_LIBRARY_PATH"):
            env[var] = _LIB_DIR + os.pathsep + env.get(var, "")
    with open(stdin_path, "rb") as fh:
        result = subprocess.run([TTTR_BIN] + list(args), stdin=fh,
                                capture_output=True, text=True, env=env)
    assert result.returncode == 0, (
        f"tttr {' '.join(args)} exited {result.returncode}\n"
        f"stdout: {result.stdout}\nstderr: {result.stderr}")
    return result


def test_sm_reads_a_piped_stream(sim_file, tmp_path, two_detector):
    """`tttr sm -` reads stdin, so one command
    feeds the next without the user managing an intermediate file.

    The bursts must be the ones the same input produces from a path — the only
    thing a pipe changes is where the bytes came from."""
    pto, direct_table, _ = two_detector
    piped_tsv = tmp_path / "piped.tsv"
    _run_piped(sim_file, "sm", "-", "--setup", _SETUP, *_SEARCH,
               "--output", str(tmp_path / "piped.pto"), "--csv", str(piped_tsv))

    piped_table, _ = _read_tsv(piped_tsv)
    assert list(piped_table) == list(direct_table)
    for column in direct_table:
        if column in ("First File", "Last File"):
            continue          # the source genuinely differs; see below
        assert piped_table[column] == direct_table[column], f"{column} differs"


def test_a_piped_stream_is_called_stdin_not_dash(sim_file, tmp_path):
    """`-` is the shell's word for stdin, not a name a reader can resolve later.
    It must not end up in the burst table's source column, nor as the stem of
    the container's objects."""
    tsv = tmp_path / "piped.tsv"
    pto = tmp_path / "piped.pto"
    _run_piped(sim_file, "sm", "-", "--setup", _SETUP, *_SEARCH,
               "--output", str(pto), "--csv", str(tsv))

    table, _ = _read_tsv(tsv)
    assert set(table["First File"]) == {"stdin"}
    assert set(table["Last File"]) == {"stdin"}

    listing = _run("pto", "ls", str(pto)).stdout
    assert "stdin.bur" in listing, listing
    assert "/-." not in listing, f"the literal dash reached an object name:\n{listing}"


def test_the_stdin_spool_file_is_cleaned_up(sim_file, tmp_path):
    """stdin is spooled to a temporary file because every container reader
    seeks. The temporary is the implementation's business and must not outlive
    the command."""
    import glob
    import tempfile
    before = set(glob.glob(os.path.join(tempfile.gettempdir(), "tttr-stdin-*")))
    _run_piped(sim_file, "sm", "-", "--setup", _SETUP, *_SEARCH,
               "--output", str(tmp_path / "p.pto"), "--csv", str(tmp_path / "p.tsv"))
    after = set(glob.glob(os.path.join(tempfile.gettempdir(), "tttr-stdin-*")))
    assert after <= before, f"left behind: {sorted(after - before)}"


# -------------------------------------------------------------- column order

def test_window_columns_come_out_in_file_order(sim_file, tmp_path):
    """A setup's windows are ordered by the file, not alphabetically.

    The values were always right; only the order of the `S <window> <detector>`
    block was wrong, which matters exactly when another tool reads the table
    positionally. `prompt` before `delayed` is the case that shows it — sorted,
    they come out the other way round."""
    with open(_SETUP) as fh:
        setups = json.load(fh)
    name = list(setups["setups"])[0]
    setups["setups"][name]["windows"] = {"prompt": [0, 2047], "delayed": [2048, 4095]}
    setups["last_used"] = name
    setup_path = tmp_path / "windows_setup.json"
    setup_path.write_text(json.dumps(setups))

    tsv = tmp_path / "w.tsv"
    _run("sm", str(sim_file), "--setup", str(setup_path), *_SEARCH,
         "--output", str(tmp_path / "w.pto"), "--csv", str(tsv))

    _, header = _read_tsv(tsv)
    windowed = [c for c in header if c.startswith("S ")]
    assert windowed, f"no window columns in {header}"
    order = []
    for c in windowed:
        w = c.split()[1]
        if w not in order:
            order.append(w)
    assert order == ["prompt", "delayed"], (
        f"window columns are in {order}, the file says ['prompt', 'delayed'] "
        f"— alphabetical order would give ['delayed', 'prompt']")
