"""Every burst search, measured against a simulation whose bursts are known.

The A/B tests ask "does this agree with somebody else's implementation?". This
file asks the question that does not need anybody else, and that two agreeing
implementations can both fail: **the stream is simulated, so the bursts in it
are known -- are they the ones that come back?**

That is worth having on its own terms, and it caught something a reference
could not. The Kalman search was A/B'd against an independent filterpy-based
implementation in a regime where a burst was shorter than one time bin: both
returned three detections covering all forty injected bursts and agreed
perfectly while resolving nothing. Ground truth says three is not forty.

Two numbers per search, both against the injected bursts:

* **precision** -- every burst returned overlaps one that was injected. A
  search that invents bursts fails here.
* **recall** -- most injected bursts come back, *individually*. A search that
  returns one detection spanning the measurement covers every burst and fails
  here, which is the point.

The searches are taken from the registry rather than listed, so a search added
later is tested by this file the day it registers. Its parameters come from the
registry's own defaults where they work on this workload, and from the table
below where the method needs to be told the timescale (a bin width or a
background rate is data, not a universal default).

The workload has to be the measurement these methods are *for*
---------------------------------------------------------------
An earlier version of this file put 40 bursts into short gaps, which left 77%
of the photons inside a burst. Two searches then looked broken -- the max-tree
recovered 55% of the bursts, and Bayesian blocks 57% no matter what it was
asked. Neither was broken. `BurstSearchMaxTree.h` states the assumption in its
header: the baseline is a median rate, so the contrast and significance filters
are only background filters *while bursts are a minority of the trace*, and it
records the inversion (F1 0.95 at ~24% occupancy, 0.63 at ~69%). At 77% the
median rate was itself a burst rate, and both methods were measuring bursts
against bursts.

So the simulation below is a dilute measurement: a fixed 50 kHz Poisson
background, transits 3-8 ms apart, ~30% of photons in a burst. At that
occupancy every search resolves the bursts individually, most of them at the
registry's defaults. The lesson is worth more than the fix -- a ground-truth
workload that is not the regime a method documents will fail the method rather
than test it.
"""
import json
import math

import numpy as np
import pytest

import tttrlib

RES = 1e-8                      # seconds per macro-time tick
BACKGROUND_CPS = 5.0e4          #: simulated background, counts per second


# --------------------------------------------------------------- the truth --

def simulate(seed=7, n_bursts=40, two_channels=True):
    """A dilute measurement with `n_bursts` transits in it, and where each is.

    Poisson background at `BACKGROUND_CPS`, transits of 60-200 photons over
    100-400 us (0.25-1.5 MHz, so 5-30x the background) separated by 3-8 ms --
    a diffusing single molecule at a few hundred Hz. The photon index range of
    every burst is recorded as it is built, so the truth is exact rather than
    reconstructed.

    The background is drawn from a Poisson count over the gap rather than a
    fixed number of photons, so `BACKGROUND_CPS` is a rate a search can be
    *told* (`cusum_sprt` takes one) instead of a number only this file knows.
    """
    rng = np.random.default_rng(seed)
    ticks, channels, truth = [], [], []
    t = 0
    n = 0
    for _ in range(n_bursts):
        gap = int(rng.integers(300_000, 800_000))        # 3 - 8 ms
        n_bg = int(rng.poisson(BACKGROUND_CPS * gap * RES))
        ticks.append(np.sort(rng.integers(0, gap, n_bg)) + t)
        channels.append(rng.integers(0, 2, n_bg) if two_channels else np.zeros(n_bg, int))
        t += gap
        n += n_bg
        width = int(rng.integers(10_000, 40_000))        # 100 - 400 us
        n_burst = int(rng.integers(60, 200))
        ticks.append(np.sort(rng.integers(0, width, n_burst)) + t)
        channels.append(rng.integers(0, 2, n_burst) if two_channels else np.zeros(n_burst, int))
        truth.append((n, n + n_burst - 1))
        t += width
        n += n_burst
    ticks = np.concatenate(ticks).astype(np.uint64)
    assert np.all(np.diff(ticks.astype(np.int64)) >= 0), "stream not in time order"
    tttr = tttrlib.TTTR()
    tttr.append_events(ticks, np.zeros(len(ticks), np.uint16),
                       np.concatenate(channels).astype(np.int8),
                       np.zeros(len(ticks), np.int8))
    tttr.header.set_macro_time_resolution(RES)
    return tttr, np.asarray(truth, dtype=np.int64)


#: More than one, because a floor that holds for one seed is a floor somebody
#: tuned. Every assertion below holds for all of these.
SEEDS = (3, 7, 11)


@pytest.fixture(scope="module", params=SEEDS, ids=lambda s: f"seed{s}")
def simulated(request):
    return simulate(seed=request.param)


def precision_and_recall(found, truth):
    """(precision, recall) by overlap: a detection is correct if it overlaps an
    injected burst; an injected burst is recovered if some detection overlaps
    it."""
    found = np.asarray(found).reshape(-1, 2)
    if len(found) == 0:
        return 0.0, 0.0
    correct = sum(1 for a, b in found
                  if np.any((truth[:, 0] <= b) & (truth[:, 1] >= a)))
    covered = sum(1 for a, b in truth
                  if np.any((found[:, 0] <= b) & (found[:, 1] >= a)))
    return correct / len(found), covered / len(truth)


# ------------------------------------------------------- what each needs --
#
# The timescale is data. A search that takes a bin width or a background rate
# cannot have a default that fits every measurement, so those are given here;
# everything else is the registry's default. Where a value was chosen rather
# than inherited, the reason is the note beside it -- an unexplained constant
# in a test is a number somebody tuned until it passed.
SETTINGS = {
    # a burst is 60-200 photons in 100-400 us, so 5 photons in 20 us is well
    # inside one and well outside a 50 kHz background (5 photons there take
    # 100 us).
    "sliding_window": dict(L=20, m=5, T=2e-5),

    # the one search that is *told* the background; the ratio is the contrast
    # it should trigger on, and the transits here are 5-30x background.
    "cusum_sprt": dict(min_photons=20, background_cps=BACKGROUND_CPS,
                       signal_to_background_ratio=8.0),

    # `q` is the process noise of the *rate* state, in (counts/s)^2 per bin: it
    # says how fast the background itself may drift. sqrt(1e7) = 3 kHz per bin
    # against a 50 kHz background. This is the parameter to get right -- at
    # q = 1e9 the filter may move 32 kHz per bin, so it simply follows the
    # burst up and reports no innovation (recall 100% -> 48% on this workload).
    "kalman": dict(L=20, dt=5e-5, q=1e7, r_scale=1.0, z_thresh=3.0,
                   min_len=2, merge_gap=3, per_channel=True, warmup_bins=50),

    # the hazard rate is a prior on how often the rate changes, and it is not
    # scale-free: it has to be read together with `dt`. At 20 us bins and
    # cp = 0.2 the transits come back individually; at cp <= 0.02 the run
    # length never resets and the whole measurement is returned as one burst
    # (see `test_a_detection_is_not_the_whole_measurement`, which is the test
    # that says so).
    "bocpd": dict(L=20, dt=2e-5, prior_count=1.0, prior_duration=1.0,
                  changepoint_prob=0.2, max_run=256, per_channel=False),

    # the registry's own defaults, with the Li & Ma statistic its schema
    # recommends for new work. Written out rather than left implicit because
    # loosening them is what made this search look broken: at m = 5, a 2 ms
    # background window and 3 sigma it flagged 22 bursts in pure background,
    # which is what an uncorrected 3 sigma over ~12 000 positions means.
    "maxtree": dict(L=20, m=10, background_window=0.05, min_contrast=2.0,
                    min_significance=4.0, significance_mode=2),

    # registry defaults throughout, including `pad_photons`.
    "bayesian_blocks": dict(L=20, m=5),
}

#: Searches that cannot be driven by name from a photon stream alone.
#: `coincident` is defined over a *channel grouping*, which is an instrument
#: fact this synthetic stream does not carry; it raises rather than guessing
#: (see `TTTR::burst_search_coincident`).
NEEDS_MORE_THAN_A_STREAM = {"coincident"}


def registered_searches():
    entries = json.loads(tttrlib.registry_json())["burst_search"]
    return sorted(k for k in entries if k not in NEEDS_MORE_THAN_A_STREAM)


def run_search(tttr, name):
    params = SETTINGS.get(name)
    if params is None:
        pytest.skip(f"{name} has no ground-truth workload in SETTINGS; add one")
    return np.asarray(tttr.burst_search_by_name(name, **params)).reshape(-1, 2)


# ------------------------------------------------------------------ tests --

def test_the_simulation_is_what_it_claims(simulated):
    """The truth itself: 40 bursts, disjoint, inside the stream."""
    tttr, truth = simulated
    assert len(truth) == 40
    assert np.all(truth[:, 1] >= truth[:, 0])
    assert np.all(truth[1:, 0] > truth[:-1, 1])          # disjoint, ordered
    assert truth[-1, 1] < len(tttr)


def test_the_measurement_is_dilute(simulated):
    """Bursts are a minority of the photons.

    This is a property of the *workload*, asserted because two of the searches
    below depend on it and say so: a median-rate baseline is a background
    estimate only while most photons are background (`BurstSearchMaxTree.h`).
    A future edit that makes the stream denser must fail here rather than
    silently turn this file into a test of the wrong regime -- which is what it
    was before this assertion existed."""
    tttr, truth = simulated
    occupancy = (truth[:, 1] - truth[:, 0] + 1).sum() / len(tttr)
    assert 0.15 <= occupancy <= 0.45, (
        f"{occupancy:.0%} of photons are in a burst; the searches here assume "
        f"bursts are a minority of the trace")


def test_every_registered_search_has_a_ground_truth_workload():
    """A search that registers must be measurable here. This is the line that
    fails when someone adds a search and only A/Bs it against something."""
    missing = [n for n in registered_searches() if n not in SETTINGS]
    assert not missing, (
        f"burst searches with no ground-truth workload: {missing}. Add one to "
        f"SETTINGS (the timescale is data; everything else can come from the "
        f"registry defaults), or to NEEDS_MORE_THAN_A_STREAM with the reason.")


@pytest.mark.parametrize("name", registered_searches())
def test_every_burst_it_finds_was_really_there(simulated, name):
    """Precision: no invented bursts."""
    tttr, truth = simulated
    found = run_search(tttr, name)
    assert len(found) > 0, f"{name} found nothing at all"
    precision, _ = precision_and_recall(found, truth)
    assert precision >= 0.9, (
        f"{name}: only {precision:.0%} of its {len(found)} detections overlap "
        f"an injected burst")
    assert len(found) <= 2 * len(truth), (
        f"{name}: {len(found)} detections for {len(truth)} bursts -- it is "
        f"fragmenting them")


@pytest.mark.parametrize("name", registered_searches())
def test_it_recovers_the_bursts_individually(simulated, name):
    """Recall, and one detection per burst.

    The second assertion is the one that matters: a search returning a single
    detection over the whole measurement has perfect recall by overlap and has
    found nothing. Half as many detections as bursts is already suspicious."""
    tttr, truth = simulated
    found = run_search(tttr, name)
    _, recall = precision_and_recall(found, truth)
    assert recall >= 0.85, f"{name}: recovered {recall:.0%} of the injected bursts"
    assert len(found) >= 0.8 * len(truth), (
        f"{name}: {len(found)} detections for {len(truth)} bursts -- it is "
        f"merging them, not resolving them")


@pytest.mark.parametrize("name", registered_searches())
def test_a_detection_is_not_the_whole_measurement(simulated, name):
    """The degenerate answer, stated on its own so its failure is unambiguous.

    The bound is 5% of the stream, not "less than everything": a burst here is
    60-200 of ~16 000 photons, so a correct detection is around 1.2% and any
    detection an order of magnitude wider has merged transits. This is the
    assertion that fails for `bocpd` when its hazard rate is too small for its
    bin width, where the run length never resets and one detection spans the
    whole measurement while scoring 100% recall by overlap."""
    tttr, truth = simulated
    found = run_search(tttr, name)
    widest = int(np.max(found[:, 1] - found[:, 0] + 1))
    assert widest < 0.05 * len(tttr), (
        f"{name}: its widest detection covers {widest} of {len(tttr)} photons "
        f"({widest / len(tttr):.1%}); a burst is about "
        f"{np.mean(truth[:, 1] - truth[:, 0] + 1) / len(tttr):.1%}")


@pytest.mark.parametrize("name", registered_searches())
def test_a_stream_with_no_bursts_gives_few_of_them(name):
    """The other side of precision: pure Poisson background, nothing to find.

    A threshold search will always flag some fluctuation, so this is not "zero"
    -- it is "not the same as when there were bursts", which is what separates
    a detector from a random-number generator.

    The stream is the same background rate and roughly the same duration as
    `simulate()` with the transits taken out, so the searches run at exactly
    the settings they are measured with above."""
    rng = np.random.default_rng(3)
    duration = 0.25                                   # seconds, as in simulate()
    n = int(rng.poisson(BACKGROUND_CPS * duration))
    ticks = np.sort(rng.integers(0, int(duration / RES), n)).astype(np.uint64)
    tttr = tttrlib.TTTR()
    tttr.append_events(ticks, np.zeros(n, np.uint16),
                       rng.integers(0, 2, n).astype(np.int8), np.zeros(n, np.int8))
    tttr.header.set_macro_time_resolution(RES)
    found = run_search(tttr, name)
    assert len(found) <= 0.25 * 40, (
        f"{name} found {len(found)} bursts in {n} photons of flat background, "
        f"against ~40 when there were bursts to find")


def _flat_background(seed=3, duration=0.25):
    """Poisson background at `BACKGROUND_CPS`, no transits."""
    rng = np.random.default_rng(seed)
    n = int(rng.poisson(BACKGROUND_CPS * duration))
    ticks = np.sort(rng.integers(0, int(duration / RES), n)).astype(np.uint64)
    tttr = tttrlib.TTTR()
    tttr.append_events(ticks, np.zeros(n, np.uint16),
                       rng.integers(0, 2, n).astype(np.int8), np.zeros(n, np.int8))
    tttr.header.set_macro_time_resolution(RES)
    return tttr


def test_the_false_alarm_rate_control_controls_false_alarms(simulated):
    """`maxtree`'s `max_false_alarm_rate` measured the only way it can be.

    Its schema says the setting "replaces the sigma threshold with a
    post-trials one" and that "the trials correction is approximate; verify
    against a background-only measurement". This is that measurement, and it
    pins what the setting is for -- **asking for fewer false alarms gives fewer
    false alarms, without costing bursts** -- rather than the absolute rate,
    which the schema is honest about being approximate. Measured here it is
    optimistic by about an order of magnitude (0.1/s over 0.25 s predicts 0.025
    and delivers 1), so a number, not a direction, is what this must not assert.
    """
    tttr, truth = simulated
    background = _flat_background()
    settings = dict(SETTINGS["maxtree"])
    settings.pop("min_significance", None)

    false_alarms, recalls = [], []
    for rate in (10.0, 1.0, 0.1):
        kwargs = dict(settings, min_significance=4.0, max_false_alarm_rate=rate)
        found = np.asarray(tttr.burst_search_by_name("maxtree", **kwargs)).reshape(-1, 2)
        spurious = np.asarray(
            background.burst_search_by_name("maxtree", **kwargs)).reshape(-1, 2)
        false_alarms.append(len(spurious))
        recalls.append(precision_and_recall(found, truth)[1])

    assert false_alarms == sorted(false_alarms, reverse=True), (
        f"asking for fewer false alarms per second gave {false_alarms} of them "
        f"at 10, 1 and 0.1 per second")
    assert false_alarms[0] > false_alarms[-1], (
        "the false-alarm rate setting made no difference at all")
    assert min(recalls) >= 0.9, (
        f"tightening the false-alarm rate cost bursts: recall {recalls}")
