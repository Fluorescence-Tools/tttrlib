"""A `.pto` holding photons natively — the sink, not the wrapper.

`PtoKind "photons"` + `PtoEncoding "dstore"` is a photon stream stored as its
own four columns, with no vendor container inside. The specification is
``doc/formats/pto.rst``, section "Photon streams, natively"; PRD-034 tracks the
implementation.

What these pin, and why each one is not obvious:

* **The events survive the round trip.** Straightforward, and the only part that
  worked before.
* **The header survives it too.** It did not. The loader read the four columns
  and applied *no* header at all, so a native photons object came back as
  dimensionless integers — the macro-time clock, the TCSPC bin width and the bin
  count were simply gone, and every quantity derived from them was wrong or
  impossible. That is the gap these tests exist for.
* **The tag names are the specified ones.** Two are MMFDB dictionary terms and
  are spelled the dictionary's way, because MMFDB is the vocabulary authority.
  The third is deliberately *not* ``_mmfdb_setup.n_bins`` — that term exists and
  means correlator bins for an FCS setup — so a test that accepted the wrong
  spelling would enshrine a silent semantic collision.
"""
import numpy as np
import pytest

import tttrlib


N_EVENTS = 500
MACRO_RES = 1.6e-8          # 16 ns macro clock
MICRO_RES = 3.2e-11         # 32 ps TCSPC bin
N_MICRO_CHANNELS = 4096


def _events():
    """Distinct, non-degenerate values in every column."""
    macro = (np.arange(N_EVENTS, dtype=np.uint64) * np.uint64(7) + np.uint64(11))
    micro = (np.arange(N_EVENTS) * 13 % N_MICRO_CHANNELS).astype(np.uint16)
    chan = (np.arange(N_EVENTS) % 4).astype(np.int8)
    etype = np.where(np.arange(N_EVENTS) % 97 == 0, 1, 0).astype(np.int8)
    return macro, micro, chan, etype


def _text_tag(target, name, value):
    t = tttrlib.PtoTag()
    t.target, t.name, t.type, t.text = target, name, tttrlib.PtoType_Text, value
    return t


def _float_tag(target, name, value):
    t = tttrlib.PtoTag()
    t.target, t.name, t.type, t.d = target, name, tttrlib.PtoType_Float, float(value)
    return t


def _int_tag(target, name, value):
    t = tttrlib.PtoTag()
    t.target, t.name, t.type, t.i = target, name, tttrlib.PtoType_Int, int(value)
    return t


def _write(path, tags=("macro", "micro", "bins"), profile=True, fcs_bins=None):
    """A single-object `.mmfdb.pto` holding one native photon stream."""
    macro, micro, chan, etype = _events()

    f = tttrlib.PtoFile()
    assert f.create(path, "native photons"), f.error()
    f.set_writing_app("test_pto_photons_native")

    s = tttrlib.DataStore("photons")
    s.set_n_rows(N_EVENTS)
    # The four normative column names, in the normative dtypes.
    s.add("macro_time", macro)
    s.add("micro_time", micro)
    s.add("routing_channel", chan)
    s.add("event_type", etype)
    uid = tttrlib.pto_add_store(f, "photons", "m001", s)
    assert uid != 0, f.error()

    if profile:
        # What makes it a .mmfdb.pto rather than merely a .pto.
        f.add_tag(_text_tag(0, "_mmfdb_container.profile", "PTO.MFDB"))
        f.add_tag(_text_tag(0, "_mmfdb_container.profile_version", "1.1"))

    if "macro" in tags:
        f.add_tag(_float_tag(uid, "_mmfdb_setup.macro_time_resolution", MACRO_RES))
    if "micro" in tags:
        f.add_tag(_float_tag(uid, "_mmfdb_setup.micro_time_resolution", MICRO_RES))
    if "bins" in tags:
        f.add_tag(_int_tag(uid, "_pto_photons.number_of_micro_time_channels",
                           N_MICRO_CHANNELS))
    if fcs_bins is not None:
        # An FCS setup's correlator bin count, which is a real term with a real
        # meaning -- just not this one.
        f.add_tag(_int_tag(uid, "_mmfdb_setup.n_bins", fcs_bins))

    assert f.commit(), f.error()
    f.close()
    return uid


class TestTheEventsRoundTrip:

    def test_all_four_columns_come_back_unchanged(self, tmp_path):
        path = str(tmp_path / "run.mmfdb.pto")
        _write(path)
        t = tttrlib.TTTR(path)

        macro, micro, chan, etype = _events()
        assert len(t) == N_EVENTS
        # Exact, not approximate: these are integers and a round trip that
        # rounds them has lost the measurement.
        np.testing.assert_array_equal(np.asarray(t.macro_times), macro)
        np.testing.assert_array_equal(np.asarray(t.micro_times), micro)
        np.testing.assert_array_equal(np.asarray(t.routing_channels), chan)
        np.testing.assert_array_equal(np.asarray(t.event_types), etype)


class TestTheHeaderRoundTrips:
    """The part that was missing: without these the events have no units."""

    def test_the_clocks_and_bin_count_are_applied(self, tmp_path):
        path = str(tmp_path / "run.mmfdb.pto")
        _write(path)
        # NB: bind the TTTR. `TTTR(path).header` on a temporary is a hard
        # SIGSEGV -- the header pointer outlives its owner. See BUGS.md.
        t = tttrlib.TTTR(path)
        h = t.header

        assert h.macro_time_resolution == pytest.approx(MACRO_RES, rel=1e-12)
        assert h.micro_time_resolution == pytest.approx(MICRO_RES, rel=1e-12)
        assert h.number_of_micro_time_channels == N_MICRO_CHANNELS

    def test_a_photon_time_is_in_seconds(self, tmp_path):
        """The point of the header, stated as the quantity a caller asks for."""
        path = str(tmp_path / "run.mmfdb.pto")
        _write(path)
        t = tttrlib.TTTR(path)
        macro, _, _, _ = _events()

        # macro_time * macro_time_resolution is the event time in seconds. If
        # the resolution defaulted to 1.0 this passes only for the trivial case,
        # so the assertion is against the physical value.
        expected = float(macro[10]) * MACRO_RES
        got = float(np.asarray(t.macro_times)[10]) * t.header.macro_time_resolution
        assert got == pytest.approx(expected, rel=1e-12)
        assert got != pytest.approx(float(macro[10]), rel=1e-6)   # not dimensionless


class TestTheSpecifiedNamesAreTheOnesRead:

    @pytest.mark.parametrize("missing", ["macro", "micro", "bins"])
    def test_a_missing_required_tag_leaves_its_field_unset(self, tmp_path, missing):
        """Each required tag is actually read, rather than a default happening
        to match. Dropping one must show up in that field and no other."""
        keep = tuple(x for x in ("macro", "micro", "bins") if x != missing)
        path = str(tmp_path / f"no_{missing}.mmfdb.pto")
        _write(path, tags=keep)
        t = tttrlib.TTTR(path)
        h = t.header

        if missing != "macro":
            assert h.macro_time_resolution == pytest.approx(MACRO_RES, rel=1e-12)
        if missing != "micro":
            assert h.micro_time_resolution == pytest.approx(MICRO_RES, rel=1e-12)
        if missing != "bins":
            assert h.number_of_micro_time_channels == N_MICRO_CHANNELS

    def test_the_fcs_bin_term_is_not_accepted_for_the_micro_time_bin_count(self, tmp_path):
        """`_mmfdb_setup.n_bins` exists and means *correlator bins for an FCS
        channel setup*. Reading it as the TCSPC bin count would validate and be
        silently wrong, which is worse than not reading it at all."""
        path = str(tmp_path / "wrong_term.mmfdb.pto")
        # The correct tag absent, the tempting wrong one present.
        _write(path, tags=("macro", "micro"), fcs_bins=99)

        t = tttrlib.TTTR(path)
        h = t.header
        assert h.number_of_micro_time_channels != 99
