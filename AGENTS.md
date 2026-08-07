# AGENTS.md

Notes for anyone — human or agent — writing code in this repo.

## Inline comments are fragments; prose goes in the docstring

Two registers, don't mix them.

**Inline, in the body** — telegraphic. Lowercase, no full sentences, one or two
lines, sitting on the line it explains:

```cpp
// load-bearing: ~log2(dt) chained products drift off 1
// side effect: score simplex-only
if (s > 0.0) { ... }
```

**Docstring, above the declaration** — full prose, the argument, the measured
numbers, the thing that was tried and rejected. This is what ends up in the API
docs, so it can be long if it earns it.

Never write a paragraph inside a function body, and never leave a docstring so
terse it just restates the signature.

## Comments carry the *why*, not the *what*

The code says what it does; restating it is noise. Write down what is no longer
visible: the measurement that settled an argument, the alternative that was
tried and was worse, the trap that cost a day. If a line looks pointless and
isn't, say so — someone will otherwise "simplify" it back into a bug.

Keep them short. Any number you quote must have been measured, and replicated
over seeds — several single-seed figures here turned out to be wrong.

## Swearing is fine in code that users never read

Comments, commit messages, test names, developer-facing logs: swear if it makes
the point land. "Don't fuck with this ordering, it's load-bearing" beats three
sentences of hedging and flags the danger better.

Keep it out of anything a user sees:

- exception messages and warnings
- Doxygen/docstrings that end up in the published API docs
- `doc/`, `README.md`, `CHANGELOG.md`, gallery examples
- public identifiers — class, method, parameter, file names

Aim it at code, never at people. A vendor's undocumented record layout is fair
game; a contributor is not.

## The rest

Build commands, test invocation and the hard constraints (no public API breaks,
std-only C++) live in `BUILDING.md` and `modules/README.md`.
