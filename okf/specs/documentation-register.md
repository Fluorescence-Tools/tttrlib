# Two registers: the code, and the documentation

Normative. Applies to everything under `doc/`, to `README.md`, `BUILDING.md`,
`CHANGELOG.md`, to Doxygen and docstrings that reach the published API pages,
and to user-visible strings.

`AGENTS.md` already fixes the register for code comments: inline comments are
telegraphic fragments, prose belongs in the docstring, swearing is fine in code
a user never reads. That guidance is about the source tree, and it has been
read as if it applied everywhere. It does not.

## The rule

**Source-tree comments may be ugly. Published documentation may not.**

A comment is written for someone who already has the file open. Documentation
is written for someone who does not, and who has no way to check what a
sentence glossed over. The two therefore differ in register and in how much
they are allowed to leave out.

| | source comment | published documentation |
|---|---|---|
| register | telegraphic, informal, blunt | measured, plain, complete sentences |
| length | one or two lines | as long as the subject needs |
| omission | assume the reader has the code | assume the reader has nothing else |
| a striking phrase | good, it makes the point land | usually a fact left unstated |

## What "less sloppy" means concretely

These are the failure modes that produced this document, taken from a real
change to `doc/fit-guide.rst`:

- **Colloquial verbs for API behaviour.** "a fit *hands back* a
  `DecayFitOutcome`", "the piece you *reach for*". Write "returns" and "the
  entry point for".
- **A quip standing in for the fact.** "freeing them produces confident
  nonsense" is memorable and tells the reader nothing actionable. The fact is:
  the optimiser converges, reports a plausible `twoIstar`, and returns
  parameter values that are not determined by the data. Say that.
- **Compression that reads as style.** "one wire, every binding", "the same
  three fields widened by row". The reader cannot expand these. Give the
  shapes: `n_rows * n_parameters`, row-major.
- **Conversational headings.** "Asking the registry what it holds", "The rest
  of the kernel" → "Inspecting the registry", "The remaining decay kernels".
- **Naming a symbol instead of documenting it.** Mentioning
  `dfa_vv_vh_decay` in a sentence satisfies a coverage checker; it does not
  tell anyone what the arguments are or what layout comes back. Give the
  signature, each argument, and the shape of the return value.

## What "more detailed" means concretely

For anything a caller invokes, state:

1. the signature, with argument names as they appear in the binding;
2. what each non-obvious argument means, including units and, for flags, what
   each value selects;
3. the shape and layout of the return value — length, row-major or not, and
   the order of any packed channels;
4. how to obtain any ordering the caller needs (which `*_names()` call gives
   the order of a vector), rather than asserting that an order exists;
5. the failure that a reader would otherwise walk into, stated as behaviour
   and not as a warning adjective.

## What does not change

Accuracy rules are the same in both registers, and the stricter one wins:
every number quoted must have been measured, and a claim about how something
performs must be checkable. If you cannot substantiate it, describe what the
function does and stop. A confident sentence in published documentation is
harder to catch than the same sentence in a comment, because the reader has no
code in front of them to disagree with.
