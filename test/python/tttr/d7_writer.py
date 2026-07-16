# SPDX-License-Identifier: BSD-3-Clause
"""Minimal writer for Photonscore ".photons" (D7) files.

This produces a byte-exact D7 container that the tttrlib reader (and the public
photonsfile reference) can decode. It exists so the test suite can round-trip
".photons" files without shipping a proprietary sample. The physical layout is:
a stream of 16384-byte pages, each starting with a 2-byte block header; with the
block headers removed the stream is a protobuf Header, a run of Data blocks, a
global Index and an Epilogue.
"""
import struct

MAGIC = b"D7 Photons Data"
PAGE = 16384
PAYLOAD = PAGE - 2  # logical bytes per page (2 bytes are the block header)


def _varint(v):
    v = int(v)
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)


def _zigzag(n):
    n = int(n)
    return ((n << 1) ^ (n >> 63)) & 0xFFFFFFFFFFFFFFFF


def _tag(field, wiretype):
    return _varint((field << 3) | wiretype)


def _len_delim(field, payload):
    return _tag(field, 2) + _varint(len(payload)) + payload


def _dataset_info(name, type_code):
    b = _len_delim(1, name.encode("ascii"))       # name
    b += _tag(2, 0) + _varint(type_code)          # TypeCode
    return b


def _header_msg(datasets, page_size=PAGE):
    b = _len_delim(1, MAGIC)                       # signatue
    b += _tag(2, 0) + _varint(1)                   # version_major
    b += _tag(3, 0) + _varint(0)                   # version_minor
    b += _tag(4, 0) + _varint(1)                   # version_patch
    b += _tag(6, 0) + _varint(512)                 # index_step
    b += _tag(8, 0) + _varint(page_size)           # bytes_per_page
    for name, tc in datasets:
        b += _len_delim(7, _dataset_info(name, tc))  # toc
    return b


def _data_msg(dataset_id, values):
    seed = values[0] if values else 0
    deltas = [values[i + 1] - values[i] for i in range(len(values) - 1)]
    packed = b"".join(_varint(_zigzag(d)) for d in deltas)
    b = _tag(1, 0) + _varint(dataset_id)           # dataset_id
    b += _tag(3, 0) + _varint(_zigzag(seed))       # seed (sint64)
    b += _len_delim(4, packed)                     # integers (packed sint32)
    return b


def _data_info(dataset_id, phys_offset):
    b = _tag(1, 0) + _varint(dataset_id)           # dataset_id
    b += _tag(2, 0) + _varint(phys_offset)         # offset (physical)
    return b


def _index_msg(data_infos, attributes):
    b = b""
    for did, off in data_infos:
        b += _len_delim(1, _data_info(did, off))   # data_info (repeated, field 1)
    for k, v in attributes.items():
        entry = _len_delim(1, k.encode("utf8")) + _len_delim(2, v.encode("utf8"))
        b += _len_delim(2, entry)                   # attributes map (field 2)
    return b


def _epilogue_msg(index_phys_offset):
    b = _tag(1, 0) + _varint(index_phys_offset)    # global_index_offset
    b += _len_delim(2, b"End of D7 Photons Data File")  # signatue
    return b


def _phys_from_logical(L):
    return (L // PAYLOAD) * PAGE + 2 + (L % PAYLOAD)


def write_photons(path, datasets, attributes=None):
    """Write a ".photons" file.

    Parameters
    ----------
    path : str
        Output filename.
    datasets : list of (name, type_code, values)
        Datasets in file order. ``type_code`` follows the D7 TypeCode enum
        (2 = int32, 3 = int64, ...); ``values`` is a list of integers.
    attributes : dict, optional
        String key/value metadata (e.g. ``{"/photons/TacBits": "12"}``).
    """
    attributes = attributes or {}
    toc = [(name, tc) for (name, tc, _) in datasets]

    logical = bytearray()
    logical += _len_delim(1, _header_msg(toc))         # Header FileEntry

    data_info = []
    for did, (name, tc, values) in enumerate(datasets):
        off = len(logical)
        logical += _len_delim(2, _data_msg(did, list(values)))  # Data FileEntry
        data_info.append((did, _phys_from_logical(off)))

    index_off = len(logical)
    index_phys = _phys_from_logical(index_off)
    logical += _len_delim(3, _index_msg(data_info, attributes))  # Index FileEntry
    logical += _len_delim(4, _epilogue_msg(index_phys))          # Epilogue FileEntry

    # Paginate: prepend a 2-byte block header (low 14 bits = payload length) to
    # every 16382-byte logical chunk.
    physical = bytearray()
    pos = 0
    n = len(logical)
    while pos < n:
        chunk = bytes(logical[pos:pos + PAYLOAD])
        physical += struct.pack("<H", len(chunk) & 0x3FFF)
        physical += chunk
        pos += PAYLOAD

    with open(path, "wb") as fh:
        fh.write(physical)
    return path
