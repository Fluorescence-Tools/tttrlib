// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_EXPRESSIONENGINE_H
#define TTTRLIB_EXPRESSIONENGINE_H

/*!
 * \file ExpressionEngine.h
 * \brief A boolean or arithmetic expression over columns, compiled once and
 *        evaluated a block at a time.
 *
 * \section ee_why What this is for
 *
 * Two questions in this stack are the same question. A data explorer gates a
 * few million rows with `"(g-b)/(r-b) > 0.3"` and wants a bit per row back; a
 * fit evaluates a model equation over a curve of a few thousand points and
 * wants a curve back. Both are a string, some columns, and one pass. This
 * answers both.
 *
 * The string is tokenised, run through a shunting-yard parser into reverse
 * Polish, folded, and then evaluated **block-wise**: one operation applied
 * across 512 rows at a time rather than a tree walked once per row. That is
 * what separates this from an interpreter such as ExprTk, whose per-element
 * tree walk loses badly to any array library, and from numpy, which vectorises
 * the same way but allocates a full-length temporary per operation. A block
 * stays in L1, so a long expression never leaves cache and never touches the
 * allocator.
 *
 * \section ee_types Precision follows the data
 *
 * Evaluation is templated on the working type. A gate over columns that are all
 * float32 -- which is how burst parameters are stored -- runs in float32, so
 * nothing is widened, twice as many lanes fit in a SIMD register, and the
 * answer is bit-for-bit what the same expression gives in numpy or pandas over
 * the same float32 columns. Anything else runs in double, with each column
 * converted as its block is loaded rather than widened into a full-length
 * scratch buffer first.
 *
 * \section ee_semantics Semantics
 *
 * numpy's, because that is what callers compare against:
 *
 * - `**` binds tighter than unary minus and is right-associative, so `-2**2` is
 *   -4 and `2**3**2` is 512.
 * - Division by zero yields an infinity rather than throwing.
 * - Truthiness is `arr.astype(bool)`: anything that is not zero is true, so a
 *   negative value and a NaN are both true.
 * - `min` and `max` propagate NaN, as `numpy.minimum` and `numpy.maximum` do.
 * - Python spellings are accepted for the operators a query is written with:
 *   `**` for exponentiation and `&`, `|`, `~` for the elementwise boolean
 *   operators, alongside `and`, `or` and `not`.
 *
 * \section ee_notrepresentable What it refuses
 *
 * \ref ExpressionEngine::compile returns false, rather than throwing, for an
 * expression it cannot represent exactly -- an unknown function, a malformed
 * string, or a nesting depth beyond its block-stack budget. A caller may then
 * refuse the expression or hand it to something more general. Nothing is ever
 * evaluated approximately.
 *
 * \section ee_deadends Measured dead ends
 *
 * Recorded so they are not tried again. Each was implemented and reverted.
 *
 * - **Element-major (fused) evaluation** -- running the whole program for one
 *   row with the stack in registers, so memory is touched twice instead of
 *   twice per operation. Measured at 0.05-0.29x of numpy, five to twenty times
 *   *worse*: the per-element dispatch costs far more than the memory traffic it
 *   saves. This is the trap a tree-walking interpreter falls into.
 * - **A Newton-Raphson reciprocal in place of the hardware divide.** Accurate
 *   to machine epsilon but no faster; the per-lane guards for zero and
 *   non-finite inputs cost as much as the division.
 * - **`__restrict` without explicit intrinsics.** No measurable change.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tttrlib {
namespace data {

/*!
 * \brief The layout of one column handed to the evaluator.
 *
 * Mirrors the numeric column types of \ref DataStore, kept as its own enum so
 * the engine does not depend on the store -- a caller with a plain array uses
 * it just as well.
 */
enum class ExprScalarType {
    Float64,
    Float32,
    Int64,
    Int32,
    Int16,
    Int8,
    UInt64,
    UInt32,
    UInt16,
    UInt8
};

/*!
 * \brief One input column, as a pointer and a type.
 *
 * The engine reads the caller's memory in place; nothing is copied into it and
 * nothing is widened ahead of time. A column whose \ref is_vector is false is a
 * single value at \ref data that broadcasts over every row, which is how a fit
 * parameter enters an equation without becoming an array.
 */
struct ExprColumn {
    /// First element of the column, or of the one value when broadcasting.
    const void* data = nullptr;
    /// How to read \ref data.
    ExprScalarType type = ExprScalarType::Float64;
    /// False for a single value broadcast over all rows.
    bool is_vector = true;
};

/*!
 * \brief An expression compiled to a block-vectorised program.
 *
 * Compile once, evaluate many times. The compiled form carries no per-call
 * state, so the same engine may be evaluated over different row counts and
 * different buffers without reparsing. Scratch buffers are kept between calls
 * -- allocating the block stack per call was measured to be most of the cost
 * for the few-hundred-point curves a fit evaluates -- which makes one engine
 * instance **not** safe to evaluate from two threads at once.
 *
 * \see DataStore::select_expression, which is this engine's first caller.
 */
class ExpressionEngine {
public:
    ExpressionEngine() = default;

    /*!
     * \brief Compile an expression, or report that this evaluator cannot.
     *
     * \param expression the expression, in the Python spelling a query is
     *        written in
     * \return true when a program was produced; false when the expression is
     *         malformed, names a function this evaluator does not implement, or
     *         nests deeper than the block stack allows. Nothing is thrown: a
     *         caller that has a fallback wants an answer, not an exception.
     *
     * On false the engine is left empty, and \ref ready answers false.
     */
    bool compile(const std::string& expression);

    /// The expression as it was handed to \ref compile.
    const std::string& expression() const { return expression_; }

    /*!
     * \brief The free names of the expression, in first-appearance order.
     *
     * These are the columns \ref compute_mask and \ref compute_values expect,
     * in the order they expect them. Names the engine resolves itself -- `pi`,
     * `e`, and every function it implements -- are not among them.
     */
    const std::vector<std::string>& variables() const { return variables_; }

    /// Whether a program is compiled and can be evaluated.
    bool ready() const { return !program_.empty(); }

    /// How many instructions the compiled program holds. For tests that pin
    /// what the folder and the subexpression pass actually did.
    std::size_t program_size() const { return program_.size(); }

    /// How many block-sized slots the shared-subexpression cache asked for.
    int cache_slots() const { return program_cache_size_; }

    /*!
     * \brief Evaluate as a gate, writing one bit per row.
     *
     * \param columns one per name in \ref variables, in that order
     * \param n_rows how many rows to evaluate
     * \param words destination, `(n_rows + 63) / 64` words, bit *i* of word
     *        *i/64* being row *i*. Every word written is overwritten whole, and
     *        bits past \p n_rows in the final word are cleared.
     *
     * The answer to a gate is a bit, and returning it as an array of doubles
     * costs eight bytes a row to say one. The block loop already carries its
     * booleans as one byte per row, so only the block's closing store differs
     * from \ref compute_values: the bytes are packed into words as the block
     * finishes, and no full-length intermediate exists at any point.
     *
     * A result that is not already boolean -- `"g"`, or `"g*2"` -- is taken as
     * true where it is not zero, which is numpy's cast to bool.
     *
     * \throws std::domain_error if no program is compiled, or if \p columns is
     *         not one per variable.
     */
    void compute_mask(const std::vector<ExprColumn>& columns,
                      std::size_t n_rows, std::uint64_t* words) const;

    /*!
     * \brief Evaluate as a curve, writing one double per row.
     *
     * \param columns one per name in \ref variables, in that order
     * \param n_rows how many rows to evaluate
     * \param out destination, \p n_rows doubles
     *
     * The arithmetic itself is done in whatever type the columns are -- float32
     * columns are evaluated in float32 -- and only the result is written as a
     * double.
     *
     * \throws std::domain_error if no program is compiled, or if \p columns is
     *         not one per variable.
     */
    void compute_values(const std::vector<ExprColumn>& columns,
                        std::size_t n_rows, double* out) const;

    /*!
     * \brief Rewrite the Python and pandas spellings into the engine's own.
     *
     * `**` becomes `^`, and `&`, `|`, `~` become `and`, `or`, `not`. Exposed
     * because a caller with a second evaluator behind this one has to hand it
     * the same string, and the two must not drift.
     *
     * \param s the expression as the user wrote it
     * \return the same expression in the engine's spelling
     */
    static std::string normalise(const std::string& s);

    /*!
     * \brief The free names of an already-normalised expression.
     *
     * \param s an expression, after \ref normalise
     * \return the names, deduplicated, in first-appearance order
     *
     * Exposed for the same reason as \ref normalise: a caller that resolves
     * columns before compiling needs exactly this list.
     */
    static std::vector<std::string> free_variables(const std::string& s);

private:
    //! One instruction of the block-vectorised program.
    struct VecOp {
        int kind = 0;       //!< constant, variable, operator, function, ...
        double value = 0.0; //!< the constant, or a constant exponent
        int index = 0;      //!< operator/function id, variable slot, cache slot
    };

    /*!
     * \brief Block scratch, one set per working type.
     *
     * Kept alive between calls. A six-deep program needs a 24 kB stack in
     * double, and allocating and freeing that on every call was most of the
     * cost at the curve lengths that actually occur.
     *
     * A stack slot is one of three things at any moment -- a block of numbers,
     * a block of one-byte booleans, or a single folded scalar -- and the flags
     * say which. Getting that reconciliation wrong at the boundaries is where
     * three real bugs lived, so the type is tracked rather than assumed.
     */
    template <typename T>
    struct Blocks {
        std::vector<T> stack;
        std::vector<unsigned char> bstack;
        std::vector<char> is_bool;
        std::vector<char> is_scalar;
        std::vector<T> scalar_value;
        std::vector<T> cache;
        std::vector<unsigned char> cache_bool;
        std::vector<char> cache_is_bool;
        std::vector<char> cache_is_scalar;
        std::vector<T> cache_scalar;
        std::vector<unsigned char> pack_scratch;
    };

    //! Tokenise and shunting-yard into \ref program_, then fold and share.
    bool compile_program(const std::string& normalised);

    //! Compute a repeated subtree once and reuse it, where that is cheaper.
    bool eliminate_common_subexpressions();

    //! The block loop. Exactly one of \p out and \p words is non-null.
    template <typename T>
    void run(const std::vector<ExprColumn>& columns, std::size_t n_rows,
             double* out, std::uint64_t* words, Blocks<T>& s) const;

    //! Check the columns and pick the working type, then \ref run.
    void dispatch(const std::vector<ExprColumn>& columns, std::size_t n_rows,
                  double* out, std::uint64_t* words) const;

    std::string expression_;
    std::string normalised_;
    std::vector<std::string> variables_;
    std::vector<VecOp> program_;
    int program_depth_ = 0;
    int program_cache_size_ = 0;

    mutable Blocks<double> scratch64_;
    mutable Blocks<float> scratch32_;
};

}  // namespace data
}  // namespace tttrlib


// ===========================================================================
// Implementation
// ===========================================================================
//
// Header-only, deliberately. This engine is pure arithmetic -- a tokeniser, a
// parser, and SIMD kernels over arrays. It opens no file, allocates no
// resource and calls nothing outside libm, so there is no reason for a
// consumer to *link* anything to use it. Making imp.bff link libtttrlib for
// this would have coupled two build systems, an ABI and an install contract
// to a computation that needs none of them.
//
// So both callers include this one file and share the source of truth without
// depending on each other's libraries: `tttrlib::data::DataStore` gates burst
// columns with it, and `IMP::bff::Expression` evaluates model equations with
// it. One implementation, no link edge.
//
// The cost is that every translation unit including this compiles it. That is
// ~1600 lines, which is a second or so -- and it is why the file is included
// by two translation units in each repository, not by a public umbrella
// header.

#include "ExpressionEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && defined(__aarch64__)
#include <arm_neon.h>
#define TTTRLIB_EXPR_NEON 1
#else
#define TTTRLIB_EXPR_NEON 0
#endif

namespace tttrlib {
namespace data {

namespace {

// ---------------------------------------------------------------------------
// The instruction set
// ---------------------------------------------------------------------------

// OP_SAVE and OP_LOADC are what common subexpression elimination emits: a
// repeated subtree is computed once, copied into a cache slot, and every later
// occurrence becomes a load of that slot rather than the whole subtree again.
enum OpKind { OP_CONST = 0, OP_VAR, OP_BIN, OP_FUN, OP_POWC, OP_SAVE, OP_LOADC };
enum BinOp {
    B_ADD = 0, B_SUB, B_MUL, B_DIV, B_POW,
    B_LT, B_LE, B_GT, B_GE, B_EQ, B_NE, B_AND, B_OR
};
enum FunOp {
    F_NEG = 0, F_ABS, F_EXP, F_SQRT, F_LOG, F_LOG10,
    F_SIN, F_COS, F_TAN, F_NOT, F_POW2, F_MIN2, F_MAX2,
    // Added 2026-08-31 so imp.bff could delete its own evaluator and the
    // vendored ExprTk behind it (T-20260831-12). These are the functions
    // that were reachable *and correct* in that fallback; without them,
    // dropping it would have been a capability regression, and `floor(x/2)`
    // is the case that proved it. Spelled as numpy spells them, because
    // numpy is what callers compare against.
    F_FLOOR, F_CEIL, F_ROUND, F_TRUNC, F_SIGN,
    F_ASIN, F_ACOS, F_ATAN, F_SINH, F_COSH, F_TANH,
    F_LOG2, F_EXPM1, F_LOG1P, F_DEG2RAD, F_RAD2DEG, F_ERF, F_ERFC,
    F_ATAN2, F_HYPOT,
    // Added 2026-09-02 so the DataStore could drop its ExprTk fallback
    // entirely (T-20260831-13): these were the last reachable-and-correct
    // arity-1/2 names that still forced a query onto it. The names that
    // remain reserved but unimplemented (clamp, inrange, if, avg, ...) now
    // refuse at compile time instead of being answered wrongly.
    F_ROOT, F_LOGN, F_FRAC
};

//! 512 rows: eight 64-bit mask words, and a double stack slot of 4 kB.
const std::size_t kBlock = 512;
//! Deepest expression the block stack will carry: 64 x 4 kB of doubles.
const int kMaxDepth = 64;
//! How many repeated subtrees may be kept aside at once.
const int kMaxCache = 16;

bool binary_function(int f) {
    return f == F_POW2 || f == F_MIN2 || f == F_MAX2 ||
           f == F_ATAN2 || f == F_HYPOT || f == F_ROOT || f == F_LOGN;
}

int function_id(const std::string& n) {
    if (n == "abs" || n == "fabs") return F_ABS;
    if (n == "exp") return F_EXP;
    if (n == "sqrt") return F_SQRT;
    if (n == "log") return F_LOG;
    if (n == "log10") return F_LOG10;
    if (n == "sin") return F_SIN;
    if (n == "cos") return F_COS;
    if (n == "tan") return F_TAN;
    if (n == "pow") return F_POW2;
    if (n == "min" || n == "minimum") return F_MIN2;
    if (n == "max" || n == "maximum") return F_MAX2;
    if (n == "floor") return F_FLOOR;
    if (n == "ceil") return F_CEIL;
    if (n == "round") return F_ROUND;
    if (n == "trunc") return F_TRUNC;
    if (n == "sign" || n == "sgn") return F_SIGN;
    if (n == "asin" || n == "arcsin") return F_ASIN;
    if (n == "acos" || n == "arccos") return F_ACOS;
    if (n == "atan" || n == "arctan") return F_ATAN;
    if (n == "sinh") return F_SINH;
    if (n == "cosh") return F_COSH;
    if (n == "tanh") return F_TANH;
    if (n == "log2") return F_LOG2;
    if (n == "expm1") return F_EXPM1;
    if (n == "log1p") return F_LOG1P;
    if (n == "deg2rad" || n == "radians") return F_DEG2RAD;
    if (n == "rad2deg" || n == "degrees") return F_RAD2DEG;
    if (n == "erf") return F_ERF;
    if (n == "erfc") return F_ERFC;
    if (n == "atan2" || n == "arctan2") return F_ATAN2;
    if (n == "hypot") return F_HYPOT;
    if (n == "root") return F_ROOT;
    if (n == "logn") return F_LOGN;
    if (n == "frac") return F_FRAC;
    return -1;
}

//! Names the engine resolves itself, so they are not free variables.
/*! Longer than the set of functions the evaluator implements: a name in here
    that the evaluator does not know makes \ref compile fail rather than turn
    into a column nobody can supply. */
bool is_reserved(const std::string& name) {
    static const char* kNames[] = {
        "abs", "fabs", "exp", "sqrt", "log", "log10", "log2", "sin", "cos",
        "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "pow",
        "min", "max", "minimum", "maximum", "avg", "sum", "floor", "ceil",
        "round", "sgn", "erf", "erfc", "frac", "trunc", "clamp", "inrange",
        "root", "hypot", "logn", "expm1", "log1p", "deg2rad", "rad2deg",
        "not", "and", "or", "xor", "nand", "nor", "if", "else", "while",
        "for", "true", "false", "pi", "epsilon", "inf", "e"};
    for (const char* n : kNames) {
        if (name == n) return true;
    }
    return false;
}

//! Python's precedence: or < and < not < comparison < +- < */ < unary- < **.
/*! Spaced by two so the two prefix operators fit between the binary ones: `not`
    binds looser than a comparison and tighter than `and`, unary minus binds
    looser than `**` and tighter than `*`. \see unary_precedence */
int precedence(int op) {
    switch (op) {
        case B_OR: return -4;
        case B_AND: return -2;
        case B_LT: case B_LE: case B_GT:
        case B_GE: case B_EQ: case B_NE: return 0;
        case B_ADD: case B_SUB: return 2;
        case B_MUL: case B_DIV: return 4;
        case B_POW: return 8;
        default: return -6;
    }
}

//! Where a pending prefix operator sits in the same ladder.
/*! Load-bearing, and it was missing: a pending unary minus used to be flushed
    only once its operand was complete, and never when the operand ended in a
    `**`. So `-g**2 < 0` compiled as `-((g**2) < 0)` -- a negated *boolean* --
    where Python means `(-(g**2)) < 0`. Anything of the shape
    "unary minus, then `**`, then something that binds looser" was wrong. */
int unary_precedence(int f) { return (f == F_NOT) ? -1 : 6; }

struct Token {
    int kind = 0;  //!< 0 number, 1 name, 2 operator, 3 '(', 4 ')', 5 ','
    double number = 0.0;
    std::string name;
    int op = 0;
    bool unary = false;
};

//! Tokenise, marking a leading +/- as unary.
/*! False for any character this evaluator does not know, which is how an
    expression it cannot represent is refused rather than half-parsed. */
bool tokenize(const std::string& s, std::vector<Token>* out) {
    std::size_t i = 0;
    bool value_before = false;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            char* end = nullptr;
            const double v = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return false;
            const std::size_t stop = static_cast<std::size_t>(end - s.c_str());
            // A number may not run straight into a name. Without this "1e+"
            // tokenises as 1, +, e and quietly evaluates to 3.718 -- the one
            // hole a separate validating parser used to cover.
            if (stop < s.size() &&
                (std::isalnum(static_cast<unsigned char>(s[stop])) ||
                 s[stop] == '_' || s[stop] == '.')) {
                return false;
            }
            Token t; t.kind = 0; t.number = v; out->push_back(t);
            i = stop;
            value_before = true;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) {
                ++j;
            }
            Token t; t.name = s.substr(i, j - i);
            if (t.name == "and" || t.name == "or") {
                t.kind = 2; t.op = (t.name == "and") ? B_AND : B_OR;
                value_before = false;
            } else if (t.name == "not") {
                // Unary; carried as a function so it binds looser than a
                // comparison, which is what makes "not x > 2" mean
                // "not (x > 2)" as it does in Python.
                t.kind = 2; t.op = B_AND; t.unary = true; t.name = "not";
                value_before = false;
            } else {
                t.kind = 1;
                value_before = true;
            }
            out->push_back(t);
            i = j;
            continue;
        }
        Token t;
        if (c == '(') { t.kind = 3; value_before = false; }
        else if (c == ')') { t.kind = 4; value_before = true; }
        else if (c == ',') { t.kind = 5; value_before = false; }
        else if (c == '+' || c == '-') {
            t.kind = 2; t.op = (c == '+') ? B_ADD : B_SUB;
            t.unary = !value_before; value_before = false;
        } else if (c == '^') { t.kind = 2; t.op = B_POW; value_before = false; }
        else if (c == '*') { t.kind = 2; t.op = B_MUL; value_before = false; }
        else if (c == '/') { t.kind = 2; t.op = B_DIV; value_before = false; }
        else if (c == '<') {
            t.kind = 2; t.op = B_LT;
            if (i + 1 < s.size() && s[i + 1] == '=') { t.op = B_LE; ++i; }
            value_before = false;
        } else if (c == '>') {
            t.kind = 2; t.op = B_GT;
            if (i + 1 < s.size() && s[i + 1] == '=') { t.op = B_GE; ++i; }
            value_before = false;
        } else if (c == '=' && i + 1 < s.size() && s[i + 1] == '=') {
            t.kind = 2; t.op = B_EQ; ++i; value_before = false;
        } else if (c == '!' && i + 1 < s.size() && s[i + 1] == '=') {
            t.kind = 2; t.op = B_NE; ++i; value_before = false;
        } else {
            return false;
        }
        out->push_back(t);
        ++i;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Scalar arithmetic
//
// The constant folder calls exactly these, so a folded subtree is bit-identical
// to what the evaluator's own scalar path would have produced at run time --
// only *when* the arithmetic happens changes.
// ---------------------------------------------------------------------------

//! numpy's `minimum`: a NaN in either operand propagates.
/*! The ternary form this replaces -- `(a > b) ? b : a` -- is neither numpy's
    rule nor C's `fmin`, and made `min` non-commutative under NaN:
    `min(y, nan)` was `y` while `min(nan, y)` was `nan`. */
template <typename T>
inline T numpy_min(T a, T b) {
    if (a != a) return a;
    if (b != b) return b;
    return (b < a) ? b : a;
}

template <typename T>
inline T numpy_max(T a, T b) {
    if (a != a) return a;
    if (b != b) return b;
    return (b > a) ? b : a;
}

inline double apply_binary_scalar(int f, double a, double b) {
    switch (f) {
        case F_POW2: return std::pow(a, b);
        case F_MIN2: return numpy_min(a, b);
        case F_ATAN2: return std::atan2(a, b);
        case F_HYPOT: return std::hypot(a, b);
        // ExprTk's spellings, kept at ExprTk's semantics so a query that used
        // the fallback answers the same after its removal.
        case F_ROOT: return std::pow(a, 1.0 / b);
        case F_LOGN: return std::log(a) / std::log(b);
        default: return numpy_max(a, b);
    }
}

inline double apply_scalar_fun(int f, double a) {
    switch (f) {
        case F_NEG: return -a;
        case F_ABS: return std::fabs(a);
        case F_EXP: return std::exp(a);
        case F_SQRT: return std::sqrt(a);
        case F_LOG: return std::log(a);
        case F_LOG10: return std::log10(a);
        case F_SIN: return std::sin(a);
        case F_COS: return std::cos(a);
        case F_TAN: return std::tan(a);
        // numpy's truthiness, not ExprTk's `> 0.5`: anything not zero is true,
        // which keeps a negative value and a NaN true.
        case F_NOT: return (a != 0.0) ? 0.0 : 1.0;
        case F_FLOOR: return std::floor(a);
        case F_CEIL: return std::ceil(a);
        // std::round is half-away-from-zero; numpy's `round` is half-to-even,
        // and a gate on a .5 boundary would disagree. std::nearbyint follows
        // the current rounding mode, which is round-to-nearest-even.
        case F_ROUND: return std::nearbyint(a);
        case F_TRUNC: return std::trunc(a);
        // numpy's sign: -1, 0 or 1, and NaN for NaN -- not copysign, which
        // has no zero case and would turn 0 into 1.
        case F_SIGN: return (a > 0.0) ? 1.0 : ((a < 0.0) ? -1.0
                                                        : (a != a ? a : 0.0));
        case F_ASIN: return std::asin(a);
        case F_ACOS: return std::acos(a);
        case F_ATAN: return std::atan(a);
        case F_SINH: return std::sinh(a);
        case F_COSH: return std::cosh(a);
        case F_TANH: return std::tanh(a);
        case F_LOG2: return std::log2(a);
        case F_EXPM1: return std::expm1(a);
        case F_LOG1P: return std::log1p(a);
        case F_DEG2RAD: return a * (3.14159265358979323846 / 180.0);
        case F_RAD2DEG: return a * (180.0 / 3.14159265358979323846);
        case F_ERF: return std::erf(a);
        case F_ERFC: return std::erfc(a);
        // Fractional part with ExprTk's (and C's) toward-zero convention:
        // frac(-1.25) is -0.25, not 0.75.
        case F_FRAC: return a - std::trunc(a);
        default: return a;
    }
}

// ---------------------------------------------------------------------------
// Block kernels
//
// Templated on the working type, because the two callers differ in it: a gate
// over float32 burst columns runs in float32 (four lanes, and bit-for-bit what
// numpy gives over the same columns) while everything else runs in double.
// ---------------------------------------------------------------------------

#if TTTRLIB_EXPR_NEON
template <typename T> struct Neon;

template <> struct Neon<double> {
    typedef float64x2_t vec;
    static const std::size_t lanes = 2;
    static vec load(const double* p) { return vld1q_f64(p); }
    static void store(double* p, vec v) { vst1q_f64(p, v); }
    static vec dup(double v) { return vdupq_n_f64(v); }
    static vec add(vec a, vec b) { return vaddq_f64(a, b); }
    static vec sub(vec a, vec b) { return vsubq_f64(a, b); }
    static vec mul(vec a, vec b) { return vmulq_f64(a, b); }
    static vec div(vec a, vec b) { return vdivq_f64(a, b); }
    static vec sqrt_(vec a) { return vsqrtq_f64(a); }
    static vec abs_(vec a) { return vabsq_f64(a); }
    static vec neg(vec a) { return vnegq_f64(a); }
};

template <> struct Neon<float> {
    typedef float32x4_t vec;
    static const std::size_t lanes = 4;
    static vec load(const float* p) { return vld1q_f32(p); }
    static void store(float* p, vec v) { vst1q_f32(p, v); }
    static vec dup(float v) { return vdupq_n_f32(v); }
    static vec add(vec a, vec b) { return vaddq_f32(a, b); }
    static vec sub(vec a, vec b) { return vsubq_f32(a, b); }
    static vec mul(vec a, vec b) { return vmulq_f32(a, b); }
    static vec div(vec a, vec b) { return vdivq_f32(a, b); }
    static vec sqrt_(vec a) { return vsqrtq_f32(a); }
    static vec abs_(vec a) { return vabsq_f32(a); }
    static vec neg(vec a) { return vnegq_f32(a); }
};
#endif

// Elementwise a op= b. The compiler does not vectorise these to the width the
// hardware has -- measured, not assumed -- and a scalar loop runs one lane per
// cycle where NEON will do two doubles or four floats.
#if TTTRLIB_EXPR_NEON
#define TTTRLIB_EXPR_KERNEL2(name, vop, sop)                                  \
    template <typename T>                                                     \
    inline void name(T* __restrict a, const T* __restrict b, std::size_t n) { \
        typedef Neon<T> N;                                                    \
        std::size_t i = 0;                                                    \
        for (; i + N::lanes <= n; i += N::lanes)                              \
            N::store(a + i, N::vop(N::load(a + i), N::load(b + i)));           \
        for (; i < n; ++i) a[i] sop b[i];                                     \
    }
#else
#define TTTRLIB_EXPR_KERNEL2(name, vop, sop)                                  \
    template <typename T>                                                     \
    inline void name(T* __restrict a, const T* __restrict b, std::size_t n) { \
        for (std::size_t i = 0; i < n; ++i) a[i] sop b[i];                    \
    }
#endif

TTTRLIB_EXPR_KERNEL2(simd_add, add, +=)
TTTRLIB_EXPR_KERNEL2(simd_sub, sub, -=)
TTTRLIB_EXPR_KERNEL2(simd_mul, mul, *=)
// A Newton-Raphson reciprocal was tried here and measured no faster: the
// per-lane guards for zero and non-finite inputs cost as much as the division.
TTTRLIB_EXPR_KERNEL2(simd_div, div, /=)

#undef TTTRLIB_EXPR_KERNEL2

//! a op= v, against one broadcast value.
/*! A scalar parameter never becomes an array, which is where a column of a
    million rows used to be written out once per scalar operand. */
template <typename T>
inline void simd_add_s(T* __restrict a, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::add(N::load(a + i), w));
#endif
    for (; i < n; ++i) a[i] += v;
}

template <typename T>
inline void simd_mul_s(T* __restrict a, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::mul(N::load(a + i), w));
#endif
    for (; i < n; ++i) a[i] *= v;
}

template <typename T>
inline void simd_sub_s(T* __restrict a, T v, std::size_t n) {
    simd_add_s(a, static_cast<T>(-v), n);
}

//! Whether 1/v is exact, so a divide may become a multiply.
/*! The reciprocal trick is worth real time -- division is the slowest
    arithmetic instruction on every machine this runs on -- but it is not
    exact, and a gate compares a computed value against a boundary where one
    ulp flips a row. So it is taken only where it changes nothing: a power of
    two, whose reciprocal is another power of two. */
template <typename T>
inline bool reciprocal_is_exact(T v) {
    if (!(v == v) || v == T(0)) return false;
    int e = 0;
    const T m = static_cast<T>(std::frexp(static_cast<double>(v), &e));
    if (m != T(0.5) && m != T(-0.5)) return false;
    // Denormal or huge: the reciprocal itself may not be representable.
    const T r = T(1) / v;
    return r == r && r != T(0) && std::isfinite(static_cast<double>(r));
}

template <typename T>
inline void simd_div_s(T* __restrict a, T v, std::size_t n) {
    if (reciprocal_is_exact(v)) { simd_mul_s(a, static_cast<T>(T(1) / v), n); return; }
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::div(N::load(a + i), w));
#endif
    for (; i < n; ++i) a[i] /= v;
}

// scalar op vector, written straight to its destination slot. The obvious form
// -- compute in the right-hand slot, then copy it down to the left -- costs an
// extra read and write of the whole block per operation.
template <typename T>
inline void simd_add_sv(T* __restrict dst, const T* __restrict src, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(dst + i, N::add(N::load(src + i), w));
#endif
    for (; i < n; ++i) dst[i] = src[i] + v;
}

template <typename T>
inline void simd_mul_sv(T* __restrict dst, const T* __restrict src, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(dst + i, N::mul(N::load(src + i), w));
#endif
    for (; i < n; ++i) dst[i] = src[i] * v;
}

template <typename T>
inline void simd_rsub_sv(T* __restrict dst, const T* __restrict src, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(dst + i, N::sub(w, N::load(src + i)));
#endif
    for (; i < n; ++i) dst[i] = v - src[i];
}

template <typename T>
inline void simd_rdiv_sv(T* __restrict dst, const T* __restrict src, T v, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec w = N::dup(v);
    for (; i + N::lanes <= n; i += N::lanes) N::store(dst + i, N::div(w, N::load(src + i)));
#endif
    for (; i < n; ++i) dst[i] = v / src[i];
}

template <typename T>
inline void simd_recip(T* __restrict a, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    const typename N::vec one = N::dup(T(1));
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::div(one, N::load(a + i)));
#endif
    for (; i < n; ++i) a[i] = T(1) / a[i];
}

template <typename T>
inline void simd_sqrt(T* __restrict a, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::sqrt_(N::load(a + i)));
#endif
    for (; i < n; ++i) a[i] = std::sqrt(a[i]);
}

template <typename T>
inline void simd_abs(T* __restrict a, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::abs_(N::load(a + i)));
#endif
    for (; i < n; ++i) a[i] = std::abs(a[i]);
}

template <typename T>
inline void simd_neg(T* __restrict a, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    typedef Neon<T> N;
    for (; i + N::lanes <= n; i += N::lanes) N::store(a + i, N::neg(N::load(a + i)));
#endif
    for (; i < n; ++i) a[i] = -a[i];
}

//! Raise a block to a constant power.
/*! `x**-1` and `x**0.5` are what real equations are made of, and a general
    std::pow() per element costs an order of magnitude more than the reciprocal
    or square root it stands for. numpy special-cases these too -- its
    `fast_scalar_power` rewrites `arr ** -1` before the ufunc is reached -- and
    not doing so was the whole of one benchmark row's loss to it. */
template <typename T>
inline void apply_powc(double c, T* __restrict a, std::size_t n) {
    if (c == -1.0) {
        simd_recip(a, n);
    } else if (c == 0.5) {
        simd_sqrt(a, n);
    } else if (c == -0.5) {
        for (std::size_t i = 0; i < n; ++i) a[i] = T(1) / std::sqrt(a[i]);
    } else if (c == 2.0) {
        for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * a[i];
    } else if (c == 3.0) {
        for (std::size_t i = 0; i < n; ++i) a[i] = a[i] * a[i] * a[i];
    } else if (c == 1.0) {
        // nothing to do
    } else if (c == -2.0) {
        for (std::size_t i = 0; i < n; ++i) a[i] = T(1) / (a[i] * a[i]);
    } else {
        const T e = static_cast<T>(c);
        for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], e);
    }
}

//! One binary arithmetic operation across a block.
/*! Comparisons and the boolean operators never arrive here: they are answered
    on the byte stack, which is the whole point of carrying one. */
template <typename T>
inline void apply_bin(int op, T* __restrict a, const T* __restrict b, std::size_t n) {
    switch (op) {
        case B_ADD: simd_add(a, b, n); break;
        case B_SUB: simd_sub(a, b, n); break;
        case B_MUL: simd_mul(a, b, n); break;
        case B_DIV: simd_div(a, b, n); break;
        default: for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], b[i]);
    }
}

template <typename T>
inline void apply_fun(int f, T* __restrict a, std::size_t n) {
    switch (f) {
        case F_NEG: simd_neg(a, n); break;
        case F_ABS: simd_abs(a, n); break;
        case F_EXP: for (std::size_t i = 0; i < n; ++i) a[i] = std::exp(a[i]); break;
        case F_SQRT: simd_sqrt(a, n); break;
        case F_LOG: for (std::size_t i = 0; i < n; ++i) a[i] = std::log(a[i]); break;
        case F_LOG10: for (std::size_t i = 0; i < n; ++i) a[i] = std::log10(a[i]); break;
        case F_SIN: for (std::size_t i = 0; i < n; ++i) a[i] = std::sin(a[i]); break;
        case F_COS: for (std::size_t i = 0; i < n; ++i) a[i] = std::cos(a[i]); break;
        case F_TAN: for (std::size_t i = 0; i < n; ++i) a[i] = std::tan(a[i]); break;
        // Scalar loops, not intrinsics. There is no NEON kernel for a
        // transcendental, and these are all libm calls; writing them as a
        // plain loop is what the existing exp/log/sin cases already do.
        case F_FLOOR: for (std::size_t i = 0; i < n; ++i) a[i] = std::floor(a[i]); break;
        case F_CEIL: for (std::size_t i = 0; i < n; ++i) a[i] = std::ceil(a[i]); break;
        case F_ROUND: for (std::size_t i = 0; i < n; ++i) a[i] = std::nearbyint(a[i]); break;
        case F_TRUNC: for (std::size_t i = 0; i < n; ++i) a[i] = std::trunc(a[i]); break;
        case F_SIGN: for (std::size_t i = 0; i < n; ++i) {
            const T v = a[i];
            a[i] = (v > T(0)) ? T(1) : ((v < T(0)) ? T(-1) : (v != v ? v : T(0)));
        } break;
        case F_ASIN: for (std::size_t i = 0; i < n; ++i) a[i] = std::asin(a[i]); break;
        case F_ACOS: for (std::size_t i = 0; i < n; ++i) a[i] = std::acos(a[i]); break;
        case F_ATAN: for (std::size_t i = 0; i < n; ++i) a[i] = std::atan(a[i]); break;
        case F_SINH: for (std::size_t i = 0; i < n; ++i) a[i] = std::sinh(a[i]); break;
        case F_COSH: for (std::size_t i = 0; i < n; ++i) a[i] = std::cosh(a[i]); break;
        case F_TANH: for (std::size_t i = 0; i < n; ++i) a[i] = std::tanh(a[i]); break;
        case F_LOG2: for (std::size_t i = 0; i < n; ++i) a[i] = std::log2(a[i]); break;
        case F_EXPM1: for (std::size_t i = 0; i < n; ++i) a[i] = std::expm1(a[i]); break;
        case F_LOG1P: for (std::size_t i = 0; i < n; ++i) a[i] = std::log1p(a[i]); break;
        case F_DEG2RAD: for (std::size_t i = 0; i < n; ++i)
            a[i] = a[i] * T(3.14159265358979323846 / 180.0); break;
        case F_RAD2DEG: for (std::size_t i = 0; i < n; ++i)
            a[i] = a[i] * T(180.0 / 3.14159265358979323846); break;
        case F_ERF: for (std::size_t i = 0; i < n; ++i) a[i] = std::erf(a[i]); break;
        case F_ERFC: for (std::size_t i = 0; i < n; ++i) a[i] = std::erfc(a[i]); break;
        case F_FRAC: for (std::size_t i = 0; i < n; ++i) a[i] = a[i] - std::trunc(a[i]); break;
        default: break;
    }
}

template <typename T>
inline void apply_fun2(int f, T* __restrict a, const T* __restrict b, std::size_t n) {
    switch (f) {
        case F_POW2: for (std::size_t i = 0; i < n; ++i) a[i] = std::pow(a[i], b[i]); break;
        case F_MIN2: for (std::size_t i = 0; i < n; ++i) a[i] = numpy_min(a[i], b[i]); break;
        case F_ATAN2: for (std::size_t i = 0; i < n; ++i) a[i] = std::atan2(a[i], b[i]); break;
        case F_HYPOT: for (std::size_t i = 0; i < n; ++i) a[i] = std::hypot(a[i], b[i]); break;
        case F_ROOT: for (std::size_t i = 0; i < n; ++i)
            a[i] = std::pow(a[i], T(1) / b[i]); break;
        case F_LOGN: for (std::size_t i = 0; i < n; ++i)
            a[i] = std::log(a[i]) / std::log(b[i]); break;
        default: for (std::size_t i = 0; i < n; ++i) a[i] = numpy_max(a[i], b[i]);
    }
}

// ---------------------------------------------------------------------------
// Booleans: one byte per row, never a double
// ---------------------------------------------------------------------------

#if TTTRLIB_EXPR_NEON
inline std::size_t cmp_bytes_simd(int op, const double* a, const double* b,
                                  unsigned char* out, std::size_t n) {
    std::size_t i = 0;
    for (; i + 2 <= n; i += 2) {
        const float64x2_t x = vld1q_f64(a + i), y = vld1q_f64(b + i);
        uint64x2_t m;
        switch (op) {
            case B_LT: m = vcltq_f64(x, y); break;
            case B_LE: m = vcleq_f64(x, y); break;
            case B_GT: m = vcgtq_f64(x, y); break;
            case B_GE: m = vcgeq_f64(x, y); break;
            case B_EQ: m = vceqq_f64(x, y); break;
            default: m = vreinterpretq_u64_u32(
                         vmvnq_u32(vreinterpretq_u32_u64(vceqq_f64(x, y)))); break;
        }
        out[i] = vgetq_lane_u64(m, 0) ? 1 : 0;
        out[i + 1] = vgetq_lane_u64(m, 1) ? 1 : 0;
    }
    return i;
}

inline std::size_t cmp_bytes_simd(int op, const float* a, const float* b,
                                  unsigned char* out, std::size_t n) {
    std::size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t x = vld1q_f32(a + i), y = vld1q_f32(b + i);
        uint32x4_t m;
        switch (op) {
            case B_LT: m = vcltq_f32(x, y); break;
            case B_LE: m = vcleq_f32(x, y); break;
            case B_GT: m = vcgtq_f32(x, y); break;
            case B_GE: m = vcgeq_f32(x, y); break;
            case B_EQ: m = vceqq_f32(x, y); break;
            default: m = vmvnq_u32(vceqq_f32(x, y)); break;
        }
        const uint16x4_t m16 = vmovn_u32(m);
        const uint8x8_t m8 = vmovn_u16(vcombine_u16(m16, m16));
        const uint32_t packed =
            vget_lane_u32(vreinterpret_u32_u8(vand_u8(m8, vdup_n_u8(1))), 0);
        std::memcpy(out + i, &packed, 4);
    }
    return i;
}
#endif

//! Comparisons, branchless, writing one BYTE per row rather than a double.
/*! Carrying a gate's result as an 8-byte value is inherited from an
    everything-is-a-double interpreter and costs eight times the memory traffic
    of the byte numpy uses -- which is most of why a comparison-heavy query used
    to lose to it. */
template <typename T>
inline void cmp_to_bytes(int op, const T* __restrict a, const T* __restrict b,
                         unsigned char* __restrict out, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    i = cmp_bytes_simd(op, a, b, out, n);
#endif
    for (; i < n; ++i) {
        bool v;
        switch (op) {
            case B_LT: v = a[i] < b[i]; break;
            case B_LE: v = a[i] <= b[i]; break;
            case B_GT: v = a[i] > b[i]; break;
            case B_GE: v = a[i] >= b[i]; break;
            case B_EQ: v = a[i] == b[i]; break;
            default: v = a[i] != b[i]; break;
        }
        out[i] = v ? 1 : 0;
    }
}

inline void bool_combine(int op, unsigned char* __restrict a,
                         const unsigned char* __restrict b, std::size_t n) {
    std::size_t i = 0;
#if TTTRLIB_EXPR_NEON
    if (op == B_AND) {
        for (; i + 16 <= n; i += 16)
            vst1q_u8(a + i, vandq_u8(vld1q_u8(a + i), vld1q_u8(b + i)));
    } else {
        for (; i + 16 <= n; i += 16)
            vst1q_u8(a + i, vorrq_u8(vld1q_u8(a + i), vld1q_u8(b + i)));
    }
#endif
    if (op == B_AND) {
        for (; i < n; ++i) a[i] = static_cast<unsigned char>(a[i] & b[i]);
    } else {
        for (; i < n; ++i) a[i] = static_cast<unsigned char>(a[i] | b[i]);
    }
}

inline void bool_not(unsigned char* __restrict a, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) a[i] = a[i] ? 0 : 1;
}

//! Pack 64 bytes of 0/1 into one word, bit *k* from byte *k*.
inline std::uint64_t pack64(const unsigned char* b) {
#if TTTRLIB_EXPR_NEON
    static const uint8_t kWeights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                         1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t weights = vld1q_u8(kWeights);
    std::uint64_t r = 0;
    for (int k = 0; k < 4; ++k) {
        const uint8x16_t v = vld1q_u8(b + k * 16);
        const uint8x16_t bits = vandq_u8(vtstq_u8(v, v), weights);
        const std::uint64_t lo = vaddv_u8(vget_low_u8(bits));
        const std::uint64_t hi = vaddv_u8(vget_high_u8(bits));
        r |= (lo | (hi << 8)) << (16 * k);
    }
    return r;
#else
    std::uint64_t r = 0;
    for (int k = 0; k < 64; ++k) {
        if (b[k]) r |= 1ULL << k;
    }
    return r;
#endif
}

//! Pack a block of mask bytes into words. Bits past \p len are cleared.
inline void pack_block(const unsigned char* src, std::size_t len, std::uint64_t* w) {
    std::size_t k = 0;
    for (; k + 64 <= len; k += 64) w[k >> 6] = pack64(src + k);
    if (k < len) {
        unsigned char tail[64];
        std::memset(tail, 0, sizeof(tail));
        std::memcpy(tail, src + k, len - k);
        w[k >> 6] = pack64(tail);
    }
}

// ---------------------------------------------------------------------------
// Reading a column into a block, converting as it goes
//
// The alternative -- widening every referenced column into a full-length double
// buffer before evaluating -- costs a pass over memory and n extra doubles per
// column. A block load has to copy anyway, so converting inside that copy is
// free.
// ---------------------------------------------------------------------------

template <typename T, typename S>
inline void convert_block(const S* src, std::size_t n, T* dst) {
    for (std::size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(src[i]);
}

inline void convert_block(const double* src, std::size_t n, double* dst) {
    std::memcpy(dst, src, n * sizeof(double));
}

inline void convert_block(const float* src, std::size_t n, float* dst) {
    std::memcpy(dst, src, n * sizeof(float));
}

template <typename T>
inline void load_block(const ExprColumn& c, std::size_t base, std::size_t len, T* dst) {
    switch (c.type) {
        case ExprScalarType::Float64:
            convert_block(static_cast<const double*>(c.data) + base, len, dst); break;
        case ExprScalarType::Float32:
            convert_block(static_cast<const float*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int64:
            convert_block(static_cast<const std::int64_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int32:
            convert_block(static_cast<const std::int32_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int16:
            convert_block(static_cast<const std::int16_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::Int8:
            convert_block(static_cast<const std::int8_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt64:
            convert_block(static_cast<const std::uint64_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt32:
            convert_block(static_cast<const std::uint32_t*>(c.data) + base, len, dst); break;
        case ExprScalarType::UInt16:
            convert_block(static_cast<const std::uint16_t*>(c.data) + base, len, dst); break;
        default:
            convert_block(static_cast<const std::uint8_t*>(c.data) + base, len, dst); break;
    }
}

template <typename T>
inline T broadcast_value(const ExprColumn& c) {
    T v = T(0);
    load_block(c, 0, 1, &v);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Compilation
// ---------------------------------------------------------------------------

inline std::string ExpressionEngine::normalise(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (c == '*' && next == '*') {
            out.push_back('^');
            ++i;
        } else if (c == '&') {
            out += " and ";
            if (next == '&') ++i;
        } else if (c == '|') {
            out += " or ";
            if (next == '|') ++i;
        } else if (c == '~') {
            out += " not ";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

inline std::vector<std::string> ExpressionEngine::free_variables(const std::string& s) {
    std::vector<std::string> names;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) {
                ++j;
            }
            const std::string name = s.substr(i, j - i);
            std::size_t k = j;
            while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
            const bool is_call = (k < s.size() && s[k] == '(');
            if (!is_call && !is_reserved(name) &&
                std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            // Skip a number whole, so the 'e' of 1e-3 is never taken for a name.
            std::size_t j = i;
            while (j < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                ++j;
            }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
                if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                    j = k;
                    while (j < s.size() &&
                           std::isdigit(static_cast<unsigned char>(s[j]))) {
                        ++j;
                    }
                }
            }
            i = j;
        } else {
            ++i;
        }
    }
    return names;
}

inline bool ExpressionEngine::compile(const std::string& expression) {
    program_.clear();
    program_depth_ = 0;
    program_cache_size_ = 0;
    expression_ = expression;
    normalised_ = normalise(expression);
    variables_ = free_variables(normalised_);
    if (!compile_program(normalised_)) {
        program_.clear();
        program_depth_ = 0;
        program_cache_size_ = 0;
        return false;
    }
    return true;
}

inline bool ExpressionEngine::compile_program(const std::string& normalised) {
    program_.clear();
    program_cache_size_ = 0;
    std::vector<Token> tokens;
    if (!tokenize(normalised, &tokens)) return false;
    if (tokens.empty()) return false;

    std::vector<std::pair<bool, int> > stack;  // (is_function, id); '(' is (false,-1)
    auto emit = [this](int kind, double value, int index) {
        VecOp o; o.kind = kind; o.value = value; o.index = index;
        program_.push_back(o);
    };

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == 0) {
            emit(OP_CONST, t.number, 0);
        } else if (t.kind == 1) {
            const bool is_call = (i + 1 < tokens.size() && tokens[i + 1].kind == 3);
            if (is_call) {
                const int f = function_id(t.name);
                if (f < 0) return false;
                stack.push_back(std::make_pair(true, f));
            } else if (t.name == "pi") {
                emit(OP_CONST, 3.14159265358979323846, 0);
            } else if (t.name == "e") {
                emit(OP_CONST, 2.71828182845904523536, 0);
            } else {
                std::vector<std::string>::const_iterator it =
                    std::find(variables_.begin(), variables_.end(), t.name);
                if (it == variables_.end()) return false;
                emit(OP_VAR, 0.0, static_cast<int>(it - variables_.begin()));
            }
        } else if (t.kind == 2) {
            if (t.unary) {
                if (t.name == "not") stack.push_back(std::make_pair(true, F_NOT));
                else if (t.op == B_SUB) stack.push_back(std::make_pair(true, F_NEG));
                continue;
            }
            while (!stack.empty()) {
                const bool is_fun = stack.back().first;
                const int top = stack.back().second;
                int p;
                if (is_fun) {
                    // Only a pending prefix operator can be a bare function on
                    // the stack; a call always pushed its '(' straight after.
                    if (top != F_NEG && top != F_NOT) break;
                    p = unary_precedence(top);
                } else {
                    if (top < 0) break;  // '('
                    p = precedence(top);
                }
                const bool higher = p > precedence(t.op);
                const bool equal_left = p == precedence(t.op) && t.op != B_POW;
                if (!(higher || equal_left)) break;
                emit(is_fun ? OP_FUN : OP_BIN, 0.0, top);
                stack.pop_back();
            }
            stack.push_back(std::make_pair(false, t.op));
        } else if (t.kind == 3) {
            stack.push_back(std::make_pair(false, -1));
        } else if (t.kind == 4) {
            bool found = false;
            while (!stack.empty()) {
                if (!stack.back().first && stack.back().second == -1) {
                    stack.pop_back(); found = true; break;
                }
                emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
                stack.pop_back();
            }
            if (!found) return false;
            if (!stack.empty() && stack.back().first && stack.back().second != F_NEG) {
                emit(OP_FUN, 0.0, stack.back().second);
                stack.pop_back();
            }
        } else if (t.kind == 5) {
            while (!stack.empty() &&
                   !(!stack.back().first && stack.back().second == -1)) {
                emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
                stack.pop_back();
            }
        }
        // A pending unary minus applies once its operand is complete, but not
        // before a '^': -x^2 is -(x^2), as in Python.
        if (t.kind == 0 || t.kind == 1 || t.kind == 4) {
            while (!stack.empty() && stack.back().first &&
                   (stack.back().second == F_NEG || stack.back().second == F_NOT)) {
                // 'not' takes the whole comparison to its right, so it waits.
                if (stack.back().second == F_NOT) break;
                if (i + 1 < tokens.size() && tokens[i + 1].kind == 3) break;
                if (i + 1 < tokens.size() && tokens[i + 1].kind == 2 &&
                    tokens[i + 1].op == B_POW && !tokens[i + 1].unary) break;
                emit(OP_FUN, 0.0, F_NEG);
                stack.pop_back();
            }
        }
    }
    while (!stack.empty()) {
        if (!stack.back().first && stack.back().second == -1) return false;
        emit(stack.back().first ? OP_FUN : OP_BIN, 0.0, stack.back().second);
        stack.pop_back();
    }
    if (program_.empty()) return false;

    // Fold every constant subtree, and with it "push constant, then raise to
    // it" into one instruction, so the exponent is known when the block runs
    // rather than fetched per element from a buffer.
    //
    // The fold used to look only for a bare OP_CONST under a power, which
    // missed the shape real equations are made of: `x**(-1)` parses as a
    // *negated* constant -- OP_CONST 1 then OP_FUN F_NEG -- so the exponent
    // stayed a buffer and every element paid a std::pow() call, 9.7 ns a point.
    {
        std::vector<VecOp> folded;
        folded.reserve(program_.size());
        auto is_const_at = [&folded](std::size_t back) {
            return folded.size() >= back &&
                   folded[folded.size() - back].kind == OP_CONST;
        };
        for (std::size_t i = 0; i < program_.size(); ++i) {
            const VecOp& o = program_[i];
            // `not` is left alone: folding it to a plain 1.0 or 0.0 would lose
            // the fact that its slot is a mask, which is reconciled at the
            // stack's boundaries and not here. Comparisons, below, likewise.
            if (o.kind == OP_FUN && !binary_function(o.index) && o.index != F_NOT &&
                is_const_at(1)) {
                folded.back().value = apply_scalar_fun(o.index, folded.back().value);
                continue;
            }
            if (o.kind == OP_FUN && binary_function(o.index) && is_const_at(2) &&
                is_const_at(1)) {
                const double b = folded.back().value;
                folded.pop_back();
                folded.back().value = apply_binary_scalar(o.index, folded.back().value, b);
                continue;
            }
            if (o.kind == OP_POWC && is_const_at(1)) {
                folded.back().value = std::pow(folded.back().value, o.value);
                continue;
            }
            if (o.kind == OP_BIN && o.index <= B_POW && is_const_at(2) && is_const_at(1)) {
                const double b = folded.back().value;
                folded.pop_back();
                double& a = folded.back().value;
                switch (o.index) {
                    case B_ADD: a += b; break;
                    case B_SUB: a -= b; break;
                    case B_MUL: a *= b; break;
                    case B_DIV: a /= b; break;
                    default: a = std::pow(a, b); break;
                }
                continue;
            }
            if (o.kind == OP_BIN && o.index == B_POW && is_const_at(1)) {
                const double c = folded.back().value;
                folded.pop_back();
                VecOp p; p.kind = OP_POWC; p.value = c; p.index = 0;
                folded.push_back(p);
                continue;
            }
            folded.push_back(o);
        }
        program_.swap(folded);
    }

    if (!eliminate_common_subexpressions()) return false;

    // A well-formed program leaves exactly one value, and never needs a stack
    // deeper than the buffers reserved for it.
    int depth = 0, max_depth = 0;
    for (const VecOp& o : program_) {
        if (o.kind == OP_CONST || o.kind == OP_VAR || o.kind == OP_LOADC) ++depth;
        else if (o.kind == OP_POWC || o.kind == OP_SAVE) { /* in place */ }
        else if (o.kind == OP_BIN) depth -= 1;
        else if (binary_function(o.index)) depth -= 1;
        if (depth < 1) return false;
        max_depth = std::max(max_depth, depth);
    }
    if (depth != 1) return false;
    // The block stack is depth x 512 x sizeof(T); without a cap a deeply
    // nested expression asks for unbounded scratch.
    if (max_depth > kMaxDepth) return false;
    program_depth_ = max_depth;
    return true;
}

/*!
 * \brief Compute a repeated subtree once and reuse it, where that is cheaper.
 *
 * The RPN compiler emits a subtree wherever it appears, so an equation that
 * mentions `4*D*x/w_r**2` three times evaluates it three times.
 *
 * The pass hash-conses the program into a DAG: walking the RPN with a stack of
 * node ids, a node keyed by (opcode, left id, right id) that has been seen
 * before *is* the earlier node, because the operands are pure. A node reached
 * more than once is a candidate.
 *
 * Sharing is not free -- a cached slot costs one block copy to fill and one to
 * read back -- so a candidate is only taken when recomputing it costs more than
 * that. `x/1.2` does not qualify; `exp(-x/tau)` does. Cost-blind sharing was
 * measured to be a pessimisation, which is why this is per candidate.
 *
 * \return false only for a malformed program, which the caller treats as "this
 *         evaluator cannot represent the expression".
 */
inline bool ExpressionEngine::eliminate_common_subexpressions() {
    program_cache_size_ = 0;
    const std::size_t n_ops = program_.size();
    if (n_ops < 4) return true;

    struct CseKey { int kind; int index; double value; int a; int b; };
    std::vector<CseKey> keys;
    std::vector<int> counts, costs;
    std::vector<int> ids(n_ops, -1), child_a(n_ops, -1), child_b(n_ops, -1);

    // What one instruction costs, in passes over a block. A hardware divide, a
    // multiply and a memcpy are all "one"; exp/log/sin and a general
    // std::pow() are a libm call per element and are not close.
    auto op_cost = [](const VecOp& o) -> int {
        if (o.kind == OP_POWC) {
            const double c = o.value;
            if (c == -1.0 || c == 0.5 || c == 1.0 || c == 2.0) return 1;
            if (c == 3.0 || c == -2.0 || c == -0.5) return 2;
            return 8;
        }
        if (o.kind == OP_BIN) return (o.index == B_POW) ? 8 : 1;
        switch (o.index) {  // OP_FUN
            case F_EXP: case F_LOG: case F_LOG10:
            case F_SIN: case F_COS: case F_TAN: case F_POW2: return 8;
            default: return 1;
        }
    };

    std::vector<int> positions;
    positions.reserve(n_ops);
    for (std::size_t i = 0; i < n_ops; ++i) {
        const VecOp& o = program_[i];
        int arity;
        if (o.kind == OP_CONST || o.kind == OP_VAR) arity = 0;
        else if (o.kind == OP_POWC) arity = 1;
        else if (o.kind == OP_BIN) arity = 2;
        else arity = binary_function(o.index) ? 2 : 1;
        if (static_cast<int>(positions.size()) < arity) return false;
        int a = -1, b = -1;
        if (arity == 2) {
            b = positions.back(); positions.pop_back();
            a = positions.back(); positions.pop_back();
        } else if (arity == 1) {
            a = positions.back(); positions.pop_back();
        }
        child_a[i] = a;
        child_b[i] = b;

        CseKey k;
        k.kind = o.kind;
        k.index = o.index;
        k.value = o.value;
        k.a = (a >= 0) ? ids[static_cast<std::size_t>(a)] : -1;
        k.b = (b >= 0) ? ids[static_cast<std::size_t>(b)] : -1;
        int found = -1;
        for (std::size_t j = 0; j < keys.size(); ++j) {
            // The constant is compared bitwise, so -0.0 is not 0.0 and a NaN is
            // only itself: two subtrees are shared when they are the same text,
            // never when they merely compare equal.
            if (keys[j].kind == k.kind && keys[j].index == k.index &&
                keys[j].a == k.a && keys[j].b == k.b &&
                std::memcmp(&keys[j].value, &k.value, sizeof(double)) == 0) {
                found = static_cast<int>(j);
                break;
            }
        }
        if (found < 0) {
            found = static_cast<int>(keys.size());
            keys.push_back(k);
            counts.push_back(0);
            int c = 0;
            if (o.kind == OP_CONST) {
                c = 0;
            } else if (o.kind == OP_VAR) {
                c = 1;  // a column enters as a copy of the block
            } else {
                c = op_cost(o);
                if (k.a >= 0) c += costs[static_cast<std::size_t>(k.a)];
                if (k.b >= 0) c += costs[static_cast<std::size_t>(k.b)];
            }
            costs.push_back(c);
        }
        ids[i] = found;
        counts[static_cast<std::size_t>(found)] += 1;
        positions.push_back(static_cast<int>(i));
    }
    if (positions.size() != 1) return false;

    std::vector<std::pair<int, int> > ranked;  // (benefit, node id)
    for (std::size_t j = 0; j < keys.size(); ++j) {
        if (counts[j] < 2 || costs[j] < 3) continue;
        ranked.push_back(std::make_pair((counts[j] - 1) * (costs[j] - 1),
                                        static_cast<int>(j)));
    }
    if (ranked.empty()) return true;
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<int, int>& l, const std::pair<int, int>& r) {
                  return l.first > r.first;
              });
    std::vector<int> slot_of(keys.size(), -1);
    int n_cache = 0;
    for (std::size_t j = 0; j < ranked.size() && n_cache < kMaxCache; ++j) {
        slot_of[static_cast<std::size_t>(ranked[j].second)] = n_cache++;
    }

    // Re-emit in the same left-to-right post-order, replacing every occurrence
    // of a shared node after the first with a load of its slot.
    std::vector<VecOp> out;
    out.reserve(n_ops + static_cast<std::size_t>(n_cache));
    std::vector<char> saved(keys.size(), 0);
    std::vector<std::pair<int, int> > work;  // (position, visited-children?)
    work.push_back(std::make_pair(static_cast<int>(n_ops) - 1, 0));
    while (!work.empty()) {
        const int pos = work.back().first;
        const int phase = work.back().second;
        work.pop_back();
        const std::size_t id =
            static_cast<std::size_t>(ids[static_cast<std::size_t>(pos)]);
        if (phase == 0) {
            if (slot_of[id] >= 0 && saved[id]) {
                VecOp l; l.kind = OP_LOADC; l.value = 0.0; l.index = slot_of[id];
                out.push_back(l);
                continue;
            }
            work.push_back(std::make_pair(pos, 1));
            if (child_b[static_cast<std::size_t>(pos)] >= 0) {
                work.push_back(std::make_pair(child_b[static_cast<std::size_t>(pos)], 0));
            }
            if (child_a[static_cast<std::size_t>(pos)] >= 0) {
                work.push_back(std::make_pair(child_a[static_cast<std::size_t>(pos)], 0));
            }
        } else {
            out.push_back(program_[static_cast<std::size_t>(pos)]);
            if (slot_of[id] >= 0 && !saved[id]) {
                VecOp s; s.kind = OP_SAVE; s.value = 0.0; s.index = slot_of[id];
                out.push_back(s);
                saved[id] = 1;
            }
        }
    }

    // A node whose every repeat sat inside a *larger* shared node is now
    // reached once, and its store is pure cost. Drop those.
    std::vector<char> loaded(static_cast<std::size_t>(n_cache), 0);
    for (const VecOp& o : out) {
        if (o.kind == OP_LOADC) loaded[static_cast<std::size_t>(o.index)] = 1;
    }
    std::vector<VecOp> kept;
    kept.reserve(out.size());
    for (const VecOp& o : out) {
        if (o.kind == OP_SAVE && !loaded[static_cast<std::size_t>(o.index)]) continue;
        kept.push_back(o);
    }
    program_.swap(kept);
    program_cache_size_ = n_cache;
    return true;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

template <typename T>
void ExpressionEngine::run(const std::vector<ExprColumn>& columns,
                           std::size_t n_rows, double* out, std::uint64_t* words,
                           Blocks<T>& s) const {
    const std::size_t depth = static_cast<std::size_t>(program_depth_);
    if (s.stack.size() < depth * kBlock) s.stack.assign(depth * kBlock, T(0));
    if (s.bstack.size() < depth * kBlock) s.bstack.assign(depth * kBlock, 0);
    if (s.is_bool.size() < depth) s.is_bool.assign(depth, 0);
    if (s.is_scalar.size() < depth) {
        s.is_scalar.assign(depth, 0);
        s.scalar_value.assign(depth, T(0));
    }
    const std::size_t n_cache = static_cast<std::size_t>(program_cache_size_);
    if (n_cache > 0) {
        if (s.cache.size() < n_cache * kBlock) s.cache.assign(n_cache * kBlock, T(0));
        if (s.cache_bool.size() < n_cache * kBlock) s.cache_bool.assign(n_cache * kBlock, 0);
        if (s.cache_is_bool.size() < n_cache) {
            s.cache_is_bool.assign(n_cache, 0);
            s.cache_is_scalar.assign(n_cache, 0);
            s.cache_scalar.assign(n_cache, T(0));
        }
    }
    if (words != nullptr && s.pack_scratch.size() < kBlock) s.pack_scratch.assign(kBlock, 0);

    T* stack = s.stack.data();
    unsigned char* bstack = s.bstack.data();
    char* is_bool = s.is_bool.data();
    char* is_scalar = s.is_scalar.data();
    T* scalar_value = s.scalar_value.data();

    for (std::size_t base = 0; base < n_rows; base += kBlock) {
        const std::size_t len = std::min(kBlock, n_rows - base);
        std::fill(s.is_bool.begin(), s.is_bool.end(), 0);
        int top = 0;

        // The mirror of booleanise(): a comparison's byte mask used as a
        // number. `(x > 2) * 3` is legal and means 0 or 3, so the mask is
        // widened back before any arithmetic reads the slot.
        auto numerify = [&](int slot) {
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (!is_bool[sl]) return;
            const unsigned char* src = bstack + sl * kBlock;
            T* dst = stack + sl * kBlock;
            for (std::size_t i = 0; i < len; ++i) dst[i] = src[i] ? T(1) : T(0);
            is_bool[sl] = 0;
            is_scalar[sl] = 0;
        };
        auto materialise = [&](int slot) {
            numerify(slot);
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (!is_scalar[sl]) return;
            T* dst = stack + sl * kBlock;
            const T v = scalar_value[sl];
            for (std::size_t i = 0; i < len; ++i) dst[i] = v;
            is_scalar[sl] = 0;
        };
        // `and`, `or` and `not` read the byte stack, so a slot holding numbers
        // is cast to bytes first -- `(x > 2) and y` is legal and its right
        // operand is a column of numbers. Nonzero is true, as in numpy.
        auto booleanise = [&](int slot) {
            const std::size_t sl = static_cast<std::size_t>(slot);
            if (is_bool[sl]) return;
            unsigned char* dst = bstack + sl * kBlock;
            if (is_scalar[sl]) {
                std::memset(dst, scalar_value[sl] != T(0) ? 1 : 0, len);
            } else {
                const T* src = stack + sl * kBlock;
                for (std::size_t i = 0; i < len; ++i) dst[i] = (src[i] != T(0)) ? 1 : 0;
            }
            is_bool[sl] = 1;
        };

        for (const VecOp& o : program_) {
            if (o.kind == OP_CONST) {
                // A push owns the slot outright: whatever it last held -- in
                // particular a comparison's byte mask -- is gone. Leaving the
                // boolean flag set makes `a>0 and b<1 or c>2` reuse slot 1 for
                // `c` while still believing it holds `b<1`.
                is_bool[top] = 0;
                is_scalar[top] = 1;
                scalar_value[top] = static_cast<T>(o.value);
                ++top;
            } else if (o.kind == OP_VAR) {
                const ExprColumn& c = columns[static_cast<std::size_t>(o.index)];
                is_bool[top] = 0;
                if (c.is_vector) {
                    load_block(c, base, len, stack + static_cast<std::size_t>(top) * kBlock);
                    is_scalar[top] = 0;
                } else {
                    is_scalar[top] = 1;
                    scalar_value[top] = broadcast_value<T>(c);
                }
                ++top;
            } else if (o.kind == OP_SAVE) {
                // A shared subtree, finished: copied aside *with* its type, so
                // a load restores the flags a push would have set.
                const std::size_t src = static_cast<std::size_t>(top - 1);
                const std::size_t c = static_cast<std::size_t>(o.index);
                if (is_bool[src]) {
                    std::memcpy(&s.cache_bool[c * kBlock], bstack + src * kBlock, len);
                    s.cache_is_bool[c] = 1;
                    s.cache_is_scalar[c] = 0;
                } else if (is_scalar[src]) {
                    s.cache_scalar[c] = scalar_value[src];
                    s.cache_is_bool[c] = 0;
                    s.cache_is_scalar[c] = 1;
                } else {
                    std::memcpy(&s.cache[c * kBlock], stack + src * kBlock, len * sizeof(T));
                    s.cache_is_bool[c] = 0;
                    s.cache_is_scalar[c] = 0;
                }
            } else if (o.kind == OP_LOADC) {
                const std::size_t dst = static_cast<std::size_t>(top);
                const std::size_t c = static_cast<std::size_t>(o.index);
                if (s.cache_is_bool[c]) {
                    std::memcpy(bstack + dst * kBlock, &s.cache_bool[c * kBlock], len);
                    is_bool[dst] = 1;
                    is_scalar[dst] = 0;
                } else if (s.cache_is_scalar[c]) {
                    is_bool[dst] = 0;
                    is_scalar[dst] = 1;
                    scalar_value[dst] = s.cache_scalar[c];
                } else {
                    std::memcpy(stack + dst * kBlock, &s.cache[c * kBlock], len * sizeof(T));
                    is_bool[dst] = 0;
                    is_scalar[dst] = 0;
                }
                ++top;
            } else if (o.kind == OP_POWC) {
                numerify(top - 1);
                if (is_scalar[top - 1]) {
                    T& v = scalar_value[top - 1];
                    v = static_cast<T>(std::pow(static_cast<double>(v), o.value));
                    continue;
                }
                apply_powc(o.value, stack + static_cast<std::size_t>(top - 1) * kBlock, len);
            } else if (o.kind == OP_BIN && o.index >= B_LT && o.index <= B_NE) {
                // comparison: two numbers in, one byte mask out
                const int rhs = --top, lhs = top - 1;
                materialise(lhs);
                materialise(rhs);
                cmp_to_bytes(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                             stack + static_cast<std::size_t>(rhs) * kBlock,
                             bstack + static_cast<std::size_t>(lhs) * kBlock, len);
                is_bool[lhs] = 1;
            } else if (o.kind == OP_BIN && (o.index == B_AND || o.index == B_OR)) {
                const int rhs = --top, lhs = top - 1;
                booleanise(lhs);
                booleanise(rhs);
                bool_combine(o.index, bstack + static_cast<std::size_t>(lhs) * kBlock,
                             bstack + static_cast<std::size_t>(rhs) * kBlock, len);
                is_bool[lhs] = 1;
            } else if (o.kind == OP_FUN && o.index == F_NOT) {
                booleanise(top - 1);
                bool_not(bstack + static_cast<std::size_t>(top - 1) * kBlock, len);
                is_bool[top - 1] = 1;
            } else if (o.kind == OP_BIN) {
                const int rhs = --top, lhs = top - 1;
                numerify(lhs);
                numerify(rhs);
                const bool rs = is_scalar[rhs] != 0;
                const bool ls = is_scalar[lhs] != 0;
                if (ls && rs) {
                    // Both still scalar: fold, and no array is touched at all.
                    T& a = scalar_value[lhs];
                    const T b = scalar_value[rhs];
                    switch (o.index) {
                        case B_ADD: a += b; break;
                        case B_SUB: a -= b; break;
                        case B_MUL: a *= b; break;
                        case B_DIV: a /= b; break;
                        default: a = std::pow(a, b);
                    }
                    continue;
                }
                if (rs && o.index == B_POW) {
                    // vector ** scalar takes the same reciprocal/sqrt/square
                    // kernels a folded constant exponent does, whether the
                    // exponent was written as a literal or arrived as a column.
                    apply_powc(static_cast<double>(scalar_value[rhs]),
                               stack + static_cast<std::size_t>(lhs) * kBlock, len);
                    continue;
                }
                if (rs && o.index <= B_DIV) {
                    T* a = stack + static_cast<std::size_t>(lhs) * kBlock;
                    const T v = scalar_value[rhs];
                    switch (o.index) {
                        case B_ADD: simd_add_s(a, v, len); break;
                        case B_SUB: simd_sub_s(a, v, len); break;
                        case B_MUL: simd_mul_s(a, v, len); break;
                        default: simd_div_s(a, v, len); break;
                    }
                    continue;
                }
                if (ls && o.index <= B_DIV) {
                    const T* src = stack + static_cast<std::size_t>(rhs) * kBlock;
                    T* dst = stack + static_cast<std::size_t>(lhs) * kBlock;
                    const T v = scalar_value[lhs];
                    switch (o.index) {
                        case B_ADD: simd_add_sv(dst, src, v, len); break;
                        case B_SUB: simd_rsub_sv(dst, src, v, len); break;
                        case B_MUL: simd_mul_sv(dst, src, v, len); break;
                        default: simd_rdiv_sv(dst, src, v, len); break;
                    }
                    is_scalar[lhs] = 0;
                    continue;
                }
                materialise(lhs);
                materialise(rhs);
                apply_bin(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                          stack + static_cast<std::size_t>(rhs) * kBlock, len);
            } else if (binary_function(o.index)) {
                const int rhs = --top, lhs = top - 1;
                materialise(lhs);
                materialise(rhs);
                apply_fun2(o.index, stack + static_cast<std::size_t>(lhs) * kBlock,
                           stack + static_cast<std::size_t>(rhs) * kBlock, len);
            } else {
                numerify(top - 1);
                if (is_scalar[top - 1]) {
                    T& v = scalar_value[top - 1];
                    v = static_cast<T>(apply_scalar_fun(o.index, static_cast<double>(v)));
                    continue;
                }
                apply_fun(o.index, stack + static_cast<std::size_t>(top - 1) * kBlock, len);
            }
        }

        // The block's closing store, and the only place the two callers part.
        // Truthiness is numpy's cast to bool: anything that is not zero is
        // true, which keeps a negative value and a NaN true. A gate written as
        // a comparison never reaches that rule -- its slot is already a byte
        // mask and packs straight out.
        if (words != nullptr) {
            // kBlock is a multiple of 64, so a block always starts on a word.
            std::uint64_t* w = words + (base >> 6);
            if (is_bool[0]) {
                pack_block(bstack, len, w);
            } else if (is_scalar[0]) {
                const bool t = scalar_value[0] != T(0);
                std::size_t k = 0;
                for (; k + 64 <= len; k += 64) w[k >> 6] = t ? ~0ULL : 0ULL;
                if (k < len) w[k >> 6] = t ? ((1ULL << (len - k)) - 1ULL) : 0ULL;
            } else {
                unsigned char* tmp = s.pack_scratch.data();
                const T* v = stack;
                for (std::size_t i = 0; i < len; ++i) tmp[i] = (v[i] != T(0)) ? 1 : 0;
                pack_block(tmp, len, w);
            }
        } else if (is_bool[0]) {
            const unsigned char* m = bstack;
            for (std::size_t i = 0; i < len; ++i) out[base + i] = m[i] ? 1.0 : 0.0;
        } else if (is_scalar[0]) {
            const double v = static_cast<double>(scalar_value[0]);
            for (std::size_t i = 0; i < len; ++i) out[base + i] = v;
        } else {
            const T* v = stack;
            for (std::size_t i = 0; i < len; ++i) out[base + i] = static_cast<double>(v[i]);
        }
    }
}

inline void ExpressionEngine::dispatch(const std::vector<ExprColumn>& columns,
                                std::size_t n_rows, double* out,
                                std::uint64_t* words) const {
    if (program_.empty()) {
        throw std::domain_error(
            "ExpressionEngine: no compiled program; compile() must succeed first");
    }
    if (columns.size() != variables_.size()) {
        throw std::domain_error(
            "ExpressionEngine: one column is needed per variable, in the order "
            "variables() lists them");
    }
    for (const ExprColumn& c : columns) {
        if (c.data == nullptr) {
            throw std::domain_error("ExpressionEngine: a column has no data");
        }
    }
    if (n_rows == 0) return;
    // Float32 columns are evaluated in float32: nothing is widened, twice as
    // many lanes fit a SIMD register, and the answer is bit-for-bit what numpy
    // gives over the same columns. Any other mix runs in double, which is exact
    // for every integer type up to 2^53.
    bool all_f32 = !columns.empty();
    for (const ExprColumn& c : columns) {
        if (c.type != ExprScalarType::Float32) { all_f32 = false; break; }
    }
    if (all_f32) {
        run<float>(columns, n_rows, out, words, scratch32_);
    } else {
        run<double>(columns, n_rows, out, words, scratch64_);
    }
}

inline void ExpressionEngine::compute_mask(const std::vector<ExprColumn>& columns,
                                    std::size_t n_rows, std::uint64_t* words) const {
    dispatch(columns, n_rows, nullptr, words);
}

inline void ExpressionEngine::compute_values(const std::vector<ExprColumn>& columns,
                                      std::size_t n_rows, double* out) const {
    dispatch(columns, n_rows, out, nullptr);
}

}  // namespace data
}  // namespace tttrlib

#endif  // TTTRLIB_EXPRESSIONENGINE_H
