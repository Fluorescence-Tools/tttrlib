"""StreamingCLSMImage against the batch CLSMImage.

A scanned image reconstructed from a live stream has to be the same image the
file reader produces from the same events — per frame, not just in aggregate:

    LIVE         the frame being scanned right now, filling in as it goes
    INTEGRATING  the sum over completed frames, which must equal the batch's
                 frames summed

and the switch between the two has to work *while events are still arriving*,
without losing what has already been accumulated.

Real acquisitions, not synthetic markers: frame and line marker semantics are
instrument-specific, and a synthetic stream would only exercise the part this
class does not delegate to the batch reconstruction.
"""
import numpy as np
import pytest

import tttrlib

from test_settings import settings, DATA_AVAILABLE  # type: ignore

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")

HT3 = settings["clsm_ht3_sample1_filename"]
HT3_PARAMS = dict(
    marker_frame_start=[4],
    marker_line_start=1,
    marker_line_stop=2,
    marker_event_type=1,
    n_pixel_per_line=256,
    reading_routine="default",
    skip_before_first_frame_marker=True,
)

LIVE = tttrlib.StreamingCLSMImage.LIVE
INTEGRATING = tttrlib.StreamingCLSMImage.INTEGRATING


def batch_frames(tttr, **params):
    """(n_frames, n_lines, n_pixel) from the ordinary reconstruction."""
    img = tttrlib.CLSMImage(tttr_data=tttr, fill=True, **params)
    return np.asarray(img.get_intensity_u32(), dtype=np.uint64)


def settings_for(**params):
    s = tttrlib.CLSMSettings()
    s.marker_frame_start = tttrlib.VectorInt32(params["marker_frame_start"])
    s.marker_line_start = params["marker_line_start"]
    s.marker_line_stop = params["marker_line_stop"]
    s.marker_event_type = params["marker_event_type"]
    s.n_pixel_per_line = params["n_pixel_per_line"]
    s.skip_before_first_frame_marker = params["skip_before_first_frame_marker"]
    return s


def events_of(tttr):
    return (np.asarray(tttr.macro_times, dtype=np.uint64),
            np.asarray(tttr.micro_times, dtype=np.uint16),
            np.asarray(tttr.routing_channels, dtype=np.int8),
            np.asarray(tttr.event_types, dtype=np.int8))


def feed(img, tttr, lo=0, hi=None):
    """Bulk delivery, as an acquisition hands over a buffer of records."""
    hi = tttr.size() if hi is None else hi
    if hi <= lo:
        return
    img.push_tttr(tttrlib.TTTR(tttr, np.arange(lo, hi, dtype=np.int32)))


def new_stream(ht3, mode):
    return tttrlib.StreamingCLSMImage(ht3, settings_for(**HT3_PARAMS), mode)


@pytest.fixture(scope="module")
def ht3():
    return tttrlib.TTTR(HT3, "HT3")


@pytest.fixture(scope="module")
def ht3_batch(ht3):
    return batch_frames(ht3, **HT3_PARAMS)


# ------------------------------------------------------------- the two modes --

def test_integrating_matches_the_batch_sum(ht3, ht3_batch):
    img = new_stream(ht3, INTEGRATING)
    feed(img, ht3)
    img.flush()
    assert img.frames_completed() == ht3_batch.shape[0], (
        f"{img.frames_completed()} frames streamed vs {ht3_batch.shape[0]} batch")
    np.testing.assert_array_equal(np.asarray(img.get_intensity(), dtype=np.uint64),
                                  ht3_batch.sum(axis=0))


@pytest.mark.heavy
def test_every_completed_frame_matches_its_batch_frame(ht3, ht3_batch):
    """Not only the total: each frame as it closes. Event-at-a-time on purpose —
    this is the delivery a live acquisition makes."""
    img = new_stream(ht3, LIVE)
    mt, mi, rc, et = events_of(ht3)
    seen = 0
    for i in range(ht3.size()):
        img.push_event(int(mt[i]), int(mi[i]), int(rc[i]), int(et[i]))
        if img.frames_completed() > seen:
            np.testing.assert_array_equal(
                np.asarray(img.get_last_frame(), dtype=np.uint64), ht3_batch[seen],
                err_msg=f"completed frame {seen}")
            seen += 1
    img.flush()
    assert seen >= 1


# ---------------------------------------------------- switching mid-stream --

def test_switch_mid_stream_keeps_everything(ht3, ht3_batch):
    """The switch the user of a live viewer actually performs: watch it scan,
    look at the total, go back to watching — without pausing the acquisition
    and without losing what was already integrated."""
    n = ht3.size()
    img = new_stream(ht3, LIVE)

    feed(img, ht3, 0, n // 3)
    frames_at_first_switch = img.frames_completed()
    assert frames_at_first_switch >= 1, "need at least one completed frame to compare"

    # Switch to INTEGRATING mid-stream: the sum must already hold every frame
    # completed so far, not just the ones after the switch.
    img.set_mode(INTEGRATING)
    assert img.get_mode() == INTEGRATING
    partial_sum = np.asarray(img.get_intensity(), dtype=np.uint64)
    np.testing.assert_array_equal(partial_sum,
                                  ht3_batch[:frames_at_first_switch].sum(axis=0))

    # Keep feeding across the switch, then switch back.
    feed(img, ht3, n // 3, 2 * n // 3)
    img.set_mode(LIVE)
    assert img.get_mode() == LIVE
    feed(img, ht3, 2 * n // 3, n)
    img.flush()

    # Having switched twice mid-stream must leave the total exactly where an
    # untouched run leaves it.
    img.set_mode(INTEGRATING)
    np.testing.assert_array_equal(np.asarray(img.get_intensity(), dtype=np.uint64),
                                  ht3_batch.sum(axis=0))
    assert img.frames_completed() == ht3_batch.shape[0]


@pytest.mark.slow
def test_switching_does_not_perturb_the_result(ht3, ht3_batch):
    """A run switched many times mid-stream and a run never switched must end
    identical — the mode is a view, not a processing path."""
    n = ht3.size()
    straight = new_stream(ht3, INTEGRATING)
    feed(straight, ht3)
    straight.flush()

    switched = new_stream(ht3, LIVE)
    step = max(1, n // 11)
    for k, lo in enumerate(range(0, n, step)):
        feed(switched, ht3, lo, min(n, lo + step))
        switched.set_mode(INTEGRATING if k % 2 else LIVE)
        switched.get_intensity()          # a viewer redraws on every switch
    switched.flush()
    switched.set_mode(INTEGRATING)

    np.testing.assert_array_equal(np.asarray(switched.get_intensity()),
                                  np.asarray(straight.get_intensity()))


# ------------------------------------------------------- the current frame --

def test_current_frame_matches_the_batch_over_the_events_it_holds(ht3):
    """The partially scanned frame is not an approximation: it is what the batch
    reconstruction produces from exactly the events buffered for it.

    Note the batch has no partial frame of its own — reconstructing a prefix of
    the stream *drops* the frame in progress, which is precisely the gap this
    class fills. So the comparison is against a batch run over the buffered
    events alone, and it is asserted on the lines that have been scanned; the
    rest of the canvas is black by construction."""
    n = ht3.size()
    et = np.asarray(ht3.event_types)
    rc = np.asarray(ht3.routing_channels)
    frame_markers = np.where((et == HT3_PARAMS["marker_event_type"]) &
                             (rc == HT3_PARAMS["marker_frame_start"][0]))[0]

    prefix = int(n // 4)
    open_from = int(frame_markers[frame_markers < prefix].max())

    img = new_stream(ht3, LIVE)
    feed(img, ht3, 0, prefix)
    assert img.events_in_current_frame() == prefix - open_from
    live = np.asarray(img.get_intensity(), dtype=np.uint64)
    assert live.shape == (img.n_lines(), img.n_pixel())

    buffered = tttrlib.TTTR(ht3, np.arange(open_from, prefix, dtype=np.int32))
    batch_partial = batch_frames(buffered, **HT3_PARAMS)[0]
    scanned = batch_partial.shape[0]
    assert 0 < scanned < img.n_lines(), "expected a frame caught mid-scan"

    np.testing.assert_array_equal(live[:scanned], batch_partial)
    assert live[scanned:].sum() == 0, "unscanned lines must still be black"


def test_current_frame_grows_and_then_becomes_the_completed_frame(ht3, ht3_batch):
    mt, mi, rc, et = events_of(ht3)
    img = new_stream(ht3, LIVE)

    totals = []
    i = 0
    while img.frames_completed() < 1 and i < ht3.size():
        img.push_event(int(mt[i]), int(mi[i]), int(rc[i]), int(et[i]))
        i += 1
        if i % 20000 == 0:
            totals.append(int(np.asarray(img.get_intensity(), dtype=np.uint64).sum()))

    assert len(totals) >= 2, "expected several polls while the first frame scanned"
    assert all(b >= a for a, b in zip(totals, totals[1:])), (
        f"a partially scanned frame must only gain photons: {totals}")
    np.testing.assert_array_equal(
        np.asarray(img.get_last_frame(), dtype=np.uint64), ht3_batch[0])


# ------------------------------------------------------------ bookkeeping --

def test_chunked_delivery_is_identical(ht3):
    whole = new_stream(ht3, INTEGRATING)
    feed(whole, ht3)
    whole.flush()

    chunked = new_stream(ht3, INTEGRATING)
    n = ht3.size()
    step = max(1, n // 41)
    for lo in range(0, n, step):
        feed(chunked, ht3, lo, min(n, lo + step))
    chunked.flush()

    np.testing.assert_array_equal(np.asarray(chunked.get_intensity()),
                                  np.asarray(whole.get_intensity()))


def test_clear_resets_to_nothing(ht3):
    img = new_stream(ht3, INTEGRATING)
    feed(img, ht3)
    img.flush()
    assert img.frames_completed() > 0
    img.clear()
    assert img.frames_completed() == 0
    assert np.asarray(img.get_intensity()).sum() == 0


def test_a_prototype_is_required():
    """The header carries the scan geometry and the marker layout."""
    with pytest.raises(Exception):
        tttrlib.StreamingCLSMImage(None, tttrlib.CLSMSettings(), LIVE)
