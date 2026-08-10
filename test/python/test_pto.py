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
import textwrap

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


def test_libebml_agrees_this_is_a_valid_pto(made):
    """The one check on the framing that is not written against our own parser.

    Every other EBML assertion here reads the file with code from this
    repository, which means a consistent misreading of RFC 8794 would satisfy
    the writer and the reader together and pass all of them. This hands the same
    file to libebml -- the reference implementation Matroska is built on -- and
    lets it disagree.

    Opt-in, because tttrlib must not acquire a dependency on libebml: the whole
    argument for the format is that a reader needs an EBML parser and twenty
    element IDs, not a framework. See test/tools/README.md for the two commands
    that build the checker and the variable that turns this on.
    """
    import subprocess

    checker = os.environ.get("TTTRLIB_PTO_EBML_CHECK", "").strip()
    if not checker or not os.path.exists(checker):
        pytest.skip("set TTTRLIB_PTO_EBML_CHECK; see test/tools/README.md")
    # --aligned as well: this container came from the default writer, which
    # aligns. A `compact(tight=True)` copy would rightly fail that flag.
    done = subprocess.run([checker, made[0], "--aligned"], capture_output=True, text=True)
    assert done.returncode == 0, done.stdout + done.stderr


def test_every_payload_starts_on_an_eight_byte_boundary(made):
    """EBML guarantees no alignment -- an element header is a variable number of
    octets, so ``FileData`` would otherwise begin wherever the name and the
    encoding strings happened to leave it.

    That is fine for bytes and wrong for what a payload actually is: a PTU
    record stream is ``uint32``, a ``.dstore`` column is ``double``, and a
    ``.dstore``'s own blob offsets are 8-aligned *relative to the store*, so
    they are only aligned in the file if the store itself begins on a boundary.
    The writer pads with ``Void`` to make it so.
    """
    path = made[0]
    f = tttrlib.PtoFile()
    assert f.open(path), f.error()
    for o in f.objects():
        assert o.offset % 8 == 0, "%s starts at %d" % (o.name, o.offset)


def test_alignment_survives_a_relocation(made):
    """The padding is chosen when the object is laid down, so an object that
    outgrows its room and is written somewhere else has to be aligned again --
    it is a fresh lay-down, not a move."""
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()
    before = f.object(uid_bursts).offset
    tttrlib.pto_update_store(f, uid_bursts, store(30000, name="Tau"))
    assert f.commit(), f.error()
    assert f.object(uid_bursts).offset != before, "the object did not move"
    for o in f.objects():
        assert o.offset % 8 == 0, "%s starts at %d" % (o.name, o.offset)


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


def test_an_update_corrects_the_row_count_the_header_claims(made):
    """`pto ls`, `pto info` and the TUI trust the object header's row count,
    not the store's own, so an update that finds a different number of rows
    must correct the header too -- in place, relocated, and on a file that was
    closed and reopened between the write and the update."""
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()
    assert f.object(uid_bursts).rows == 40

    # In place: the smaller payload stays where it lies, the count follows.
    assert tttrlib.pto_update_store(f, uid_bursts, store(10, name="Tau")), f.error()
    assert f.object(uid_bursts).rows == 10
    assert f.commit(), f.error()
    f.close()

    # The file says it too, which is what `pto ls` reads.
    g = tttrlib.PtoFile()
    assert g.open(path, writable=True), g.error()
    assert g.object(uid_bursts).rows == 10

    # Relocated: the fresh header carries the count with it.
    assert tttrlib.pto_update_store(g, uid_bursts, store(20000, name="Tau")), g.error()
    assert g.object(uid_bursts).rows == 20000
    assert g.commit(), g.error()
    g.close()

    # An update on a reopened file patches the header where the parser found
    # it -- the `tttr sm` re-run scenario, two processes apart.
    h = tttrlib.PtoFile()
    assert h.open(path, writable=True), h.error()
    assert h.object(uid_bursts).rows == 20000
    assert tttrlib.pto_update_store(h, uid_bursts, store(70, name="Tau")), h.error()
    assert h.commit(), h.error()
    h.close()

    k = tttrlib.PtoFile()
    assert k.open(path), k.error()
    assert k.object(uid_bursts).rows == 70


def test_compact_keeps_the_row_count(made):
    """Compaction copies every object into a fresh file; the row count is part
    of the object and has to arrive with it."""
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path), f.error()
    to = path + ".compact.pto"
    assert f.compact(to, True), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(to), g.error()
    assert g.object(uid_bursts).rows == 40


def test_nothing_but_the_rewritten_object_ever_moves(made):
    """The claim the whole format rests on, stated as the shape it is used in:
    a photon stream that is written once beside a table that keeps changing
    size. The table grows, shrinks, and outgrows its room; the stream does not
    move, is not rewritten, and stays byte-identical.

    Growth inside the reserved room does not move even the table. Only
    outgrowing it does, and then it is the only object that moves -- the file
    is never reshuffled.
    """
    path, uid_photons, uid_bursts, uid_raw, original = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()
    stream_at = f.object(uid_photons).offset
    raw_at = f.object(uid_raw).offset
    table_at = f.object(uid_bursts).offset

    # Inside the 8 KiB reserved at creation: nothing moves at all.
    for rows in (60, 200, 20, 400):
        assert tttrlib.pto_update_store(f, uid_bursts, store(rows, name="Tau"))
        assert f.commit(), f.error()
        assert f.object(uid_bursts).offset == table_at, "%d rows moved it" % rows

    # Past it: the table moves and nothing else does.
    assert tttrlib.pto_update_store(f, uid_bursts, store(50_000, name="Tau"))
    assert f.commit(), f.error()
    assert f.object(uid_bursts).offset != table_at
    assert f.object(uid_photons).offset == stream_at, "the photon stream moved"
    assert f.object(uid_raw).offset == raw_at, "a neighbour moved"

    photons = tttrlib.DataStore()
    tttrlib.pto_read_store(f, uid_photons, photons)
    np.testing.assert_array_equal(photons["macro_time"].numpy(),
                                  np.arange(2000, dtype=np.uint64))
    assert f.read(uid_raw) == original


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


def test_the_unused_end_of_a_reused_hole_is_still_a_void(made):
    """Carving the front of a hole must re-header what is left of it.

    A hole is one Void spanning the whole freed run, and an element written
    into its front overwrites that header. Without a fresh Void over the
    remainder, the tail is the old element's bytes with nothing declaring them,
    and the next reader to walk the Segment parses them as an element -- so the
    file reads as damaged at an offset in the middle of it, reported by the
    reader for something the writer did.

    The sibling test above stops at "the hole was used"; that passes either
    way, because it never reopens the file. This one reopens.
    """
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    f.open(path, writable=True)
    tttrlib.pto_update_store(f, uid_bursts, store(20000, name="Tau"))
    f.commit()
    hole = max(e.bytes for e in f.free_extents())
    assert hole > 1024, "the freed run is too small for this to prove anything"
    # Small enough to leave a remainder, which is the case that breaks.
    f.add("table", "raw", "filler", b"x" * 64)
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path) is True, g.error()
    assert g.n_objects() == 4
    for o in g.objects():
        assert g.read(o.uid) is not None
    g.close()


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


def _tag(target, name, *, text=None, uid=None):
    t = tttrlib.PtoTag()
    t.target = target
    t.name = name
    if uid is not None:
        t.type = tttrlib.PtoType_UID
        t.u = uid
    else:
        t.type = tttrlib.PtoType_Text
        t.text = text
    return t


def test_adding_the_same_fact_twice_records_it_once(made):
    """Re-running an analysis re-writes its parent edge, and a container
    analysed three times used to claim the same source four times. An exact
    duplicate is the same fact; two different parents are two facts."""
    path, uid_photons, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()

    for _ in range(3):
        f.add_tag(_tag(uid_bursts, "pto.parent", uid=uid_photons))
    parents = [t.u for t in f.tags_for(uid_bursts) if t.name == "pto.parent"]
    assert parents == [uid_photons], "one source, recorded %d times" % len(parents)

    # A second, different parent is not a duplicate and both survive.
    f.add_tag(_tag(uid_bursts, "pto.parent", uid=12345))
    parents = [t.u for t in f.tags_for(uid_bursts) if t.name == "pto.parent"]
    assert parents == [uid_photons, 12345]


def test_set_tag_replaces_what_was_stated_before(made):
    """set_tag is "the value IS x": re-describing an object leaves one value,
    the latest, and does not touch other objects or other names."""
    path, uid_photons, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()

    f.set_tag(_tag(uid_bursts, "_mmfdb_artifact.row_grain", text="burst"))
    f.set_tag(_tag(uid_bursts, "_mmfdb_artifact.row_grain", text="photon"))
    f.set_tag(_tag(uid_photons, "_mmfdb_artifact.row_grain", text="event"))
    f.commit()
    f.close()

    g = tttrlib.PtoFile()
    g.open(path)
    grains = [t.text for t in g.tags_for(uid_bursts)
              if t.name == "_mmfdb_artifact.row_grain"]
    assert grains == ["photon"], "re-describing left %r" % grains
    assert [t.text for t in g.tags_for(uid_photons)
            if t.name == "_mmfdb_artifact.row_grain"] == ["event"]


def test_clear_tags_for_one_name_leaves_the_rest(made):
    path, _, uid_bursts, _, _ = made
    f = tttrlib.PtoFile()
    assert f.open(path, writable=True), f.error()
    f.add_tag(_tag(uid_bursts, "a", text="1"))
    f.add_tag(_tag(uid_bursts, "b", text="2"))
    f.clear_tags(uid_bursts, "a")
    names = sorted(t.name for t in f.tags_for(uid_bursts))
    assert "a" not in names and "b" in names


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
    # Not zero: every payload is padded onto an 8-byte boundary, so a compacted
    # file still carries at most a boundary's worth of Void per object. What
    # compaction drops is the holes an update left, which are unbounded.
    assert sum(e.bytes for e in g.free_extents()) < 8 * g.n_objects()
    assert os.path.getsize(lean_path) < fat


def _fragmented(tmp_path, name="frag.pto"):
    """A container with a hole in it: an object grown past its room, so the
    space it used to occupy is dead."""
    path = str(tmp_path / name)
    f = tttrlib.PtoFile()
    assert f.create(path, "fragmented"), f.error()
    f.add("photons", "raw", "before.bin", b"P" * 200_000)
    tab = tttrlib.pto_add_store(f, "table", "bursts", store(1000, name="Tau"), 0)
    f.add("attachment", "raw", "after.bin", b"A" * 50_000)
    assert f.commit(), f.error()
    assert tttrlib.pto_update_store(f, tab, store(40_000, name="Tau")), f.error()
    assert f.commit(), f.error()
    assert sum(e.bytes for e in f.free_extents()) > 0, "nothing to compact"
    return f, path


def test_compact_by_default_leaves_only_the_alignment_padding(tmp_path):
    """The holes go; the padding that puts each payload on a boundary stays,
    because it is structure rather than waste."""
    f, path = _fragmented(tmp_path)
    lean = str(tmp_path / "lean.pto")
    assert f.compact(lean), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(lean), g.error()
    assert all(o.offset % 8 == 0 for o in g.objects()), "payloads lost their alignment"
    assert sum(e.bytes for e in g.free_extents()) < 8 * g.n_objects()
    assert os.path.getsize(lean) < os.path.getsize(path)


def test_compact_tight_leaves_no_void_at_all(tmp_path):
    """For an archive, or a copy about to be sent somewhere. The padding goes
    too, so the payloads can no longer be mapped in place -- which is the trade,
    and why it is not the default."""
    f, path = _fragmented(tmp_path)
    tight = str(tmp_path / "tight.pto")
    loose = str(tmp_path / "loose.pto")
    assert f.compact(tight, tight=True), f.error()
    assert f.compact(loose), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(tight), g.error()
    assert sum(e.bytes for e in g.free_extents()) == 0
    assert not all(o.offset % 8 == 0 for o in g.objects()), \
        "nothing was actually packed tighter"
    assert os.path.getsize(tight) < os.path.getsize(loose)
    # Still a container, and still readable.
    assert g.read(g.find("after.bin")) == b"A" * 50_000
    back = tttrlib.pto_store(g, g.find("bursts"))
    assert back.n_rows() == 40_000


def test_compact_can_reserve_room_for_what_comes_next(tmp_path):
    """The opposite trade: a bigger file that absorbs the next few updates
    without relocating anything. What a container still being edited wants."""
    f, _ = _fragmented(tmp_path)
    roomy = str(tmp_path / "roomy.pto")
    assert f.compact(roomy, reserve=0.25), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(roomy, writable=True), g.error()
    tab = g.find("bursts")
    for o in g.objects():
        assert o.capacity >= o.size * 1.2, "%s got no room" % o.name

    # ...and it is real room: the table grows by a quarter without moving.
    was = g.object(tab).offset
    assert tttrlib.pto_update_store(g, tab, store(48_000, name="Tau")), g.error()
    assert g.commit(), g.error()
    assert g.object(tab).offset == was, "it relocated despite the reserve"


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


# -- targeted reads and streaming -------------------------------------
#
# The container was built so a multi-gigabyte payload is cheap to keep beside a
# table that gets recomputed. Writing was streamed from the start; reading was
# all-or-nothing, and these are the claims that closed that gap.
#
# Byte-volume is asserted through resident memory rather than a wall clock. A
# timer on a warm page cache measures the cache; RSS measures whether the reader
# actually materialised the payload, which is the thing in question.

import hashlib
import resource
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_settings import DATA_AVAILABLE, DATA_ROOT  # type: ignore  # noqa: E402

SMALL_PTU = DATA_ROOT / "pq" / "ptu" / "pq_ptu_hh_t3.ptu"
BIG_PTU = DATA_ROOT / "pq" / "ptu" / "pq_ptu_hh_t3_cw_5GB.ptu"

needs_data = pytest.mark.skipif(not DATA_AVAILABLE, reason="no tttr-data")


def _rss_mb():
    """Peak resident set size in MB. ru_maxrss is bytes on macOS, KiB on Linux."""
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    return peak / (1 << 20) if sys.platform == "darwin" else peak / 1024


def _table(n_rows=100_000):
    s = tttrlib.DataStore("t")
    s.set_n_rows(n_rows)
    s.add("a", np.arange(n_rows, dtype=np.float64))
    s.add("b", np.arange(n_rows, dtype=np.int32))
    s.add("c", np.linspace(0.0, 1.0, n_rows))
    return s


@pytest.fixture
def container(tmp_path):
    """A container holding a real instrument file and a table."""
    if not DATA_AVAILABLE or not SMALL_PTU.exists():
        pytest.skip("no tttr-data")
    path = str(tmp_path / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path, "prd-020"), f.error()
    uid_raw = f.add_file("tttr_photon_stream", "ptu", SMALL_PTU.name, str(SMALL_PTU))
    uid_tab = tttrlib.pto_add_store(f, "burst_table", "bursts", _table(), 1 << 16)
    assert f.commit(), f.error()
    return f, path, uid_raw, uid_tab


# -- Part 1: the composable store read ------------------------------------------


def test_only_the_named_columns_come_back(container):
    """A store inside a container is always the read-at-an-offset case, so
    before this the one combination the container makes routine -- a column
    subset of an embedded store -- was the one it could not express."""
    f, _, _, uid = container
    sub = tttrlib.pto_store(f, uid, columns=["b"])
    assert list(sub.names) == ["b"]
    assert sub.n_rows() == 100_000
    np.testing.assert_array_equal(sub["b"].numpy(), np.arange(100_000, dtype=np.int32))


def test_a_subset_equals_the_same_columns_of_the_whole(container):
    f, _, _, uid = container
    whole = tttrlib.pto_store(f, uid)
    sub = tttrlib.pto_store(f, uid, columns=["a", "c"])
    for name in ("a", "c"):
        np.testing.assert_array_equal(sub[name].numpy(), whole[name].numpy())


def test_the_columns_can_be_listed_without_reading_them(container):
    f, _, _, uid = container
    assert list(tttrlib.pto_store_columns(f, uid)) == ["a", "b", "c"]


def test_an_object_reports_the_region_its_store_occupies(container):
    """What makes the read-at-an-offset path addressable at all."""
    f, _, _, uid = container
    region = tttrlib.pto_store_region(f, uid)
    assert region.offset > 0 and region.size > 0
    assert region.uid == uid


# -- Part 2: bytes without the whole payload ------------------------------------


def test_a_ranged_read_matches_the_corresponding_slice(container):
    f, _, uid, _ = container
    whole = bytes(f.read(uid))
    for at, n in ((0, 16), (1024, 512), (len(whole) // 2, 4096)):
        assert bytes(f.read(uid, at, n)) == whole[at : at + n]


def test_a_ranged_read_past_the_end_reads_short(container):
    """Like a file read, rather than throwing: a caller paging through a
    payload should not have to know the length to ask for the last block."""
    f, _, uid, _ = container
    size = len(bytes(f.read(uid)))
    tail = bytes(f.read(uid, size - 8, 4096))
    assert len(tail) == 8
    assert bytes(f.read(uid, size + 100, 16)) == b""


def test_paging_a_payload_reassembles_it_exactly(container):
    f, _, uid, _ = container
    whole = bytes(f.read(uid))
    block, out, at = 1 << 16, bytearray(), 0
    while True:
        chunk = bytes(f.read(uid, at, block))
        if not chunk:
            break
        out += chunk
        at += len(chunk)
    assert bytes(out) == whole


def test_a_ranged_read_of_an_object_that_is_not_there(container):
    """The same contract as the whole-payload read, so a caller cannot tell
    them apart by how they fail."""
    f, _, _, _ = container
    with pytest.raises(RuntimeError):
        f.read(12345, 0, 4)


def test_extract_is_now_stream_and_behaves_the_same(container, tmp_path):
    """Criterion 7. `extract` became `stream` with a file-writing sink, which
    is what it already was inside -- and the failure paths have to survive
    that, since they are the half a refactor quietly loses."""
    f, _, uid, _ = container
    out = str(tmp_path / "out.ptu")
    assert f.extract(uid, out), f.error()
    assert open(out, "rb").read() == bytes(f.read(uid))

    assert f.extract(999, out) is False
    assert "no object" in f.error()
    assert f.extract(uid, str(tmp_path / "no" / "such" / "dir" / "x")) is False
    assert f.error() != ""


# -- Part 3: row ranges ---------------------------------------------------------


def test_a_row_window_equals_the_slice_of_the_whole_column(container):
    """What a table viewer needs: paging a million-row burst table used to
    decode a million rows to show fifty."""
    f, _, _, uid = container
    win = tttrlib.pto_store(f, uid, columns=["a"], first_row=500, n_rows=50)
    assert win.n_rows() == 50
    np.testing.assert_array_equal(
        win["a"].numpy(), np.arange(500, 550, dtype=np.float64)
    )


def test_a_row_window_past_the_end_is_short_not_an_error(container):
    f, _, _, uid = container
    win = tttrlib.pto_store(f, uid, columns=["a"], first_row=99_990, n_rows=1000)
    assert win.n_rows() == 10


def test_n_rows_zero_means_to_the_end(container):
    f, _, _, uid = container
    win = tttrlib.pto_store(f, uid, columns=["a"], first_row=99_000, n_rows=0)
    assert win.n_rows() == 1000


# -- Part 4: cues ----------------------------------------------------------------


def test_cues_index_a_photon_payload(container):
    f, _, uid, _ = container
    n = f.build_cues(uid, 100_000)
    assert f.commit(), f.error()
    cues = f.cues(uid)
    assert n == len(cues) and len(cues) > 1
    assert [c.event for c in cues] == sorted(c.event for c in cues)
    assert [c.offset for c in cues] == sorted(c.offset for c in cues)


def test_a_ranged_event_read_matches_the_full_decode(container):
    """The claim cues exist for. Checked against a full decode rather than
    against another number this library computed."""
    f, path, uid, _ = container
    f.build_cues(uid, 100_000)
    assert f.commit(), f.error()

    full = tttrlib.TTTR(path)
    part = tttrlib.pto_events(path, 1000, 500)
    np.testing.assert_array_equal(part.macro_times, full.macro_times[1000:1500])
    np.testing.assert_array_equal(part.micro_times, full.micro_times[1000:1500])
    np.testing.assert_array_equal(
        part.routing_channels, full.routing_channels[1000:1500]
    )


@pytest.mark.slow
def test_a_cue_offset_really_is_where_that_event_starts(container):
    """Criterion 11, and the only honest way to check it: decode from the cue's
    own offset and see whether the first event that comes out is the cue's own
    event. Comparing two numbers this library computed would check nothing."""
    f, path, uid, _ = container
    f.build_cues(uid, 100_000)
    assert f.commit(), f.error()
    cues = list(f.cues(uid))
    full = tttrlib.TTTR(path)
    for c in cues:
        one = tttrlib.pto_events(path, c.event, 1)
        assert one.size() == 1
        assert one.macro_times[0] == full.macro_times[c.event]
        assert one.micro_times[0] == full.micro_times[c.event]
        assert c.time == full.macro_times[c.event], "the cue's macro time is wrong"


def test_a_range_is_reachable_through_the_reader_parameters(container):
    """How a binding asks for a range. A TTTR constructor taking (first, n)
    would be ambiguous with the four it already has, so the range goes through
    the reader-parameter mechanism the registry already declares schemas for."""
    f, path, uid, _ = container
    f.build_cues(uid, 100_000)
    assert f.commit(), f.error()
    f.close()

    full = tttrlib.TTTR(path)
    t = tttrlib.TTTR(path, "PTO", '{"first_event": 1000, "n_events": 500}')
    assert t.size() == 500
    np.testing.assert_array_equal(t.macro_times, full.macro_times[1000:1500])


def test_cues_are_dropped_when_the_payload_is_replaced(container):
    """A cue into bytes that changed is worse than no cue at all."""
    f, _, uid, uid_tab = container
    assert f.build_cues(uid, 50_000) > 0, f.error()
    assert f.update(uid, b"a shorter payload entirely"), f.error()
    assert len(f.cues(uid)) == 0


def test_building_cues_over_something_that_is_not_a_record_stream(container):
    """A table is not a record stream and a cue into one would index nothing,
    so this says so rather than writing an index of the wrong thing."""
    f, _, _, uid_tab = container
    assert f.build_cues(uid_tab, 10) == 0
    assert f.error() != ""
    assert f.build_cues(12345, 10) == 0
    assert "no object" in f.error()


def test_a_ranged_read_without_cues_is_still_correct(tmp_path):
    """A cue is advisory. Without one the payload is decoded and sliced, which
    is slower and must not be different."""
    if not DATA_AVAILABLE or not SMALL_PTU.exists():
        pytest.skip("no tttr-data")
    path = str(tmp_path / "nocues.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    f.add_file("tttr_photon_stream", "ptu", SMALL_PTU.name, str(SMALL_PTU))
    assert f.commit(), f.error()
    f.close()

    full = tttrlib.TTTR(path)
    part = tttrlib.pto_events(path, 2000, 300)
    np.testing.assert_array_equal(part.macro_times, full.macro_times[2000:2300])


def test_a_container_with_cues_is_read_by_something_that_ignores_them(container):
    """Cues are a new element inside their own master, so a reader that has
    never heard of them skips them by size."""
    f, path, uid, _ = container
    before = hashlib.sha256(bytes(f.read(uid))).hexdigest()
    f.build_cues(uid, 50_000)
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    assert g.n_objects() == 2
    assert hashlib.sha256(bytes(g.read(uid))).hexdigest() == before
    assert [o.name for o in g.objects()] == [SMALL_PTU.name, "bursts"]


def test_a_generic_ebml_parser_still_walks_a_container_with_cues(container):
    """The reason cues reuse Matroska's id and live in a sized master."""
    f, path, uid, _ = container
    f.build_cues(uid, 50_000)
    assert f.commit(), f.error()
    f.close()

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
        out, i = [], start
        while i < end:
            eid, idn = vint(buf, i, False)
            size, szn = vint(buf, i + idn, True)
            out.append((eid, i + idn + szn, size))
            i += idn + szn + size
        return out

    top = walk(data, 0, len(data))
    seg = [e for e in top if e[0] == 0x18538067][0]
    kinds = [c[0] for c in walk(data, seg[1], seg[1] + seg[2])]
    assert 0x1C53BB6B in kinds, "Cues is not a top-level element"


def test_cues_survive_compaction_and_go_with_a_removed_object(container, tmp_path):
    f, path, uid, _ = container
    f.build_cues(uid, 50_000)
    assert f.commit(), f.error()
    n = len(f.cues(uid))

    lean = str(tmp_path / "lean.pto")
    assert f.compact(lean), f.error()
    f.close()

    g = tttrlib.PtoFile()
    g.open(lean, writable=True)
    assert len(g.cues(uid)) == n, "compaction dropped the cues"
    g.clear_cues(uid)
    assert g.commit(), g.error()
    assert g.cues(uid) == () or len(g.cues(uid)) == 0
    g.close()


# -- Part 5: streaming in ---------------------------------------------------------


def test_add_file_and_add_produce_the_same_container(tmp_path):
    """Not a second code path: the same header and slot bookkeeping with a
    different source of bytes."""
    if not DATA_AVAILABLE or not SMALL_PTU.exists():
        pytest.skip("no tttr-data")
    payload = SMALL_PTU.read_bytes()

    a = str(tmp_path / "a.pto")
    fa = tttrlib.PtoFile()
    fa.create(a, "t")
    ua = fa.add("tttr_photon_stream", "ptu", "x.ptu", payload)
    fa.commit()
    fa.close()

    b = str(tmp_path / "b.pto")
    fb = tttrlib.PtoFile()
    fb.create(b, "t")
    ub = fb.add_file("tttr_photon_stream", "ptu", "x.ptu", str(SMALL_PTU))
    fb.commit()
    fb.close()

    ga, gb = tttrlib.PtoFile(), tttrlib.PtoFile()
    ga.open(a)
    gb.open(b)
    assert bytes(ga.read(ua)) == bytes(gb.read(ub)) == payload
    assert ga.object(ua).size == gb.object(ub).size
    assert ga.object(ua).kind == gb.object(ub).kind


def test_add_file_refuses_a_path_that_is_not_there(tmp_path):
    f = tttrlib.PtoFile()
    f.create(str(tmp_path / "t.pto"))
    assert f.add_file("attachment", "raw", "nope", str(tmp_path / "absent")) == 0
    assert f.error()


@pytest.mark.slow
@needs_data
def test_embedding_a_gigabyte_does_not_hold_it_in_memory(tmp_path):
    """The reason add_file exists. Measured as resident memory, because that is
    what "did not materialise it" means -- a timer would measure the cache."""
    if not BIG_PTU.exists():
        pytest.skip("no multi-gigabyte fixture")
    size_gb = BIG_PTU.stat().st_size / (1 << 30)
    before = _rss_mb()

    f = tttrlib.PtoFile()
    f.create(str(tmp_path / "big.pto"))
    uid = f.add_file("tttr_photon_stream", "ptu", BIG_PTU.name, str(BIG_PTU))
    assert f.commit(), f.error()
    grew = _rss_mb() - before
    f.close()

    assert uid, f.error()
    assert grew < 256, f"embedding {size_gb:.1f} GB grew RSS by {grew:.0f} MB"


@pytest.mark.slow
@needs_data
def test_compacting_an_object_does_not_hold_it_in_memory(tmp_path):
    """Compaction touches every payload in the file, so materialising them
    would make compacting an eight-gigabyte container need eight gigabytes --
    for the operation whose whole purpose is to make the file smaller."""
    if not BIG_PTU.exists():
        pytest.skip("no multi-gigabyte fixture")
    path = str(tmp_path / "big.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    f.add_file("tttr_photon_stream", "ptu", BIG_PTU.name, str(BIG_PTU))
    assert f.commit(), f.error()

    before = _rss_mb()
    assert f.compact(str(tmp_path / "big-lean.pto")), f.error()
    grew = _rss_mb() - before
    f.close()
    assert grew < 256, f"compacting grew RSS by {grew:.0f} MB"


@pytest.mark.slow
@needs_data
def test_taking_a_gigabyte_back_out_does_not_hold_it_either(tmp_path):
    """Criterion 6, and the read side of the one above. `extract` is now
    `stream` with a file-writing sink, so this is what bounds `stream`."""
    if not BIG_PTU.exists():
        pytest.skip("no multi-gigabyte fixture")
    path = str(tmp_path / "big.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    uid = f.add_file("tttr_photon_stream", "ptu", BIG_PTU.name, str(BIG_PTU))
    assert f.commit(), f.error()

    out = str(tmp_path / "back.ptu")
    before = _rss_mb()
    assert f.extract(uid, out), f.error()
    grew = _rss_mb() - before
    f.close()

    assert os.path.getsize(out) == BIG_PTU.stat().st_size
    assert grew < 256, f"extracting grew RSS by {grew:.0f} MB"


@pytest.mark.slow
@needs_data
def test_a_column_subset_of_a_large_store_does_not_materialise_it(tmp_path):
    """Criterion 2, as memory rather than as a stopwatch."""
    path = str(tmp_path / "wide.pto")
    f = tttrlib.PtoFile()
    f.create(path)
    n = 4_000_000
    s = tttrlib.DataStore("t")
    s.set_n_rows(n)
    for name in ("a", "b", "c", "d", "e"):
        s.add(name, np.arange(n, dtype=np.float64))
    uid = tttrlib.pto_add_store(f, "burst_table", "wide", s, 0)
    assert f.commit(), f.error()
    f.close()
    del s

    g = tttrlib.PtoFile()
    g.open(path)
    before = _rss_mb()
    sub = tttrlib.pto_store(g, uid, columns=["b"])
    grew = _rss_mb() - before
    assert sub.n_rows() == n and list(sub.names) == ["b"]
    # One column is ~32 MB; the whole store is ~160 MB.
    assert grew < 120, f"reading one of five columns grew RSS by {grew:.0f} MB"
    g.close()


def test_two_columns_of_a_wide_store_read_under_one_percent_of_it(tmp_path):
    """Criterion 2 as it is actually stated: instrumented read volume, not a
    stopwatch and not RSS.

    A timer on a warm page cache measures the cache, and RSS measures what was
    allocated rather than what was fetched. The claim is that the reader *did
    not touch* those bytes, and only a counter on the reads themselves can make
    it. `store_bytes_read` is that counter.
    """
    path = str(tmp_path / "verywide.pto")
    f = tttrlib.PtoFile()
    assert f.create(path), f.error()
    n, columns = 20_000, 256
    s = tttrlib.DataStore("t")
    s.set_n_rows(n)
    for k in range(columns):
        s.add("c%03d" % k, np.arange(n, dtype=np.float64) + k)
    uid = tttrlib.pto_add_store(f, "burst_table", "wide", s)
    assert f.commit(), f.error()
    f.close()
    del s

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()

    before = tttrlib.store_bytes_read()
    tttrlib.pto_store(g, uid)
    whole = tttrlib.store_bytes_read() - before

    before = tttrlib.store_bytes_read()
    two = tttrlib.pto_store(g, uid, columns=["c001", "c200"])
    subset = tttrlib.store_bytes_read() - before

    assert two.names == ["c001", "c200"]
    np.testing.assert_array_equal(two["c200"].numpy(),
                                  np.arange(n, dtype=np.float64) + 200)
    assert subset < whole / 100, \
        "two of %d columns read %d of %d bytes" % (columns, subset, whole)


def test_every_file_uid_is_written_in_eight_octets(container):
    """A regression test for a corruption that only showed up 1 time in 256.

    ``FileUID`` is the one element rewritten in place: when an object outgrows
    its room it is written afresh with a new uid and the old uid is then written
    back over it. The writer packed unsigned integers to the smallest width that
    held the value, so a uid whose top byte happened to be zero produced a
    *shorter* element -- and every sibling after it, ``PtoKind`` and
    ``PtoEncoding`` among them, was then read from the wrong offset. The symptom
    was an object that came back with an empty encoding, on one relocation in
    256, which is a suite that goes green.

    Walked structurally rather than by scanning for the id, because an
    eighteen-megabyte payload contains every two-byte sequence including this
    one, and the width has to be asserted where the element actually is.
    """
    f, path, uid, _ = container
    tttrlib.pto_update_store(f, uid, _table(400_000))   # forces a relocation
    assert f.commit(), f.error()
    f.close()

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
        out, i = [], start
        while i < end:
            eid, idn = vint(buf, i, False)
            size, szn = vint(buf, i + idn, True)
            out.append((eid, i + idn + szn, size))
            i += idn + szn + size
        return out

    seg = [e for e in walk(data, 0, len(data)) if e[0] == 0x18538067][0]
    widths = []
    for eid, at, size in walk(data, seg[1], seg[1] + seg[2]):
        if eid != 0x1941A469:                       # Attachments
            continue
        for feid, fat, fsize in walk(data, at, at + size):
            if feid != 0x61A7:                      # AttachedFile
                continue
            first = walk(data, fat, fat + fsize)[0]
            assert first[0] == 0x46AE, "FileUID is not the first child"
            widths.append(first[2])

    assert widths, "no AttachedFile found"
    assert set(widths) == {8}, f"FileUID written at widths {sorted(set(widths))}, not only 8"


def test_an_object_keeps_its_metadata_across_a_relocation(container):
    """The behaviour the width bug broke, stated as behaviour."""
    f, path, _, uid = container
    before = f.object(uid)
    kind, encoding, name = before.kind, before.encoding, before.name

    tttrlib.pto_update_store(f, uid, _table(400_000))
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    after = g.object(uid)
    assert (after.kind, after.encoding, after.name) == (kind, encoding, name)
    assert tttrlib.pto_store(g, uid).n_rows() == 400_000
    g.close()


# -- malformed input ------------------------------------------------------------
#
# A Data Size is read out of the file, so in a damaged or hostile one it is
# whatever the file says -- including a number far larger than the file itself.
# Everything sized from it has to be bounded by what the file can contain before
# a byte of it is believed. Found by sweeping single-byte corruptions through a
# valid container: two of them took the reader out with the out-of-memory killer
# rather than reporting a damaged file.


def _corrupt(path, tmp_path, offset, value):
    data = bytearray(path.read_bytes())
    data[offset] = value
    out = tmp_path / "corrupt.pto"
    out.write_bytes(bytes(data))
    return out


@pytest.fixture
def small_container(tmp_path):
    path = tmp_path / "small.pto"
    f = tttrlib.PtoFile()
    assert f.create(str(path), "malformed")
    tttrlib.pto_add_store(f, "burst_table", "t", _table(50), 4096)
    f.add("attachment", "raw", "blob", b"x" * 4096)
    assert f.commit(), f.error()
    f.close()
    return path


def test_a_size_larger_than_the_file_is_refused(small_container, tmp_path):
    """The specific shape that killed the process: a Data Size that claims more
    than the file holds, believed far enough to allocate from."""
    data = small_container.read_bytes()
    seg_id = data.find(b"\x18\x53\x80\x67")
    assert seg_id > 0, "no Segment"
    size_at = seg_id + 4                       # the wide, eight-octet size VINT

    broken = bytearray(data)
    broken[size_at] = 0x01                     # keep the 8-octet marker
    broken[size_at + 1 : size_at + 8] = b"\xff" * 7   # ~2^56 bytes of Segment
    out = tmp_path / "huge.pto"
    out.write_bytes(bytes(broken))

    f = tttrlib.PtoFile()
    assert f.open(str(out)) is False, "a Segment larger than the file was accepted"
    assert f.error()


def test_no_single_byte_corruption_takes_the_reader_down(small_container, tmp_path):
    """Swept in-process on purpose: if this regresses the failure is an
    out-of-memory kill, and a dead test process is the right amount of loud."""
    size = small_container.stat().st_size
    for offset in range(0, min(size, 300)):
        for value in (0x00, 0x01, 0xFF):
            path = _corrupt(small_container, tmp_path, offset, value)
            f = tttrlib.PtoFile()
            try:
                if f.open(str(path)):
                    f.n_objects()
                    for o in f.objects():
                        try:
                            f.read(o.uid)
                        except Exception:
                            pass
                    f.tags()
            except Exception:
                pass                            # refusing is fine; dying is not
            finally:
                f.close()


def test_a_header_declaring_wider_ids_than_we_parse_is_refused(small_container, tmp_path):
    """EBMLMaxIDLength/EBMLMaxSizeLength say how wide this document's ids and
    sizes may be. A reader built for four and eight cannot walk more, and
    reading a five-octet id as a four-octet one yields a plausible wrong answer
    rather than a failure."""
    data = bytearray(small_container.read_bytes())
    at = data.find(b"\x42\xf2")                # EBMLMaxIDLength
    assert at > 0, "no EBMLMaxIDLength in the header"
    assert data[at + 2] == 0x81, "expected a one-octet value"
    data[at + 3] = 5
    out = tmp_path / "wide.pto"
    out.write_bytes(bytes(data))

    f = tttrlib.PtoFile()
    assert f.open(str(out)) is False
    assert "wider" in f.error()


# -- one writer at a time ------------------------------------------------------
#
# Two writers each hold their own slot table, freelist and generation counter,
# and nothing is visible until commit(), so they allocate from freelists
# computed before either committed. The last commit wins, and the loser's bytes
# are still in the file being pointed at by the winner's index -- which is how a
# burst table came back with a duration of zero beside 1951 photons. Nothing
# afterwards says two writers were there, so this has to be refused up front.


def _in_another_process(body, *args):
    """Run `body` in a fresh interpreter, and give back what it printed."""
    import subprocess
    import sys
    src = "import tttrlib, os, sys\n" + textwrap.dedent(body)
    env = dict(os.environ)
    build_ext = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "build", "ext"
    )
    if os.path.isdir(build_ext):
        env["PYTHONPATH"] = build_ext + os.pathsep + env.get("PYTHONPATH", "")
    r = subprocess.run([sys.executable, "-c", src, *map(str, args)],
                       capture_output=True, text=True, timeout=60, env=env)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


def test_a_second_writer_is_refused_while_the_first_holds_the_file(made):
    path = made[0]
    first = tttrlib.PtoFile()
    assert first.open(path, True), first.error()
    try:
        out = _in_another_process("""
            f = tttrlib.PtoFile()
            print("ok" if f.open(sys.argv[1], True) else "refused", f.error())
            """, path)
    finally:
        first.close()
    assert out.startswith("refused"), out
    assert "open for writing elsewhere" in out


def test_a_refused_writer_fails_at_once_rather_than_waiting(made):
    """A writer that blocks is indistinguishable from one that hung: an
    analysis legitimately taking a minute gives the caller no way to tell."""
    import time
    path = made[0]
    first = tttrlib.PtoFile()
    assert first.open(path, True), first.error()
    try:
        t0 = time.time()
        second = tttrlib.PtoFile()
        assert second.open(path, True) is False
        assert time.time() - t0 < 2.0
    finally:
        first.close()


def test_a_reader_is_not_locked_out_while_a_writer_holds_the_file(made):
    """A viewer open during an analysis is the normal case, and the format
    already accounts for the reader seeing the pre-commit state."""
    path, uid_photons = made[0], made[1]
    first = tttrlib.PtoFile()
    assert first.open(path, True), first.error()
    try:
        reader = tttrlib.PtoFile()
        assert reader.open(path, False), reader.error()
        assert reader.n_objects() == 3
        assert len(reader.read(uid_photons)) > 0
        reader.close()

        out = _in_another_process("""
            f = tttrlib.PtoFile()
            print("ok" if f.open(sys.argv[1], False) else "refused", f.n_objects())
            """, path)
        assert out == "ok 3", out
    finally:
        first.close()


def test_create_refuses_rather_than_truncating_a_held_container(made):
    """`create` truncates, so it has to take the lock before it does -- not
    fopen("w+b") and then ask. Getting this backwards destroys the file it is
    about to be told it may not have."""
    path = made[0]
    before = os.path.getsize(path)
    first = tttrlib.PtoFile()
    assert first.open(path, True), first.error()
    try:
        usurper = tttrlib.PtoFile()
        assert usurper.create(path, "usurper") is False
        assert "open for writing elsewhere" in usurper.error()
        assert first.n_objects() == 3
    finally:
        first.close()
    assert os.path.getsize(path) == before


def test_closing_releases_the_lock(made):
    path = made[0]
    first = tttrlib.PtoFile()
    assert first.open(path, True), first.error()
    first.close()
    second = tttrlib.PtoFile()
    assert second.open(path, True), second.error()
    second.close()


def test_a_refused_open_does_not_leave_the_file_locked(tmp_path):
    """The failure path has to drop the lock too. It is taken before the
    container is parsed, so everything the parser rejects -- not EBML, not PTO,
    damaged, too new -- runs with the lock held."""
    junk = tmp_path / "junk.pto"
    junk.write_bytes(b"nothing like an EBML header" * 8)

    bad = tttrlib.PtoFile()
    assert bad.open(str(junk), True) is False
    assert "not an EBML file" in bad.error()

    again = tttrlib.PtoFile()
    assert again.open(str(junk), True) is False
    assert "not an EBML file" in again.error(), "the rejected open kept the lock"

    out = _in_another_process("""
        f = tttrlib.PtoFile()
        f.open(sys.argv[1], True)
        print(f.error())
        """, str(junk))
    assert "not an EBML file" in out, "the rejected open kept the lock"


def test_a_writer_that_dies_does_not_lock_the_file_forever(made):
    """The lock is on the descriptor, so the kernel drops it when the process
    goes -- no stale lock to clean up, which a sidecar file cannot promise."""
    path = made[0]
    out = _in_another_process("""
        f = tttrlib.PtoFile()
        f.open(sys.argv[1], True)
        print("held")
        sys.stdout.flush()
        os._exit(0)                     # no close(), no destructor
        """, path)
    assert out == "held"
    after = tttrlib.PtoFile()
    assert after.open(path, True), after.error()
    after.close()


def test_disassemble_creates_directories(tmp_path):
    """disassemble and extract must create parent directories if an object's
    name implies a directory structure."""
    pto_path = str(tmp_path / "nested.pto")
    f = tttrlib.PtoFile()
    assert f.create(pto_path)
    f.add("burst_table", "csv", "countrate_All 0.2000#30/bursts", b"col1,col2\n1,2\n")
    assert f.commit(), f.error()
    f.close()

    unpack_dir = str(tmp_path / "unpack")
    g = tttrlib.PtoFile()
    assert g.open(pto_path)
    written = g.disassemble(unpack_dir)
    assert len(written) == 1
    assert os.path.exists(os.path.join(unpack_dir, "countrate_All 0.2000#30", "bursts"))
    assert open(written[0], "rb").read() == b"col1,col2\n1,2\n"
    g.close()



# -- bundling files ---------------------------------------------------------------
#
# A measurement is rarely one file, and the folder it arrives as is what has to
# go in. The claims below are the ones that make that folder survive the trip:
# what each file IS is worked out rather than declared, a `.set` stays tied to
# its `.spc`, and the layout comes back out the way it went in.

SPC_QC = DATA_ROOT / "bh" / "bh_spcqc004.spc"
SPC_QC_SET = DATA_ROOT / "bh" / "bh_spcqc004.set"


def _folder(root):
    """A measurement as it arrives: an instrument file, its sidecar, a note."""
    (root / "raw").mkdir(parents=True, exist_ok=True)
    (root / "notes").mkdir(parents=True, exist_ok=True)
    for path in (SPC_QC, SPC_QC_SET):
        (root / "raw" / path.name).write_bytes(path.read_bytes())
    (root / "notes" / "protocol.txt").write_text("sample: DNA ruler\n")
    (root / "notes" / "bursts.csv").write_text("burst,ms\n1,2.5\n")
    (root / "meta.json").write_text('{"operator": "tp"}')
    return root


@needs_data
def test_a_photon_stream_is_classified_by_its_contents(tmp_path):
    """Four formats claim ".spc" and only the bytes say which one this is. An
    extension table would have made every one of them an SPC-130."""
    if not SPC_QC.exists():
        pytest.skip("no B&H SPC-QC fixture")
    guess = tttrlib.pto_classify_path(str(SPC_QC))
    assert guess.kind == "photons"
    assert guess.encoding == "spc-qc"

    # The same name, without the bytes behind it, is not a photon stream.
    absent = tttrlib.pto_classify_path(str(tmp_path / "bh_spcqc004.spc"))
    assert absent.kind != "photons"


def test_what_a_name_alone_says(tmp_path):
    known = {
        "notes.md": ("attachment", "text", "text/markdown"),
        "bursts.csv": ("table", "csv", "text/csv"),
        "figure.png": ("image", "png", "image/png"),
        "protocol.pdf": ("attachment", "pdf", "application/pdf"),
        "setup.json": ("attachment", "json", "application/json"),
    }
    for name, expected in known.items():
        guess = tttrlib.pto_classify_path(name)
        assert (guess.kind, guess.encoding, guess.media_type) == expected, name

    # Anything unrecognised is carried, named, and left alone -- never refused.
    unknown = tttrlib.pto_classify_path("instrument.qqq")
    assert (unknown.kind, unknown.encoding, unknown.media_type) == ("attachment", "raw", "")


def test_attach_names_the_object_after_the_file(tmp_path):
    (tmp_path / "sub").mkdir()
    note = tmp_path / "sub" / "note.md"
    note.write_text("hello")

    f = tttrlib.PtoFile()
    assert f.create(str(tmp_path / "one.pto")), f.error()
    uid = f.attach(str(note))
    assert uid
    o = f.object(uid)
    # The filename, not the path it was found at: a container is not a copy of
    # somebody's directory layout.
    assert o.name == "note.md"
    assert (o.kind, o.encoding, o.media_type) == ("attachment", "text", "text/markdown")
    assert bytes(f.read(uid)) == b"hello"

    # Every override is honoured, and overriding one does not lose the others.
    other = f.attach(str(note), "renamed", "table")
    assert f.object(other).name == "renamed"
    assert f.object(other).kind == "table"
    assert f.object(other).encoding == "text"
    f.close()


def test_attach_refuses_a_path_that_is_not_there(tmp_path):
    f = tttrlib.PtoFile()
    assert f.create(str(tmp_path / "t.pto"))
    assert f.attach(str(tmp_path / "absent.ptu")) == 0
    assert f.error()
    f.close()


@needs_data
def test_a_folder_becomes_one_file_and_a_folder_again(tmp_path):
    """The round trip that is the whole point: what went in comes back out,
    byte for byte, with its directories."""
    if not SPC_QC.exists():
        pytest.skip("no B&H SPC-QC fixture")
    folder = _folder(tmp_path / "measurement")

    path = str(tmp_path / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path, "DNA ruler"), f.error()
    made = tttrlib.pto_bundle(f, folder)
    assert f.commit(), f.error()
    f.close()

    # A directory means everything under it, named relative to it -- and in
    # sorted order, so the same folder bundles the same way twice running.
    names = [o.name for o in made]
    assert names == ["meta.json", "notes/bursts.csv", "notes/protocol.txt",
                     "raw/bh_spcqc004.set", "raw/bh_spcqc004.spc"]

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    back = tmp_path / "back"
    assert len(g.disassemble(str(back))) == len(names)
    g.close()

    for name in names:
        assert (back / name).read_bytes() == (folder / name).read_bytes(), name


@needs_data
def test_a_set_stays_tied_to_its_spc(tmp_path):
    """Half a Becker & Hickl header lives in the `.set`, so the pair has to be
    handed to the reader together -- and after bundling, only the container
    remembers they belong to each other."""
    if not SPC_QC.exists():
        pytest.skip("no B&H SPC-QC fixture")
    folder = _folder(tmp_path / "measurement")

    path = str(tmp_path / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path), f.error()
    made = {o.name: o.uid for o in tttrlib.pto_bundle(f, folder)}
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    links = [(t.target, t.u) for t in g.tags() if t.name == tttrlib.kPtoSidecarTag]
    assert links == [(made["raw/bh_spcqc004.set"], made["raw/bh_spcqc004.spc"])]
    g.close()

    # And the stream reads out of the container exactly as it reads on its own.
    inside = tttrlib.pto_events(path + "|raw/bh_spcqc004.spc")
    outside = tttrlib.TTTR(str(folder / "raw" / "bh_spcqc004.spc"))
    assert len(inside) == len(outside)
    assert np.array_equal(inside.macro_times, outside.macro_times)


@needs_data
def test_bundling_can_be_told_not_to_link_the_sidecar(tmp_path):
    if not SPC_QC.exists():
        pytest.skip("no B&H SPC-QC fixture")
    folder = _folder(tmp_path / "measurement")
    path = str(tmp_path / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path), f.error()
    tttrlib.pto_bundle(f, folder, link_sidecars=False)
    assert f.commit(), f.error()
    assert not [t for t in f.tags() if t.name == tttrlib.kPtoSidecarTag]
    f.close()


def test_a_container_does_not_bundle_itself(tmp_path):
    """The container being written lives in the folder being bundled, and a
    file cannot carry itself."""
    folder = tmp_path / "here"
    folder.mkdir()
    (folder / "note.txt").write_text("hello")

    path = str(folder / "run.pto")
    f = tttrlib.PtoFile()
    assert f.create(path), f.error()
    made = tttrlib.pto_bundle(f, folder)
    assert f.commit(), f.error()
    assert [o.name for o in made] == ["note.txt"]
    f.close()


def test_the_media_type_is_written_down_and_survives_compaction(tmp_path):
    """FileMediaType was in the format and readable from the day PTO existed,
    and nothing but an embedded store ever wrote one."""
    (tmp_path / "figure.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\0" * 32)

    path = str(tmp_path / "a.pto")
    f = tttrlib.PtoFile()
    assert f.create(path), f.error()
    uid = f.attach(str(tmp_path / "figure.png"))
    assert f.commit(), f.error()
    f.close()

    g = tttrlib.PtoFile()
    assert g.open(path), g.error()
    assert g.object(uid).media_type == "image/png"
    small = str(tmp_path / "b.pto")
    assert g.compact(small), g.error()
    g.close()

    h = tttrlib.PtoFile()
    assert h.open(small), h.error()
    assert h.object(uid).media_type == "image/png"
    h.close()
