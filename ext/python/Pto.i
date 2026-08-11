// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
#include "TTTRStreamWriter.h"
#include "io_pto.h"
%}

%include "std_string.i"
%include "std_vector.i"

// A payload is bytes on both sides. Without these, `add` wants a SWIG pointer
// and `read` hands back an opaque vector proxy -- neither of which is what a
// caller has or wants when the thing in question is a photon stream.
#ifdef SWIGPYTHON
%typemap(in) (const unsigned char* data, std::size_t n) {
    char* buffer = 0;
    Py_ssize_t length = 0;
    if (PyBytes_AsStringAndSize($input, &buffer, &length) < 0) SWIG_fail;
    // $1_ltype rather than the written type: SWIG strips the const from the
    // parameter it declares, so a plain cast to `const unsigned char*` fails.
    $1 = ($1_ltype) buffer;
    $2 = ($2_ltype) length;
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING)
        (const unsigned char* data, std::size_t n) {
    $1 = PyBytes_Check($input) ? 1 : 0;
}
%typemap(out) std::vector<unsigned char> {
    $result = PyBytes_FromStringAndSize(
            $1.empty() ? "" : reinterpret_cast<const char*>($1.data()),
            static_cast<Py_ssize_t>($1.size()));
}
// PtoTag::bytes is a member, and SWIG's generated getter and setter deal in a
// pointer to the member rather than a value, so the pair above is not enough.
//
// Matched on the member's NAME as well as its type. An unqualified
// `std::vector<unsigned char>*` typemap is global from here on and lands on the
// `self` argument of every method of VectorUint8 -- which Sim.i instantiates
// further down tttrlib.i, so `len(sidecar.states)` came back as
// "expected bytes, VectorUint8 found" from a file that has nothing to do with
// PTO. Do not widen these two.
%typemap(out) std::vector<unsigned char> * bytes {
    $result = PyBytes_FromStringAndSize(
            $1->empty() ? "" : reinterpret_cast<const char*>($1->data()),
            static_cast<Py_ssize_t>($1->size()));
}
%typemap(in) std::vector<unsigned char> * bytes (std::vector<unsigned char> tmp) {
    char* buffer = 0;
    Py_ssize_t length = 0;
    if (PyBytes_AsStringAndSize($input, &buffer, &length) < 0) SWIG_fail;
    tmp.assign(buffer, buffer + length);
    $1 = &tmp;
}
#endif  // SWIGPYTHON

// ── A uid crosses to R as a STRING ─────────────────────────────────────────
//
// R has no 64-bit integer type: `integer` is 32-bit and `numeric` is an IEEE
// double, so a uid is stored with 53 bits of mantissa and comes back wrong --
//
//     written   14523661926200792394
//     read back 14523661926200793088     (every 2048th integer, at this size)
//
// and the lookup then fails with "no object with that uid". A 64-bit float is
// the widest numeric R has, and it is still not wide enough; three consecutive
// uids collapse onto the same double.
//
// A character string is the only thing base R holds exactly without a new
// dependency (bit64 would be the other answer). So in R a uid IS a string:
// produced as one, accepted as one, never arithmetic. Every other language
// keeps its integer -- JavaScript gained BigInt scalars, and Python and Java
// were always exact.
//
// Scoped to the IDENTIFIERS only. rows, offset, size and capacity are
// magnitudes that stay far below 2^53, and a double carries them fine; turning
// those into strings would make the API awkward for no gain -- and would break
// the conformance cases, which compare them as numbers.
#ifdef SWIGR
%typemap(out) std::uint64_t pto_add_store, std::uint64_t PtoFile::add_file,
              std::uint64_t PtoFile::attach,
              std::uint64_t PtoObject::uid, std::uint64_t PtoAnnotation::target,
              std::uint64_t PtoExtent::target %{
  {
    char buf_[24];
    std::snprintf(buf_, sizeof(buf_), "%llu", (unsigned long long) $1);
    $result = Rf_mkString(buf_);
  }
%}

// Accepts the string it produced, and a number too -- a small uid typed by
// hand should not need quoting, and anything a double can hold exactly is
// unambiguous.
%typemap(in) std::uint64_t uid, std::uint64_t primary, std::uint64_t target %{
  if (TYPEOF($input) == STRSXP && Rf_length($input) == 1) {
    $1 = (std::uint64_t) strtoull(CHAR(STRING_ELT($input, 0)), NULL, 10);
  } else {
    $1 = (std::uint64_t) Rf_asReal($input);
  }
%}
%typemap(scoercein) std::uint64_t uid, std::uint64_t primary,
                    std::uint64_t target %{ %}

// The overload dispatcher has to accept the string too, or every candidate
// fails its is.numeric() test and R reports "cannot find overloaded function".
%typemap("rtypecheck") std::uint64_t uid, std::uint64_t primary,
                       std::uint64_t target
  %{ (is.character($arg) || is.numeric($arg)) && length($arg) == 1 %}
#endif  // SWIGR

// The abstract stream writer, before the PTO implementation that derives from
// it: without this SWIG sees an unknown base class, silently drops it, and the
// inherited half of the API (error, set_auto_checkpoint, the counters) is
// missing from every binding.
%include "TTTRStreamWriter.h"

// PtoPhotonStream::append takes the four event arrays the way TTTR's
// constructor does, so it accepts the same NumPy arrays with no copy.
%apply (unsigned long long* IN_ARRAY1, int DIM1)
       {(const unsigned long long* macro_times, std::size_t n_macro)}
%apply (unsigned short* IN_ARRAY1, int DIM1)
       {(const unsigned short* micro_times, std::size_t n_micro)}
%apply (signed char* IN_ARRAY1, int DIM1)
       {(const signed char* routing_channels, std::size_t n_routing)}
%apply (signed char* IN_ARRAY1, int DIM1)
       {(const signed char* event_types, std::size_t n_event)}

%include "io_pto.h"

%template(PtoObjectVector) std::vector<tttrlib::io::PtoObject>;
%template(PtoTagVector) std::vector<tttrlib::io::PtoTag>;
%template(PtoAnnotationVector) std::vector<tttrlib::io::PtoAnnotation>;
%template(PtoExtentVector) std::vector<tttrlib::io::PtoExtent>;
%template(PtoCueVector) std::vector<tttrlib::io::PtoCue>;

// A payload crosses as bytes: Python has the typemaps above, JavaScript gets a
// Uint8Array from jsarrays.i. Java and R have neither, and wrap nothing else
// that instantiates this -- Sim.i does, and neither of them includes it -- so
// without this `read` hands them an opaque proxy of the payload they asked for.
#if defined(SWIGJAVA) || defined(SWIGR)
%template(VectorUint8) std::vector<unsigned char>;
#endif

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

