"""`tttr.write("run.pto")` — the container as a sink, not a wrapper.

Until this landed a `.pto` could only *carry* a photon stream as an
embedded vendor file reached through `run.pto|m001.ptu`; there was no writer at
all, so the native photons table the reader already understood could only be
produced by something else. These pin the writer against the reader that was
built for it.

The interesting assertions are not "the events came back":

* **The header comes back too.** A native photons object carries its clocks and
  bin count as tags. Without them the stream is dimensionless integers, and a
  writer that omitted them would be manufacturing the exact defect
  `apply_photon_header` was added to fix — so the round trip is asserted on the
  *physical* values, not just on the arrays.
* **The tag spellings are the reader's.** Writer and reader are one contract;
  a test pinning only the round trip would not notice them drifting together
  into a spelling nothing else accepts. So one test reads the raw tags.
* **A second write appends.** A container holds measurements. Silently
  discarding the ones already in it is not a write, it is a deletion nobody
  asked for.
"""
import glob
import os

import numpy as np
import pytest

import tttrlib


def _source(pattern):
    hits = sorted(glob.glob(f"tttr-data/{pattern}", recursive=True))
    if not hits:
        pytest.skip(f"no test data matching {pattern}")
    return hits[0]


@pytest.fixture(scope="module")
def ptu():
    return tttrlib.TTTR(_source("**/*.ptu"))


ARRAYS = ("macro_times", "micro_times", "routing_channels", "event_types")


class TestTheRoundTripIsExact:

    def test_every_event_column_survives(self, ptu, tmp_path):
        out = str(tmp_path / "run.pto")
        assert ptu.write(out)
        back = tttrlib.TTTR(out)

        assert len(back) == len(ptu)
        for name in ARRAYS:
            a, b = np.asarray(getattr(ptu, name)), np.asarray(getattr(back, name))
            # Exact and same dtype: these are integers, and a round trip that
            # widens or rounds them has changed the measurement.
            assert a.dtype == b.dtype, name
            np.testing.assert_array_equal(a, b, err_msg=name)

    def test_the_header_survives(self, ptu, tmp_path):
        """The half that makes the events mean something."""
        out = str(tmp_path / "run.pto")
        assert ptu.write(out)
        back = tttrlib.TTTR(out)
        h, hb = ptu.header, back.header

        assert hb.macro_time_resolution == pytest.approx(h.macro_time_resolution, rel=1e-12)
        assert hb.micro_time_resolution == pytest.approx(h.micro_time_resolution, rel=1e-12)
        assert hb.number_of_micro_time_channels == h.number_of_micro_time_channels

    def test_a_photon_time_is_still_in_seconds(self, ptu, tmp_path):
        """Stated as the quantity a caller actually asks for, so a resolution
        that silently defaulted to 1.0 fails here even if the arrays match."""
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        back = tttrlib.TTTR(out)

        i = len(ptu) // 2
        expected = float(np.asarray(ptu.macro_times)[i]) * ptu.header.macro_time_resolution
        got = float(np.asarray(back.macro_times)[i]) * back.header.macro_time_resolution
        assert got == pytest.approx(expected, rel=1e-12)

    @pytest.mark.parametrize("pattern", ["**/*.spc", "**/*.ht3"])
    def test_other_source_formats_round_trip_too(self, pattern, tmp_path):
        """The sink is not PTU-shaped. A Becker & Hickl stream and a HydraHarp
        one differ in record layout, macro clock and channel numbering, and the
        native table is none of those things — it stores decoded events."""
        src = tttrlib.TTTR(_source(pattern))
        out = str(tmp_path / "x.pto")
        assert src.write(out)
        back = tttrlib.TTTR(out)

        assert len(back) == len(src)
        for name in ARRAYS:
            np.testing.assert_array_equal(
                np.asarray(getattr(src, name)), np.asarray(getattr(back, name)),
                err_msg=f"{pattern}:{name}")
        assert back.header.macro_time_resolution == pytest.approx(
            src.header.macro_time_resolution, rel=1e-12)


class TestWhatIsActuallyInTheContainer:

    def test_it_is_a_native_photons_object_not_an_embedded_file(self, ptu, tmp_path):
        """The point of the PRD: no vendor format inside."""
        out = str(tmp_path / "run.pto")
        ptu.write(out)

        f = tttrlib.PtoFile()
        assert f.open(out)
        objs = list(f.objects())
        assert len(objs) == 1
        assert objs[0].kind == "photons"
        assert objs[0].encoding == "dstore"
        assert objs[0].rows == len(ptu)

    def test_the_four_normative_columns_are_there_by_name(self, ptu, tmp_path):
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        f = tttrlib.PtoFile()
        f.open(out)
        uid = f.objects()[0].uid

        store = tttrlib.DataStore()
        tttrlib.pto_read_store(f, uid, store)
        assert set(store.column_names()) == {
            "macro_time", "micro_time", "routing_channel", "event_type"}

    def test_the_header_tags_are_spelled_the_way_the_reader_reads_them(self, ptu, tmp_path):
        """Writer and reader are one contract. Pinned separately from the round
        trip, which would keep passing if both drifted to a spelling no other
        implementation accepts — and these two names are MMFDB dictionary
        terms, so drifting is not tttrlib's call to make."""
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        f = tttrlib.PtoFile()
        f.open(out)
        uid = f.objects()[0].uid

        names = {t.name for t in f.tags_for(uid)}
        assert "_mmfdb_setup.macro_time_resolution" in names
        assert "_mmfdb_setup.micro_time_resolution" in names
        assert "_pto_photons.number_of_micro_time_channels" in names
        # The near-miss that means correlator bins for an FCS setup.
        assert "_mmfdb_setup.n_bins" not in names


class TestASecondWriteAppends:
    """One measurement per object — `tttr pto add` semantics."""

    def test_writing_twice_keeps_both(self, ptu, tmp_path):
        out = str(tmp_path / "multi.pto")
        assert ptu.write(out + "|green")
        assert ptu.write(out + "|red")

        f = tttrlib.PtoFile()
        f.open(out)
        assert [(o.kind, o.name) for o in f.objects()] == [
            ("photons", "green"), ("photons", "red")]

    def test_the_selector_names_the_object_and_reads_it_back(self, ptu, tmp_path):
        out = str(tmp_path / "multi.pto")
        ptu.write(out + "|green")
        ptu.write(out + "|red")

        green = tttrlib.TTTR(out + "|green")
        assert len(green) == len(ptu)
        np.testing.assert_array_equal(
            np.asarray(green.macro_times), np.asarray(ptu.macro_times))
        assert green.header.macro_time_resolution == pytest.approx(
            ptu.header.macro_time_resolution, rel=1e-12)

    def test_only_one_file_is_created(self, ptu, tmp_path):
        """The selector is not part of the filename. Before this was handled,
        `write("run.pto|green")` found no extension on the whole string, fell
        back to the SOURCE container and wrote a PTU into a file literally
        named `run.pto|green` — the wrong format under the right name, with no
        error."""
        out = str(tmp_path / "multi.pto")
        ptu.write(out + "|green")
        assert os.listdir(tmp_path) == ["multi.pto"]


class TestASelectorIsRefusedWhereItMeansNothing:

    def test_a_format_without_objects_rejects_one(self, ptu, tmp_path):
        """A PTU holds one measurement and has nothing to name. Folding the
        selector into the filename would produce a file with a '|' in its name
        whose contents are not what the caller asked for."""
        assert not ptu.write(str(tmp_path / "x.ptu") + "|sel")
        assert not (tmp_path / "x.ptu|sel").exists()


class TestTheFormatTableAgrees:

    def test_pto_advertises_that_it_can_be_written(self):
        """`can_write` and the writer are set together, so a caller checking
        the registry before writing is not told a different story from the one
        `write` will tell."""
        reg = tttrlib.registry("file_container")
        assert "PTO" in reg, "PTO is not in the format registry"
        assert reg["PTO"]["can_read"] is True
        assert reg["PTO"]["can_write"] is True


class TestRangesOverANativeTable:
    """A cue index answers "where does event N start" for a
    record stream, which must be decoded from a known point to be counted at
    all. Columnar storage answers it arithmetically — the row number *is* the
    seek position — so the cue machinery is not needed on this path.

    It was not being used, though: the native path read the whole object and
    sliced it in memory. Correct, and 9x slower than the embedded-PTU-with-cues
    path it is supposed to beat.
    """

    def test_a_range_equals_the_same_slice_of_the_whole(self, ptu, tmp_path):
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        whole = tttrlib.TTTR(out)
        m, u = np.asarray(whole.macro_times), np.asarray(whole.micro_times)

        part = tttrlib.TTTR(out, "PTO", '{"first_event": 400000, "n_events": 5000}')
        assert len(part) == 5000
        np.testing.assert_array_equal(np.asarray(part.macro_times), m[400000:405000])
        np.testing.assert_array_equal(np.asarray(part.micro_times), u[400000:405000])

    def test_a_range_still_carries_the_header(self, ptu, tmp_path):
        """A slice is a measurement too: without the clocks it is integers."""
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        part = tttrlib.TTTR(out, "PTO", '{"first_event": 1000, "n_events": 100}')
        assert part.header.macro_time_resolution == pytest.approx(
            ptu.header.macro_time_resolution, rel=1e-12)

    def test_a_range_reads_less_than_the_whole_object(self, ptu, tmp_path):
        """The property the row slice exists for, measured rather than assumed:
        asking for 5,000 of 870,161 events must not cost a full read. Timed
        against the full read on the same file, so the threshold does not
        depend on the machine."""
        import time
        out = str(tmp_path / "run.pto")
        ptu.write(out)

        def best(fn, n=7):
            t = 1e9
            for _ in range(n):
                t0 = time.perf_counter()
                fn()
                t = min(t, time.perf_counter() - t0)
            return t

        whole = best(lambda: tttrlib.TTTR(out))
        part = best(lambda: tttrlib.TTTR(
            out, "PTO", '{"first_event": 400000, "n_events": 5000}'))
        # 5,000 of 870,161 rows is 0.6% of the data, and the slice measures
        # ~10x cheaper than the full read here. The ratio is not portable: the
        # slice is dominated by the fixed cost of opening the container and
        # walking its directory, which a Windows runner pays far more of -- it
        # scored 3.85x (6.8 ms against 26.0 ms) and failed a 4x bar. 2x is the
        # threshold that still fails outright when the whole object is read and
        # sliced in memory, which scores about 1x.
        assert part < whole / 2, f"range {part*1e3:.1f} ms vs whole {whole*1e3:.1f} ms"

    def test_build_cues_on_a_native_table_is_a_no_op_not_an_error(self, ptu, tmp_path):
        """Returning zero cues with no error is the honest report. An error
        would tell a caller that indexes before reading that the object is
        unreadable, when it is the one kind that never needed an index."""
        out = str(tmp_path / "run.pto")
        ptu.write(out)
        f = tttrlib.PtoFile()
        f.open(out)
        uid = f.objects()[0].uid

        assert f.build_cues(uid, 1000) == 0
        assert f.error() == ""
