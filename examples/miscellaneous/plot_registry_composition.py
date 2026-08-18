"""
==========================================================
The registry: discover algorithms and compose a pipeline
==========================================================

Everything ``tttrlib`` can do is in **one registry** (``tttrlib.registry()``):
every algorithm, fit model, objective, prior, correlation method and pipeline
operation registers itself there -- next to its own code -- when the library
loads, and so does every capability a drop-in plugin brings. Nothing is listed
by hand, so nothing can drift from what the code actually offers.

That turns "what can this library do?" into a query, and "run this analysis"
into a *name plus parameters*. This example does both on simulated data:

1. **discover** -- list the categories, look an entry up, read its parameters;
2. **compose** -- chain registered steps into a pipeline with
   ``tttrlib.compose``, without naming a single tttrlib function;
3. **replay** -- the same pipeline described as plain data (what a ``.pto``
   provenance record or a GUI would store) and executed from it.

The data is simulated inside the example, so it runs anywhere and the answers
can be checked against the ground truth.
"""
import json

import numpy as np
import matplotlib.pylab as plt

import tttrlib

# %%
# 1. Discover
# -----------
# The registry is a dict of categories, each a dict of entries. Ask what
# exists rather than reading the manual.
registry = tttrlib.registry()
print(f"{len(registry)} categories, "
      f"{sum(len(v) for v in registry.values())} entries")
for category in sorted(registry):
    print(f"  {category:22s} {len(registry[category]):3d}  "
          f"{', '.join(sorted(registry[category])[:4])}"
          f"{' ...' if len(registry[category]) > 4 else ''}")

# %%
# One entry says what it is, what it takes, what it produces, and what
# implements it -- ``describe`` finds it in whatever category it lives in.
entry = tttrlib.describe("burst_significance")
print(entry["label"])
print(entry["summary"])
print("operation_type:", entry["operation_type"])
print("parameters:", json.dumps(tttrlib.defaults("burst_significance")))
print("implemented by:", entry["api"])
print("cite:", entry["references"][0]["title"])

# %%
# ``resolve`` returns the callable an entry names: a function, a
# ``(class, method)`` pair when the step applies to an object, or the class
# itself when constructing it *is* the step.
print(tttrlib.resolve("region_segmentation"))
print(tttrlib.resolve("burst_significance"))
print(tttrlib.resolve("fcs_correlation"))

# %%
# 2. Simulate a photon stream
# ---------------------------
# Bursts of a diffusing molecule on a background, two detection channels.
rng = np.random.default_rng(2)
macro, chan = [], []
t = 0
for _ in range(80):
    t += int(rng.integers(20_000, 50_000))                 # dark gap
    n = int(rng.integers(80, 200))                         # a burst
    arrivals = np.sort(rng.integers(0, 5_000, n)) + t
    macro.append(arrivals)
    chan.append(rng.integers(0, 2, n))
    t = int(arrivals[-1])
    n_bg = int(rng.integers(5, 25))                        # background photons
    bg = np.sort(rng.integers(0, 40_000, n_bg)) + t
    macro.append(bg)
    chan.append(rng.integers(0, 2, n_bg))

macro_times = np.sort(np.concatenate(macro).astype(np.uint64))
channels = np.concatenate(chan).astype(np.int8)

data = tttrlib.TTTR()
data.append_events(macro_times, np.zeros(len(macro_times), np.uint16),
                   channels, np.zeros(len(macro_times), np.int8))
data.header.set_macro_time_resolution(1e-9)                # 1 tick = 1 ns
print(f"{len(data)} photons, {macro_times[-1] * 1e-9:.3f} s")

# %%
# 3. Compose a pipeline from registry names
# -----------------------------------------
# Each step is ``(name, parameters, adapter)``. The adapter says how the
# previous step's output becomes this step's input -- everything else is the
# registry's business. Note that no tttrlib function is named here.
search = tttrlib.compose(
    ("burst_selection", {"L": 30, "m": 5, "T": 5e-6},
     lambda tttr: ((tttr, "sliding_window"), {})),
)
bursts = np.asarray(search(data))
print(f"{len(bursts)} bursts, steps = {search.steps}")

significance = tttrlib.compose(
    ("burst_significance", {"background_window": 0.05, "significance_mode": 2},
     lambda tttr: ((tttr, bursts.ravel().tolist()), {})),
)
sigma = np.asarray(significance(data))
print(f"significance: median {np.median(sigma):.1f} sigma, "
      f"{np.count_nonzero(sigma > 5)} bursts above 5 sigma")

# %%
# 4. The same pipeline as data
# ----------------------------
# A pipeline written as a list of dicts is what a ``.pto`` provenance record
# stores, and what a GUI or a config file would produce. Executing it needs no
# code that knows the steps -- only the registry.
recipe = [
    {"operation": "burst_selection",
     "parameters": {"L": 30, "m": 5, "T": 5e-6}, "algorithm": "sliding_window"},
    {"operation": "burst_significance",
     "parameters": {"background_window": 0.05, "significance_mode": 2}},
]

for step in recipe:                       # every step must exist before running
    described = tttrlib.describe(step["operation"])
    print(f"{step['operation']:22s} -> {described['label']}")

value = data
found = None
for step in recipe:
    name, params = step["operation"], dict(step["parameters"])
    target = tttrlib.resolve(name)
    cls, attr = target if isinstance(target, tuple) else (None, None)
    method = getattr(value, attr) if cls is not None else target
    if name == "burst_selection":
        found = np.asarray(method(step["algorithm"], **params))
        value = data
    else:
        value = np.asarray(method(found.ravel().tolist(), *params.values()))
print(f"replayed: {len(found)} bursts, median {np.median(value):.1f} sigma")
assert np.array_equal(value, sigma)       # identical to the composed pipeline

# %%
# 5. What the registry buys
# -------------------------
# The burst table's columns and the significance both came from entries a
# consumer can read: it can build the form, validate the parameters, and
# replay the analysis from the record. Below: burst sizes against their
# significance, coloured by the burst duration -- all three quantities
# produced by steps named from the registry.
sizes = bursts[:, 1] - bursts[:, 0] + 1
durations_ms = (macro_times[bursts[:, 1]] - macro_times[bursts[:, 0]]) * 1e-6

fig, ax = plt.subplots(1, 2, figsize=(10, 4), constrained_layout=True)
sc = ax[0].scatter(sizes, sigma, c=durations_ms, s=18, cmap="viridis")
ax[0].set_xlabel("burst size (photons)")
ax[0].set_ylabel("significance (sigma)")
ax[0].set_title("burst_selection -> burst_significance")
fig.colorbar(sc, ax=ax[0], label="duration (ms)")

categories = sorted(registry, key=lambda c: -len(registry[c]))[:12]
ax[1].barh(categories, [len(registry[c]) for c in categories], color="#4c72b0")
ax[1].invert_yaxis()
ax[1].set_xlabel("entries")
ax[1].set_title("the one registry, by category")
plt.show()

# %%
# A plugin's capabilities appear in exactly the same categories (with
# ``provider: plugin``), so a pipeline written against the registry picks up a
# drop-in algorithm without a line of code changing -- see
# :ref:`plugins` and ``examples/plugin/tttrlib_example.c``.
