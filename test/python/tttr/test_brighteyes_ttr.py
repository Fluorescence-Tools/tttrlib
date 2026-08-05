"""BrightEyes-TTM (.ttr), checked against the official libttp reader.

A .ttr has no header and no magic, so nothing about it can be verified from the
file itself -- which makes an A/B against the vendor's own reader the only real
check. libttp is optional; the test skips without it rather than pretending.
"""
import os

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

BE_TTR = 10
MARKER_PIXEL, MARKER_LINE, MARKER_FRAME = 1, 2, 3

pytestmark = pytest.mark.skipif(not DATA_AVAILABLE, reason="test data not available")


def _sample():
    d = os.path.join(DATA_ROOT, "brighteyes")
    if not os.path.isdir(d):
        return None
    for fn in sorted(os.listdir(d)):
        if fn.lower().endswith(".ttr"):
            return os.path.join(d, fn)
    return None


@pytest.fixture(scope="module")
def ttr_path():
    p = _sample()
    if p is None:
        pytest.skip("no .ttr file in the test data")
    return p


@pytest.fixture(scope="module")
def data(ttr_path):
    return tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR")


def test_registered_as_a_container(ttr_path):
    entry = tttrlib.registry("file_container")["BRIGHTEYES-TTR"]
    assert entry["container_type"] == BE_TTR
    assert entry["extensions"] == ".ttr"
    assert entry["can_read"] is True
    # Nothing to write yet, and the format has no header to put anything in.
    assert entry["can_write"] is False


def test_never_identified_from_contents(ttr_path):
    """A bare uint16 stream: any file at all would 'match', so nothing does.

    The container has to be named, or reached by extension. This is a property
    of the format rather than a gap in the reader.
    """
    assert tttrlib.inferTTTRFileType(ttr_path) in (-1, BE_TTR)


def test_decodes_into_the_standard_arrays(data):
    assert data.n_valid_events > 0
    photons = data.event_types == 0
    assert photons.sum() > 0
    # macro time must not go backwards, or nothing downstream works
    mt = data.macro_times.astype(np.int64)
    assert np.all(np.diff(mt) >= 0)
    # the TDC payload is 8 bits
    assert data.micro_times.max() <= 255


def test_scanner_markers_match_the_acquisition_geometry(data, ttr_path):
    """The sample is 512x512, one frame -- and says so in its filename."""
    if "512x512" not in os.path.basename(ttr_path):
        pytest.skip("geometry assertion is specific to the 512x512 sample")
    markers = data.event_types == 1
    ch = data.routing_channels[markers]
    assert int((ch == MARKER_PIXEL).sum()) == 512 * 512
    assert int((ch == MARKER_LINE).sum()) == 512
    assert int((ch == MARKER_FRAME).sum()) == 1


def _clsm(data, **kw):
    return tttrlib.CLSMImage(
        tttr_data=data,
        marker_frame_start=[MARKER_FRAME],
        marker_line_start=MARKER_LINE,
        marker_line_stop=MARKER_LINE,   # one line clock, no separate stop
        marker_event_type=1,
        n_pixel_per_line=512,
        **kw,
    )


def test_reconstructs_a_square_image_from_the_pixel_clock(data, ttr_path):
    """.ttr -> TTTR -> CLSMImage, which is the whole point of reading it.

    The scanner pulses one line clock per line and never says where a line
    ends, so start-to-start pairing loses the last line. The pixel clock does
    know: it ticks to the end of that line and stops.
    """
    if "512x512" not in os.path.basename(ttr_path):
        pytest.skip("geometry assertion is specific to the 512x512 sample")

    img = _clsm(data, use_pixel_markers=True, marker_pixel=MARKER_PIXEL)
    assert (img.n_frames, img.n_lines, img.n_pixel) == (1, 512, 512)

    a = np.asarray(img.intensity)
    assert a.shape == (1, 512, 512)
    # Nearly every photon lands somewhere; the rest fall before the first pixel
    # clock or after the last, where there is no pixel to put them in.
    n_photons = int((data.event_types == 0).sum())
    assert a.sum() > 0.99 * n_photons
    assert a.sum() <= n_photons


def test_the_reconstruction_is_an_image_and_not_a_shuffle(data, ttr_path):
    """Line count alone cannot tell a picture from scrambled rows."""
    if "512x512" not in os.path.basename(ttr_path):
        pytest.skip("geometry assertion is specific to the 512x512 sample")

    a = np.asarray(_clsm(data, use_pixel_markers=True,
                         marker_pixel=MARKER_PIXEL).intensity)[0].astype(float)

    def corr(x, y):
        x, y = x.ravel() - x.mean(), y.ravel() - y.mean()
        return float((x * y).sum() / np.sqrt((x * x).sum() * (y * y).sum()))

    # Real structure is correlated across both axes. Rows in the wrong order
    # would keep the column correlation and destroy the row correlation, which
    # is exactly what an off-by-one in the line pairing produces.
    assert corr(a[:-1], a[1:]) > 0.5
    assert corr(a[:, :-1], a[:, 1:]) > 0.5


def test_one_line_clock_is_not_read_as_alternating_start_stop(data):
    """marker_line_start == marker_line_stop means start-only, not pairs.

    Read as (start, stop) pairs it yields half the lines and silently discards
    the photons of every other one.
    """
    n_line_markers = int(((data.event_types == 1) &
                          (data.routing_channels == MARKER_LINE)).sum())
    img = _clsm(data)
    assert img.n_lines > 0.9 * n_line_markers


def test_matches_the_official_libttp_reader(ttr_path, data):
    """Photon for photon, against the vendor's own decoder."""
    libttp_ttp = pytest.importorskip("libttp.ttp", reason="libttp not installed")
    df = libttp_ttp.readNewProtocolFileToPandas(ttr_path, CHANNELS=25)

    step = df["step"].to_numpy().astype(np.float64)
    ds = np.append([0], np.diff(step))
    # the vendor's unwrap: any decrease is a wrap, and a wrap is 65536
    cum = (step + np.cumsum((ds < 0) * 65536.0)).astype(np.int64)

    valid = np.column_stack([df[f"valid_tdc_{k}"].to_numpy() > 0 for k in range(25)])
    rec, chan = np.nonzero(valid)
    order = np.lexsort((chan, rec))
    ref_macro = cum[rec[order]]
    ref_chan = chan[order].astype(np.int64)

    codes = np.column_stack([df[f"t_{k}"].to_numpy() for k in range(25)])
    ref_code = codes[rec[order], chan[order]].astype(np.int64)
    t_laser = df["t_L"].to_numpy().astype(np.int64)[rec[order]]
    laser_valid = (df["valid_tdc_L"].to_numpy() > 0)[rec[order]]
    ref_micro = np.where(laser_valid, (ref_code - t_laser) % 256, ref_code)

    photons = data.event_types == 0
    assert len(ref_macro) == int(photons.sum())
    assert np.array_equal(ref_macro, data.macro_times[photons].astype(np.int64))
    assert np.array_equal(ref_chan, data.routing_channels[photons].astype(np.int64))
    assert np.array_equal(ref_micro, data.micro_times[photons].astype(np.int64))
