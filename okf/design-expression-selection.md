# Expression selection in DataStore

## Why here

`DataStore` already owns the data, the typed columns, the bit-packed row mask
and the selection primitives (`select_range`, `select_equal`, `select_rectangle`,
`select_polygon`, `where`, …), all combining through `Combine::{Replace,And,Or,AndNot}`.

What it does not have is a general expression: `where` is a condition on **one**
column, and the geometric selectors are fixed shapes. Nothing evaluates
`(g-b)/(r-b) > 0.3`.

A second table elsewhere (as `bff::Table` currently is) duplicates storage that
already exists here, and duplicates it worse: no validity masks, no groups, no
string/dictionary columns, no int types. The expression engine should come to
the data, not the data to the engine.

## The design

One new primitive, alongside the existing ones:

```cpp
/// Select the rows for which `expr` is true.
/// Columns are referred to by name; the expression is compiled once and
/// cached, so a repeated gate costs only its evaluation.
void select_expression(const std::string& expr,
                       Combine how = Combine::Replace);

/// How many rows `expr` would select, without touching the row mask.
std::size_t count_expression(const std::string& expr) const;
```

It is a `select_*` like any other: it writes a `BitMask` and hands it to
`apply(m, how)`, so it composes with every existing gate.

### Evaluation

Follow the `scan_column` pattern already in `DataStore.h`: dispatch on
`ColumnType` and read each column through its own typed pointer
(`f32_ptr`, `i32_ptr`, `i64_ptr`, …). Two paths, chosen per query:

1. **All referenced columns are `Float32`** — bind ExprTk's vectors straight to
   `f32_ptr()` and evaluate in single precision. Zero copies; the precision is
   the data's own.
2. **Mixed or non-float types** — widen the referenced columns into a scratch
   buffer held with the cached program (so it is allocated once per query, not
   per evaluation) and evaluate in double.

Only the columns the expression names are touched; a 40-column store gated on
two columns reads two.

### Integers

Widening to `double` is exact for every type up to and including `Int32` /
`UInt32`, so path 2 is correct for them. `Int64`/`UInt64` beyond 2^53 are not
exact: an `==` on a large id would silently match the wrong rows. Refuse that
case explicitly rather than answer it wrongly — an integer evaluation path can
come later if a real query needs it.

### Validity

`scan_column` already drops rows a column marks invalid: "not measured" cannot
satisfy a predicate. An expression must inherit that rule — a row is selected
only if it is valid in **every** column the expression reads.

### Caching

Key the compiled program on the expression string. Invalidate on anything that
moves a column: `add_column`, `remove_column`, `append_rows`, `set_n_rows`,
`compact`. ExprTk binds vectors by address and length, so a moved buffer must
recompile; storing the bound addresses alongside the program makes that check
exact rather than conservative.

## Where the pieces live afterwards

- **tttrlib**: the data, the columns, the masks, and now expression selection.
  The evaluator is the in-tree `ExpressionEngine` and nothing else — the
  vendored ExprTk that this note originally proposed was retired on 2026-09-02
  (T-20260831-13): its multi-argument functions evaluated at element 0 and
  broadcast, so the fallback could silently keep the wrong rows. What the
  engine does not implement is refused, loudly.
- **bff**: keeps `Expression` for *model equations* — a fit's curve is not a
  table query, and `ChiSquared`/`Sampler` consume it as a graph node.
  `bff::Table` loses its reason to exist and should go.
- **ndxplorer**: drops its own query path entirely and calls
  `store.select_expression(query, Combine::And)`; the mask it wants is already
  the store's own selection.

## What this replaces

`ndxplorer.core.DataSource.query_mask` and `_bff_query_table`, and the float32
view machinery in `bff::Table` — all of which exist only because the expression
engine sat on the far side of the data.

## Measured baseline to beat

200k rows, three float32 columns, via `bff::Table` bound to the same buffers:

| query | engine | pandas |
|---|---|---|
| `(g>2) & (r<10)` | 0.41 ms | 1.43 ms |
| `g>5 \| b<0.1`   | 0.43 ms | 1.47 ms |
| `(g-b)/(r-b) > 0.3` | 0.41 ms | 1.45 ms |

Writing a bit-packed `BitMask` instead of a `double` per row should improve on
this: one bit per row rather than eight bytes, and no mask handed back across
the language boundary at all when the caller only wants a selection.
