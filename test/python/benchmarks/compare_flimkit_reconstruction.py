"""Compare TTTRlib and FLIMKit PTU image reconstruction.

The PTU objects are opened once before timing.  This deliberately measures
reconstruction from each library's loaded representation, rather than mixing
different file-opening and metadata-parsing strategies into the result.  A
fresh TTTRlib ``CLSMImage`` is still constructed and filled on every timed
iteration.  Their lifetime fitters optimize different models and objectives,
so comparing those timings as if they were equivalent would be misleading.

FLIMKit is an optional benchmark-only dependency.  Install it (or put its
checkout on ``PYTHONPATH``), then run for example::

    PYTHONPATH=/path/to/FLIMKit python \
      test/python/benchmarks/compare_flimkit_reconstruction.py image.ptu
"""

from __future__ import annotations

import argparse
import gc
import statistics
import time
from collections.abc import Callable

import numpy as np
import tttrlib


def _measure(fn: Callable[[], np.ndarray], repeats: int):
    for _ in range(3):
        fn()
    samples = []
    result = None
    gc.disable()
    try:
        for _ in range(repeats):
            start = time.perf_counter_ns()
            result = fn()
            samples.append((time.perf_counter_ns() - start) / 1e6)
    finally:
        gc.enable()
    return {
        "best_ms": min(samples),
        "median_ms": statistics.median(samples),
        "result": result,
    }


def _tttrlib_intensity(data, channel: int):
    image = tttrlib.CLSMImage(data, channels=[channel], fill=True)
    return np.asarray(image.intensity)


def _tttrlib_decay(data, channel: int, n_bins: int):
    image = tttrlib.CLSMImage(data, channels=[channel], fill=True)
    return np.asarray(
        image.get_fluorescence_decay(
            data,
            micro_time_coarsening=1,
            stack_frames=True,
            max_micro_time_channels=n_bins,
        )
    )


def _flimkit_intensity(data, channel: int):
    return np.asarray(data.intensity_image(channel=channel))


def _flimkit_decay(data, channel: int):
    decay = data.raw_pixel_stack(channel=channel)
    if int(decay.max(initial=0)) > np.iinfo(np.uint8).max:
        raise RuntimeError(
            "this benchmark requires per-bin counts <= 255 because "
            "CLSMImage.get_fluorescence_decay currently returns uint8"
        )
    return np.asarray(decay)


def _print_row(name: str, result: dict, reference: dict | None = None):
    speedup = ""
    if reference is not None:
        speedup = f"{reference['median_ms'] / result['median_ms']:9.2f}x"
    print(
        f"{name:<24}{result['median_ms']:12.3f}"
        f"{result['best_ms']:12.3f}{speedup:>10}"
    )


def main():
    from flimkit.formats.PTU.reader import PTUFile

    parser = argparse.ArgumentParser()
    parser.add_argument("ptu", help="PicoQuant imaging PTU file")
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--repeats", type=int, default=21)
    args = parser.parse_args()

    tttr_data = tttrlib.TTTR(args.ptu)
    flimkit_data = PTUFile(args.ptu, verbose=False)
    try:
        tt_i = _measure(
            lambda: _tttrlib_intensity(tttr_data, args.channel), args.repeats
        )
        fk_i = _measure(
            lambda: _flimkit_intensity(flimkit_data, args.channel), args.repeats
        )
        tt_d = _measure(
            lambda: _tttrlib_decay(
                tttr_data, args.channel, flimkit_data.n_bins
            ),
            args.repeats,
        )
        fk_d = _measure(
            lambda: _flimkit_decay(flimkit_data, args.channel), args.repeats
        )
    finally:
        flimkit_data.close()

    # The libraries use slightly different boundary conventions when assigning
    # photons exactly on a pixel edge.  Require equivalent dimensions and total
    # photon conservation rather than falsely claiming bit-identical cubes.
    tt_shape = tuple(tt_i["result"].shape)
    fk_shape = tuple(fk_i["result"].shape)
    if tt_shape[-2:] != fk_shape[-2:]:
        raise RuntimeError(f"image shape mismatch: {tt_shape} vs {fk_shape}")
    # Validation is intentionally outside the timed region. Summing a dense
    # decay cube measures NumPy memory bandwidth, not reconstruction.
    totals = [int(tt_i["result"].sum()), int(fk_i["result"].sum())]
    relative_difference = abs(totals[0] - totals[1]) / max(totals)
    if relative_difference > 1e-3:
        raise RuntimeError(f"photon totals differ: {totals}")

    print(f"\nPTU reconstruction from pre-opened data: {args.ptu}")
    print(f"channel={args.channel}, repeats={args.repeats}")
    print(f"{'pipeline':<24}{'median [ms]':>12}{'best [ms]':>12}{'speedup':>10}")
    print("-" * 58)
    _print_row("FLIMKit intensity", fk_i)
    _print_row("TTTRlib intensity", tt_i, fk_i)
    _print_row("FLIMKit decay cube", fk_d)
    _print_row("TTTRlib decay cube", tt_d, fk_d)
    print(f"\nintensity shapes: TTTRlib={tt_shape}, FLIMKit={fk_shape}")
    print(
        "decay shapes: "
        f"TTTRlib={tt_d['result'].shape}, FLIMKit={fk_d['result'].shape}"
    )
    print(f"photon totals: TTTRlib={totals[0]}, FLIMKit={totals[1]}")


if __name__ == "__main__":
    main()
