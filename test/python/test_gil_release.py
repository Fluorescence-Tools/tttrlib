"""A long tttrlib call must not stall other Python threads.

The wrapper is generated with SWIG ``-threads``, so the GIL is released
around every C++ call. The test is the one BUGS.md prescribes: a worker
thread runs a long computation while the main thread keeps a heartbeat
alive, and the heartbeat must not stall for the duration of the call --
which is exactly what happens when the GIL is held.
"""

import threading
import time

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")


def _long_call(n_tau):
    """A CPU-bound C++ call whose duration scales with the grid size."""
    n, dt = 4096, 0.01
    t = np.arange(n) * dt
    irf = np.exp(-0.5 * ((t - 5.0) / 0.2) ** 2)
    irf /= irf.sum()
    decay = np.random.poisson(
        (np.exp(-t / 4.0) * 5e4 / np.exp(-t / 4.0).sum()) + 5.0
    ).astype(float)
    tau = np.linspace(0.5, 8.0, n_tau)
    return lambda: tttrlib.solve_tcspc_mem_lifetime(
        decay.tolist(), irf.tolist(), dt, tau.tolist(),
        0.0, 0.0, 0.0, 0, n - 1, 0.0, nu=1e-4, max_iter=2)


def test_a_long_call_does_not_stall_the_heartbeat():
    np.random.seed(0)
    # Scale the work up until one call takes long enough to be meaningful on
    # this machine; the heartbeat assertion needs a call that DOMINATES the
    # tolerated gap.
    call, duration = None, 0.0
    for n_tau in (300, 600, 900, 1400):
        call = _long_call(n_tau)
        t0 = time.perf_counter()
        call()
        duration = time.perf_counter() - t0
        if duration >= 0.4:
            break
    if duration < 0.4:
        pytest.skip("machine too fast to make the call long enough")

    gaps = []
    done = threading.Event()

    def worker():
        call()
        done.set()

    thread = threading.Thread(target=worker)
    last = time.perf_counter()
    thread.start()
    while not done.is_set():
        time.sleep(0.005)
        now = time.perf_counter()
        gaps.append(now - last)
        last = now
    thread.join()

    max_gap = max(gaps)
    # With the GIL held for the whole call the main thread beats once, after
    # the call: max_gap ~= duration. Released, it beats every few ms all the
    # way through. Half the duration is a loose bound that still separates
    # the two outcomes decisively.
    assert max_gap < 0.5 * duration, (
        f"heartbeat stalled for {max_gap:.3f}s during a {duration:.3f}s call "
        "-- the GIL is not released"
    )
