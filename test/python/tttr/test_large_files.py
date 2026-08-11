"""Containers larger than 2 GiB.

Reported from the field: files above 2 GB do not read on Windows. The cause was
that `long` is 32 bits there while it is 64 on Linux and macOS, so every offset
that went through fseek/ftell -- the PTO element walk, each payload read, the
record rewind -- silently truncated, and nothing past the 2 GiB line could be
reached. LP64 hides all of it, which is why it survived so long: the suite was
green on every machine the authors used.

The file here is sparse: a real header, then the length set to 3 GiB without
writing anything. It costs no disk on a filesystem that supports holes and
takes well under a second, because the assertions are about *offsets*, not
about content. The reads are aimed deliberately at the far end, past the line
where a 32-bit offset wraps.
"""
import os
import shutil

import pytest

import tttrlib
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# 3 GiB: comfortably past the 2 GiB (2^31) line a signed 32-bit offset wraps at.
SPARSE_BYTES = 3 * 1024 ** 3
RECORD_BYTES = 4


def _source_ptu():
    for key in ("ptu_hh_t3_filename", "ht3_v1_filename"):
        path = settings.get(key)
        if path and os.path.exists(path) and path.endswith(".ptu"):
            return path
    return None


@pytest.fixture
def sparse_container(tmp_path):
    """A real PTU header with three gibibytes of nothing after it."""
    src = _source_ptu()
    if src is None:
        pytest.skip("no .ptu in this data set to take a header from")

    header_bytes = tttrlib.TTTR(src).get_header().end()
    if header_bytes <= 0:
        pytest.skip("could not measure the header of %s" % src)

    out = str(tmp_path / "sparse.ptu")
    with open(src, "rb") as fin, open(out, "wb") as fout:
        fout.write(fin.read(header_bytes))
        fout.truncate(SPARSE_BYTES)

    # A filesystem without holes would really write three gigabytes. Skip
    # rather than fill a runner's disk.
    free = shutil.disk_usage(str(tmp_path)).free
    if free < SPARSE_BYTES:
        pytest.skip("not enough free space for the fallback of a real 3 GiB file")
    return out, header_bytes


def test_the_records_of_a_3_gib_container_are_all_counted(sparse_container):
    """The count comes from the file size, so it needs 64-bit arithmetic."""
    path, header_bytes = sparse_container
    info = tttrlib.container_records(path)
    assert info.n_records == (SPARSE_BYTES - header_bytes) // RECORD_BYTES


def test_a_read_past_the_2_gib_line_returns_its_records(sparse_container):
    """The claim the field report was about.

    A seek to this offset through a 32-bit `long` wraps negative and the read
    fails or returns the wrong part of the file. Ten records from the very end
    is the cheapest way to ask for one.
    """
    path, header_bytes = sparse_container
    n_records = (SPARSE_BYTES - header_bytes) // RECORD_BYTES
    first = n_records - 10
    assert header_bytes + first * RECORD_BYTES > 2 ** 31, "the read must be past 2 GiB"

    raw = tttrlib.container_read_records(path, first, 10)
    assert len(raw) == 10 * RECORD_BYTES


def test_the_header_of_a_3_gib_container_still_reads(sparse_container):
    """Opening one must not depend on how big it is."""
    path, _ = sparse_container
    # 0 is the PTU container; the header is at the front, so this is really a
    # check that opening does not depend on the length behind it.
    header = tttrlib.TTTRHeader(path, 0)
    assert header.get_bytes_per_record() == RECORD_BYTES
