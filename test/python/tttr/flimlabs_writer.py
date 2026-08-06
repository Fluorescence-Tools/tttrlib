# SPDX-License-Identifier: BSD-3-Clause
"""Minimal writer for FLIM LABS time-tagger ``.bin`` files.

**These files are synthetic.** No FLIM LABS sample data is published anywhere --
the ``.bin`` files come only from the GUI apps driving real hardware, and the
whole org's public repositories contain code and PDFs and nothing else. So this
writer exists to exercise the reader against the *specification*, which is
unambiguous, and it cannot catch a misreading of what the instrument actually
writes. See PRD-012 for the standing request for a real file.

The layout is the one all five FLIM LABS ``.bin`` formats share -- four ASCII
magic bytes, a little-endian ``uint32`` header length, a JSON header, then
fixed-size records -- and the records are those of the two time taggers:

* ``STT1``, 17 bytes: ``<Bdd`` = event, micro time (ns), macro time (ns)
* ``ITT1``,  9 bytes: ``<Bd``  = event, time (ns)

Confirmed against the four published non-time-tagger samples (``SP01``,
``SPF1``, ``IT02``, ``FCS1``), which share the envelope but not the records, and
against the vendor's own reader scripts, which are the source of the record
layout.
"""
import json
import struct

MARKER_FRAME = 70   # 'F'
MARKER_LINE = 76    # 'L'
MARKER_PIXEL = 80   # 'P'


def write_stt1(path, events, channels=(0, 1), laser_period_ns=25.0, extra_header=None):
    """Write a spectroscopy time-tagger file.

    events: iterable of (event_code, micro_time_ns, macro_time_ns). Written in
    the order given -- deliberately, since the real files are *not* time-ordered
    and the reader has to sort.
    """
    header = {
        "channels": list(channels),
        "laser_period_ns": laser_period_ns,
        "bin_width_micros": 100000,
        "acquisition_time_millis": 10000,
        "tau_ns": None,
        "harmonics": 1,
    }
    if extra_header:
        header.update(extra_header)
    _write(path, b"STT1", header,
           b"".join(struct.pack("<Bdd", int(e), float(mi), float(ma))
                    for e, mi, ma in events))


def write_itt1(path, events, channels=(0, 1), laser_period_ns=25.0):
    """Write an intensity-tracing / FCS time-tagger file.

    events: iterable of (event_code, time_ns). No micro time exists in this
    flavour.
    """
    header = {
        "channels": list(channels),
        "laser_period_ns": laser_period_ns,
        "bin_width_micros": 1000,
        "acquisition_time_millis": 3000,
    }
    _write(path, b"ITT1", header,
           b"".join(struct.pack("<Bd", int(e), float(t)) for e, t in events))


def _write(path, magic, header, payload):
    blob = json.dumps(header).encode("utf-8")
    with open(path, "wb") as f:
        f.write(magic)
        f.write(struct.pack("<I", len(blob)))
        f.write(blob)
        f.write(payload)
