"""PTO -- the PhoTon cOntainer.

One measurement produces a photon stream, then a burst search over it, then a
spectrum from the same sample, and a note about why half of it was discarded.
Those used to live in as many files, related by filename convention, and the
relationship died the moment somebody renamed one.

PTO puts them in one file with stable identities. It is an EBML document
(RFC 8794) with DocType "pto" and it borrows Matroska's elements wherever
Matroska already means what it needs -- an object IS a Matroska AttachedFile --
so the parts tested here are the parts that are actually about photons.
"""
import os
import struct

import numpy as np
import pytest
import tttrlib


def store(n, first=0.0, name="x"):
    s = tttrlib.DataStore()
    s.set_n_rows(n)
    s.add(name, np.arange(n, dtype=np.float64) + first)
    return s


@pytest.fixture
def made(tmp_path):
    """A file with one of everything in it."""
    path = str(tmp_path / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path, "titration 03"), f.error()
    f.set_writing_app("the test suite")

    photons = tttrlib.DataStore("photons")
    photons.set_n_rows(2000)
    photons.add("macro_time", np.arange(2000, dtype=np.uint64))
    photons.add("channel", (np.arange(2000) % 4).astype(np.uint8))
    uid_photons = tttrlib.pto_add_store(f, "photons", "channel 0-3", photons)

    bursts = tttrlib.DataStore("bursts")
    bursts.set_n_rows(40)
    bursts.add("Tau", np.linspace(1.0, 4.0, 40))
    uid_bursts = tttrlib.pto_add_store(f, "table", "bursts", bursts, 8192)

    original = b"PQTTTR" + bytes(range(256)) * 4
    uid_raw = f.add("attachment", "ptu", "titration_03.ptu", original)

    edge = tttrlib.PtoTag()
    edge.name = "chisurf.derived_from"
    edge.type = tttrlib.PtoType_UID
    edge.target = uid_bursts
    edge.u = uid_photons
    f.add_tag(edge)

    assert f.commit(), f.error()
    f.close()
    return path, uid_photons, uid_bursts, uid_raw, original


# -- it is an EBML file --------------------------------------------------------

def test_it_begins_with_an_ebml_header_saying_pto(made):
    """Not a private magic number: the first element is the standard EBML
    header, and DocType is what says which kind of EBML document this is --
    exactly how Matroska identifies itself."""
    path = made[0]
    head = open(path, "rb").read(64)
    assert head[:4] == b"\x1a\x45\xdf\xa3", "no EBML header"
    assert b"pto" in head, "no DocType"
    assert tttrlib.is_pto_file(path) is True


def test_a_file_that_is_not_pto_is_refused_quietly(tmp_path):
    text = tmp_path / "notes.txt"
    text.write_bytes(b"not an EBML document at all")
    assert tttrlib.is_pto_file(str(text)) is False
    assert tttrlib.is_pto_file(str(tmp_path / "nope.pto")) is False

    f = tttrlib.PtoFile()
    assert f.open(str(text)) is False
    assert "not an EBML file" in f.error() or "not a PTO file" in f.error()


def test_a_generic_ebml_parser_can_walk_it(made):
    """The point of reusing Matroska's ids. Nothing here knows about PTO: it
    reads variable-size integers and follows sizes, and still finds the
    Segment, the two indexes and every object."""
    path = made[0]
    data = open(path, "rb").read()

    def vint(buf, i, mask):
        first = buf[i]
        n = 1
        while n <= 8 and not (first & (0x80 >> (n - 1))):
            n += 1
        v = first & (0xFF >> n) if mask else first
        for k in range(1, n):
            v = (v << 8) | buf[i + k]
        return v, n

    def walk(buf, start, end):
        out = []
        i = start
        while i < end:
            eid, idn = vint(buf, i, False)
            size, szn = vint(buf, i + idn, True)
            out.append((eid, i + idn + szn, size))
            i += idn + szn + size
        return out

    top = walk(data, 0, len(data))
    ids = [e[0] for e in top]
    assert ids[0] == 0x1A45DFA3, "EBML header"
    assert ids[1] == 0x18538067, "Segment"

    seg = [e for e in top if e[0] == 0x18538067][0]
    children = walk(data, seg[1], seg[1] + seg[2])
    kinds = [c[0] for c in children]
    assert kinds.count(0x114D9B74) == 2, "two SeekHeads, for the atomic commit"
    assert 0x1549A966 in kinds, "Info"
    assert kinds.count(0x1941A469) == 3, "one Attachments per object"
    assert 0x1254C367 in kinds, "Tags"


def test_the_index_checksum_is_a_real_crc32(made):
    """Checked with zlib rather than with our own function, and over the range
    RFC 8794 specifies -- every sibling of the CRC-32 element including the Void
    that pads the index out. Covering only the useful part would have been
    self-consistent and unverifiable by anything else.
    """
    import zlib

    data = open(made[0], "rb").read()
    marks = [i for i in range(len(data) - 4)
             if data[i:i + 4] == b"\x11\x4d\x9b\x74"]          # SeekHead
    assert marks

    checked = 0
    for at in marks:
        payload = at + 4 + 8                                    # id + wide size
        size = int.from_bytes(data[at + 4:at + 12], "big") & ((1 << 56) - 1)
        assert data[payload] == 0xBF, "CRC-32 must be the first child"
        assert data[payload + 1] == 0x84, "and exactly four octets"
        stored = int.from_bytes(data[payload + 2:payload + 6], "little")
        covered = data[payload + 6:payload + size]
        assert zlib.crc32(covered) == stored
        checked += 1
    assert checked == 2


# -- objects -------------------------------------------------------------------

def test_everything_put_in_comes_back(made):
    path, uid_photons, uid_bursts, uid_raw, original = made
    f = tttrlib.PtoFile()
    assert f.open(path), f.error()

    assert f.title() == "titration 03"
    assert f.writing_app() == "the test suite"
    assert f.n_objects() == 3
    assert len(f.uuid()) == 16

    assert f.read(uid_raw) == original
    assert f.object(uid_raw).kind == "attachment"
    assert f.object(uid_raw).encoding == "ptu"
    assert f.object(uid_raw).name == "titration_03.ptu"

    photons = tttrlib.DataStore()
    tttrlib.pto_read_store(f, uid_photons, photons)
    assert photons.names == ["macro_time", "channel"]
    np.testing.assert_array_equal(photons["macro_time"].numpy(),
                                  np.arange(2000, dtype=np.uint64))
    assert photons["channel"].numpy().dtype == np.uint8

    bursts = tttrlib.DataStore()
    tttrlib.pto_read_store(f, uid_bursts, bursts)
    assert bursts.n_rows() == 40


def test_the_objects_come_back_in_order(made):
    f = tttrlib.PtoFile()
    f.open(made[0])
    assert [o.name for o in f.objects()] == \
        ["channel 0-3", "bursts", "titration_03.ptu"]


def test_an_object_knows_its_rows_without_being_decoded(made):
    """So a listing can say how big a table is without reading it."""
    f = tttrlib.PtoFile()
    f.open(made[0])
    assert f.object(made[1]).rows == 2000
    assert f.object(made[2]).rows == 40


def test_a_uid_is_unique_and_a_name_is_not(tmp_path):
    """Names are labels. Two objects may share one; UIDs may not."""
    f = tttrlib.PtoFile()
    f.create(str(tmp_path / "t.pto"))
    a = f.add("table", "raw", "same", b"one")
    b = f.add("table", "raw", "same", b"two")
    assert a != b
    assert f.find("same") in (a, b)
    assert f.read(a) == b"one" and f.read(b) == b"two"


def test_asking_for_an_object_that_is_not_there(made):
    f = tttrlib.PtoFile()
    f.open(made[0])
    assert f.has(1234) is False
    with pytest.raises(Exception):
        f.object(1234)
    with pytest.raises(Exception):
        f.read(1234)


# -- in-place update, which is the reason for the format ------------------------

def test_a_smaller_payload_is_rewritten_where_it_lies(made):
    """The whole point. Recomputing the burst table must not move the photon
    stream, whatever size it is."""
    path, uid_photons, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()

    before = f.object(uid_photons).offset
    where = f.object(uid_bursts).offset

    smaller = store(10, name="Tau")
    assert tttrlib.pto_update_store(f, uid_bursts, smaller), f.error()
    assert f.commit(), f.error()

    assert f.object(uid_bursts).offset == where, "the object moved"
    assert f.object(uid_photons).offset == before, "the photon stream moved"
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    back = tttrlib.DataStore()
    tttrlib.pto_read_store(g, uid_bursts, back)
    assert back.n_rows() == 10


def test_growing_within_the_reserved_room_still_does_not_move(made):
    """`reserve` is room left after the payload, and growing into it is what it
    is for."""
    path, uid_photons, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    where = f.object(uid_bursts).offset
    capacity = f.object(uid_bursts).capacity
    assert capacity > f.object(uid_bursts).size, "no room was reserved"

    bigger = store(120, name="Tau")
    assert tttrlib.pto_update_store(f, uid_bursts, bigger), f.error()
    assert f.object(uid_bursts).offset == where
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    back = tttrlib.DataStore()
    tttrlib.pto_read_store(g, uid_bursts, back)
    assert back.n_rows() == 120


def test_outgrowing_the_room_keeps_the_uid_and_moves_the_object(made):
    """The uid survives, the offset does not -- which is why nothing but the
    index may hold an offset."""
    path, uid_photons, uid_bursts, uid_raw, original = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    where = f.object(uid_bursts).offset

    huge = store(20000, name="Tau")
    assert tttrlib.pto_update_store(f, uid_bursts, huge), f.error()
    assert f.commit(), f.error()

    assert f.has(uid_bursts), "the uid did not survive the move"
    assert f.object(uid_bursts).offset != where
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    assert g.n_objects() == 3
    back = tttrlib.DataStore()
    tttrlib.pto_read_store(g, uid_bursts, back)
    assert back.n_rows() == 20000
    assert g.read(uid_raw) == original, "a neighbour was damaged"


def test_the_space_a_moved_object_left_is_reused(made):
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    tttrlib.pto_update_store(f, uid_bursts, store(20000, name="Tau"))
    f.commit()
    freed = sum(e.bytes for e in f.free_extents())
    assert freed > 0, "the old space was not released"

    grew_to = os.path.getsize(path)
    f.add("table", "raw", "filler", b"x" * 64)
    f.commit()
    f.close()
    assert os.path.getsize(path) == grew_to, "a hole was there and was not used"


def test_removing_an_object(made):
    path, uid_photons, uid_bursts, uid_raw, original = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    assert f.remove(uid_bursts) is True
    assert f.remove(uid_bursts) is False
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    assert g.n_objects() == 2
    assert g.has(uid_bursts) is False
    assert g.read(uid_raw) == original


def test_a_read_only_file_refuses_to_be_written(made):
    f = tttrlib.PtoFile()
    f.open(made[0], writable=False)
    assert f.add("table", "raw", "no", b"x") == 0
    assert "read-only" in f.error()
    assert f.commit() is False


# -- tags ------------------------------------------------------------------------

def test_a_tag_targets_an_object_and_may_name_another(made):
    """The provenance mechanism, and the whole of what PTO provides: the target
    is the subject, the name is the predicate, the UID value is the object.
    What the edge MEANS is the application's business."""
    path, uid_photons, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path)
    edges = f.tags_for(uid_bursts)
    assert len(edges) == 1
    assert edges[0].name == "chisurf.derived_from"
    assert edges[0].type == tttrlib.PtoType_UID
    assert edges[0].u == uid_photons


def test_every_tag_type_survives(tmp_path):
    """These cover PicoQuant's twelve header types, which is what lets a PTU
    header be carried across without loss."""
    path = str(tmp_path / "tags.pto")
    f = tttrlib.PtoFile()
    f.create(path)

    def tag(name, **kw):
        t = tttrlib.PtoTag()
        t.name = name
        for k, v in kw.items():
            setattr(t, k, v)
        f.add_tag(t)

    tag("flag", type=tttrlib.PtoType_Empty)
    tag("count", type=tttrlib.PtoType_UInt, u=2**63 + 7)
    tag("offset", type=tttrlib.PtoType_Int, i=-123456789)
    tag("resolution", type=tttrlib.PtoType_Float, d=8e-11)
    tag("when", type=tttrlib.PtoType_Date, i=-1234567890123)
    tag("sample", type=tttrlib.PtoType_Text, text="Cy3B-Cy5 dsDNA")
    tag("blob", type=tttrlib.PtoType_Bytes, bytes=b"\x00\x01\xfe\xff")
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    got = {t.name: t for t in g.tags()}
    assert got["flag"].type == tttrlib.PtoType_Empty
    assert got["count"].u == 2**63 + 7
    assert got["offset"].i == -123456789
    assert got["resolution"].d == pytest.approx(8e-11)
    assert got["when"].i == -1234567890123
    assert got["sample"].text == "Cy3B-Cy5 dsDNA"
    assert bytes(got["blob"].bytes) == b"\x00\x01\xfe\xff"


def test_a_ptu_header_survives_verbatim(tmp_path):
    """A PicoQuant header repeats a tag name once per array element rather than
    storing a list, and records a type code per tag. Both have to come back or
    the header cannot be written out again."""
    path = str(tmp_path / "hdr.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    for i, wavelength in enumerate((485.0, 640.0)):
        t = tttrlib.PtoTag()
        t.name = "ExcitationWavelength"
        t.type = tttrlib.PtoType_Float
        t.d = wavelength
        t.index = i
        t.source_type = 0x20000008          # tyFloat8
        f.add_tag(t)
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    got = sorted(g.tags(), key=lambda t: t.index)
    assert [t.index for t in got] == [0, 1]
    assert [t.d for t in got] == [485.0, 640.0]
    assert all(t.source_type == 0x20000008 for t in got)


def test_a_tag_with_no_target_is_about_the_file(tmp_path):
    path = str(tmp_path / "t.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    t = tttrlib.PtoTag()
    t.name = "operator"
    t.type = tttrlib.PtoType_Text
    t.text = "tpeulen"
    f.add_tag(t)
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    assert [x.name for x in g.tags_for(0)] == ["operator"]


def test_a_tag_may_point_at_an_object_that_is_gone(made):
    """Deleting an object leaves its tags dangling, deliberately: an
    application may want to remember that something was there."""
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    f.remove(uid_bursts)
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    assert len(g.tags_for(uid_bursts)) == 1, "the tag was silently dropped"
    assert g.has(uid_bursts) is False


# -- annotations -------------------------------------------------------------------

def test_an_annotation_can_name_a_range_of_rows(tmp_path):
    path = str(tmp_path / "a.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    uid = f.add("table", "raw", "bursts", b"x" * 16)
    a = tttrlib.PtoAnnotation()
    a.target = uid
    a.first_row = 1400
    a.last_row = 1600
    a.text = "laser drifted; excluded from the fit"
    a.author = "tpeulen"
    f.add_annotation(a)
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    got = g.annotations()
    assert len(got) == 1
    assert got[0].target == uid
    assert (got[0].first_row, got[0].last_row) == (1400, 1600)
    assert got[0].text.startswith("laser drifted")
    assert got[0].author == "tpeulen"


# -- committing --------------------------------------------------------------------

def test_nothing_is_visible_until_it_is_committed(tmp_path):
    path = str(tmp_path / "t.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    f.add("table", "raw", "kept", b"one")
    f.commit()
    f.add("table", "raw", "abandoned", b"two")   # written, never committed
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    assert [o.name for o in g.objects()] == ["kept"], \
        "an uncommitted object was visible"


def test_the_space_an_abandoned_write_used_is_reclaimed(tmp_path):
    """A session that died leaves valid elements the index does not vouch for.
    They are free space, not data."""
    path = str(tmp_path / "t.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    f.add("table", "raw", "kept", b"one")
    f.commit()
    f.add("table", "raw", "abandoned", b"x" * 4096)
    f.close()

    g = tttrlib.PtoFile()
    g.open(path, writable=True)
    assert sum(e.bytes for e in g.free_extents()) >= 4096


def test_the_generation_rises_with_each_commit(tmp_path):
    path = str(tmp_path / "t.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    first = f.generation()
    f.add("table", "raw", "a", b"a")
    f.commit()
    f.add("table", "raw", "b", b"b")
    f.commit()
    assert f.generation() == first + 2
    f.close()
    g = tttrlib.PtoFile()
    g.open(path)
    assert g.generation() == first + 2


def test_a_damaged_index_falls_back_to_the_other_one(made):
    """The reason there are two. Scribbling on the live one must not lose the
    file: its CRC-32 fails and the reader takes the previous index, which is
    the state as of the commit before."""
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    f.add("table", "raw", "added later", b"later")
    assert f.commit(), f.error()
    f.close()

    data = bytearray(open(path, "rb").read())
    marks = [i for i in range(len(data) - 4)
             if data[i:i + 4] == b"\x11\x4d\x9b\x74"]          # SeekHead
    assert len(marks) == 2, "there should be exactly two indexes"

    def generation(at):
        """The PtoGeneration a SeekHead carries, so the test can tell which of
        the two is live rather than assuming which slot a commit used."""
        here = data.index(b"\x1e\x54\xf0\x10", at, at + 64)     # PtoGeneration
        length = data[here + 4] & 0x7F
        return int.from_bytes(data[here + 5:here + 5 + length], "big")

    live = max(marks, key=generation)
    stale = min(marks, key=generation)
    assert generation(live) > generation(stale)

    data[live + 40] ^= 0xFF              # somewhere inside the live index
    open(path, "wb").write(bytes(data))

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    names = [o.name for o in g.objects()]
    assert "added later" not in names, "read a damaged index"
    assert "bursts" in names, "lost the file entirely"


# -- compaction ----------------------------------------------------------------------

def test_compacting_drops_the_holes_and_keeps_the_uids(made):
    path, uid_photons, uid_bursts, uid_raw, original = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    tttrlib.pto_update_store(f, uid_bursts, store(30000, name="Tau"))
    f.commit()
    fat = os.path.getsize(path)
    assert sum(e.bytes for e in f.free_extents()) > 0

    lean_path = path + ".lean"
    assert f.compact(lean_path), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(lean_path), g.error()
    assert g.n_objects() == 3
    assert g.has(uid_photons) and g.has(uid_bursts) and g.has(uid_raw)
    assert g.read(uid_raw) == original
    assert [t.name for t in g.tags_for(uid_bursts)] == ["chisurf.derived_from"]
    assert sum(e.bytes for e in g.free_extents()) == 0
    assert os.path.getsize(lean_path) < fat


# -- the shape it exists for ------------------------------------------------------

def test_one_measurement_in_one_file(tmp_path):
    """Readable as documentation: everything one acquisition produced, bound
    together, with the relationships an application can follow afterwards."""
    path = str(tmp_path / "acquisition.pto")
    f = tttrlib.PtoFile()
    assert f.create(path, "2026-08-06 Cy3B-Cy5 titration, 300 pM")

    photons = tttrlib.DataStore("photons")
    photons.set_n_rows(50000)
    photons.add("macro_time", np.arange(50000, dtype=np.uint64))
    stream = tttrlib.pto_add_store(f, "photons", "photons", photons)

    bursts = tttrlib.DataStore("bursts")
    bursts.set_n_rows(312)
    bursts.add("Tau", np.linspace(1.0, 4.0, 312))
    found = tttrlib.pto_add_store(f, "table", "bursts", bursts, 1 << 16)

    spectrum = tttrlib.DataStore("emission")
    spectrum.set_n_rows(1024)
    spectrum.add("wavelength", np.linspace(500, 750, 1024))
    spectrum.add("counts", np.random.default_rng(0).poisson(400, 1024).astype(np.int32))
    emission = tttrlib.pto_add_store(f, "spectrum", "emission", spectrum)

    instrument = f.add("attachment", "ptu", "titration_03.ptu", b"PQTTTR..." * 100)

    for target, value, step in ((found, stream, "burst search"),
                                (emission, instrument, "spectrometer export")):
        edge = tttrlib.PtoTag()
        edge.name, edge.type = "chisurf.derived_from", tttrlib.PtoType_UID
        edge.target, edge.u = target, value
        f.add_tag(edge)
        what = tttrlib.PtoTag()
        what.name, what.type = "chisurf.step", tttrlib.PtoType_Text
        what.target, what.text = target, step
        f.add_tag(what)

    assert f.commit(), f.error()
    f.close()

    # Later, somewhere else: what is in this file and how does it fit together?
    g = tttrlib.PtoFile()
    assert g.open(g_path := path), g.error()
    assert g.title().startswith("2026-08-06")
    assert sorted(o.kind for o in g.objects()) == \
        ["attachment", "photons", "spectrum", "table"]

    lineage = {t.target: t.u for t in g.tags() if t.type == tttrlib.PtoType_UID}
    assert lineage[found] == stream, "the bursts came from the photons"
    assert lineage[emission] == instrument

    back = tttrlib.DataStore()
    tttrlib.pto_read_store(g, found, back)
    assert back.n_rows() == 312
