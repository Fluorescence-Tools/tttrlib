"""Fuzz the expression gate: random *valid* queries, checked against numpy.

Random characters at a parser answer "can input crash it", and the answer here
is no -- but garbage almost never parses, so that harness never reaches the
evaluator and tests the refusal rather than the arithmetic. This one builds a
random expression tree instead and renders it twice: once as the query string
`DataStore.select_expression` is given, once as the numpy expression that means
the same thing. Then it demands the two agree, row for row.

That is the only harness that reaches the failure mode this evaluator actually
has. Its stack slots hold three different things -- a block of numbers, a block
of one-byte booleans, or a single folded scalar -- and it is wrong whenever a
slot's type is not reconciled at a boundary. Every such bug needs a *valid*
expression mixing numbers and booleans to show up:

* `(x>2)*3` -- a comparison used as a number, which read the wrong stack.
* `(x>2) and y` -- a number used as a boolean, which read uninitialised bytes.
* `a>0 and b<1 or c>2` -- a slot reused while still claiming to be a mask.
* `-x**2 < 0` -- a pending unary minus that outlived its operand and negated
  the *comparison* instead of the value.

Deterministic in the seed, and it prints the seed to reproduce with. Not named
`test_*`, so pytest does not collect it: a long fuzz run belongs on a clock the
suite does not own. `test_datastore_expression.py` keeps a short in-suite run
for the shapes that must never regress.

    python test/python/fuzz_expression.py --seed 1 --cases 20000
"""

import argparse
import random
import sys

import numpy as np

import tttrlib

VARIABLES = ["x", "y", "z"]
UNARY = ["abs", "exp", "sqrt", "log", "log10", "sin", "cos", "tan", "frac"]
BINARY_FUN = ["pow", "min", "max", "atan2", "hypot", "root", "logn"]
ARITH = ["+", "-", "*", "/", "**"]
COMPARE = ["<", "<=", ">", ">=", "==", "!="]


def gen(rng, depth, want="num"):
    """A random expression tree. `want` is 'num' or 'bool'."""
    if want == "bool":
        if depth <= 0 or rng.random() < 0.35:
            return ("cmp", rng.choice(COMPARE),
                    gen(rng, depth - 1, "num"), gen(rng, depth - 1, "num"))
        k = rng.random()
        if k < 0.30:
            return ("not", gen(rng, depth - 1, "bool"))
        if k < 0.65:
            # Deliberately allow a *numeric* operand: `(x>2) and y` is legal
            # and means "and y is nonzero".
            return ("logic", rng.choice(["and", "or"]),
                    gen(rng, depth - 1, rng.choice(["bool", "num"])),
                    gen(rng, depth - 1, rng.choice(["bool", "num"])))
        return ("cmp", rng.choice(COMPARE),
                gen(rng, depth - 1, "num"), gen(rng, depth - 1, "num"))

    if depth <= 0:
        # Weighted towards variables: a tree of pure constants folds to a
        # scalar and has no columns to evaluate over, so it tests nothing.
        if rng.random() < 0.75:
            return ("var", rng.choice(VARIABLES))
        return ("const", round(rng.uniform(-4, 4), 3))

    k = rng.random()
    if k < 0.08:
        return ("asnum", gen(rng, depth - 1, "bool"))
    if k < 0.20:
        return ("un", rng.choice(UNARY), gen(rng, depth - 1, "num"))
    if k < 0.30:
        return ("bin2", rng.choice(BINARY_FUN),
                gen(rng, depth - 1, "num"), gen(rng, depth - 1, "num"))
    if k < 0.42:
        return ("var", rng.choice(VARIABLES))
    if k < 0.50:
        return ("const", round(rng.uniform(-4, 4), 3))
    if k < 0.56:
        return ("neg", gen(rng, depth - 1, "num"))
    return ("arith", rng.choice(ARITH),
            gen(rng, depth - 1, "num"), gen(rng, depth - 1, "num"))


def render(node):
    """The query string, in the Python spelling a caller writes."""
    kind = node[0]
    if kind == "var":   return node[1]
    if kind == "const": return repr(node[1])
    if kind == "neg":   return f"(-({render(node[1])}))"
    if kind == "asnum": return f"({render(node[1])})"
    if kind == "un":    return f"{node[1]}({render(node[2])})"
    if kind == "bin2":  return f"{node[1]}({render(node[2])}, {render(node[3])})"
    if kind in ("arith", "cmp", "logic"):
        return f"(({render(node[2])}) {node[1]} ({render(node[3])}))"
    if kind == "not":   return f"not({render(node[1])})"
    raise AssertionError(kind)


def render_numpy(node):
    """The same tree as a numpy expression.

    `and`/`or`/`not` cannot be rendered literally -- Python's keywords raise on
    arrays -- so they become `&`/`|`/`~` over operands explicitly cast to bool.
    That cast is also the semantics being asserted: nonzero is true, which is
    what `arr.astype(bool)` does and what the evaluator copies.
    """
    kind = node[0]
    if kind == "var": return node[1]
    if kind == "const":
        # np.float64, not a bare literal: Python evaluates a constant-only
        # subtree with Python's own operator semantics, and `(-3.7) ** 2.1` is
        # a complex number in Python where C and numpy both give NaN.
        return f"np.float64({node[1]!r})"
    if kind == "neg":   return f"(-({render_numpy(node[1])}))"
    if kind == "asnum":
        # np.asarray: an all-constant subtree evaluates to a Python bool, which
        # has no .astype and would otherwise lose the case.
        return f"(np.asarray({render_numpy(node[1])}).astype(np.float64))"
    if kind == "un":
        fn = {"frac": "np_frac"}.get(node[1], f"np.{node[1]}")
        return f"{fn}({render_numpy(node[2])})"
    if kind == "bin2":
        fn = {"pow": "np.power", "min": "np.minimum", "max": "np.maximum",
              "atan2": "np.arctan2", "hypot": "np.hypot",
              "root": "np_root", "logn": "np_logn"}[node[1]]
        return f"{fn}({render_numpy(node[2])}, {render_numpy(node[3])})"
    if kind in ("arith", "cmp"):
        return (f"(({render_numpy(node[2])}) {node[1]} "
                f"({render_numpy(node[3])}))")
    if kind == "not":   return f"(~({as_bool(node[1])}))"
    if kind == "logic":
        op = "&" if node[1] == "and" else "|"
        return f"(({as_bool(node[2])}) {op} ({as_bool(node[3])}))"
    raise AssertionError(kind)


def as_bool(node):
    """A numpy subexpression forced to bool, the evaluator's truthiness rule."""
    if node[0] in ("cmp", "not", "logic"):
        return f"np.asarray({render_numpy(node)})"
    return f"(np.asarray({render_numpy(node)}) != 0)"


def np_frac(a):
    """ExprTk's frac, toward zero: frac(-1.25) is -0.25."""
    return a - np.trunc(a)


def np_root(a, b):
    return np.power(a, 1.0 / np.asarray(b, dtype=np.float64))


def np_logn(a, b):
    return np.log(a) / np.log(b)


NUMPY_ENV = {"np": np, "np_frac": np_frac, "np_root": np_root,
             "np_logn": np_logn, "__builtins__": {}}


def make_store(columns):
    s = tttrlib.DataStore()
    s.set_n_rows(len(next(iter(columns.values()))))
    for name, values in columns.items():
        s.add(name, values)
    return s


def gate(store, query, n_rows):
    store.select_expression(query)
    out = np.asarray(store.selection()).astype(bool)
    store.clear_row_mask()
    return out


def run(seed, cases, n_rows=1289, verbose=False):
    """One fuzz run. Returns a summary dict; `failures` empty means it passed."""
    rng = random.Random(seed + 1_000_003)
    # Deliberately not a multiple of the 512-row block nor of the 64-row mask
    # word, so both tails are exercised on every single case.
    columns = {
        "x": np.array([rng.uniform(-3, 3) for _ in range(n_rows)]),
        "y": np.array([rng.uniform(-3, 3) for _ in range(n_rows)]),
        # Positive, so sqrt and log have a real branch to exercise rather than
        # returning NaN for half the rows and proving nothing.
        "z": np.array([rng.uniform(0.1, 5) for _ in range(n_rows)]),
    }
    store = make_store(columns)

    checked = refused = no_oracle = 0
    failures = []
    for _ in range(cases):
        tree = gen(rng, rng.randint(1, 4), "bool")
        text = render(tree)
        try:
            with np.errstate(all="ignore"):
                expected = eval(render_numpy(tree), dict(NUMPY_ENV), columns)
        except Exception:
            no_oracle += 1  # numpy itself refused; there is no oracle to check
            continue
        expected = np.asarray(expected)
        if expected.ndim == 0:
            expected = np.full(n_rows, expected)
        expected = expected.astype(bool)
        try:
            got = gate(store, text, n_rows)
        except Exception as exc:
            # A query the evaluator will not take is allowed; one it takes and
            # then fails on is not, and neither is an unknown-column error over
            # columns that plainly exist.
            if "unknown column" in str(exc):
                failures.append((text, f"refused a column that exists: {exc}"))
            else:
                refused += 1
            continue
        if not np.array_equal(got, expected):
            bad = np.flatnonzero(got != expected)
            i = int(bad[0])
            failures.append((text, f"row {i} of {bad.size}: got {got[i]}, "
                                   f"expected {expected[i]}"))
            continue
        checked += 1
        if verbose and checked < 6:
            print(f"  e.g. {text}")
    return {"checked": checked, "refused": refused, "no_oracle": no_oracle,
            "failures": failures}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--cases", type=int, default=20_000)
    ap.add_argument("--rows", type=int, default=1289)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    result = run(args.seed, args.cases, args.rows, args.verbose)
    print(f"grammar  seed={args.seed}  cases={args.cases:,}  "
          f"checked={result['checked']:,}  refused={result['refused']:,}  "
          f"no-oracle={result['no_oracle']:,}")
    if result["failures"]:
        print(f"\n{len(result['failures'])} FAILURES "
              f"(reproduce with --seed {args.seed}):\n")
        for text, why in result["failures"][:20]:
            print(f"  {text}\n    {why}")
        return 1
    print("         -> every query agreed with numpy")
    return 0


if __name__ == "__main__":
    sys.exit(main())
