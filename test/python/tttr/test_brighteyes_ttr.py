"""BrightEyes-TTM (.ttr): geometry, markers, round trip.

A .ttr has no header and no magic, so nothing about it can be verified from the
file itself -- which makes an A/B against the vendor's own reader the only real
check. That A/B lives in ``test_ab_core_reference.py::…::
test_brighteyes_ttr_matches_libttp`` (recorded libttp decode of the first 4 M
words: photons, macro/micro times and marker edges identical); this file checks
what the sample's filename promises about the acquisition.
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
    assert entry["can_write"] is True


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


def test_round_trips_through_its_own_writer(data, tmp_path):
    """Every event array, bit for bit, markers included.

    The counter in the file is 16 bits and the reader recovers absolute time by
    counting decreases, so a stream starting hundreds of millions of ticks in
    only survives if the writer walks the counter up from zero.
    """
    out = str(tmp_path / "rt.ttr")
    assert data.write(out, "BRIGHTEYES-TTR")
    back = tttrlib.TTTR(out, "BRIGHTEYES-TTR")

    assert back.n_valid_events == data.n_valid_events
    for name in ("macro_times", "micro_times", "routing_channels", "event_types"):
        assert np.array_equal(np.asarray(getattr(back, name)),
                              np.asarray(getattr(data, name))), name


def test_the_written_file_still_reconstructs_the_image(data, ttr_path, tmp_path):
    """Markers are edges in the file, so they are the part a writer loses."""
    if "512x512" not in os.path.basename(ttr_path):
        pytest.skip("geometry assertion is specific to the 512x512 sample")
    out = str(tmp_path / "rt.ttr")
    data.write(out, "BRIGHTEYES-TTR")
    img = _clsm(tttrlib.TTTR(out, "BRIGHTEYES-TTR"),
                use_pixel_markers=True, marker_pixel=MARKER_PIXEL)
    assert (img.n_frames, img.n_lines, img.n_pixel) == (1, 512, 512)


def test_refuses_what_the_format_cannot_hold(data, tmp_path):
    """A marker that is not the pixel, line or frame clock has no field.

    Refusing beats writing a file that quietly lacks it.
    """
    sel = tttrlib.TTTR(data, np.arange(1000, dtype=np.int32))
    et = np.asarray(sel.event_types).astype(np.int8)
    ch = np.asarray(sel.routing_channels).astype(np.int8)
    et[0], ch[0] = 1, 9          # a marker on a channel the format has no bit for
    bad = tttrlib.TTTR()
    bad.append_events(np.asarray(sel.macro_times).astype(np.uint64),
                      np.asarray(sel.micro_times).astype(np.uint16), ch, et)
    assert not bad.write(str(tmp_path / "bad.ttr"), "BRIGHTEYES-TTR")


def _tag(tttr, name, default=None):
    """One header tag's value, by name."""
    values = tttr.header.data.get(name)
    return default if not values else values[0]


# ---------------------------------------------------------------- parameters

def test_the_container_declares_what_it_needs_to_be_told():
    """A .ttr carries neither its clock nor its laser nor its channel count.

    Every other built-in container describes itself completely. This one
    publishes what it needs as JSON Schema, in the registry, so a caller in any
    language can ask instead of being told.
    """
    entry = tttrlib.registry("file_container")["BRIGHTEYES-TTR"]
    schema = entry["params_schema"]
    assert set(schema["properties"]) == {
        "n_channels", "sysclk_MHz", "laser_MHz", "tdc_ps_per_code",
        "auto_calibrate_tdc", "drop_filler",
    }
    assert schema["properties"]["sysclk_MHz"]["default"] == 240.0
    # and it is still the only one that cannot be READ without being told. The
    # other schemas in the registry are ranges: optional, and about
    # how much of a file to decode rather than about what the bytes mean.
    for name, other in tttrlib.registry("file_container").items():
        if name == "BRIGHTEYES-TTR":
            continue
        assert set(other["params_schema"].get("properties", {})) <= {
            "first_record", "n_records", "first_event", "n_events",
        }, f"{name} declares a parameter that is not a range"
    assert not tttrlib.registry("file_container")["PHOTON-HDF5"]["params_schema"]


def test_parameters_reach_the_reader(ttr_path):
    """The sample clock is what a macro time is counted in."""
    slow = tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR", '{"sysclk_MHz": 120.0}')
    assert slow.header.macro_time_resolution == pytest.approx(1.0 / 120e6)
    assert slow.get_container_parameters() == '{"sysclk_MHz": 120.0}'

    default = tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR")
    assert default.header.macro_time_resolution == pytest.approx(1.0 / 240e6)


def test_a_misspelled_parameter_is_refused(ttr_path):
    """Quietly reading with the defaults instead is a wrong answer that looks right."""
    d = tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR", '{"sysclock_MHz": 240.0}')
    assert d.n_valid_events == 0


def test_a_container_with_no_parameters_refuses_them(ttr_path):
    d = tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR")   # a real read, to compare against
    assert d.n_valid_events > 0
    empty = tttrlib.TTTR(ttr_path, "BRIGHTEYES-TTR", "{}")
    assert empty.n_valid_events == d.n_valid_events   # an empty object is not an offer


# --------------------------------------------------------------- calibration

def test_micro_times_are_flagged_uncalibrated(data):
    """A delay-line code is not a time, and must not be mistaken for one."""
    assert _tag(data, "BrightEyes_MicroTimeCalibrated") == 0
    assert _tag(data, "BrightEyes_MicroTimeUnit") == "tdc_code"


@pytest.fixture(scope="module")
def calibrated(ttr_path):
    return tttrlib.TTTR(
        ttr_path, "BRIGHTEYES-TTR",
        '{"laser_MHz": 80.0, "sysclk_MHz": 240.0, "auto_calibrate_tdc": true}')


def test_calibration_says_so_in_the_header(calibrated):
    assert _tag(calibrated, "BrightEyes_MicroTimeCalibrated") == 1
    assert _tag(calibrated, "BrightEyes_MicroTimeUnit") == "picoseconds"
    # one nominal TDC least-significant bit: 4166.7 ps / 256
    assert calibrated.header.micro_time_resolution == pytest.approx(4166.67e-12 / 256, rel=1e-3)
    # and the axis spans one laser period
    assert calibrated.header.number_of_micro_time_channels == 768


def test_calibrating_changes_no_event_but_the_micro_time(data, calibrated):
    """It is a reinterpretation of one column, not a different decode."""
    assert calibrated.n_valid_events == data.n_valid_events
    for name in ("macro_times", "routing_channels", "event_types"):
        assert np.array_equal(np.asarray(getattr(calibrated, name)),
                              np.asarray(getattr(data, name))), name


def test_the_calibrated_decay_has_no_delay_line_comb(calibrated, ttr_path):
    """The point of calibrating: unequal taps stop looking like structure.

    Uncalibrated, the arrival histogram carries spikes wherever the delay line
    has a wide tap -- they are the tap widths, not photons. The check is on the
    slow-rising side of the decay, away from the sharp edge, where a real decay
    is smooth and a comb is not.
    """
    if "80MHz" not in os.path.basename(ttr_path):
        pytest.skip("the laser rate is taken from the sample's filename")

    photons = np.asarray(calibrated.event_types) == 0
    micro = np.asarray(calibrated.micro_times)[photons]
    h, _ = np.histogram(micro, bins=64, range=(0, 768))
    assert h.sum() > 0

    # The decay's own curvature is gentle over one bin, so a bin differing
    # sharply from both neighbours is the delay line and not the sample.
    mid = h[1:-1].astype(float)
    neighbour_mean = (h[:-2] + h[2:]) / 2.0
    keep = neighbour_mean > 100
    excess = np.abs(mid[keep] - neighbour_mean[keep]) / neighbour_mean[keep]
    assert excess.max() < 0.25


@pytest.mark.slow
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
