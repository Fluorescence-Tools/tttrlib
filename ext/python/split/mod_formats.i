// SPDX-License-Identifier: BSD-3-Clause
// Split Python extension `tttrlib.formats`: the file formats a DataStore or a TTTR
// is read from and written to (CSV, HDF5 tables, .store, .pto, the table
// vocabulary, record streams, the B&H .set sidecar, TIFF arrays).
%module(directors="1", package="tttrlib") formats
#define TTTRLIB_TEMPLATES_IMPORTED
%include "split/common.i"
// The SWIG library pieces (std_vector, numpy.i and its import_array, the
// typemaps) must be *included* here so their runtime fragments are emitted into
// this wrapper; the %template instantiations inside are guarded off and come
// from core through the %import below.
%include "misc_types.i"
%{
#include "TTTR.h"
#include "TTTRMask.h"
#include "Channel.h"
%}
%import "split/mod_core.i"
%pythoncode %{
_tttrlib = _formats
from tttrlib.core import *
%}


// The active global %exception here must be what this fragment inherited in
// the monolith's include order (a directive in an %imported file does not
// carry over); restated verbatim from the fragment that set it there.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}
#ifndef TTTRLIB_WITHOUT_IO_CSV
%include "CsvReader.i"
%include "CsvWriter.i"
#endif
#ifndef TTTRLIB_WITHOUT_IO_HDF5_TABLE
%include "Hdf5Table.i"
#endif
#ifndef TTTRLIB_WITHOUT_IO_STORE
%include "StoreFile.i"
#endif
#ifndef TTTRLIB_WITHOUT_IO_PTO
%include "Pto.i"
#endif
/* One vocabulary for a table in a file, whatever the file is. Must follow
   StoreFile.i, Hdf5Table.i, Csv.i and Pto.i: it dispatches to all four. */
#ifndef TTTRLIB_WITHOUT_IO_TABLE
%include "Table.i"
#endif
/* Decoding a buffer, reading a container in pieces, and the whole B&H
   ".set" sidecar. RecordStream.i must follow Pto.i (and TTTR.i, imported). */
%include "RecordStream.i"
%include "BhSet.i"
#ifndef TTTRLIB_WITHOUT_IO_IMAGE
%include "Tiff.i"
#endif

%include "stdint.i"
