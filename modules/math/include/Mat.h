// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_MAT_H
#define TTTRLIB_MAT_H

// Validation: A/B-TESTED 2026-08-17 -- vs numpy.linalg solve/inv/lstsq(rcond)/matrix_power and `@` (1e-9*cond; minimum-
//   norm and rank-deficient cases; scaled-singular flagged). test/python/misc/test_math_ab_numerics.py.
//   Register: okf/testing/math-kernel-validation.md

// Mat.h -- a standalone, dependency-free dense matrix library.
//
// Design goals, in priority order:
//
//   1. No external dependency.  std-only C++17, plus OpenMP when the compiler
//      offers it.  Replaces Eigen inside tttrlib's modules so that the neural
//      net (and anything else that needs a GEMM) builds without a third-party
//      linear-algebra package.
//
//   2. Armadillo-flavoured syntax.  ``Mat A(3, 4, fill::zeros); Mat C = A *
//      B.t(); A.each_row() -= mu; double s = accu(square(C));`` read like
//      Armadillo and compose the same way.
//
//   3. Competitive GEMM.  The matrix product is the only O(n^3) kernel; it
//      gets cache-friendly ikj loop nesting, compiler auto-vectorisation
//      hints (``#pragma omp simd``), and OpenMP thread parallelism.  For the
//      matrix sizes inside the neural net (hidden layers up to 256) this
//      lands within striking distance of a hand-tuned BLAS, which is all the
//      application needs.
//
// Storage is row-major throughout, matching NumPy C-contiguous arrays and the
// rest of tttrlib.  Transposes are *not* expression-template lazy views with
// clever lifetime management; ``.t()`` on a matrix returns a lightweight
// ``MatTrans`` proxy (pointer + dimension swap, zero copy) that the three
// ``operator*`` overloads (NN, NT, TN) recognise and dispatch into the matching
// GEMM variant.  The proxy is safe inside one full expression; storing it
// beyond the ``;`` is a dangling-reference trap shared with every expression-
// template library.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// SIMD headers must be included at global scope, BEFORE any namespace, so
// their typedefs (float64x2_t, __m128d, __m256d, ...) land in the global
// namespace where compiler intrinsics and third-party headers expect them.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  #include <arm_neon.h>
#elif defined(__AVX__) || defined(__AVX2__)
  #include <immintrin.h>
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
  #include <emmintrin.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

namespace tttrlib {

// ---------------------------------------------------------------------------
// Fill-type tags (Armadillo's fill::zeros / fill::ones pattern)
// ---------------------------------------------------------------------------
struct fill_zeros_t {};
struct fill_ones_t {};
inline constexpr fill_zeros_t fill_zeros{};
inline constexpr fill_ones_t fill_ones{};

// ---------------------------------------------------------------------------
// Mat -- owning row-major dense matrix
// ---------------------------------------------------------------------------
struct MatTrans;

class Mat {
public:
    Mat() = default;

    Mat(int n_rows, int n_cols)
        : n_rows_(n_rows), n_cols_(n_cols),
          data_(static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols)) {
        if (n_rows < 0 || n_cols < 0)
            throw std::invalid_argument("Mat: dimensions must be non-negative");
    }

    Mat(int n_rows, int n_cols, double v)
        : n_rows_(n_rows), n_cols_(n_cols),
          data_(static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols), v) {
        if (n_rows < 0 || n_cols < 0)
            throw std::invalid_argument("Mat: dimensions must be non-negative");
    }

    Mat(int n_rows, int n_cols, fill_zeros_t)
        : n_rows_(n_rows), n_cols_(n_cols),
          data_(static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols), 0.0) {}

    Mat(int n_rows, int n_cols, fill_ones_t)
        : n_rows_(n_rows), n_cols_(n_cols),
          data_(static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols), 1.0) {}

    /// Copy ``n_rows * n_cols`` values from ``src`` (row-major) into a new
    /// owning matrix.  This is the zero-copy-to-owning bridge for data that
    /// arrives as a bare ``const double*`` from NumPy, SWIG, or tttrlib's own
    /// flat arrays.
    Mat(int n_rows, int n_cols, const double* src)
        : n_rows_(n_rows), n_cols_(n_cols),
          data_(src, src + static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols)) {}

    // -- properties (Armadillo names) --------------------------------------
    int n_rows() const { return n_rows_; }
    int n_cols() const { return n_cols_; }
    int n_elem() const { return static_cast<int>(data_.size()); }
    bool is_empty() const { return data_.empty(); }

    // -- element access -----------------------------------------------------
    double& operator()(int row, int col) {
        return data_[static_cast<size_t>(row) * static_cast<size_t>(n_cols_) + col];
    }
    double operator()(int row, int col) const {
        return data_[static_cast<size_t>(row) * static_cast<size_t>(n_cols_) + col];
    }
    /// Linear (row-major) index.
    double& at(int i) { return data_[i]; }
    double at(int i) const { return data_[i]; }

    // -- raw pointer --------------------------------------------------------
    double* memptr() { return data_.data(); }
    const double* memptr() const { return data_.data(); }

    // -- size mutation ------------------------------------------------------
    void set_size(int n_rows, int n_cols) {
        if (n_rows < 0 || n_cols < 0)
            throw std::invalid_argument("Mat::set_size: dimensions must be non-negative");
        n_rows_ = n_rows;
        n_cols_ = n_cols;
        data_.assign(static_cast<size_t>(n_rows) * static_cast<size_t>(n_cols), 0.0);
    }
    void resize(int n_rows, int n_cols) { set_size(n_rows, n_cols); }

    // -- fill ---------------------------------------------------------------
    void fill(double v) { std::fill(data_.begin(), data_.end(), v); }
    void zeros() { fill(0.0); }
    void ones() { fill(1.0); }

    // -- transpose (returns a zero-copy proxy; see MatTrans below) ----------
    MatTrans t() const;

    // -- submatrix extraction (returns owning copies) -----------------------
    Mat row(int r) const {
        if (r < 0 || r >= n_rows_)
            throw std::out_of_range("Mat::row: index " + std::to_string(r) +
                                    " out of range [0, " + std::to_string(n_rows_) + ")");
        Mat m(1, n_cols_);
        const double* src = data_.data() + static_cast<size_t>(r) * n_cols_;
        std::copy(src, src + n_cols_, m.data_.data());
        return m;
    }
    Mat col(int c) const {
        if (c < 0 || c >= n_cols_)
            throw std::out_of_range("Mat::col: index out of range");
        Mat m(n_rows_, 1);
        for (int i = 0; i < n_rows_; ++i)
            m.data_[i] = data_[static_cast<size_t>(i) * n_cols_ + c];
        return m;
    }

    // -- compound assignment: scalar ----------------------------------------
    Mat& operator+=(double s) { for (auto& x : data_) x += s; return *this; }
    Mat& operator-=(double s) { for (auto& x : data_) x -= s; return *this; }
    Mat& operator*=(double s) { for (auto& x : data_) x *= s; return *this; }
    Mat& operator/=(double s) { for (auto& x : data_) x /= s; return *this; }

    // -- compound assignment: element-wise matrix ---------------------------
    Mat& operator+=(const Mat& o) {
        check_same_size(o, "+=");
        for (size_t i = 0; i < data_.size(); ++i) data_[i] += o.data_[i];
        return *this;
    }
    Mat& operator-=(const Mat& o) {
        check_same_size(o, "-=");
        for (size_t i = 0; i < data_.size(); ++i) data_[i] -= o.data_[i];
        return *this;
    }
    /// Schur (element-wise) product.
    Mat& operator%=(const Mat& o) {
        check_same_size(o, "%=");
        for (size_t i = 0; i < data_.size(); ++i) data_[i] *= o.data_[i];
        return *this;
    }
    /// Element-wise division.
    Mat& operator/=(const Mat& o) {
        check_same_size(o, "/=");
        for (size_t i = 0; i < data_.size(); ++i) data_[i] /= o.data_[i];
        return *this;
    }

    // -- each_row / each_col broadcasting proxies ---------------------------
    // Definitions appear after the Mat class (they reference Mat by &).
    struct each_row_proxy;
    struct each_col_proxy;

    each_row_proxy each_row();
    each_col_proxy each_col();

private:
    int n_rows_ = 0;
    int n_cols_ = 0;
    std::vector<double> data_;

    void check_same_size(const Mat& o, const char* op) const {
        if (n_rows_ != o.n_rows_ || n_cols_ != o.n_cols_)
            throw std::invalid_argument(
                std::string("Mat::operator") + op + ": size mismatch (" +
                std::to_string(n_rows_) + "x" + std::to_string(n_cols_) + " vs " +
                std::to_string(o.n_rows_) + "x" + std::to_string(o.n_cols_) + ")");
    }
};

// ---------------------------------------------------------------------------
// MatTrans -- zero-copy transpose proxy
// ---------------------------------------------------------------------------
// Returned by ``Mat::t()``.  The three ``operator*`` overloads below recognise
// it and dispatch into the matching GEMM variant so no data is copied.
//
// Lifetime: valid for the duration of the full expression that created it.
// Do not store in a named variable beyond the ``;`` — the same trap as every
// expression-template library.
struct MatTrans {
    const Mat* m;

    explicit MatTrans(const Mat& mat) : m(&mat) {}

    // Logical (transposed) dimensions
    int n_rows() const { return m->n_cols(); }
    int n_cols() const { return m->n_rows(); }
    // Physical storage dimensions (the underlying matrix, row-major)
    int phys_rows() const { return m->n_rows(); }
    int phys_cols() const { return m->n_cols(); }
    const double* memptr() const { return m->memptr(); }
    int n_elem() const { return m->n_elem(); }

    /// Materialise the transpose into an owning matrix.
    operator Mat() const {
        Mat r(n_rows(), n_cols());
        for (int i = 0; i < phys_rows(); ++i)
            for (int j = 0; j < phys_cols(); ++j)
                r(j, i) = (*m)(i, j);
        return r;
    }
};

inline MatTrans Mat::t() const { return MatTrans(*this); }

// ---------------------------------------------------------------------------
// each_row / each_col broadcasting proxies
// ---------------------------------------------------------------------------
// ``A.each_row() -= v`` subtracts the row vector ``v`` (1 x n_cols) from every
// row of ``A`` in place.  ``each_col`` is the column-wise analogue.  Only the
// compound-assignment forms are provided; they are what the broadcasting
// pattern needs and they avoid the ambiguity of a read proxy that is also the
// write target.
struct Mat::each_row_proxy {
    Mat& m;
    explicit each_row_proxy(Mat& mat) : m(mat) {}

    /// ``v`` must be 1 x n_cols (or n_elem == n_cols).
    template <typename V>
    each_row_proxy& operator-=(const V& v) {
        broadcast_row(m, v, [](double& a, double b) { a -= b; });
        return *this;
    }
    template <typename V>
    each_row_proxy& operator+=(const V& v) {
        broadcast_row(m, v, [](double& a, double b) { a += b; });
        return *this;
    }
    template <typename V>
    each_row_proxy& operator%=(const V& v) {
        broadcast_row(m, v, [](double& a, double b) { a *= b; });
        return *this;
    }
    template <typename V>
    each_row_proxy& operator/=(const V& v) {
        broadcast_row(m, v, [](double& a, double b) { a /= b; });
        return *this;
    }

private:
    template <typename V, typename Op>
    static void broadcast_row(Mat& m, const V& v, Op op) {
        const int nr = m.n_rows(), nc = m.n_cols();
        if (v.n_elem() != nc)
            throw std::invalid_argument(
                "each_row: vector length " + std::to_string(v.n_elem()) +
                " != n_cols " + std::to_string(nc));
        double* d = m.memptr();
        const double* vp = v.memptr();
        for (int i = 0; i < nr; ++i) {
            double* row = d + static_cast<size_t>(i) * nc;
            for (int j = 0; j < nc; ++j) op(row[j], vp[j]);
        }
    }
};

struct Mat::each_col_proxy {
    Mat& m;
    explicit each_col_proxy(Mat& mat) : m(mat) {}

    /// ``v`` must be n_rows x 1 (or n_elem == n_rows).
    template <typename V>
    each_col_proxy& operator-=(const V& v) {
        broadcast_col(m, v, [](double& a, double b) { a -= b; });
        return *this;
    }
    template <typename V>
    each_col_proxy& operator+=(const V& v) {
        broadcast_col(m, v, [](double& a, double b) { a += b; });
        return *this;
    }
    template <typename V>
    each_col_proxy& operator%=(const V& v) {
        broadcast_col(m, v, [](double& a, double b) { a *= b; });
        return *this;
    }
    template <typename V>
    each_col_proxy& operator/=(const V& v) {
        broadcast_col(m, v, [](double& a, double b) { a /= b; });
        return *this;
    }

private:
    template <typename V, typename Op>
    static void broadcast_col(Mat& m, const V& v, Op op) {
        const int nr = m.n_rows(), nc = m.n_cols();
        if (v.n_elem() != nr)
            throw std::invalid_argument(
                "each_col: vector length " + std::to_string(v.n_elem()) +
                " != n_rows " + std::to_string(nr));
        double* d = m.memptr();
        const double* vp = v.memptr();
        for (int i = 0; i < nr; ++i) {
            double vi = vp[i];
            double* row = d + static_cast<size_t>(i) * nc;
            for (int j = 0; j < nc; ++j) op(row[j], vi);
        }
    }
};

// -- deferred Mat method definitions (proxy types are now complete) ----------
inline Mat::each_row_proxy Mat::each_row() { return each_row_proxy(*this); }
inline Mat::each_col_proxy Mat::each_col() { return each_col_proxy(*this); }

// ===========================================================================
// SIMD back-end and GEMM kernels
// ===========================================================================
// High-performance matrix multiply without BLAS.  The approach follows the
// GotoBLAS / Eigen recipe adapted for a header-only library:
//
//   1. **Panel packing.**  For the NT and TN variants the B (or A) operand
//      has strided memory access — the #1 performance killer.  We pack it
//      into a contiguous buffer so the inner kernel always streams B
//      sequentially.
//
//   2. **Register blocking (MR).**  The inner NN kernel processes MR rows of
//      A simultaneously, so each B row is loaded from L1 once and reused MR
//      times — a 4× reduction in B traffic.
//
//   3. **Explicit SIMD SAXPY.**  The rank-1 update ``y += a * x`` is written
//      with NEON / SSE / AVX intrinsics directly, not left to auto-
//      vectorisation which silently no-ops when aliasing can't be disproved.
//
//   4. **``__restrict``** on every pointer the compiler can't otherwise prove
//      non-aliasing.

// --- restrict keyword ---
#if defined(_MSC_VER)
  #define TTTRLIB_RESTRICT __restrict
#else
  #define TTTRLIB_RESTRICT __restrict__
#endif

// --- loop vectorisation hints (for element-wise ops, kept simple) ---
// `_Pragma` takes a string literal, and a macro parameter is NOT substituted
// inside one — `_Pragma("omp simd reduction(+:var)")` emits a pragma naming a
// variable literally called `var`, which every reduction site then fails to
// compile ("use of undeclared identifier 'var'"). Stringify through a helper
// so the caller's name reaches the pragma.
#define TTTRLIB_PRAGMA(x) _Pragma(#x)
#if defined(_OPENMP) && !defined(_MSC_VER)
  #define TTTRLIB_VEC _Pragma("omp simd")
  #define TTTRLIB_VEC_REDUCTION(var) TTTRLIB_PRAGMA(omp simd reduction(+ : var))
#elif defined(__clang__)
  #define TTTRLIB_VEC _Pragma("clang loop vectorize(enable) interleave(enable)")
  #define TTTRLIB_VEC_REDUCTION(var)
#elif defined(__GNUC__)
  #define TTTRLIB_VEC _Pragma("GCC ivdep")
  #define TTTRLIB_VEC_REDUCTION(var)
#else
  #define TTTRLIB_VEC
  #define TTTRLIB_VEC_REDUCTION(var)
#endif

namespace mat_detail {

// --- SIMD vector abstraction ----------------------------------------------
// One struct per ISA with static wrappers around the intrinsic types.  The
// GEMM kernels call only these wrappers, so adding a new ISA is one struct.

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  #define TTTRLIB_SIMD_DBL 2
  struct simd_t {
      using type = float64x2_t;
      static type zero() { return vdupq_n_f64(0.0); }
      static type set1(double x) { return vdupq_n_f64(x); }
      static type load(const double* p) { return vld1q_f64(p); }
      static void store(double* p, type v) { vst1q_f64(p, v); }
      static type fma(type a, type b, type c) { return vfmaq_f64(c, a, b); }
      static double sum(type v) { return vaddvq_f64(v); }
  };
#elif defined(__AVX__) || defined(__AVX2__)
  #define TTTRLIB_SIMD_DBL 4
  struct simd_t {
      using type = __m256d;
      static type zero() { return _mm256_setzero_pd(); }
      static type set1(double x) { return _mm256_set1_pd(x); }
      static type load(const double* p) { return _mm256_loadu_pd(p); }
      static void store(double* p, type v) { _mm256_storeu_pd(p, v); }
      static type fma(type a, type b, type c) {
      #if defined(__FMA__)
          return _mm256_fmadd_pd(a, b, c);
      #else
          return _mm256_add_pd(_mm256_mul_pd(a, b), c);
      #endif
      }
      static double sum(type v) {
          __m128d hi = _mm256_extractf128_pd(v, 1);
          __m128d lo = _mm256_castpd256_pd128(v);
          __m128d s  = _mm_add_pd(hi, lo);
          double tmp[2]; _mm_storeu_pd(tmp, s);
          return tmp[0] + tmp[1];
      }
  };
#elif defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
  #define TTTRLIB_SIMD_DBL 2
  struct simd_t {
      using type = __m128d;
      static type zero() { return _mm_setzero_pd(); }
      static type set1(double x) { return _mm_set1_pd(x); }
      static type load(const double* p) { return _mm_loadu_pd(p); }
      static void store(double* p, type v) { _mm_storeu_pd(p, v); }
      static type fma(type a, type b, type c) {
      #if defined(__FMA__)
          return _mm_fmadd_pd(a, b, c);
      #else
          return _mm_add_pd(_mm_mul_pd(a, b), c);
      #endif
      }
      static double sum(type v) {
          double tmp[2]; _mm_storeu_pd(tmp, v);
          return tmp[0] + tmp[1];
      }
  };
#else
  #define TTTRLIB_SIMD_DBL 0
  struct simd_t {
      using type = double;
      static type zero() { return 0.0; }
      static type set1(double x) { return x; }
      static type load(const double* p) { return *p; }
      static void store(double* p, type v) { *p = v; }
      static type fma(type a, type b, type c) { return a * b + c; }
      static double sum(type v) { return v; }
  };
#endif

/// SIMD SAXPY: ``y[0:n] += alpha * x[0:n]``.  Explicit intrinsics so it works
/// even when the compiler can't prove non-aliasing.  Two-way unrolled so the
/// FMA pipeline stays busy on in-order cores.
inline void saxpy(int n, double alpha,
                  const double* TTTRLIB_RESTRICT x,
                  double* TTTRLIB_RESTRICT y) {
#if TTTRLIB_SIMD_DBL > 0
    auto av = simd_t::set1(alpha);
    int j = 0;
    for (; j + 2 * TTTRLIB_SIMD_DBL <= n; j += 2 * TTTRLIB_SIMD_DBL) {
        simd_t::store(y + j, simd_t::fma(av, simd_t::load(x + j), simd_t::load(y + j)));
        simd_t::store(y + j + TTTRLIB_SIMD_DBL,
                      simd_t::fma(av, simd_t::load(x + j + TTTRLIB_SIMD_DBL),
                                  simd_t::load(y + j + TTTRLIB_SIMD_DBL)));
    }
    for (; j + TTTRLIB_SIMD_DBL <= n; j += TTTRLIB_SIMD_DBL)
        simd_t::store(y + j, simd_t::fma(av, simd_t::load(x + j), simd_t::load(y + j)));
    for (; j < n; ++j) y[j] += alpha * x[j];
#else
    TTTRLIB_VEC
    for (int j = 0; j < n; ++j) y[j] += alpha * x[j];
#endif
}

/// SIMD dot product of two contiguous arrays.  Two independent accumulators
/// for pipeline throughput.
inline double dot(int n,
                  const double* TTTRLIB_RESTRICT a,
                  const double* TTTRLIB_RESTRICT b) {
#if TTTRLIB_SIMD_DBL > 0
    auto acc0 = simd_t::zero();
    auto acc1 = simd_t::zero();
    int k = 0;
    for (; k + 2 * TTTRLIB_SIMD_DBL <= n; k += 2 * TTTRLIB_SIMD_DBL) {
        acc0 = simd_t::fma(simd_t::load(a + k), simd_t::load(b + k), acc0);
        acc1 = simd_t::fma(simd_t::load(a + k + TTTRLIB_SIMD_DBL),
                           simd_t::load(b + k + TTTRLIB_SIMD_DBL), acc1);
    }
    for (; k + TTTRLIB_SIMD_DBL <= n; k += TTTRLIB_SIMD_DBL)
        acc0 = simd_t::fma(simd_t::load(a + k), simd_t::load(b + k), acc0);
    double s = simd_t::sum(acc0) + simd_t::sum(acc1);
    for (; k < n; ++k) s += a[k] * b[k];
    return s;
#else
    double s = 0.0;
    for (int k = 0; k < n; ++k) s += a[k] * b[k];
    return s;
#endif
}

/// In-place Givens rotation of two contiguous columns:
/// ``(xp, xq) <- (c*xp - s*xq, s*xp + c*xq)``.  Both new values depend on both
/// old ones, so the pair is loaded before either store.
inline void jacobi_rotate(int n, double c, double s,
                          double* TTTRLIB_RESTRICT xp,
                          double* TTTRLIB_RESTRICT xq) {
#if TTTRLIB_SIMD_DBL > 0
    const auto cv = simd_t::set1(c);
    const auto sv = simd_t::set1(-s);
    const auto sv2 = simd_t::set1(s);
    int i = 0;
    for (; i + TTTRLIB_SIMD_DBL <= n; i += TTTRLIB_SIMD_DBL) {
        const auto vp = simd_t::load(xp + i);
        const auto vq = simd_t::load(xq + i);
        // xp' = c*vp + (-s)*vq ; xq' = s*vp + c*vq
        simd_t::store(xp + i, simd_t::fma(sv, vq, simd_t::fma(cv, vp, simd_t::zero())));
        simd_t::store(xq + i, simd_t::fma(cv, vq, simd_t::fma(sv2, vp, simd_t::zero())));
    }
    for (; i < n; ++i) {
        const double vp = xp[i], vq = xq[i];
        xp[i] = c * vp - s * vq;
        xq[i] = s * vp + c * vq;
    }
#else
    for (int i = 0; i < n; ++i) {
        const double vp = xp[i], vq = xq[i];
        xp[i] = c * vp - s * vq;
        xq[i] = s * vp + c * vq;
    }
#endif
}

// ===========================================================================
// GEMM kernels
// ===========================================================================
// The inner kernel is a register-blocked micro-kernel that keeps C
// accumulators in SIMD registers across the entire K loop.  This is the
// single most important optimisation: SAXPY reads/writes C from memory every
// k iteration (2*K*MR*N memory ops); the micro-kernel reads/writes C once
// (2*MR*N ops) — a K-fold reduction in C traffic.

inline constexpr int GEMM_MR = 4;   // register-block rows
// NR must divide the common matrix sizes (128, 256) to avoid tail overhead.
// On NEON double (2-wide) NR=8 → 16 accumulators, fits comfortably in 32 regs.
inline constexpr int GEMM_NR = 8;

/// Register-blocked micro-kernel: C[MR×NR] = A[MR×K] × B[K×NR].
/// A has stride ldA, B has stride ldB, C has stride ldC.  C accumulators
/// stay in SIMD registers across the K loop — no C memory traffic per k.
template<int MR, int NR>
inline void microkernel(int K,
    const double* TTTRLIB_RESTRICT A, int ldA,
    const double* TTTRLIB_RESTRICT B, int ldB,
    double* TTTRLIB_RESTRICT C, int ldC) {
#if TTTRLIB_SIMD_DBL > 0
    constexpr int W = TTTRLIB_SIMD_DBL;
    constexpr int NV = NR / W;   // SIMD vectors per C row

    simd_t::type acc[MR][NV];
    for (int r = 0; r < MR; ++r)
        for (int v = 0; v < NV; ++v)
            acc[r][v] = simd_t::zero();

    for (int k = 0; k < K; ++k) {
        // Load B row (NR values → NV SIMD loads), reused across all MR rows
        simd_t::type bv[NV];
        const double* brow = B + static_cast<size_t>(k) * ldB;
        for (int v = 0; v < NV; ++v)
            bv[v] = simd_t::load(brow + v * W);

        // Broadcast A[r,k] and FMA into each row's accumulators
        for (int r = 0; r < MR; ++r) {
            simd_t::type va = simd_t::set1(A[static_cast<size_t>(r) * ldA + k]);
            for (int v = 0; v < NV; ++v)
                acc[r][v] = simd_t::fma(va, bv[v], acc[r][v]);
        }
    }

    for (int r = 0; r < MR; ++r)
        for (int v = 0; v < NV; ++v)
            simd_t::store(C + static_cast<size_t>(r) * ldC + v * W, acc[r][v]);
#else
    // Scalar fallback
    for (int r = 0; r < MR; ++r)
        for (int j = 0; j < NR; ++j) {
            double s = 0.0;
            for (int k = 0; k < K; ++k)
                s += A[static_cast<size_t>(r) * ldA + k] *
                     B[static_cast<size_t>(k) * ldB + j];
            C[static_cast<size_t>(r) * ldC + j] = s;
        }
#endif
}

/// Core NN kernel: C = A * B, all row-major, all contiguous.
/// Outer loop blocks over (MR × NR) tiles, dispatching to the register-blocked
/// micro-kernel.  Row/column tails fall back to SAXPY / scalar dot.
inline void gemm_nn_core(int M, int N, int K,
                         const double* TTTRLIB_RESTRICT A,
                         const double* TTTRLIB_RESTRICT B,
                         double* TTTRLIB_RESTRICT C) {
    std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
    if (M == 0 || N == 0 || K == 0) return;

#if TTTRLIB_SIMD_DBL > 0
    int i = 0;
    for (; i + GEMM_MR <= M; i += GEMM_MR) {
        const double* arow = A + static_cast<size_t>(i) * K;
        double* crow = C + static_cast<size_t>(i) * N;

        int j = 0;
        for (; j + GEMM_NR <= N; j += GEMM_NR) {
            microkernel<GEMM_MR, GEMM_NR>(K,
                arow, K,
                B + j, N,
                crow + j, N);
        }
        // column tail: scalar dot products for remaining columns
        for (; j < N; ++j) {
            for (int r = 0; r < GEMM_MR; ++r) {
                double s = 0.0;
                const double* a = arow + static_cast<size_t>(r) * K;
                for (int k = 0; k < K; ++k)
                    s += a[k] * B[static_cast<size_t>(k) * N + j];
                crow[static_cast<size_t>(r) * N + j] = s;
            }
        }
    }
#else
    int i = 0;
    for (; i + GEMM_MR <= M; i += GEMM_MR) {
        for (int k = 0; k < K; ++k) {
            const double* brow = B + static_cast<size_t>(k) * N;
            for (int r = 0; r < GEMM_MR; ++r) {
                double aik = A[static_cast<size_t>(i + r) * K + k];
                double* crow = C + static_cast<size_t>(i + r) * N;
                saxpy(N, aik, brow, crow);
            }
        }
    }
#endif
    // row tail: SAXPY
    for (; i < M; ++i) {
        const double* arow = A + static_cast<size_t>(i) * K;
        double* crow = C + static_cast<size_t>(i) * N;
        for (int k = 0; k < K; ++k)
            saxpy(N, arow[k], B + static_cast<size_t>(k) * N, crow);
    }
}

/// NN GEMM (with optional OpenMP thread parallelism).
inline void gemm_nn(int M, int N, int K,
                    const double* A, const double* B, double* C) {
    std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
    if (M == 0 || N == 0 || K == 0) return;
#if defined(_OPENMP)
    #pragma omp parallel for schedule(static) if(M > 64)
    for (int i = 0; i < M; ++i) {
        const double* TTTRLIB_RESTRICT arow = A + static_cast<size_t>(i) * K;
        double* TTTRLIB_RESTRICT crow = C + static_cast<size_t>(i) * N;
        for (int k = 0; k < K; ++k)
            saxpy(N, arow[k], B + static_cast<size_t>(k) * N, crow);
    }
#else
    gemm_nn_core(M, N, K, A, B, C);
#endif
}

/// Reusable packing buffer — avoids per-call heap allocation in hot loops
/// (training does thousands of GEMMs per epoch).
// Function-local rather than a namespace-scope inline thread_local: MinGW
// emits the TLS init function in every TU and the DLL link fails on the
// duplicate ("multiple definition of TLS init function").
inline std::vector<double>& pack_buffer() {
    static thread_local std::vector<double> buf;
    return buf;
}

/// Cache-blocked transpose of an r×c row-major matrix into c×r row-major.
/// Blocking at BT×BT keeps both source and destination tiles in L1, avoiding
/// the thrash that makes the naive O(NK) transpose 10× slower than it should be.
inline void blocked_transpose(int rows, int cols,
                              const double* src, double* dst) {
    constexpr int BT = 32;
    for (int ii = 0; ii < rows; ii += BT) {
        const int ie = std::min(ii + BT, rows);
        for (int jj = 0; jj < cols; jj += BT) {
            const int je = std::min(jj + BT, cols);
            for (int i = ii; i < ie; ++i) {
                const double* s = src + static_cast<size_t>(i) * cols + jj;
                for (int j = jj; j < je; ++j)
                    dst[static_cast<size_t>(j) * rows + i] = s[j - jj];
            }
        }
    }
}

/// NT GEMM: C(M×N) = A(M×K) * B(N×K)^T.  B is stored as N×K row-major.
/// For small M the packing cost dominates the GEMM — use dot products directly.
/// For larger M, cache-blocked-transpose B into a contiguous K×N buffer and
/// dispatch to the register-blocked NN micro-kernel.
inline void gemm_nt(int M, int N, int K,
                    const double* A, const double* B, double* C) {
    if (M == 0 || N == 0 || K == 0) {
        std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
        return;
    }
    // Tiny M (inference, single sample): dot-product path avoids allocation
    // and transpose overhead.  For M >= MR the packing amortises instantly.
    if (M < GEMM_MR) {
        for (int i = 0; i < M; ++i) {
            const double* TTTRLIB_RESTRICT arow = A + static_cast<size_t>(i) * K;
            double* TTTRLIB_RESTRICT crow = C + static_cast<size_t>(i) * N;
            for (int j = 0; j < N; ++j)
                crow[j] = dot(K, arow, B + static_cast<size_t>(j) * K);
        }
        return;
    }
    std::vector<double>& pack_buf = pack_buffer();
    pack_buf.resize(static_cast<size_t>(K) * N);
    double* Bt = pack_buf.data();
    blocked_transpose(N, K, B, Bt);
    gemm_nn_core(M, N, K, A, Bt, C);
}

/// TN GEMM: C(M×N) = A(K×M)^T * B(K×N).  A is stored as K×M row-major.
/// For small M, use SAXPY directly (B rows are contiguous).  For larger M,
/// cache-blocked-transpose A into a contiguous M×K buffer and dispatch to NN.
inline void gemm_tn(int M, int N, int K,
                    const double* A, const double* B, double* C) {
    if (M == 0 || N == 0 || K == 0) {
        std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
        return;
    }
    // Small M: SAXPY path — B rows are contiguous, A[k,i] is a gathered scalar.
    if (M < GEMM_MR * 2) {
        std::fill(C, C + static_cast<size_t>(M) * N, 0.0);
        for (int i = 0; i < M; ++i) {
            double* TTTRLIB_RESTRICT crow = C + static_cast<size_t>(i) * N;
            for (int k = 0; k < K; ++k)
                saxpy(N, A[static_cast<size_t>(k) * M + i],
                      B + static_cast<size_t>(k) * N, crow);
        }
        return;
    }
    std::vector<double>& pack_buf = pack_buffer();
    pack_buf.resize(static_cast<size_t>(M) * K);
    double* At = pack_buf.data();
    blocked_transpose(K, M, A, At);
    gemm_nn_core(M, N, K, At, B, C);
}

} // namespace mat_detail

// ===========================================================================
// Public SIMD utility functions (operate on raw double* buffers)
// ===========================================================================
// Thin wrappers over the mat_detail SIMD kernels, exposed so modules that
// work on flat arrays (PDA, FCS, SuperRes) get the same NEON/SSE/AVX speed
// without rolling their own intrinsics.

/// SIMD dot product of two contiguous arrays.
inline double simd_dot(const double* a, const double* b, int n) {
    return mat_detail::dot(n, a, b);
}

/// SIMD SAXPY: ``y[0:n] += alpha * x[0:n]``.
inline void simd_axpy(double alpha, const double* x, double* y, int n) {
    mat_detail::saxpy(n, alpha, x, y);
}

/// SIMD element-wise scale: ``y[0:n] *= alpha``.
inline void simd_scale(double* y, int n, double alpha) {
#if TTTRLIB_SIMD_DBL > 0
    auto av = mat_detail::simd_t::set1(alpha);
    int j = 0;
    for (; j + TTTRLIB_SIMD_DBL <= n; j += TTTRLIB_SIMD_DBL)
        mat_detail::simd_t::store(y + j,
            mat_detail::simd_t::fma(mat_detail::simd_t::load(y + j), av,
                                    mat_detail::simd_t::zero()));
    for (; j < n; ++j) y[j] *= alpha;
#else
    for (int j = 0; j < n; ++j) y[j] *= alpha;
#endif
}

/// SIMD element-wise add: ``acc[0:n] += b[0:n]``.
inline void simd_add(double* acc, const double* b, int n) {
#if TTTRLIB_SIMD_DBL > 0
    int j = 0;
    for (; j + TTTRLIB_SIMD_DBL <= n; j += TTTRLIB_SIMD_DBL)
        mat_detail::simd_t::store(acc + j,
            mat_detail::simd_t::fma(mat_detail::simd_t::load(b + j),
                                    mat_detail::simd_t::set1(1.0),
                                    mat_detail::simd_t::load(acc + j)));
    for (; j < n; ++j) acc[j] += b[j];
#else
    for (int j = 0; j < n; ++j) acc[j] += b[j];
#endif
}

// ===========================================================================
// operator* -- matrix product, dispatched on transpose flags
// ===========================================================================

/// C = A * B (ordinary matrix product, row-major).
inline Mat operator*(const Mat& A, const Mat& B) {
    if (A.n_cols() != B.n_rows())
        throw std::invalid_argument(
            "Mat operator*: inner dimension mismatch (" +
            std::to_string(A.n_rows()) + "x" + std::to_string(A.n_cols()) + " * " +
            std::to_string(B.n_rows()) + "x" + std::to_string(B.n_cols()) + ")");
    Mat C(A.n_rows(), B.n_cols());
    mat_detail::gemm_nn(A.n_rows(), B.n_cols(), A.n_cols(),
                        A.memptr(), B.memptr(), C.memptr());
    return C;
}

/// C = A * B^T.  B^T is the zero-copy transpose proxy from ``B.t()``.
inline Mat operator*(const Mat& A, const MatTrans& Bt) {
    // Bt is logically (K x N), physically stored as (N x K) row-major.
    const int M = A.n_rows(), K = A.n_cols(), N = Bt.n_cols();
    if (K != Bt.n_rows())
        throw std::invalid_argument(
            "Mat operator* (NT): inner dimension mismatch");
    Mat C(M, N);
    mat_detail::gemm_nt(M, N, K, A.memptr(), Bt.memptr(), C.memptr());
    return C;
}

/// C = A^T * B.  A^T is the zero-copy transpose proxy from ``A.t()``.
inline Mat operator*(const MatTrans& At, const Mat& B) {
    // At is logically (M x K), physically stored as (K x M) row-major.
    const int M = At.n_rows(), K = At.n_cols(), N = B.n_cols();
    if (K != B.n_rows())
        throw std::invalid_argument(
            "Mat operator* (TN): inner dimension mismatch");
    Mat C(M, N);
    mat_detail::gemm_tn(M, N, K, At.memptr(), B.memptr(), C.memptr());
    return C;
}

// ===========================================================================
// Element-wise binary operators (same-shape)
// ===========================================================================
inline Mat operator+(const Mat& a, const Mat& b) {
    if (a.n_rows() != b.n_rows() || a.n_cols() != b.n_cols())
        throw std::invalid_argument("Mat operator+: size mismatch");
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); const double* pb = b.memptr();
    double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] + pb[i];
    return r;
}
inline Mat operator-(const Mat& a, const Mat& b) {
    if (a.n_rows() != b.n_rows() || a.n_cols() != b.n_cols())
        throw std::invalid_argument("Mat operator-: size mismatch");
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); const double* pb = b.memptr();
    double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] - pb[i];
    return r;
}
/// Schur (element-wise / Hadamard) product.
inline Mat operator%(const Mat& a, const Mat& b) {
    if (a.n_rows() != b.n_rows() || a.n_cols() != b.n_cols())
        throw std::invalid_argument("Mat operator%: size mismatch");
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); const double* pb = b.memptr();
    double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] * pb[i];
    return r;
}
/// Element-wise division.
inline Mat operator/(const Mat& a, const Mat& b) {
    if (a.n_rows() != b.n_rows() || a.n_cols() != b.n_cols())
        throw std::invalid_argument("Mat operator/: size mismatch");
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); const double* pb = b.memptr();
    double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] / pb[i];
    return r;
}

// ===========================================================================
// Scalar operators
// ===========================================================================
inline Mat operator+(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] + s;
    return r;
}
inline Mat operator+(double s, const Mat& a) { return a + s; }
inline Mat operator-(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] - s;
    return r;
}
inline Mat operator-(double s, const Mat& a) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = s - pa[i];
    return r;
}
inline Mat operator-(const Mat& a) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = -pa[i];
    return r;
}
inline Mat operator*(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] * s;
    return r;
}
inline Mat operator*(double s, const Mat& a) { return a * s; }
inline Mat operator/(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = pa[i] / s;
    return r;
}
inline Mat operator/(double s, const Mat& a) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = s / pa[i];
    return r;
}

// ===========================================================================
// Comparison → 0/1 mask (used for activation gradients, e.g. ReLU)
// ===========================================================================
inline Mat operator>(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] > s) ? 1.0 : 0.0;
    return r;
}
inline Mat operator>=(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] >= s) ? 1.0 : 0.0;
    return r;
}
inline Mat operator<(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem(); const double* pa = a.memptr(); double* pr = r.memptr();
    for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] < s) ? 1.0 : 0.0;
    return r;
}

// ===========================================================================
// Generators
// ===========================================================================
inline Mat zeros(int n_rows, int n_cols) { return Mat(n_rows, n_cols, fill_zeros); }
inline Mat ones(int n_rows, int n_cols) { return Mat(n_rows, n_cols, fill_ones); }

// ===========================================================================
// Element-wise math (return new matrices)
// ===========================================================================
#define TTTRLIB_MAT_ELEM_FN(name, expr)                                    \
    inline Mat name(const Mat& a) {                                        \
        Mat r(a.n_rows(), a.n_cols());                                     \
        const size_t n = a.n_elem();                                       \
        const double* pa = a.memptr(); double* pr = r.memptr();            \
        TTTRLIB_VEC                                                        \
        for (size_t i = 0; i < n; ++i) pr[i] = (expr);                     \
        return r;                                                          \
    }

TTTRLIB_MAT_ELEM_FN(sqrt,  std::sqrt(pa[i]))
TTTRLIB_MAT_ELEM_FN(exp,   std::exp(pa[i]))
TTTRLIB_MAT_ELEM_FN(tanh,  std::tanh(pa[i]))
TTTRLIB_MAT_ELEM_FN(square, pa[i] * pa[i])
TTTRLIB_MAT_ELEM_FN(abs,   std::fabs(pa[i]))
TTTRLIB_MAT_ELEM_FN(sigmoid, 1.0 / (1.0 + std::exp(-pa[i])))

#undef TTTRLIB_MAT_ELEM_FN

// ---- in-place variants (avoid heap allocation in hot loops) ----------------
#define TTTRLIB_MAT_ELEM_FN_INPLACE(name, expr)                             \
    inline void name##_inplace(Mat& a) {                                    \
        const size_t n = a.n_elem();                                        \
        double* pa = a.memptr();                                            \
        TTTRLIB_VEC                                                         \
        for (size_t i = 0; i < n; ++i) pa[i] = (expr);                      \
    }

TTTRLIB_MAT_ELEM_FN_INPLACE(tanh,  std::tanh(pa[i]))
TTTRLIB_MAT_ELEM_FN_INPLACE(square, pa[i] * pa[i])
TTTRLIB_MAT_ELEM_FN_INPLACE(sigmoid, 1.0 / (1.0 + std::exp(-pa[i])))
#undef TTTRLIB_MAT_ELEM_FN_INPLACE

/// In-place ReLU: clamps every element to [0, +inf). No allocation.
inline void relu_inplace(Mat& a) {
    const size_t n = a.n_elem();
    double* pa = a.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pa[i] = (pa[i] > 0.0) ? pa[i] : 0.0;
}

inline Mat pow(const Mat& a, double p) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = std::pow(pa[i], p);
    return r;
}

/// Element-wise maximum of matrix and scalar (ReLU is ``max(z, 0.0)``).
inline Mat max(const Mat& a, double s) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); double* pr = r.memptr();
    TTTRLIB_VEC
    for (size_t i = 0; i < n; ++i) pr[i] = (pa[i] > s) ? pa[i] : s;
    return r;
}

/// Element-wise clamp to [lo, hi].
inline Mat clamp(const Mat& a, double lo, double hi) {
    Mat r(a.n_rows(), a.n_cols());
    const size_t n = a.n_elem();
    const double* pa = a.memptr(); double* pr = r.memptr();
    for (size_t i = 0; i < n; ++i)
        pr[i] = (pa[i] < lo) ? lo : (pa[i] > hi) ? hi : pa[i];
    return r;
}

// ===========================================================================
// Reductions
// ===========================================================================

/// Sum of all elements.
inline double accu(const Mat& a) {
    double s = 0.0;
    const size_t n = a.n_elem();
    const double* pa = a.memptr();
    TTTRLIB_VEC_REDUCTION(s)
    for (size_t i = 0; i < n; ++i) s += pa[i];
    return s;
}

/// Column-wise sum (dim 0 → 1 x n_cols) or row-wise sum (dim 1 → n_rows x 1).
inline Mat sum(const Mat& a, int dim) {
    if (dim == 0) {
        Mat r(1, a.n_cols(), fill_zeros);
        double* rp = r.memptr();
        for (int i = 0; i < a.n_rows(); ++i) {
            const double* row = a.memptr() + static_cast<size_t>(i) * a.n_cols();
            TTTRLIB_VEC
            for (int j = 0; j < a.n_cols(); ++j) rp[j] += row[j];
        }
        return r;
    } else {
        Mat r(a.n_rows(), 1, fill_zeros);
        for (int i = 0; i < a.n_rows(); ++i) {
            const double* row = a.memptr() + static_cast<size_t>(i) * a.n_cols();
            double s = 0.0;
            TTTRLIB_VEC_REDUCTION(s)
            for (int j = 0; j < a.n_cols(); ++j) s += row[j];
            r.at(i) = s;
        }
        return r;
    }
}

/// Column-wise mean (dim 0) or row-wise mean (dim 1).
inline Mat mean(const Mat& a, int dim) {
    Mat r = sum(a, dim);
    double denom = (dim == 0) ? a.n_rows() : a.n_cols();
    if (denom > 0) r /= denom;
    return r;
}

/// Global maximum element.
inline double max(const Mat& a) {
    if (a.is_empty()) return 0.0;
    double m = a.at(0);
    const size_t n = a.n_elem();
    const double* pa = a.memptr();
    for (size_t i = 1; i < n; ++i)
        if (pa[i] > m) m = pa[i];
    return m;
}

/// Global minimum element.
inline double min(const Mat& a) {
    if (a.is_empty()) return 0.0;
    double m = a.at(0);
    const size_t n = a.n_elem();
    const double* pa = a.memptr();
    for (size_t i = 1; i < n; ++i)
        if (pa[i] < m) m = pa[i];
    return m;
}

// ===========================================================================
// Small-matrix utility operations (shared by HMM, burst, decay)
// ===========================================================================
// These operate on raw row-major buffers, not Mat objects, because the callers
// (HMM forward/backward, Kalman burst search) work with flat arrays for cache
// locality on tiny matrices (n = 2–4). Wrapping them in Mat would add heap
// allocation to every photon.

/// Rescale every row of an (rows × cols) row-major buffer to sum to 1.
/// Zero rows become uniform. Used by HMM propagator normalisation.
inline void row_normalize(double* a, int rows, int cols) {
    for (int i = 0; i < rows; ++i) {
        double s = 0.0;
        double* row = a + static_cast<size_t>(i) * cols;
        for (int j = 0; j < cols; ++j) s += row[j];
        if (s > 0.0) {
            for (int j = 0; j < cols; ++j) row[j] /= s;
        } else {
            for (int j = 0; j < cols; ++j) row[j] = 1.0 / cols;
        }
    }
}

inline void row_normalize(std::vector<double>& a, int rows, int cols) {
    row_normalize(a.data(), rows, cols);
}

/// Gauss-Jordan matrix inverse of an n×n row-major buffer `a`, in place.
/// `scratch` must point at n*n writable doubles; its contents are ignored on
/// entry and undefined on exit. Returns false if the matrix is singular.
/// Allocation-free so callers in a per-bin/per-photon loop (Kalman burst
/// search) can hoist the buffer out.
inline bool mat_inverse_inplace(double* TTTRLIB_RESTRICT a, int n,
                                double* TTTRLIB_RESTRICT scratch) {
    const size_t nn = static_cast<size_t>(n) * n;
    double* inv = scratch;
    std::fill(inv, inv + nn, 0.0);
    for (int i = 0; i < n; ++i) inv[static_cast<size_t>(i) * n + i] = 1.0;

    // Scale-relative singularity test: an absolute floor calls a badly scaled
    // but perfectly invertible matrix regular, and a rank-deficient one with
    // large entries singular.
    double amax = 0.0;
    for (size_t i = 0; i < nn; ++i) amax = std::max(amax, std::fabs(a[i]));
    const double tol = std::numeric_limits<double>::epsilon() * amax;

    for (int col = 0; col < n; ++col) {
        int pivot = col;
        double best = std::fabs(a[static_cast<size_t>(col) * n + col]);
        for (int row = col + 1; row < n; ++row) {
            const double value = std::fabs(a[static_cast<size_t>(row) * n + col]);
            if (value > best) { best = value; pivot = row; }
        }
        if (!(best > tol) || best < 1e-300) return false;
        if (pivot != col) {
            for (int k = 0; k < n; ++k) {
                std::swap(a[static_cast<size_t>(col) * n + k],
                          a[static_cast<size_t>(pivot) * n + k]);
                std::swap(inv[static_cast<size_t>(col) * n + k],
                          inv[static_cast<size_t>(pivot) * n + k]);
            }
        }
        const double inv_diag = 1.0 / a[static_cast<size_t>(col) * n + col];
        double* arow = a + static_cast<size_t>(col) * n;
        double* irow = inv + static_cast<size_t>(col) * n;
        for (int k = 0; k < n; ++k) { arow[k] *= inv_diag; irow[k] *= inv_diag; }
        for (int row = 0; row < n; ++row) {
            if (row == col) continue;
            const double factor = a[static_cast<size_t>(row) * n + col];
            if (factor == 0.0) continue;
            mat_detail::saxpy(n, -factor, arow, a + static_cast<size_t>(row) * n);
            mat_detail::saxpy(n, -factor, irow, inv + static_cast<size_t>(row) * n);
        }
    }
    std::copy(inv, inv + nn, a);
    return true;
}

/// Allocating convenience overload of the above.
inline bool mat_inverse_inplace(std::vector<double>& a, int n) {
    std::vector<double> scratch(static_cast<size_t>(n) * n);
    return mat_inverse_inplace(a.data(), n, scratch.data());
}

/// Solve A x = b for a general square matrix A (n x n row-major) via
/// Gaussian elimination with partial pivoting — the Eigen PartialPivLU::solve
/// equivalent. A is destroyed; b is overwritten with the solution.
/// Returns false if A is (numerically) singular.
inline bool mat_solve(std::vector<double>& A, std::vector<double>& b, int n) {
    double* pa = A.data();
    double* pb = b.data();
    // Scale-relative "numerically exact" singularity test. An absolute floor
    // (1e-300) misses a rank-deficient matrix whose entries are large: its
    // pivot collapses to eps*scale, not to zero. eps*amax keeps the criterion
    // as tight as LAPACK's exact-zero test while being scale invariant.
    const size_t nn = static_cast<size_t>(n) * n;
    double amax = 0.0;
    for (size_t i = 0; i < nn; ++i) amax = std::max(amax, std::fabs(pa[i]));
    const double tol = std::numeric_limits<double>::epsilon() * amax;

    for (int k = 0; k < n; ++k) {
        int piv = k;
        double best = std::fabs(pa[static_cast<size_t>(k) * n + k]);
        for (int i = k + 1; i < n; ++i) {
            const double v = std::fabs(pa[static_cast<size_t>(i) * n + k]);
            if (v > best) { best = v; piv = i; }
        }
        if (!(best > tol) || best < 1e-300) return false;
        if (piv != k) {
            std::swap_ranges(pa + static_cast<size_t>(k) * n + k,
                             pa + static_cast<size_t>(k) * n + n,
                             pa + static_cast<size_t>(piv) * n + k);
            std::swap(pb[k], pb[piv]);
        }
        const double inv = 1.0 / pa[static_cast<size_t>(k) * n + k];
        const double* krow = pa + static_cast<size_t>(k) * n;
        const int len = n - k - 1;
        for (int i = k + 1; i < n; ++i) {
            double* TTTRLIB_RESTRICT irow = pa + static_cast<size_t>(i) * n;
            const double f = irow[k] * inv;
            if (f == 0.0) continue;
            irow[k] = 0.0;
            const double* TTTRLIB_RESTRICT src = krow + k + 1;
            double* TTTRLIB_RESTRICT dst = irow + k + 1;
            TTTRLIB_VEC
            for (int j = 0; j < len; ++j) dst[j] -= f * src[j];
            pb[i] -= f * pb[k];
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        const double* irow = pa + static_cast<size_t>(i) * n;
        double s = pb[i] - mat_detail::dot(n - i - 1, irow + i + 1, pb + i + 1);
        pb[i] = s / irow[i];
    }
    return true;
}

/// Minimum-norm least-squares solve A x = b via one-sided Jacobi SVD —
/// the Eigen JacobiSVD::solve equivalent. A is m x n row-major, destroyed.
/// b (length m) is overwritten with the solution (length n).
///
/// `rcond` is *relative*: a direction is dropped when its singular value falls
/// below `rcond * sigma_max`, the way numpy's `lstsq` and LAPACK's `gelsd`
/// define it. An absolute cutoff is not a rank test — it deletes every
/// direction of a uniformly small matrix and keeps the null space of a large
/// one, so the min-norm solution it returns is neither minimum-norm nor a
/// solution.
///
/// The working copy is column-major: one-sided Jacobi only ever touches whole
/// columns, which are strided in the row-major input and contiguous here, so
/// every rotation and inner product runs on contiguous memory. Column norms
/// are carried through each rotation by their closed form instead of being
/// recomputed, which removes two of the three length-m passes per index pair.
inline bool mat_lstsq_minnorm(std::vector<double>& A, std::vector<double>& b,
                              int m, int n, double rcond = 1e-14) {
    if (m <= 0 || n <= 0) { b.assign(n > 0 ? n : 0, 0.0); return n > 0; }

    // Column-major working copy: column j lives at Ac + j*m, contiguous.
    std::vector<double> Ac(static_cast<size_t>(n) * m);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            Ac[static_cast<size_t>(j) * m + i] = A[static_cast<size_t>(i) * n + j];

    // V accumulates the rotations, also column-major (column j at Vc + j*n).
    std::vector<double> Vc(static_cast<size_t>(n) * n, 0.0);
    for (int j = 0; j < n; ++j) Vc[static_cast<size_t>(j) * n + j] = 1.0;

    std::vector<double> nrm(n);
    const double eps = std::numeric_limits<double>::epsilon();

    for (int sweep = 0; sweep < 60; ++sweep) {
        // Refresh the carried norms once per sweep to stop rounding drift.
        for (int j = 0; j < n; ++j) {
            const double* cj = Ac.data() + static_cast<size_t>(j) * m;
            nrm[j] = mat_detail::dot(m, cj, cj);
        }
        bool changed = false;
        for (int p = 0; p < n - 1; ++p) {
            double* cp = Ac.data() + static_cast<size_t>(p) * m;
            for (int q = p + 1; q < n; ++q) {
                double* cq = Ac.data() + static_cast<size_t>(q) * m;
                const double app = nrm[p], aqq = nrm[q];
                const double apq = mat_detail::dot(m, cp, cq);
                if (std::fabs(apq) <= eps * std::sqrt(app * aqq)) continue;

                const double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
                const double c = std::cos(phi), s = std::sin(phi);
                mat_detail::jacobi_rotate(m, c, s, cp, cq);
                mat_detail::jacobi_rotate(n, c, s,
                                          Vc.data() + static_cast<size_t>(p) * n,
                                          Vc.data() + static_cast<size_t>(q) * n);
                // Closed form of the rotated column norms — the rotation is
                // exactly a 2x2 orthogonal change of basis on (app, apq, aqq).
                const double cc = c * c, ss = s * s, cs2 = 2.0 * c * s;
                nrm[p] = cc * app - cs2 * apq + ss * aqq;
                nrm[q] = ss * app + cs2 * apq + cc * aqq;
                if (nrm[p] < 0.0) nrm[p] = 0.0;
                if (nrm[q] < 0.0) nrm[q] = 0.0;
                changed = true;
            }
        }
        if (!changed) break;
    }

    // Columns are now orthogonal: col_j = sigma_j * u_j, and A_in = U S V^T.
    // x = V S^-1 U^T b = sum_j V[:,j] * (col_j . b) / sigma_j^2.
    std::vector<double> sigma(n);
    double smax = 0.0;
    for (int j = 0; j < n; ++j) {
        sigma[j] = std::sqrt(nrm[j] > 0.0 ? nrm[j] : 0.0);
        smax = std::max(smax, sigma[j]);
    }
    const double cutoff = rcond * smax;

    std::vector<double> x(n, 0.0);
    for (int j = 0; j < n; ++j) {
        if (!(sigma[j] > cutoff) || sigma[j] == 0.0) continue;
        const double* cj = Ac.data() + static_cast<size_t>(j) * m;
        const double coeff = mat_detail::dot(m, cj, b.data()) / (sigma[j] * sigma[j]);
        mat_detail::saxpy(n, coeff, Vc.data() + static_cast<size_t>(j) * n, x.data());
    }
    b = std::move(x);
    return true;
}

/// Matrix power by binary exponentiation: out = a^power (n×n row-major).
/// Uses mat_detail::gemm_nn for the multiply so it gets SIMD for free.
inline std::vector<double> mat_power(const double* a, int n, int power) {    // result = I
    std::vector<double> result(static_cast<size_t>(n) * n, 0.0);
    std::vector<double> base(a, a + static_cast<size_t>(n) * n);
    for (int i = 0; i < n; ++i) result[static_cast<size_t>(i) * n + i] = 1.0;

    // One scratch buffer, reused: binary exponentiation does up to 2*log2(p)
    // products and a fresh allocation for each one shows up in HMM surrogate
    // fitting, where mat_power runs per E-step.
    std::vector<double> tmp(static_cast<size_t>(n) * n);
    while (power > 0) {
        if (power & 1) {
            mat_detail::gemm_nn(n, n, n, result.data(), base.data(), tmp.data());
            result.swap(tmp);
        }
        power >>= 1;
        if (power > 0) {
            mat_detail::gemm_nn(n, n, n, base.data(), base.data(), tmp.data());
            base.swap(tmp);
        }
    }
    return result;
}

} // namespace tttrlib

#endif // TTTRLIB_MAT_H
