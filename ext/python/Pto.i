// SPDX-License-Identifier: BSD-3-Clause
%module tttrlib
%{
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
%typemap(out) std::vector<unsigned char> * {
    $result = PyBytes_FromStringAndSize(
            $1->empty() ? "" : reinterpret_cast<const char*>($1->data()),
            static_cast<Py_ssize_t>($1->size()));
}
%typemap(in) std::vector<unsigned char> * (std::vector<unsigned char> tmp) {
    char* buffer = 0;
    Py_ssize_t length = 0;
    if (PyBytes_AsStringAndSize($input, &buffer, &length) < 0) SWIG_fail;
    tmp.assign(buffer, buffer + length);
    $1 = &tmp;
}
#endif  // SWIGPYTHON

%include "io_pto.h"

%template(PtoObjectVector) std::vector<tttrlib::io::PtoObject>;
%template(PtoTagVector) std::vector<tttrlib::io::PtoTag>;
%template(PtoAnnotationVector) std::vector<tttrlib::io::PtoAnnotation>;
%template(PtoExtentVector) std::vector<tttrlib::io::PtoExtent>;

