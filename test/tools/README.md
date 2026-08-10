Two programs live here. Neither is part of the build or the suite, and both
exist for the same reason: a test written against our own code shares its
assumptions, so it cannot catch an assumption that is wrong. These check the
same things against somebody else's implementation, and the dependency that
needs is one tttrlib must not acquire.

| | checks | against |
|---|---|---|
| `pto_ebml_check.cpp` | that a `.pto` is a valid EBML document | **libebml**, the reference implementation |
| `compare_burst_table_to_chisurf.py` | that `tttr sm`'s burst table is ChiSurf's burst table | **ChiSurf**'s `generate_burst_dataframe` |

---

# Checking a burst table against ChiSurf

`compare_burst_table_to_chisurf.py` runs ChiSurf's own writer over the same
photons as `tttr sm --csv` and diffs the two tables cell for cell. The suite's
`test/python/misc/test_cli_sm_burst_table.py` checks the same thing against a
NumPy reimplementation of ChiSurf's documented rules — which is what makes it
runnable in CI, and also what leaves the gap this closes: a rule read wrongly is
read the same wrong way in the reimplementation. ChiSurf's `burst.py` is the
format; if the two disagree, tttrlib is wrong.

Usage is in the module docstring, including the two ways `import tttrlib` ends
up resolving to a different build than the binary under test (an env built for
another Python or architecture, and an editable install's meta path finder
outranking `PYTHONPATH`). The program prints `tttrlib.__file__` first so a run
can be trusted.

---

# Checking a `.pto` with somebody else's parser

`pto_ebml_check.cpp` validates a PTO container using **libebml** — the reference
EBML implementation, the one Matroska is built on.

Everything else that checks the container's framing is written against tttrlib's
own parser, in `test/python/test_pto.py`. Those tests are worth having and they
share a blind spot: a consistent misreading of RFC 8794 would satisfy the writer
and the reader together and pass every one of them. This program does not share
it. If the two disagree, tttrlib is wrong.

It is deliberately **not** part of the build. PTO writes its own EBML and
tttrlib must not acquire a dependency on libebml — the whole argument for the
format is that a reader needs an EBML parser and twenty element IDs, not a
framework. So this is a standalone program you build when you want it.

## Building it

```sh
git clone https://github.com/Matroska-Org/libebml
cmake -S libebml -B /tmp/ebml-build -DCMAKE_INSTALL_PREFIX=/tmp/ebml \
      -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/ebml-build -j && cmake --install /tmp/ebml-build

c++ -std=c++17 -I/tmp/ebml/include test/tools/pto_ebml_check.cpp \
    /tmp/ebml/lib/libebml.a -o /tmp/pto_ebml_check
```

## Running it

```sh
/tmp/pto_ebml_check run.pto            # exit 0 if it is a valid PTO
/tmp/pto_ebml_check run.pto --verbose  # print the element tree
/tmp/pto_ebml_check run.pto --aligned  # also require aligned payloads
```

Set `TTTRLIB_PTO_EBML_CHECK=/tmp/pto_ebml_check` and
`test/python/test_pto.py::test_libebml_agrees_this_is_a_valid_pto` runs it over
a freshly written container. Without the variable that test skips, which is why
the suite still passes on a machine with no libebml.

## What it checks, and what each check is for

| | |
|---|---|
| the EBML header parses as an `EbmlHead` | libebml's own semantic read, so `DocType`, `DocTypeReadVersion` and the two `Max*Length` values are validated by their code rather than ours |
| `DocType` is `"pto"` | the file says what it is |
| `DocTypeReadVersion` is 1 | a 1.0 reader must still be able to read it — this is what makes cues an addition rather than a break |
| every element has a **known** Data Size | PTO promises never to write an unknown size, because that is what lets a reader skip an element it does not understand |
| no element runs past its parent | sizes are consistent all the way down |
| the children of a master exactly fill it | no gap and no overlap, at every level |
| how many `FileData` payloads are **not** 8-byte aligned | payloads are `uint32` record streams and `double` columns; a reader that maps the file must be able to point at one. **Reported, not enforced** — the spec makes alignment a writer SHOULD, and `compact(tight=True)` drops it on purpose. Pass `--aligned` to make it a failure, which is right for a file the default writer produced |

The walk uses `EbmlId::FromBuffer` and `ReadCodedSizeValue` rather than
`EbmlStream::FindNextElement`. That is not a shortcut. `FindNextElement` needs a
semantic context, and given one that does not list an ID it *scans forward a
byte at a time* looking for something it recognises — correct for recovering a
damaged Matroska stream, and useless as a validator, because it would report a
plausible tree for a file with no valid framing at all. The two decoding
functions are what a generic parser actually is, and they are still libebml's.
