// SPDX-License-Identifier: BSD-3-Clause
//
// The whole Becker & Hickl ".set" sidecar (PRD-021, part 3).
//
// `read_bh_set_file` -- the five imaging tags a photon reader needs -- is a
// TTTRHeader method and is wrapped with it. This is the other ~115 parameters,
// which were C++-only: `%include`d in no binding, so no Python, R, Java or
// JavaScript caller could reach even the five.
%{
#include "io_bh_set.h"
%}

%include "std_string.i"
%include "std_vector.i"

%include "io_bh_set.h"

%template(BhSetParameterVector) std::vector<tttrlib::io::BhSetParameter>;

#ifdef SWIGPYTHON
%pythoncode %{
def bh_set(filename=None, content=None):
    """The whole ``.set`` sidecar of a Becker & Hickl ``.spc``, as a dict.

    ``{section: {name: value}}``, e.g.::

        s = tttrlib.bh_set("FocalCheck_m1.set")
        s["SYS_PARA"]["SP_TAC_R"]      # '6.554e-08'
        s["IDENTIFICATION"]["Title"]

    Values are the text the file holds, not numbers. A ``.set`` is a device
    configuration file and its types are per-parameter; a parser that guesses
    is a parser that is wrong about one field in a hundred and silent about
    it. The type letter the file declares is available from the flat form,
    :func:`read_set_file`.

    Later parameters of the same name in the same section win, which only
    happens in the indexed trace and window blocks.

    :param filename: read this file.
    :param content: parse these bytes instead, for a sidecar that came out of
        a container rather than off a disk.
    """
    if (filename is None) == (content is None):
        raise ValueError("pass exactly one of filename, content")
    if content is None:
        flat = read_set_file(filename)
    else:
        if isinstance(content, bytes):
            content = content.decode("latin-1")
        flat = parse_set(content)
    out = {}
    for p in flat:
        out.setdefault(p.section, {})[p.name] = p.value
    return out
%}
#endif  // SWIGPYTHON
