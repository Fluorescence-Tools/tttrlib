# PRD-025 — A container you can read

> **PRD #:** 025 · **Status:** 🔵 Proposed · **Created:** 2026-08-07 · **Updated:** 2026-08-08 · **Owner:** tpeulen

> [!NOTE]
> **Change of Plan (2026-08-08):** Executable binary target is restricted strictly to **Linux** (ELF64) and **macOS** (Mach-O 64-bit) binary framing. Multi-platform APE / PE-COFF wrapping has been superseded. Full C99 decoding instructions, ASCII parsing code for coding agents, and embedded TTTR format (`fmt`) definitions are specified in [`pto-binary-decoding.md`](file:///Users/tpeulen/dev/tttrlib/okf/specs/pto-binary-decoding.md).

## Summary

Make a `.pto` container answerable by somebody with nothing installed: they can
see what is in it — the element tree, the object table, and on request the
objects unpacked back into loose files — and if they want to poke around rather
than read a table, they get a terminal UI.

Four things follow, and the order matters:

* **The reader has to exist first, and is useful on its own.** A C99 reader core
  plus a plain CLI (`pto ls`, `tree`, `cat`, `extract`) — no libebml, no
  tttrlib, no dependency beyond libc. This is the deliverable even if nothing is
  ever made executable and no TUI is ever written. It is also the *actual* gap:
  the format ships today with no way to look inside it that does not involve a
  compiler.
* **A few hundred bytes of ASCII inside the file answer most of the complaint.**
  A `PtoBanner` element right after the EBML header makes `strings`, a mail
  preview and `head -c` say what the file is and where to get the reader. No
  executable, no polyglot, still valid EBML. Cheapest thing in this document.
* **The interactive front end is a second binary over the same core**, using
  [cpp-tui](https://github.com/jonoton/cpp-tui) (single header, MIT, C++17).
  It is optional, and it is where the only real dependency cost in this PRD sits.
* **Executability is a delivery convenience, not a property of the format.** The
  mechanism is [APE](https://justine.lol/apeloader/) (Actually Portable
  Executable): one file that runs on Linux, macOS, Windows, FreeBSD, OpenBSD and
  NetBSD, on x86-64 and arm64, with no interpreter and no install. But the
  framing is **a reader that can carry a payload**, not a data file that
  executes — see Part 5, which is why the bundle is last and opt-in.

## What was verified

Byte-level and measured facts. The design turns entirely on these.

### The two magics collide at byte 0

| | |
|---|---|
| APE first bytes | `4D 5A 71 46 70 44 3D 27 0A` — `MZqFpD='\n`, from `ape/ape.S` |
| PTO first bytes | `1A 45 DF A3` — required at **absolute offset 0** |

`is_pto_file` (`modules/io/pto/src/io_pto.cpp:1958`) and `PtoFile::open`
(`io_pto.cpp:1068`) both do `read_element(f, 0, …)` and demand `kEBML`. The
writer mirrors it at `io_pto.cpp:963` (`m.f.at(0, head…)`).

Byte 0 is `4D` or it is `1A`. **A file cannot be both a plain EBML document and
an executable.** Everything else in this PRD is about how much that costs and
where to pay it. In particular it is why the "entry point, then text, then data"
layout only ever describes the *bundle*, never a plain `.pto`.

### `MZqF` is, by accident, a well-formed EBML element

Decode the APE header as EBML:

```
4D 5A     ID   — 0x4D = 0b01001101, one leading zero → 2-byte VINT → ID 0x4D5A
71 46     size — 0x71 = 0b01110001, one leading zero → 2-byte VINT
                 (0x71 & 0x3F) << 8 | 0x46 = 0x3146 = 12614
```

`0x4D5A` is a **valid** Element ID: VINT_DATA is `0x0D5A` = 3418, above the 126
a one-byte ID can hold, so it is shortest-form; and it is not the all-ones
`0x7FFF`. So the first four bytes of every APE binary declare an unknown element
whose data runs to offset **12618**, and RFC 8794's skip-by-size rule steps over
it without complaint.

That is the entire forward-compatibility story of EBML doing exactly what it was
designed to do, to a Windows executable header. It is a coincidence and it is
usable.

**Two things it does not buy:**

1. **RFC 8794 still says the EBML Header is the *first* element of a document**
   — verbatim: *"An EBML Document MUST start with an EBML Header that declares
   significant characteristics of the entire EBML Body."* A leading unknown
   element is a deviation however well-formed it is.
   `test/tools/pto_ebml_check.cpp` and libebml will reject such a file — and
   should keep rejecting it (see Part 7).
2. **12618 is not a multiple of 8.** `12618 mod 8 = 2`. PTO's writer aligns
   `FileData` payloads to 8 bytes so a reader can mmap and point at a `uint32`
   record stream or a `double` column (PRD-020). A raw 12618-byte prefix breaks
   every one of them, and the size VINT cannot be widened — `qF` is fixed by the
   shell polyglot.

   **Fix:** a top-level `Void` (`0xEC`) after the fake element, sized to bring
   the prefix to a multiple of 8. The `Void` may also *hold* stub content, so
   the stub is not capped at 12618 bytes either. Both problems, one element.

### A PTO container is position-independent

Nothing on disk stores an absolute file offset:

| Written as | Relative to | Where |
|---|---|---|
| `SeekPosition` | Segment's data start | `io_pto.cpp:849`, `:858` |
| `PtoCueOffset` | the payload it indexes | `io_pto.cpp:2134` |

`PtoObject::offset` is absolute, but it is computed at open time from wherever
the Segment was found (`io_pto.cpp:1106`) — it is in-memory state, not a stored
field.

**So prepending N bytes to a valid `.pto` produces a container that is still
internally consistent, with no rewriting of any kind.** The bundler is `cat`.

### Windows decides the filename

`cmd.exe` and Explorer execute what is in `PATHEXT`; `.pto` is not and cannot
usefully be. A polyglot named `run.pto` gets you `./run.pto` on Unix only — the
shell's ENOEXEC fallback runs the `MZqFpD='` line as a script. That is half a
platform matrix, for the whole cost.

`run.pto.com` runs on all of them.

### A `Void` may legally hold text — but a `Void` is the freelist

RFC 8794 on `Void` (`0xEC`): *"Used to void data or to avoid unexpected
behaviors when using damaged data. The content is discarded."* It does not
require zeros. So a human-readable banner inside a `Void` is legal EBML and
libebml still validates the file — no polyglot needed for the text idea.

**But it must not be a `Void` here.** `io_pto.cpp:1219` pushes every `Void` it
walks onto the freelist, so a banner in a `Void` gets handed out as payload space
for the next object written and silently disappears. `write_void`
(`io_pto.cpp:614`) fills the payload with zeros today; that is the wrong hook.

The banner therefore needs a real element the reader knows and preserves — see
Part 2.

### cpp-tui, measured today

| | |
|---|---|
| `cpptui.hpp` | 18,106 lines, **570,572 bytes**, one header, MIT (© 2025 cpp-tui) |
| Standard | C++17. No ncurses; `<termios.h>`/`<sys/ioctl.h>` under POSIX, `<windows.h>`/`<conio.h>` under `_WIN32` |
| Standard-library needs | `<regex>`, `<iostream>`, `<filesystem>`, `<mutex>`, `<sstream>`, `<iomanip>`, `<memory>` |
| Hello-world (a `Label` in a `Vertical`), clang++ `-std=c++17 -Os`, macOS arm64, dynamic libc++ | **139,752-byte binary, 44,436-byte `__text`** |
| Throws | `grep -c "throw "` → **0**. Exception tables are emitted anyway (`__gcc_except_tab` 2,084 bytes) from the standard library. |
| Widgets present, and the ones Part 3 wants | `TreeView` (+ `TreeNode`, `on_select`/`on_submit`/`on_expand`), `TableScrollable` (`columns`/`rows`/`add_row`/`on_change`/`on_submit`), `SplitPane`, `StatusBar`, `ShortcutBar`, `Dialog`, `Notification`, `SearchInput`, `FileExplorer`, `Paragraph` |
| `isatty` in cpptui | **zero occurrences.** `App::run` enters raw mode and the alternate screen (`\033[?1049h`, line 1926) unconditionally. |

Two consequences:

* **The TUI can never be entered on a guess.** cpptui does not check whether it
  is talking to a terminal, so our `main` must, and the dispatch rule in Part 3
  is load-bearing rather than a nicety.
* **44 KB of our own code is not the size question.** The size question is
  static libc++ with `<regex>` and `<iostream>` instantiated inside an APE
  binary, which is unmeasured. `<regex>` alone is historically the worst
  offender in the standard library. This is the number that decides whether the
  interactive tool is 200 KB or 1.5 MB, and it is the same argument that put the
  reader core in C.

### Not yet verified

Flagged so nobody quotes them as findings:

* Binary size of a cosmocc-built C99 `pto` — **assume 100–200 KB, measure before
  committing to a budget.**
* Binary size of a `cosmoc++`-built TUI, per above. Unmeasured and the single
  biggest open number in this PRD.
* `cosmocc` and `cosmoc++` are **not installed on this machine** (`which` → not
  found).
* That cpptui's POSIX branch works under Cosmopolitan on Windows. An APE build
  does not define `_WIN32`, so the `<termios.h>`/`ioctl(TIOCGWINSZ)` path is
  what gets compiled, and Cosmopolitan's Windows emulation of `tcsetattr`,
  `TIOCGWINSZ` and console `read()` has to carry it. Plausible, unproven.
* That cpptui's UTF-8 box and block glyphs (`█`, `░`) render in `cmd.exe` without
  `chcp 65001`.
* That `execvp` from an APE binary works on Windows (needed by Part 5's
  launcher).
* That the APE shell-script prologue can carry our own text where `head -c`
  reaches it.
* That `0x4D5A` is unassigned in Matroska. It is not among the IDs PTO borrows
  (`io_pto.cpp:34-76`), and Matroska's nearest neighbour is `0x4D80`
  (`MuxingApp`), but this must be checked against the Matroska element registry
  before the spec leans on it.

## Problem / motivation

A `.pto` holds a photon stream, the burst search over it, the selection, a
spectrum and a note about why half of it was discarded — one file where there
used to be five, which is the whole point of PRD-020's container.

Then you email it to a collaborator and it is an opaque 4 GB blob. They have no
tttrlib, no Python, and no reason to install either to answer "what is in here?"
There is **no dumper today**: `pto_ebml_check --verbose` prints an element tree
but needs libebml built from source and is deliberately outside the build
(`test/tools/README.md`), and it validates framing rather than describing
content.

So the first part of this is a plain gap — the container format ships without a
way to look inside it that does not involve a compiler.

The second part is that a table is the wrong answer to some of the questions. "3
objects, 4.1 GB, 2.4 MB, 380 B" is a complete answer to *what is in here*. It is
no answer at all to *which of these 400 bursts*, *what are the columns of that
dstore*, or *is the note worth reading* — those are browsing, and browsing wants
a cursor. That is the TUI.

The third part is that once a reader exists, making it *carry* a container costs
almost nothing, because the container does not care what is in front of it.

## Proposal

### Part 1 — the reader core and the CLI

`tools/pto_read.c` + `tools/pto_read.h` — the reader core. C99, no libebml, no
tttrlib, no dependency beyond libc. Reading only; it never writes into a
container. The same argument as PTO itself: reading this format is an EBML parser
and twenty element IDs, not a framework.

`tools/pto.c` — the CLI over it, in the default build, built with the host
compiler like anything else.

**Two front ends, one core, and the core is C.** The CLI and the TUI (Part 3)
share `pto_read.h` and nothing else. This is what keeps the C++17 dependency out
of the artifact that has to be small.

#### Grammar

```
pto [GLOBAL…] <command> [ARGS…] <file.pto>     # standalone
./run.pto.com [GLOBAL…] [<command>] [ARGS…]    # bundle: the container is the program
```

The container argument is **implicit when the program is a bundle** and required
otherwise. One rule, stated once, so `--extract` reads the same in both.

| Command | Does |
|---|---|
| `ls` (alias `objects`) | the object table — the default command |
| `tree` | the element framing, with names and 8-byte-alignment per `FileData` |
| `info` | one screen: title, uuid, generation, object count, total bytes, tags |
| `cat <sel>` | one object's payload to stdout |
| `extract [<sel>…]` | payloads to files, all objects by default, `-o DIR` |
| `tags [<sel>]` | tags, container-level and per object |
| `verify` | walk the framing: sizes close, `SeekHead` resolves, payloads aligned |
| `ui` | the TUI (Part 3 build only; a clear error otherwise) |
| `bundle <in.pto>` | `-o out.pto.com` (Part 5; never available inside a bundle) |

`<sel>` is a uid or a name: `0x`-prefixed or all-digits reads as a uid,
otherwise a name, with `--uid`/`--name` to force it. A name matching more than
one object is an error that lists the candidates.

| Global | Does |
|---|---|
| `--json` | machine output for every read command, so nothing has to parse a table |
| `--color=auto\|always\|never` | `auto` means isatty |
| `-q`, `--quiet` / `-v`, `--verbose` | |
| `-h`, `--help` / `--version` | `--version` prints the reader version *and* the highest `DocTypeVersion` it understands |
| `--` | end of options |

Payload bytes go to stdout, diagnostics to stderr, so `pto cat bursts | xxd`
works. Exit codes: `0` ok · `1` not a container, or damaged · `2` usage · `3`
selector matched nothing · `4` I/O.

#### What the modes look like

**`tree`** — the framing:

```
EBML (1A45DFA3) 35 bytes @0
  DocType "pto"  DocTypeVersion 2  DocTypeReadVersion 1
Segment (18538067) 4.2 GB @47
  PtoBanner (1E54F030) 384 @59
  SeekHead 58 @451
  Attachments @2048
    AttachedFile @2056
      FileName "green.rec"  PtoKind "photon-stream"  PtoEncoding "ptu"
      FileData 4.1 GB @2210  (8-byte aligned)
```

**`ls`** — the table of contents rather than the framing:

```
run.pto  "FRET titration 3"  generation 7  uuid 3f9b…  3 objects

UID          KIND           ENC     SIZE     NAME
0x0001a3f2   photon-stream  ptu     4.1 GB   green.rec
0x0001a3f3   burst-list     dstore  2.4 MB   bursts
0x0001a3f4   note           utf-8   380 B    discarded.txt

tags: sample=DNA-15bp  operator=tp
```

**`extract`** — each object's payload written out as its own file. Turns the
container back into the loose files it replaced.

### Part 2 — the banner, and why it is not a `Void`

A new element `PtoBanner` (`0x1E54F030`, in the block where PTO assigns its own
ids), written as the **Segment's first child**, before `SeekHead`. Contents are
UTF-8 lines with no interpretation: what the file is, what wrote it, and where to
get a reader.

```
$ head -c 400 run.pto | strings
pto
This is a .pto photon container (tttrlib PRD-020).
Written by tttrlib 0.30.1 on 2026-08-07.
3 objects, 4.2 GB. Get a reader: https://github.com/…/releases
Nothing here executes. `pto ls run.pto` prints the contents.
```

Four properties, and they are why this is the cheapest item in the document:

* **Valid EBML.** libebml and `pto_ebml_check` still accept the file; Part 7 is
  untouched.
* **Near byte 0.** The EBML header is 35–47 bytes, so a mail preview, `strings`,
  and `head -c` all reach it.
* **Survives compaction**, because it is a known element and not free space. A
  banner in a `Void` would be reissued as payload space by
  `allocate_aligned`/`write_void` and vanish — see the verified section.
* **No executable anywhere.** This is the 90% answer to "opaque blob" for zero
  risk, and it works in 2050 whatever happened to APE.

The writer emits it by default; a container without one is legal and reads fine.
Unlike everything downstream of here, this one changes files tttrlib writes —
which makes it the only part of this PRD that needs a `DocTypeVersion`
conversation.

### Part 3 — the TUI

`tools/ptoview.cpp` over `pto_read.h`, with `cpptui.hpp` vendored in
`thirdparty/cpptui/` and pinned by commit. **Optional and out of the default
build** (`TTTRLIB_PTO_TUI=ON`). It does not link tttrlib — the whole point is a
binary you can hand somebody.

One screen, `SplitPane`:

* **Left** — `TreeView`, toggling between the object list grouped by kind and
  the raw element tree (`t`).
* **Right** — the selected object: kind, encoding, size, uid, alignment,
  description, tags; a hexdump head for opaque payloads; for `dstore`, column
  names, dtypes and row counts (and nothing more — see non-goals).
* **Footer** — `ShortcutBar`: `↑↓/Enter` move and expand · `/` search
  (`SearchInput`) · `e` extract selected · `E` extract all · `q` quit.
* **Extracting from a viewer asks first** — a `Dialog` with the target directory
  (`FileExplorer` to change it), and a `Notification` with bytes written when it
  finishes.

**Keyboard is sufficient for everything.** Mouse support is cpptui's and is
welcome, but no operation may require it — this gets run over ssh.

#### Dispatch, which is load-bearing

cpptui never checks `isatty` (verified above), so:

| Invocation | Runs |
|---|---|
| any arguments given | the CLI, always — except `ui`, which is the TUI |
| no arguments, stdout is a tty, TUI built in | the TUI |
| no arguments, stdout is not a tty | `ls` — so `./run.pto.com \| grep` works |
| no arguments, no TUI in this build | `ls` |

`ui` on a non-tty is an error, not a fallback.

### Part 4 — the portable build

`cosmocc` produces one `pto.com` for every supported OS and both architectures;
`cosmoc++` does the same for `ptoview.com` if the TUI is enabled. **Optional and
out of the default build**: a CMake option (`TTTRLIB_APE_TOOLS=ON`, needs
`COSMOCC` on `PATH`) produces the portable ones.

Nobody's normal build acquires a toolchain dependency, and CI produces the
portable artifacts on one runner.

### Part 5 — the bundle: a reader that carries a payload

**Framing first, because it decides the defaults.** This is not "make `.pto`
executable" — it is redbean's trick, a reader with a payload appended. The
distinction is not cosmetic:

* A bundle costs **one stub per file**. A standalone `pto.com` is **one tool for
  every file, forever** — including files that do not exist yet.
* A bundle **freezes the reader** at the day it was made. A container written in
  2029 with elements a 2027 stub never heard of shows up thin; a standalone `pto`
  reads both.
* No scientific format ships executable, and the reason is not technical — it is
  Zenodo, mail gateways, AV and cluster policy (see Risks).

So the standalone tool is the deliverable, and this part is a convenience for one
real case: the collaborator who will not download anything. It is implemented
last.

#### Layout

```
offset 0       APE stub          — parses as unknown EBML element 0x4D5A
offset 12618   Void (0xEC)       — pads the prefix to a multiple of 8;
                                   may carry stub content beyond 12618
offset P       EBML header       — the container, byte for byte, unmodified
               PtoBanner         — still where Part 2 put it
               Segment           — P ≡ 0 (mod 8), so payload alignment survives
```

`P` is **derived from the stub actually in hand**, never from the constant 12618.

The program finds its payload at `P` by locating its own executable —
Cosmopolitan's `GetProgramExecutableName()` is portable, which is exactly the
part that is awkward everywhere else.

#### Entry point → text → data, and what the entry point should be

The natural reading of "a jump at the front, then the human-readable text, then
the data" is right about the order and needs one correction about the front. Two
notes:

* **The text cannot be reached by `head -c` in a bundle.** Anything we write
  lands in the `Void` at 12618 or later, and the first ~12 KB is Justine's shell
  prologue. Whether the APE prologue can carry our own text is listed as
  unverified; until then a bundle's banner is reachable by `strings`, by `pto
  info`, or by running it — and `head -c` only works on a plain `.pto`, via
  Part 2. That asymmetry is an argument for Part 2 being the primary answer.
* **The entry point should be the small reader, not a bare jump.** A stub whose
  only job is to hand off has nothing to say when the handoff fails. A stub that
  *is* the C99 CLI answers `ls`, `tree`, `cat`, `verify` and `extract` by itself
  — the questions that actually get asked — and delegates only what it cannot
  do.

So: **`--bundle` embeds `pto` (C99, one fixed size), and the TUI is
delegated, never embedded.** Asking a bundle for `ui`:

1. `$PTO_READER` if set, else a `pto`/`ptoview` **on `PATH`**;
2. found → `execvp(reader, {reader, self, "ui", …})`;
3. not found → print where to get one and fall back to `ls`, exit 0.

Three tiers of capability from one prefix: table always, full CLI always,
interactive when a reader is installed. It also answers the two objections above
— the delegated reader is *current* rather than frozen, and the prefix is
**byte-identical across every bundle**, so it hashes the same, dedupes, caches,
and can be whitelisted. A unique multi-megabyte binary per data file looks
exactly like malware; a fixed known-hash 150 KB prefix does not.

**Search order is a security boundary, not a convenience.** `$PTO_READER` and
`PATH` only. **Never** `.`, and never the directory holding the data file —
otherwise mailing somebody `data.pto.com` plus a file named `pto` is remote code
execution with extra steps. Never download a reader either: a data file that
fetches code is worse than one that carries it.

A `--bundle --with-tui` flavor that embeds `ptoview.com` is a **deferred**
option, not part of this PRD, and it is not worth discussing until the size in
Part 4 is measured.

#### Why not a ZIP member

APE binaries are also ZIP archives, so the container could be a stored entry,
readable via `/zip/` and extractable with `unzip -p`. Tempting and the wrong
trade: the ZIP local header lands between offset 0 and the EBML magic, so the
file stops being walkable as EBML from byte 0 and the tolerance in Part 6 has to
become a byte scan. Keep the container's position parseable; `extract` already
covers getting the bytes out.

### Part 6 — reader tolerance, as the skip rule and nothing looser

`PtoFile::open` and `is_pto_file` learn one rule:

> If the element at offset 0 is not `0x1A45DFA3`, skip it by its declared size
> and look again. Accept the first `0x1A45DFA3` whose `DocType` is `"pto"`.

That is RFC 8794's skip-by-size applied at the top level — **not** a scan for
magic bytes, which would find a plausible header inside any large payload.
Bounded: at most 4 elements and 2 MB of prefix, then fail as today. Sizes must
be known; an unknown-size leading element is a hard reject.

Everything downstream already works in absolute offsets computed from where the
Segment was found (`io_pto.cpp:1106-1113`), so this is the only change — the
prefix does not propagate.

Also teach `identify()` (PRD-024) the bundle, so `load("run.pto.com")` opens it
as a container rather than sniffing `MZ` and giving up.

### Part 7 — what stays strict, and the test that says so

**`.pto` files written by tttrlib do not gain a prefix.** The writer gains no
flag; a container is a container. (Part 2's banner is an element *inside* the
Segment and does not touch this.)

`pto_ebml_check` **must reject a bundle**, and a test must assert that it does.
The bundle is not a `.pto` that libebml can read and must not be sold as one —
it is a program that contains one. The moment that test starts passing, somebody
has quietly made the polyglot the default.

`pto_ebml_check` **must keep accepting** a container with a `PtoBanner`, and a
test must assert that too — that is the whole claim of Part 2.

### Part 8 — non-goals

* **Not the default output.** No `PtoFile` API produces a bundle.
* **Not self-modifying.** A bundle is read-only; edit the container, re-bundle.
* **Not a general viewer.** No plotting — cpptui has `LineChart` and `Heatmap`
  and neither goes in. No decoding of `.ptu` records, no `dstore` column *data*
  beyond names, dtypes and row counts.
* **Not a writer.** Neither the CLI nor the TUI ever modifies a container.
  `extract` writes new files and nothing else.
* **No network, ever.** Not to fetch a reader, not to phone home.
* **Not signed.** Distribution consequences are listed under Risks, not solved.
* **No embedded TUI in a bundle** in this PRD (Part 5).

## Acceptance criteria

1. The reader core builds from one C source file with the host compiler and no
   dependency beyond libc, and `pto` is in the default build.
2. Every read command works on a container written by `PtoFile::create`,
   including one with an object above 4 GB and one that has been `compact`ed.
3. `tree` reports 8-byte alignment per `FileData`, matching what
   `pto_ebml_check --aligned` concludes about the same file.
4. `--json` is available for every read command, and the documented exit codes
   are asserted — including `3` for a selector that matches nothing and `2` for
   a name that matches two objects.
5. `PtoBanner` round-trips: the writer emits one, `head -c 512` on the raw file
   shows it, `pto_ebml_check` still accepts the file, and it is **still present
   after a `compact`** — with the failure mode (banner in a `Void`, eaten by the
   freelist) covered by its own regression test.
6. `cosmocc`-built `pto.com` runs on Linux x86-64, macOS arm64 and Windows.
   *(Windows verified in CI; the others locally.)*
7. TUI: builds behind `TTTRLIB_PTO_TUI=ON` with the vendored header; every
   operation reachable from the keyboard alone; and a pty smoke test starts it,
   sends `q`, and asserts exit `0` with the terminal mode and screen restored.
8. Dispatch: all four rows of the Part 3 table asserted, in particular that
   piping produces `ls` output and never an escape sequence.
9. `pto --bundle` output: runs on those three platforms, prints the object table
   with no arguments, and `extract` reproduces every payload byte-for-byte
   against `PtoFile::extract`.
10. A bundle asked for `ui` with no reader installed prints where to get one and
    falls back to `ls` with exit `0`; with `$PTO_READER` set it execs that. A
    test asserts the search order **excludes** `.` and the container's own
    directory.
11. `PtoFile::open` and `is_pto_file` accept the bundle; `pto_read_events`
    targeted reads work through it unchanged (PRD-020's tests, re-run against a
    bundled copy of the same container).
12. `pto_ebml_check` **rejects** the bundle, and a test asserts the rejection
    with its reason.
13. Prefix length `P` is a multiple of 8 for every bundle, asserted directly.
14. Binary sizes recorded in this PRD as measured numbers: `pto.com`, and
    `ptoview.com` if built.

## Risks

| Risk | Weight |
|---|---|
| **Data files that execute code.** Mail gateways strip them, S3 serves them with the wrong content type, AV quarantines them, repositories and reviewers distrust them, and a collaborator who receives an executable named after a measurement has to trust the sender. This is the real cost, it is why bundling is opt-in and per-file, and it is why Part 2 exists to make the common case need none of it. | High |
| **TUI binary size under `cosmoc++`.** `<regex>` + `<iostream>` + `<filesystem>` statically linked; unmeasured, and the number could be 10× the C tool. Contained by the TUI being a separate optional binary that is never embedded — but if it is bad, `ptoview.com` stops being emailable and only Part 3's local build survives. | High |
| **cpptui under Cosmopolitan on Windows.** An APE build compiles the POSIX branch, so `termios`/`TIOCGWINSZ`/console `read()` all go through Cosmopolitan's emulation, plus UTF-8 block glyphs in `cmd.exe`. Fallback is a Windows CLI and a Unix-only TUI, which is survivable but should be known before shipping. | Medium |
| **Vendoring 570 KB of third-party C++17** into `thirdparty/cpptui/`, MIT, pinned by commit, plus a licence entry. Ordinary for this repo (`HighFive`, `libtiff`, `nlohmann_json`) but it is the largest single header in the tree. | Low |
| **`PtoBanner` changes written files.** The only part of this PRD that does. Needs a `DocTypeVersion` decision and old readers must skip an unknown Segment child cleanly — which is what `io_pto.cpp` already does, but it must be tested rather than assumed. | Medium |
| APE header bytes change upstream, invalidating the 12618 arithmetic. The bundler must derive `P` from the stub it actually has, never from a constant. | Medium |
| `file(1)` says "DOS/MZ executable" for a bundle. Correct, and worth a line in the docs so nobody debugs it. | Low |
| `cosmocc` as a build dependency for the portable artifacts. Contained: optional CMake flag, one CI runner. | Low |
| `0x4D5A` turns out to be assigned in Matroska. Costs the "walks as EBML" property, not the bundle. | Low |

## Open questions

1. **Is the bundle worth building at all once `pto.com` exists?** The honest case
   for it is one collaborator who will not download a 150 KB file. Part 5 makes
   it cheap and safe, but "ship the tool" answers the motivation section on its
   own, and every argument in Part 5's framing section is an argument for
   deferring the bundle indefinitely.
2. **Does `PtoBanner` deserve a `DocTypeVersion` bump, or is an unknown Segment
   child a non-event?** Old readers skip it; the question is whether the format
   wants to say it happened.
3. **Is the EBML-prefix layout worth it, or is `extract` enough?** The layout
   buys `PtoFile::open("run.pto.com")` working directly. If nobody wants that,
   the ZIP layout is more conventional and `unzip -p` is a nicer escape hatch
   than a custom flag.
4. **One binary or two?** A single `pto` that contains the TUI is simpler to
   explain and to install; two binaries keep the small one small. The size
   measurement in Part 4 decides this, not taste.
5. **Do `tools/` conventions stretch to C and C++?** `tools/` is Python today.
   These are shipped tools rather than test aids, which argues for `tools/`
   anyway, but `test/tools/` already holds `pto_ebml_check.cpp`.
6. **Should the bundle carry a `.pto` name at all?** `run.pto.com` is honest.
   `run.pto` on Unix only is the alternative, and the Windows finding above is
   why it is not the proposal.
