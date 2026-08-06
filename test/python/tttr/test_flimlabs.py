"""FLIM LABS time-tagger files (``STT1`` and ``ITT1``).

**Every fixture here is synthetic.** No FLIM LABS sample data is published --
see ``flimlabs_writer.py`` and PRD-012 -- so these tests check the reader
against the specification, which is unambiguous, and against the vendor's own
reader scripts, which are its source. What they cannot check is that the
instrument writes what the specification says. Until a real file exists, treat
the reader as spec-conformant rather than verified.

What the tests are really about is the one thing the format forces a reader to
decide: its times are floating-point nanoseconds and tttrlib's are integer
ticks, and the file does not say what tick to use.
"""
import json
import os
import struct

import numpy as np
import pytest
import tttrlib
from test_settings import DATA_ROOT  # type: ignore

from flimlabs_writer import (  # type: ignore
    MARKER_FRAME, MARKER_LINE, MARKER_PIXEL, write_itt1, write_stt1,
)

STT1, ITT1 = 11, 12
PERIOD = 25.0          # ns; 40 MHz, the rate in the published sample headers
MICRO_BIN = PERIOD / 256.0


def _tag(tttr, name):
    """One header tag's value, by name."""
    return tttr.header.data[name][0]


@pytest.fixture
def simple(tmp_path):
    """One frame, two lines, a pixel each, and two photons per pixel.

    Written out of time order on purpose: the real files interleave per-channel
    FIFOs, which is why every vendor script sorts after loading.
    """
    events = [
        (0,  1.0,  200.0),      # a late photon, written first
        (MARKER_FRAME, 0.0, 0.0),
        (MARKER_LINE,  0.0, 0.0),
        (MARKER_PIXEL, 0.0, 0.0),
        (0,  2.0,  50.0),
        (1, 12.5,  75.0),
        (MARKER_LINE,  0.0, 100.0),
        (MARKER_PIXEL, 0.0, 100.0),
        (1, 24.9, 150.0),
    ]
    p = str(tmp_path / "tagger.bin")
    write_stt1(p, events, channels=(0, 1), laser_period_ns=PERIOD)
    return p


def test_registered_as_containers():
    reg = tttrlib.registry("file_container")
    assert reg["FLIMLABS-STT1"]["container_type"] == STT1
    assert reg["FLIMLABS-ITT1"]["container_type"] == ITT1
    for name in ("FLIMLABS-STT1", "FLIMLABS-ITT1"):
        assert reg[name]["extensions"] == ".bin"
        assert reg[name]["can_read"] is True
        # Writing bakes the tick choice into a file we emit as if the
        # instrument had. Not before the reader has seen a real one.
        assert reg[name]["can_write"] is False


def test_identified_by_magic_not_by_extension(simple, tmp_path):
    """``.bin`` claims nothing; the four magic bytes claim everything."""
    assert tttrlib.inferTTTRFileType(simple) == STT1

    itt = str(tmp_path / "trace.bin")
    write_itt1(itt, [(0, 10.0), (1, 20.0)])
    assert tttrlib.inferTTTRFileType(itt) == ITT1

    # An analysis product sharing the envelope is not a photon stream. There is
    # nothing to read a decay curve into, so nothing claims it.
    other = str(tmp_path / "decay.bin")
    with open(other, "wb") as f:
        blob = json.dumps({"channels": [1, 3], "laser_period_ns": 25.0}).encode()
        f.write(b"SP01" + struct.pack("<I", len(blob)) + blob + b"\x00" * 1032
                )
    assert tttrlib.inferTTTRFileType(other) == -1


def test_the_published_vendor_samples_are_not_claimed():
    """The only real FLIM LABS bytes that exist, and none of them is a photon stream.

    ``SP01``, ``SPF1``, ``IT02`` and ``FCS1`` are decay curves, phasors, intensity
    traces and correlation curves. They share the envelope the time taggers use,
    which is exactly the trap: recognising the envelope instead of the magic
    would read four analysis products as photon streams.
    """
    d = os.path.join(DATA_ROOT, "flimlabs", "samples")
    if not os.path.isdir(d):
        pytest.skip("no flimlabs sample directory in the test data")
    samples = [f for f in sorted(os.listdir(d)) if f.endswith(".bin")]
    if not samples:
        pytest.skip("no .bin samples")
    for fn in samples:
        p = os.path.join(d, fn)
        with open(p, "rb") as f:
            magic = f.read(4)
        assert magic not in (b"STT1", b"ITT1"), f"{fn} is a time tagger after all"
        assert tttrlib.inferTTTRFileType(p) == -1, fn


def test_a_truncated_record_is_not_a_match(simple):
    """Four bytes are weak evidence on an extension as generic as ``.bin``."""
    with open(simple, "rb") as f:
        data = f.read()
    broken = simple + ".broken.bin"
    with open(broken, "wb") as f:
        f.write(data[:-3])          # half a record left over
    try:
        assert tttrlib.inferTTTRFileType(broken) == -1
    finally:
        os.remove(broken)


def test_output_is_time_ordered(simple):
    """The file is not sorted, and everything downstream assumes it is."""
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    mt = np.asarray(d.macro_times).astype(np.int64)
    assert np.all(np.diff(mt) >= 0)
    assert d.n_valid_events == 9


def test_macro_times_are_laser_pulses(simple):
    """The tick is the laser period, so the container behaves like a T3 file.

    That is what keeps ``macro_time * resolution`` exact instead of approximate,
    and it is the unit the instrument works in.
    """
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    mt = np.asarray(d.macro_times).astype(np.int64)
    # 0, 0, 0, 50, 75, 100, 100, 150, 200 ns at 25 ns per pulse
    assert list(mt) == [0, 0, 0, 2, 3, 4, 4, 6, 8]

    assert _tag(d, "MeasDesc_GlobalResolution") == pytest.approx(PERIOD * 1e-9)


def test_the_quantisation_choice_is_written_into_the_header(simple):
    """It cannot be recovered from the file, so it has to be recorded."""
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    assert _tag(d, "FlimLabs_MacroTimeUnit") == "laser_pulse"
    assert _tag(d, "FlimLabs_LaserPeriod_ns") == PERIOD
    # Every macro time in the fixture is a whole number of pulses, so nothing
    # was lost. A residual near one period would say the file's macro times are
    # absolute arrival times instead -- both are handled, and this says which.
    assert _tag(d, "FlimLabs_MacroTimeResidual_ns") == pytest.approx(0.0)


def test_absolute_macro_times_keep_their_remainder(tmp_path):
    """If the file times photons absolutely, the pulse is the whole part.

    Rounding instead of flooring would push a photon arriving late in its period
    onto the next pulse, while its micro time still says late -- shifting it a
    full period. The residual tag reports which reading the file turned out to
    need.
    """
    p = str(tmp_path / "absolute.bin")
    # 24.0 ns into pulse 4: absolute 124.0, micro 24.0
    write_stt1(p, [(0, 24.0, 124.0)], laser_period_ns=PERIOD)
    d = tttrlib.TTTR(p, "FLIMLABS-STT1")
    assert int(d.macro_times[0]) == 4
    assert int(d.micro_times[0]) == int(24.0 / MICRO_BIN)
    assert _tag(d, "FlimLabs_MacroTimeResidual_ns") == pytest.approx(24.0)


def test_a_long_acquisition_does_not_drift_a_pulse(tmp_path):
    """A pulse count survives the round trip through nanoseconds.

    ``k`` pulses reach the file as the nearest double to ``k * period``, and
    dividing that back can land a hair under ``k``. Floor then answers
    ``k - 1``. A fixed epsilon hides it for small ``k`` and stops working past a
    few million pulses -- which at 40 MHz is a tenth of a second, so it would
    have been wrong on every real file and right on every short fixture.
    """
    counts = [1, 7, 4_500_001, 123_456_789, 400_000_000]
    p = str(tmp_path / "long.bin")
    write_stt1(p, [(0, 0.0, k * PERIOD) for k in counts], laser_period_ns=PERIOD)
    d = tttrlib.TTTR(p, "FLIMLABS-STT1")
    assert list(np.asarray(d.macro_times).astype(np.int64)) == counts
    assert _tag(d, "FlimLabs_MacroTimeResidual_ns") == pytest.approx(0.0, abs=1e-3)


def test_micro_times_use_the_hardware_binning(simple):
    """256 bins of the laser period -- what the instrument itself histograms to."""
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    assert d.header.number_of_micro_time_channels == 256

    photons = np.asarray(d.event_types) == 0
    micro = np.asarray(d.micro_times)[photons].astype(np.int64)
    # written order 2.0, 12.5, 24.9, 1.0 ns -> sorted by macro time
    expected = [int(ns / MICRO_BIN) for ns in (2.0, 12.5, 24.9, 1.0)]
    assert list(micro) == expected

    assert _tag(d, "MeasDesc_Resolution") == pytest.approx(MICRO_BIN * 1e-9)


def test_a_micro_time_past_the_period_saturates(tmp_path):
    """It means the header's laser period is wrong.

    Wrapping would hide that in the middle of the decay; saturating leaves it at
    the end where it is visible.
    """
    p = str(tmp_path / "over.bin")
    write_stt1(p, [(0, 999.0, 0.0)], laser_period_ns=PERIOD)
    d = tttrlib.TTTR(p, "FLIMLABS-STT1")
    assert int(d.micro_times[0]) == 255


def test_markers_keep_the_codes_the_format_reserves(simple):
    """70/76/80 are ASCII F, L and P, and stay that way.

    Renumbering them to 1/2/3 would give a marker the same routing channel as a
    detector, and this hardware has channels 1, 2 and 3.
    """
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    markers = np.asarray(d.event_types) == 1
    ch = np.asarray(d.routing_channels)[markers].astype(np.int64)
    assert sorted(ch) == sorted([MARKER_FRAME, MARKER_LINE, MARKER_LINE,
                                 MARKER_PIXEL, MARKER_PIXEL])
    # and photons keep the file's zero-based channel index
    photons = np.asarray(d.event_types) == 0
    assert set(np.asarray(d.routing_channels)[photons].tolist()) == {0, 1}


def test_the_marker_convention_is_published_in_the_header(simple):
    """In the container-independent ImgHdr_* form, not a FLIM LABS-shaped one.

    CLSMImage applies these on its own only when the file also states its scan
    geometry, and a FLIM LABS file never does -- there is no pixel count in it.
    So a caller still supplies the geometry; what they do not have to supply, or
    look up, is which routing channel is a line.
    """
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    assert _tag(d, "ImgHdr_Frame") == MARKER_FRAME
    assert _tag(d, "ImgHdr_LineStart") == MARKER_LINE
    assert _tag(d, "ImgHdr_LineStop") == MARKER_LINE


def test_reconstructs_an_image_without_special_casing(simple):
    """Markers reach CLSMImage as ordinary marker events, like any other format.

    The pixel clock is what knows where a line ends: paired start-to-start, the
    two line markers make one line and the second is lost.
    """
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    img = tttrlib.CLSMImage(
        tttr_data=d,
        marker_frame_start=[MARKER_FRAME],
        marker_line_start=MARKER_LINE,
        marker_line_stop=MARKER_LINE,
        marker_event_type=1,
        n_pixel_per_line=1,
        use_pixel_markers=True,
        marker_pixel=MARKER_PIXEL,
    )
    assert (img.n_frames, img.n_lines, img.n_pixel) == (1, 2, 1)
    assert np.asarray(img.intensity).sum() > 0


def test_a_channel_that_collides_with_a_marker_is_refused(tmp_path):
    """Nothing in a record distinguishes detector 70 from a frame marker.

    No such hardware exists, but tagging a detector's photons as scanner markers
    is not something anyone downstream would notice.
    """
    p = str(tmp_path / "collide.bin")
    write_stt1(p, [(70, 1.0, 0.0)], channels=(0, 70), laser_period_ns=PERIOD)
    d = tttrlib.TTTR(p, "FLIMLABS-STT1")
    assert d.n_valid_events == 0        # the read failed, loudly, on stderr


def test_a_missing_laser_period_is_refused(tmp_path):
    """Without it there is no tick, and inventing one rescales every time."""
    p = str(tmp_path / "nolaser.bin")
    write_stt1(p, [(0, 1.0, 10.0)], laser_period_ns=None)
    d = tttrlib.TTTR(p, "FLIMLABS-STT1")
    assert d.n_valid_events == 0


def test_itt1_has_no_micro_time_and_keeps_its_resolution(tmp_path):
    """Quantising an FCS timestamp to the laser period throws away the point.

    So the tick here is one picosecond, not one pulse.
    """
    p = str(tmp_path / "trace.bin")
    # 0.5 ns apart -- a twentieth of the laser period, and invisible if the tick
    # were a pulse
    write_itt1(p, [(1, 20.0), (0, 10.0), (0, 10.5)], laser_period_ns=PERIOD)
    d = tttrlib.TTTR(p, "FLIMLABS-ITT1")

    assert list(np.asarray(d.macro_times).astype(np.int64)) == [10000, 10500, 20000]
    assert list(np.asarray(d.routing_channels).astype(np.int64)) == [0, 0, 1]
    assert np.all(np.asarray(d.micro_times) == 0)
    assert d.header.number_of_micro_time_channels == 1

    assert _tag(d, "FlimLabs_MacroTimeUnit") == "picosecond"
    assert _tag(d, "MeasDesc_GlobalResolution") == pytest.approx(1e-12)


def test_auto_detection_reads_it_without_being_told(simple):
    d = tttrlib.TTTR(simple)
    assert d.n_valid_events == 9
    assert d.get_tttr_container_type() == "FLIMLABS-STT1"


def test_a_container_that_needs_nothing_refuses_parameters(simple):
    """This format describes itself, so parameters are a misunderstanding.

    Reading the file anyway would hide it behind a plausible result.
    """
    assert tttrlib.registry("file_container")["FLIMLABS-STT1"]["params_schema"] == {}
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1", {"laser_MHz": 80})
    assert d.n_valid_events == 0


def test_writing_is_refused(simple, tmp_path):
    """Read-only until the tick choice has been checked against a real file."""
    d = tttrlib.TTTR(simple, "FLIMLABS-STT1")
    assert not d.write(str(tmp_path / "out.bin"), "FLIMLABS-STT1")
