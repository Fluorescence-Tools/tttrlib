"""The `signed char` array accessors must own the buffer they hand out.

`get_array<T>` mallocs, so every `(T** output, int* n_output)` accessor has to be
declared ARGOUTVIEWM -- the binding frees the allocation when the array dies.
`signed char` was the one type in that block declared ARGOUTVIEW instead, which
means "someone else owns this", and nobody did: `get_routing_channel`,
`get_event_type` and `get_used_routing_channels` leaked one byte per event per
call, in all four language bindings.

The bug is invisible to a correctness test -- the values were always right -- so
the regression test has to be a memory measurement. `maxrss` is a high-water
mark and never decreases, which is what makes it usable here: the leak is ~37 MB
over these iterations and the fixed path moves it by a couple of MB, so the
threshold sits an order of magnitude away from the noise rather than beside it.
"""

import gc

import pytest
import tttrlib

from test_settings import settings, DATA_AVAILABLE  # type: ignore

try:
    import resource
except ImportError:                     # POSIX only; Windows has no equivalent
    resource = None

# The whole module is a memory measurement, so without getrusage there is
# nothing here to run. Skipping beats an unguarded import, which made this a
# collection error and stopped the entire suite from running on Windows.
if resource is None:
    pytest.skip("maxrss needs the POSIX resource module", allow_module_level=True)


ITERATIONS = 200

# Accessors on the `(signed char** output, int* n_output)` pattern. The first two
# return one element per event; the third returns only the distinct channels, so
# it cannot leak enough to measure and is here to pin the declaration, not the
# footprint.
PER_EVENT_ACCESSORS = ["get_routing_channel", "get_event_type"]


def _maxrss_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1e6


@pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not downloaded")
@pytest.mark.parametrize("accessor", PER_EVENT_ACCESSORS)
def test_signed_char_accessors_do_not_leak(accessor):
    data = tttrlib.TTTR(settings["spc132_filename"], "SPC-130")
    n_events = data.size()

    # What a leaking build would add: one byte per event per call.
    leak_mb = ITERATIONS * n_events / 1e6
    assert leak_mb > 20, "file too small for this test to distinguish a leak"

    gc.collect()
    before = _maxrss_mb()
    for _ in range(ITERATIONS):
        arr = getattr(data, accessor)()
    del arr
    gc.collect()
    grew = _maxrss_mb() - before

    # A tenth of the leak: far above the few MB the fixed path actually moves,
    # far below the leak itself. A threshold at the midpoint would be a coin
    # flip on a loaded machine.
    assert grew < leak_mb / 10, (
        f"{accessor} grew maxrss by {grew:.1f} MB over {ITERATIONS} calls; "
        f"a leaking (ARGOUTVIEW) build grows it by about {leak_mb:.1f} MB. "
        f"Check the %apply for `signed char** output` in ext/python/misc_types.i."
    )


@pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not downloaded")
def test_signed_char_accessors_still_return_the_right_values():
    """Ownership changed; the values must not have."""
    data = tttrlib.TTTR(settings["spc132_filename"], "SPC-130")

    routing = data.get_routing_channel()
    assert routing.dtype.itemsize == 1
    assert len(routing) == data.size()

    used = data.get_used_routing_channels()
    assert set(used.tolist()) == set(routing.tolist())

    assert len(data.get_event_type()) == data.size()

    # Two calls must be independent buffers now: writing to one must not be
    # visible in the other. Under the old ARGOUTVIEW declaration they were
    # separate mallocs too, so this pins the copy semantics callers rely on
    # rather than detecting the leak.
    first = data.get_routing_channel()
    second = data.get_routing_channel()
    original = int(second[0])
    first[0] = 127
    assert int(second[0]) == original
