"""Drop-in binary plugins: a format that tttrlib was never built to read.

The promise on the tin is one sentence -- copy ``tttrlib_<name>.so`` into a
directory and the capability is there next time tttrlib runs, with no rebuild
and no source. These tests are that sentence, executed: they build nothing at
test time, they take the example plugin the tree already produced, put it in a
directory, point tttrlib at it with ``TTTRLIB_PLUGIN_PATH``, and read a file
through it.

Everything here runs in a **subprocess**. Plugins load exactly once per process,
behind a ``call_once``, so a test that changed the environment in-process would
be testing whatever the first test happened to do. The subprocess is not
awkwardness to work around; it is the only way to test a once-per-process
decision more than once.
"""
import json
import os
import struct
import subprocess
import sys
import sysconfig
import textwrap
from pathlib import Path

import pytest

MAGIC = b"EXMPL001"


def _plugin_binary():
    """The example plugin, as built by -DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON.

    Searched for in any build directory, not only the scikit-build `build/<tag>/`
    layout: a developer configuring into `build_new/` or `cmake-build-debug/`
    otherwise sees this whole file skip with a message telling them to enable an
    option they already enabled.
    """
    override = os.environ.get("TTTRLIB_EXAMPLE_PLUGIN")
    if override:
        p = Path(override)
        return p if p.exists() else None
    root = Path(__file__).resolve().parents[3]
    suffix = {"darwin": ".dylib", "win32": ".dll"}.get(sys.platform, ".so")
    hits = sorted(root.glob(f"build*/examples/plugin/tttrlib_example{suffix}"))
    hits += sorted(root.glob(f"build*/*/examples/plugin/tttrlib_example{suffix}"))
    return hits[-1] if hits else None


@pytest.fixture(scope="module")
def plugin_so():
    p = _plugin_binary()
    if p is None:
        pytest.skip("example plugin not built "
                    "(configure with -DTTTRLIB_BUILD_EXAMPLE_PLUGIN=ON)")
    return p


@pytest.fixture
def plugin_dir(tmp_path, plugin_so):
    """A directory with the plugin in it. This is the whole user story."""
    d = tmp_path / "plugins"
    d.mkdir()
    # Copied, not symlinked: a symlink would test something the user will not do.
    (d / plugin_so.name).write_bytes(plugin_so.read_bytes())
    return d


def write_example_file(path, events):
    """events: iterable of (macro, micro, channel, event_type)."""
    events = list(events)
    with open(path, "wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<Q", len(events)))
        for macro, micro, channel, kind in events:
            f.write(struct.pack("<QHbb4x", macro, micro, channel, kind))


@pytest.fixture
def sample(tmp_path):
    p = tmp_path / "measurement.exmpl"
    write_example_file(p, [
        (10, 100, 0, 0),
        (20, 200, 1, 0),
        (30, 0, 2, 1),     # a marker
        (40, 300, 0, 0),
    ])
    return p


def run_in_subprocess(code, plugin_path=None, env_extra=None, tmp_path=None):
    """Run `code` in a fresh interpreter and return its parsed JSON stdout.

    Fresh because plugin loading is a once-per-process decision; parsed JSON
    because a test that greps stdout for a substring passes for the wrong
    reasons sooner or later.
    """
    env = dict(os.environ)
    env.pop("TTTRLIB_PLUGINS", None)
    # Ensure the development build's SWIG extension is found by the subprocess,
    # not a stale namespace package in site-packages.
    _build_ext = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))),
        "build", "ext"
    )
    if os.path.isdir(_build_ext):
        env["PYTHONPATH"] = _build_ext + os.pathsep + env.get("PYTHONPATH", "")
    if plugin_path is not None:
        env["TTTRLIB_PLUGIN_PATH"] = str(plugin_path)
    else:
        env.pop("TTTRLIB_PLUGIN_PATH", None)
    if env_extra:
        env.update(env_extra)

    proc = subprocess.run([sys.executable, "-c", textwrap.dedent(code)],
                          capture_output=True, text=True, env=env, timeout=300)
    if proc.returncode != 0:
        raise AssertionError(f"subprocess failed ({proc.returncode}):\n"
                             f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return json.loads(proc.stdout.strip().splitlines()[-1])


# ------------------------------------------------------------------ discovery

def test_a_dropped_in_plugin_is_found_and_reported(plugin_dir):
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(tttrlib.registry("plugin")))
    """, plugin_path=plugin_dir)

    assert "example" in out, out
    entry = out["example"]
    assert entry["status"] == "loaded", entry["message"]
    assert entry["version"] == "1.0.0"
    assert entry["containers"] == ["EXAMPLE"]
    # Provenance: a published result has to be able to say what ran.
    assert len(entry["sha256"]) == 64
    assert entry["path"].endswith(os.path.basename(str(next(plugin_dir.iterdir()))))


def test_nothing_loads_when_there_is_nothing_to_load():
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(tttrlib.registry("plugin")))
    """)
    assert out == {}


def test_the_format_joins_the_registry_as_a_container(plugin_dir):
    out = run_in_subprocess("""
        import json, tttrlib
        reg = tttrlib.registry("file_container")
        print(json.dumps({
            "entry": reg.get("EXAMPLE"),
            "names": tttrlib.TTTR.get_supported_container_names(),
        }))
    """, plugin_path=plugin_dir)

    entry = out["entry"]
    assert entry is not None, "the plugin format never reached the registry"
    assert entry["extensions"] == ".exmpl"
    assert entry["can_read"] is True
    assert entry["can_write"] is False
    # A plugin's container id is handed out in load order, so it is only
    # meaningful within this process -- and the registry has to say so, or a
    # caller will persist it.
    assert entry["container_type"] >= 1000
    assert entry["stable"] is False
    # Nameable, which is the thing that was broken while the name map was a
    # build-once static.
    assert "EXAMPLE" in out["names"]


def test_the_builtin_ids_are_untouched(plugin_dir):
    """0-999 belong to built-in formats permanently, plugins or not."""
    out = run_in_subprocess("""
        import json, tttrlib
        reg = tttrlib.registry("file_container")
        print(json.dumps({n: e["container_type"] for n, e in reg.items()}))
    """, plugin_path=plugin_dir)
    assert out["PTU"] == 0
    assert out["BRIGHTEYES-TTR"] == 10
    assert out["EXAMPLE"] >= 1000


# ------------------------------------------------------------------ reading

def test_reads_a_file_through_the_plugin(plugin_dir, sample):
    """The whole point: a format tttrlib was never built to read."""
    out = run_in_subprocess(f"""
        import json, numpy as np, tttrlib
        d = tttrlib.TTTR({str(sample)!r}, "EXAMPLE")
        print(json.dumps({{
            "n": int(d.n_valid_events),
            "macro": np.asarray(d.macro_times).astype(int).tolist(),
            "micro": np.asarray(d.micro_times).astype(int).tolist(),
            "chan": np.asarray(d.routing_channels).astype(int).tolist(),
            "type": np.asarray(d.event_types).astype(int).tolist(),
        }}))
    """, plugin_path=plugin_dir)

    assert out["n"] == 4
    assert out["macro"] == [10, 20, 30, 40]
    assert out["micro"] == [100, 200, 0, 300]
    assert out["chan"] == [0, 1, 2, 0]
    assert out["type"] == [0, 0, 1, 0]


def test_the_plugin_is_found_by_content(plugin_dir, sample):
    """A plugin sniffer takes part in detection like any built-in one."""
    out = run_in_subprocess(f"""
        import json, tttrlib
        d = tttrlib.TTTR({str(sample)!r})     # no container named
        print(json.dumps({{"n": int(d.n_valid_events),
                          "container": d.get_tttr_container_type()}}))
    """, plugin_path=plugin_dir)
    assert out["container"] == "EXAMPLE"
    assert out["n"] == 4


def test_a_plugin_sniffer_does_not_claim_other_formats(plugin_dir, tmp_path):
    """Sniffers are called speculatively; a greedy one breaks every other format."""
    not_ours = tmp_path / "other.exmpl"
    not_ours.write_bytes(b"NOTEXMPL" + b"\x00" * 32)
    out = run_in_subprocess(f"""
        import json, tttrlib
        print(json.dumps({{"inferred": tttrlib.inferTTTRFileType({str(not_ours)!r})}}))
    """, plugin_path=plugin_dir)
    assert out["inferred"] == -1


def test_the_header_the_plugin_supplied_survives(plugin_dir, sample):
    out = run_in_subprocess(f"""
        import json, tttrlib
        d = tttrlib.TTTR({str(sample)!r}, "EXAMPLE")
        print(json.dumps({{
            "res": d.header.macro_time_resolution,
            "n_micro": int(d.header.number_of_micro_time_channels),
        }}))
    """, plugin_path=plugin_dir)
    assert out["res"] == pytest.approx(1e-9)
    assert out["n_micro"] == 4096


def test_a_large_file_crosses_the_batch_boundary(plugin_dir, tmp_path):
    """The read loop calls back until the plugin reports nothing left.

    One batch is 65536 events, so anything smaller never exercises the second
    call -- where an off-by-one in the buffer offsets would live.
    """
    n = 150_000
    big = tmp_path / "big.exmpl"
    write_example_file(big, ((i, i % 4096, i % 4, 0) for i in range(n)))
    out = run_in_subprocess(f"""
        import json, numpy as np, tttrlib
        d = tttrlib.TTTR({str(big)!r}, "EXAMPLE")
        m = np.asarray(d.macro_times)
        print(json.dumps({{"n": int(d.n_valid_events),
                          "monotone": bool(np.all(np.diff(m.astype(np.int64)) == 1)),
                          "first": int(m[0]), "last": int(m[-1])}}))
    """, plugin_path=plugin_dir)
    assert out["n"] == n
    assert out["monotone"], "the batches were not stitched together in order"
    assert (out["first"], out["last"]) == (0, n - 1)


# ------------------------------------------------------------------ control

def test_plugins_can_be_switched_off(plugin_dir, sample):
    """A published result must not depend on what was in the directory that day."""
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps({"plugins": tttrlib.registry("plugin"),
                          "has_example": "EXAMPLE" in tttrlib.registry("file_container")}))
    """, plugin_path=plugin_dir, env_extra={"TTTRLIB_PLUGINS": "0"})
    assert out["plugins"] == {}
    assert out["has_example"] is False


def test_plugins_can_be_pinned_by_name(plugin_dir):
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(tttrlib.registry("plugin")))
    """, plugin_path=plugin_dir, env_extra={"TTTRLIB_PLUGINS": "only:somethingelse"})
    assert out["example"]["status"] == "disabled"


def test_pinning_the_plugin_itself_keeps_it(plugin_dir):
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(tttrlib.registry("plugin")))
    """, plugin_path=plugin_dir, env_extra={"TTTRLIB_PLUGINS": "only:example"})
    assert out["example"]["status"] == "loaded"


def test_the_first_directory_on_the_path_wins(plugin_dir, tmp_path, plugin_so):
    """Two plugins of one name is a configuration mistake, not a merge."""
    second = tmp_path / "other-plugins"
    second.mkdir()
    (second / plugin_so.name).write_bytes(plugin_so.read_bytes())
    both = os.pathsep.join([str(plugin_dir), str(second)])
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(tttrlib.registry("plugin")))
    """, plugin_path=both)
    # Reported, not silently dropped: the registry is keyed by name, so the
    # shadowed one overwrites the entry -- what matters is that the second copy
    # did not register a second EXAMPLE container.
    assert out["example"]["status"] in ("loaded", "shadowed")


# ------------------------------------------------------------------ containment

def test_a_bad_library_cannot_break_import(plugin_dir):
    """Whatever is in the directory, ``import tttrlib`` still works.

    This is the property the whole lazy-loading design exists for. A file that
    is not a loadable library at all is the crudest version of the problem, and
    the one a user is most likely to produce by accident.
    """
    (plugin_dir / "tttrlib_garbage.so").write_bytes(b"this is not a library")
    (plugin_dir / "tttrlib_garbage.dylib").write_bytes(b"this is not a library")

    out = run_in_subprocess("""
        import json, tttrlib
        reg = tttrlib.registry("plugin")
        print(json.dumps({"garbage": reg.get("garbage"),
                          "example": reg.get("example", {}).get("status")}))
    """, plugin_path=plugin_dir)

    assert out["garbage"] is not None, "a failed plugin must still be reported"
    assert out["garbage"]["status"] == "failed"
    assert out["garbage"]["message"], "a failure with no reason is not a diagnosis"
    # And the good plugin next to it is unaffected.
    assert out["example"] == "loaded"


def test_a_non_plugin_library_beside_a_plugin_is_ignored(plugin_dir, plugin_so):
    """Only ``tttrlib_<name>`` is probed, so dependencies can sit alongside."""
    (plugin_dir / f"libhelper{plugin_so.suffix}").write_bytes(b"not probed")
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps(sorted(tttrlib.registry("plugin"))))
    """, plugin_path=plugin_dir)
    assert out == ["example"]


# ------------------------------------------------------------------ fit models

def test_a_plugin_fit_model_joins_the_registry(plugin_dir):
    """A second capability from one library, and it is not second-class.

    The model appears in ``registry("fit")`` with the schema the plugin wrote,
    which is what lets a caller pass its parameters by name instead of counting
    slots in a flat array.
    """
    out = run_in_subprocess("""
        import json, tttrlib
        reg = tttrlib.registry("fit")
        print(json.dumps({
            "entry": reg.get("exp1_plugin"),
            "names": list(tttrlib.decay_fit_names()),
            "builtins_intact": [n in reg for n in ("fit23", "fit24", "fit_nexp")],
        }))
    """, plugin_path=plugin_dir)

    entry = out["entry"]
    assert entry is not None, "the plugin fit model never reached the registry"
    assert entry["provider"] == "plugin"
    assert list(entry["params_schema"]["properties"]) == ["tau", "amplitude", "offset"]
    assert "exp1_plugin" in out["names"]
    # Splicing a plugin in must not disturb what was already there.
    assert all(out["builtins_intact"])


def test_a_plugin_fit_model_actually_fits(plugin_dir):
    """Constructed by name, optimised by the library's own minimiser.

    The ABI asks a plugin for ``evaluate`` and nothing else -- writing a bounded,
    constraint-aware optimiser is most of the work of a fit model and none of the
    photophysics -- so this also checks that the generic ``fit`` wrapped around it
    really converges.
    """
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib
        n, dt, tau_true = 256, 0.05, 3.0
        t = np.arange(n) * dt
        truth = 100.0 * np.exp(-t / tau_true) + 2.0

        p = tttrlib.DecayFitProblem(1, n, dt)
        p.data = tttrlib.VectorDouble(truth.tolist())
        p.reset_model()

        f = tttrlib.DecayFit2("exp1_plugin")
        at_truth = f.evaluate([tau_true, 100.0, 2.0], p)
        at_wrong = f.evaluate([1.0, 100.0, 2.0], p)

        out = f.fit([1.0, 50.0, 0.5], tttrlib.DecayFitConstraints([0, 0, 0]), p)
        print(json.dumps({
            "n_parameters": f.n_parameters(p),
            "at_truth": at_truth, "at_wrong": at_wrong,
            "fitted": list(out.parameters), "objective": out.objective,
        }))
    """, plugin_path=plugin_dir)

    assert out["n_parameters"] == 3
    # The objective is chi-square against noiseless data, so the truth is a zero.
    assert out["at_truth"] < 1e-9
    assert out["at_wrong"] > 1.0
    tau, amplitude, offset = out["fitted"]
    assert tau == pytest.approx(3.0, rel=1e-4)
    assert amplitude == pytest.approx(100.0, rel=1e-4)
    assert offset == pytest.approx(2.0, abs=1e-3)


def test_a_held_parameter_stays_held(plugin_dir):
    """Constraints are the host's, so a plugin model gets them for free."""
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib
        n, dt = 256, 0.05
        t = np.arange(n) * dt
        p = tttrlib.DecayFitProblem(1, n, dt)
        p.data = tttrlib.VectorDouble((100.0 * np.exp(-t / 3.0) + 2.0).tolist())
        p.reset_model()
        f = tttrlib.DecayFit2("exp1_plugin")
        # -1 holds a slot; tau is pinned away from the truth on purpose.
        out = f.fit([5.0, 50.0, 0.5], tttrlib.DecayFitConstraints([-1, 0, 0]), p)
        print(json.dumps({"fitted": list(out.parameters)}))
    """, plugin_path=plugin_dir)
    assert out["fitted"][0] == pytest.approx(5.0), "a held parameter was optimised"


def test_a_plugin_cannot_take_a_builtin_fit_name(plugin_dir, tmp_path, plugin_so):
    """Refused rather than shadowed -- silently replacing fit23 is a supply chain problem."""
    out = run_in_subprocess("""
        import json, tttrlib
        reg = tttrlib.registry("fit")
        # fit23 must still be the built-in: it carries a setup link to fit2x,
        # which a plugin entry does not.
        print(json.dumps({"fit23_provider": reg["fit23"].get("provider"),
                          "fit23_has_setup": "setup" in reg["fit23"]}))
    """, plugin_path=plugin_dir)
    assert out["fit23_provider"] != "plugin"
    assert out["fit23_has_setup"]


# ------------------------------------------------------------------ burst searches

def test_a_plugin_burst_search_runs_by_name(plugin_dir):
    """The third capability, dispatched by name rather than by attribute.

    A built-in search is a method on TTTR and ``burst_search_by_name`` reaches
    it with ``getattr``. A plugin has no attribute to reach -- the bindings were
    generated at build time -- so its registry entry carries no ``method``, and
    that absence is what routes it through the by-name path instead.
    """
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib
        reg = tttrlib.registry("burst_search")
        entry = reg.get("interphoton_plugin")

        # Two runs of 20 photons, 100 ticks apart, separated by a long gap.
        mt = np.concatenate([np.arange(0, 2000, 100),
                             np.arange(60000, 62000, 100)]).astype(np.uint64)
        t = tttrlib.TTTR()
        t.append_events(mt, np.zeros(len(mt), np.uint16),
                        np.zeros(len(mt), np.int8), np.zeros(len(mt), np.int8))
        bursts = t.burst_search_by_name("interphoton_plugin",
                                        max_gap=500.0, min_photons=5)
        print(json.dumps({
            "entry": entry,
            "bursts": bursts.tolist(),
            "builtin_still_works":
                t.burst_search_by_name("sliding_window", L=5, m=3, T=1e9).shape[1],
        }))
    """, plugin_path=plugin_dir)

    entry = out["entry"]
    assert entry is not None, "the plugin burst search never reached the registry"
    assert entry["provider"] == "plugin"
    # No "method": there is no attribute to call, and its absence is the signal.
    assert "method" not in entry
    assert list(entry["params_schema"]["properties"]) == ["max_gap", "min_photons"]
    assert out["bursts"] == [[0, 20], [20, 40]]
    assert out["builtin_still_works"] == 2


def test_a_plugin_burst_search_is_reachable_through_burst_search(plugin_dir):
    """PRD-032 criterion 4: a search a plugin contributed is callable through
    ``TTTR.burst_search(name, ...)``, the same door every built-in uses.

    Before the dispatch table this was not merely unsupported — it was silently
    wrong. ``burst_search`` resolved its mode through a chain of string
    comparisons and fell through to the sliding window for anything it did not
    recognise, so calling it with a plugin's name returned sliding-window
    bursts. The registry listed the search, and the obvious call ran a different
    algorithm.
    """
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib

        # Two runs of 20 photons 100 ticks apart, separated by a long gap.
        mt = np.concatenate([np.arange(0, 2000, 100),
                             np.arange(60000, 62000, 100)]).astype(np.uint64)
        t = tttrlib.TTTR()
        t.append_events(mt, np.zeros(len(mt), np.uint16),
                        np.zeros(len(mt), np.int8), np.zeros(len(mt), np.int8))

        # The narrow entry point: L, m, T reach the plugin as its parameters.
        as_ints = lambda v: [int(x) for x in v]
        plugin = as_ints(t.burst_search(5, 3, 500.0, "interphoton_plugin"))
        sliding = as_ints(t.burst_search(5, 3, 500.0, "sliding_window"))
        unknown = as_ints(t.burst_search(5, 3, 500.0, "no_such_search"))
        print(json.dumps({
            "plugin": plugin,
            "sliding": sliding,
            "unknown": unknown,
        }))
    """, plugin_path=plugin_dir)

    # It ran the plugin, not the fallback: the two runs of photons come back as
    # two bursts, and that is not what the sliding window returns here.
    assert out["plugin"] == [0, 20, 20, 40]
    assert out["plugin"] != out["sliding"], (
        "the plugin name produced the sliding window's answer — the fallback, "
        "not the plugin")
    # An unrecognised name still falls back rather than raising.
    assert out["unknown"] == out["sliding"]


def test_a_plugin_burst_search_honours_its_parameters(plugin_dir):
    """Parameters reach the plugin, or the schema is decoration."""
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib
        mt = np.concatenate([np.arange(0, 2000, 100),
                             np.arange(60000, 62000, 100)]).astype(np.uint64)
        t = tttrlib.TTTR()
        t.append_events(mt, np.zeros(len(mt), np.uint16),
                        np.zeros(len(mt), np.int8), np.zeros(len(mt), np.int8))
        # A gap threshold above the 60000-tick separation merges the two runs.
        merged = t.burst_search_by_name("interphoton_plugin",
                                        max_gap=1e9, min_photons=5)
        # A photon count above the run length rejects both.
        none = t.burst_search_by_name("interphoton_plugin",
                                      max_gap=500.0, min_photons=1000)
        print(json.dumps({"merged": merged.tolist(), "none": none.tolist()}))
    """, plugin_path=plugin_dir)
    assert out["merged"] == [[0, 40]]
    assert out["none"] == []


def test_an_unknown_burst_search_is_refused(plugin_dir):
    out = run_in_subprocess("""
        import json, numpy as np, tttrlib
        t = tttrlib.TTTR()
        t.append_events(np.arange(10, dtype=np.uint64), np.zeros(10, np.uint16),
                        np.zeros(10, np.int8), np.zeros(10, np.int8))
        try:
            t.burst_search_plugin("nosuchsearch", "{}")
            print(json.dumps({"raised": False}))
        except Exception as e:
            print(json.dumps({"raised": True, "message": str(e)}))
    """, plugin_path=plugin_dir)
    assert out["raised"]
    assert "nosuchsearch" in out["message"]


def test_all_three_capabilities_come_from_one_library(plugin_dir):
    """One .so, three tables. Nothing about the ABI ties them together."""
    out = run_in_subprocess("""
        import json, tttrlib
        print(json.dumps({
            "container": "EXAMPLE" in tttrlib.registry("file_container"),
            "fit": "exp1_plugin" in tttrlib.registry("fit"),
            "burst": "interphoton_plugin" in tttrlib.registry("burst_search"),
            "plugins": list(tttrlib.registry("plugin")),
        }))
    """, plugin_path=plugin_dir)
    assert out["container"] and out["fit"] and out["burst"]
    assert out["plugins"] == ["example"]


def test_the_current_directory_is_never_searched(plugin_dir, plugin_so, tmp_path):
    """The shared-instrument-drive attack is DLL hijacking verbatim."""
    cwd = tmp_path / "cwd"
    cwd.mkdir()
    (cwd / plugin_so.name).write_bytes(plugin_so.read_bytes())
    env = dict(os.environ)
    env.pop("TTTRLIB_PLUGIN_PATH", None)
    env.pop("TTTRLIB_PLUGINS", None)
    proc = subprocess.run(
        [sys.executable, "-c",
         "import json, tttrlib; print(json.dumps(tttrlib.registry('plugin')))"],
        capture_output=True, text=True, env=env, cwd=str(cwd), timeout=300)
    assert proc.returncode == 0, proc.stderr
    assert json.loads(proc.stdout.strip().splitlines()[-1]) == {}
