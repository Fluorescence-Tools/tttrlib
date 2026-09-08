// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "PhotonSink.h"
#include "RecordStreamWriter.h"
#include "TTTRStreamWriter.h"
#include "io_pto.h"
%}

%include "std_string.i"
%include "std_vector.i"

// The container -- pto::File, the value types, the store embedding -- is
// ptolib's and is wrapped once by Ptolib.i, together with the byte and uid
// typemaps that used to sit here. What follows is tttrlib's: the stream-writer
// hierarchy, and PtoFile (pto::File plus the photon-aware members).
%include "Ptolib.i"

// The abstract stream writer, before the PTO implementation that derives from
// it: without this SWIG sees an unknown base class, silently drops it, and the
// inherited half of the API (error, set_auto_checkpoint, the counters) is
// missing from every binding.
// TTTRStreamWriter::append takes the four event arrays the way TTTR's
// constructor does, so it accepts the same NumPy arrays. These MUST precede
// the %include that declares append(): a typemap applied afterwards does not
// reach the already-parsed declaration, and the binding then demands the
// lengths as separate arguments.
%apply (unsigned long long* IN_ARRAY1, int DIM1)
       {(const unsigned long long* macro_times, std::size_t n_macro)}
%apply (unsigned short* IN_ARRAY1, int DIM1)
       {(const unsigned short* micro_times, std::size_t n_micro)}
%apply (signed char* IN_ARRAY1, int DIM1)
       {(const signed char* routing_channels, std::size_t n_routing)}
%apply (signed char* IN_ARRAY1, int DIM1)
       {(const signed char* event_types, std::size_t n_event)}

// The sink interface first: TTTRStreamWriter derives from PhotonSink, and a
// base SWIG was not shown is silently dropped along with everything it carries.
// A unique_ptr by value cannot cross a binding -- SWIG generates a copy and
// the copy constructor is deleted. Ownership from a scripting language is the
// language's job anyway: a Python caller holds its correlator to read the
// result out of it, which is add_sink's contract, not this one's.
%ignore tttrlib::io::PhotonStreamHub::add_owned_sink;

#ifdef SWIGPYTHON
// A director calls BACK into Python, and without these the four columns arrive
// as opaque SWIG pointers -- a Python consumer could count events and nothing
// else, which makes the whole sink interface decorative. The arrays are views
// onto the C++ buffers, valid for the call only, exactly as the C++ contract
// says; a consumer that keeps them must copy.
%typemap(directorin) (const std::uint64_t* macro_times) {
    npy_intp d[1] = { (npy_intp) n };
    $input = PyArray_SimpleNewFromData(1, d, NPY_UINT64, (void*) $1_name);
}
%typemap(directorin) (const std::uint16_t* micro_times) {
    npy_intp d[1] = { (npy_intp) n };
    $input = PyArray_SimpleNewFromData(1, d, NPY_UINT16, (void*) $1_name);
}
%typemap(directorin) (const std::int8_t* routing_channels) {
    npy_intp d[1] = { (npy_intp) n };
    $input = PyArray_SimpleNewFromData(1, d, NPY_INT8, (void*) $1_name);
}
%typemap(directorin) (const std::int8_t* event_types) {
    npy_intp d[1] = { (npy_intp) n };
    $input = PyArray_SimpleNewFromData(1, d, NPY_INT8, (void*) $1_name);
}
#endif  // SWIGPYTHON

%feature("director") tttrlib::io::PhotonSink;
%include "PhotonSink.h"

// A unique_ptr return cannot be owned by a binding -- SWIG wraps the smart
// pointer itself and then has no destructor for it. The class is exposed
// directly instead, which is what a caller wants anyway: it knows the format
// it is acquiring into.
//
// Both factories are declared by TTTRStreamWriter.h, so the %ignore has to
// come before that %include: SWIG applies it when it parses the declaration,
// and afterwards is too late -- the wrappers were generated already and MSVC
// rejects them (C2280, the deleted unique_ptr copy constructor).
%ignore tttrlib::io::make_stream_writer;
%ignore tttrlib::io::make_stream_writer_for;
%include "TTTRStreamWriter.h"

%include "RecordStreamWriter.h"


%include "io_pto.h"

// A payload crosses as bytes: Python has the typemaps above, JavaScript gets a
// Uint8Array from jsarrays.i, Java and R the VectorUint8 proxy that
// misc_types.i now instantiates for every language (it used to live in Sim.i,
// which neither of them includes, with a Java/R-only copy here).

#ifdef SWIGPYTHON
%pythoncode %{
def pto_store(file, uid, columns=None, first_row=0, n_rows=0):
    """Read an embedded ``dstore`` object back as a DataStore.

    The container's whole point on the read side: a store inside a ``.pto`` is
    a region of a bigger file, and its directory says where every column and
    every row of every column is. Asking for two columns of a four-gigabyte
    table costs two seeks, and asking for fifty rows of a million-row burst
    table costs fifty rows.

    :param columns: read only these columns, if given. Names not in the file
        are skipped silently; the group tree comes back whole either way,
        being the directory.
    :param first_row: skip this many rows of every table in the tree.
    :param n_rows: how many rows to read, or 0 for all of them from
        ``first_row`` on.
    :raises RuntimeError: if there is no such object, or it is not a store.
    """
    store = DataStore()
    names = VectorString(list(columns) if columns is not None else [])
    if first_row or n_rows:
        pto_read_store(file, uid, store, names, first_row, n_rows)
    elif columns is not None:
        pto_read_store(file, uid, store, names)
    else:
        pto_read_store(file, uid, store)
    return store


def pto_bundle(file, paths, link_sidecars=True):
    """Bundle files and directories into an open container, one object each.

    :func:`pto_bundle_files` taking whatever names a path in Python -- a
    :class:`pathlib.Path`, a string, one of either, or a list::

        f = tttrlib.PtoFile()
        f.create("run.pto", "DNA ruler, run 4")
        tttrlib.pto_bundle(f, "measurement/")   # everything under it
        f.commit()

    A directory is bundled recursively and each object is named by its path
    relative to it, so :meth:`PtoFile.disassemble` puts the directory back as
    it was. A ``.set`` beside a ``.spc`` is tied to it, which is what makes the
    pair readable afterwards.

    Nothing is committed: the container becomes visible when you say so.

    :param link_sidecars: False to bundle a ``.set`` as a plain object.
    :returns: the objects made, in the order they were written.
    """
    from os import fspath
    if isinstance(paths, (str, bytes)) or hasattr(paths, "__fspath__"):
        paths = [paths]
    return pto_bundle_files(file, VectorString([fspath(p) for p in paths]),
                            link_sidecars)


def pto_events(spec, first_event=0, n_events=0):
    """Read a range of events out of a photon object in a container.

    ``spec`` is a container path, optionally naming an object after a ``|``::

        tttrlib.pto_events("run.pto|m001.ptu", 1_000_000, 1000)

    With cues built over the object (:meth:`PtoFile.build_cues`) the decode
    starts at the nearest cue at or before ``first_event``; without them the
    payload is decoded whole and sliced, which is correct and no faster than
    opening all of it.
    """
    out = TTTR()
    if not pto_read_events(spec, first_event, n_events, out):
        raise RuntimeError("could not read events from " + str(spec))
    return out
%}
#endif  // SWIGPYTHON

