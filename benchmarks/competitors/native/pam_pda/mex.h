// Minimal MEX API for PAM's PDA_histogram.cpp (the shim test/python/pda/test_ab_pda_reference.py uses).
#pragma once
#include <cstdlib>
#include <cstdio>
#include <cstddef>
typedef size_t mwSize;
struct mxArray { double* data; size_t n; };
enum { mxDOUBLE_CLASS = 6 };
enum { mxREAL = 0 };
inline double mxGetScalar(const mxArray* a) { return a->data[0]; }
inline double* mxGetPr(const mxArray* a) { return a->data; }
inline void* mxCalloc(size_t n, size_t s) { return calloc(n, s); }
inline mxArray* mxCreateNumericMatrix(mwSize m, mwSize n, int, int) { mxArray* a = new mxArray; a->data = nullptr; a->n = m * n; return a; }
inline void mxSetData(mxArray* a, void* p) { a->data = static_cast<double*>(p); }
inline void mexErrMsgIdAndTxt(const char*, const char* m) { std::fprintf(stderr, "%s\n", m); std::exit(2); }
