// Minimal MEX API: a real double matrix and the seven calls the source uses.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdarg>
#include <stdexcept>
typedef size_t mwSize;
struct mxArray { mwSize m, n; double* pr; };
#define mxREAL 0
#define MEXFUNCTION_LINKAGE
inline double* mxGetPr(const mxArray* a) { return a->pr; }
inline mwSize mxGetM(const mxArray* a) { return a->m; }
inline mwSize mxGetN(const mxArray* a) { return a->n; }
inline mwSize mxGetNumberOfElements(const mxArray* a) { return a->m * a->n; }
inline mxArray* mxCreateDoubleMatrix(mwSize m, mwSize n, int) {
    mxArray* a = new mxArray; a->m = m; a->n = n; a->pr = (double*)calloc(m * n, sizeof(double)); return a;
}
inline void mexPrintf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
inline void mexErrMsgTxt(const char* msg) { throw std::runtime_error(msg); }
