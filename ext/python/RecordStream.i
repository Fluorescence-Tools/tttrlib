// SPDX-License-Identifier: BSD-3-Clause
//
// Decoding a buffer of records, and reading a container in pieces.
//
// Must come AFTER TTTR.i (the buffer is decoded INTO a TTTR) and after Pto.i,
// which is where the `std::vector<unsigned char>` marshalling lives for Python,
// Java and R -- without it `container_read_records` hands back an opaque proxy
// of the bytes it was asked for.
%{
#include "TTTRStream.h"
%}

%include "std_string.i"
%include "std_vector.i"

// The buffer typemap and the GIL release for TTTR::decode_records are in
// TTTR.i: the method is declared in TTTR.h, and a %apply after that header is
// parsed applies to nothing.

// Reading a range of a container is file I/O and touches no Python objects.
TTTRLIB_NOGIL(tttrlib::container_read_records)
TTTRLIB_NOGIL(tttrlib::container_read_events)

// No %exception here. The range functions refuse a container that cannot be
// read in pieces by throwing std::invalid_argument, and the GLOBAL handler
// MicrotimeLinearization.i installs already turns that into a ValueError.
//
// Adding a duplicate would be harmless; ending it with `%exception;` is not.
// That clears the handler for every interface file included AFTERWARDS, and
// this one sits before Correlator.i, CLSM.i, Pda.i, DecayFit.i and Sim.i --
// doing it aborted the interpreter in CLSMSuperRes, which is the third time
// this trap has been sprung (see the notes in HistogramNd.i and
// BurstFeatureExtractor.i).
%include "TTTRStream.h"

#ifdef SWIGPYTHON
%pythoncode %{
import numpy as _np


def decode_records(buffer, record_type, state=None, tttr=None):
    """Decode a buffer of undecoded records into a :class:`TTTR`.

    The entry point for records that are not in a file -- read off a card,
    arrived over a socket, or lifted out of a container you unpacked
    yourself::

        state = tttrlib.TTTRDecodeState()
        data = tttrlib.TTTR()
        for chunk in card_chunks:
            tttrlib.decode_records(chunk, tttrlib.BH_RECORD_TYPE_SPC130,
                                   state, data)

    ``state`` carries the macro time overflow count across the chunk
    boundaries. Leaving it out decodes ``buffer`` as a stream of its own,
    which is right for a single buffer and wrong for the second chunk of one.

    :param buffer: any array of records. A ``uint32`` array is viewed as
        bytes, so both the natural form of a 32-bit record format and a raw
        byte buffer work.
    :param record_type: one of the ``*_RECORD_TYPE_*`` constants.
    :param state: a :class:`TTTRDecodeState`, created if not given.
    :param tttr: append to this object rather than a new one.
    :returns: ``(tttr, state)``.
    :raises RuntimeError: if the record type cannot be decoded from a buffer
        alone -- the message names it.
    """
    if state is None:
        state = TTTRDecodeState()
    if tttr is None:
        tttr = TTTR()
    raw = _np.ascontiguousarray(buffer)
    if raw.dtype != _np.uint8:
        raw = raw.view(_np.uint8)
    tttr.decode_records(raw, int(record_type), state)
    return tttr, state


def container_chunks(spec, chunk=1 << 20, container_type=-1):
    """Read a container's records in pieces, decoding each into one TTTR.

    The composition this module exists for, written out once::

        for data, done, total in tttrlib.container_chunks("run.ptu"):
            progress(done / total)
        # `data` is the same object each time, grown; after the last
        # iteration it equals tttrlib.TTTR("run.ptu") event for event.

    :param chunk: records per step.
    :yields: ``(tttr, records_done, records_total)``.
    """
    info = container_records(spec, container_type)
    if not info.ranged:
        raise RuntimeError(info.reason)
    data = TTTR()
    state = TTTRDecodeState()
    at = 0
    while at < info.n_records:
        raw = container_read_records(spec, at, chunk, info.container_type)
        if not len(raw):
            break
        decode_records(_np.frombuffer(raw, dtype=_np.uint8),
                       info.record_type, state, data)
        at += len(raw) // info.bytes_per_record
        yield data, at, info.n_records
    # Two containers keep part of the routing channel in the header rather than
    # in the records; a no-op for the other five.
    data.set_header(TTTRHeader(str(spec).split("|")[0], info.container_type))
    data.apply_container_channels(info.container_type)
    data.find_used_routing_channels()
    # Appending grew the store by a factor, so it holds more rows than events.
    # Once, at the end -- per chunk is what the growth factor exists to avoid.
    data.shrink_to_fit()


def container_events(spec, first_record=0, n_records=0, container_type=-1):
    """Read a range of records out of a container, decoded.

    .. warning::
       Macro times count from ``first_record`` rather than from the start of
       the file, unless it is 0. The overflow count at that record is not in
       the records, and finding it means reading everything before it -- the
       cost a ranged read exists to avoid. For absolute times over a whole
       file, read it in chunks from 0 with :func:`decode_records` and one
       carried state.
    """
    out = TTTR()
    if not container_read_events(spec, first_record, n_records, out,
                                 container_type):
        raise RuntimeError("could not read records from " + str(spec))
    return out
%}
#endif  // SWIGPYTHON
