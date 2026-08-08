# tttr CLI progress: library ticker, render in the CLI

The CLI runs long jobs (PTU/PTW conversion, burst search, image export, PTO
extraction) that outlive interactive patience. Those jobs must (1) show a
progress bar to a human on a terminal, and (2) emit machine-readable progress
so a GUI or any client can drive a progress panel from `cli -> progress -> gui`.

## Two layers, one source of truth

```
long-running work -> tttrlib::ProgressTicker  (util, no I/O)
                                 |  set_total / tick / finish
                                 v
                    Progress::open("--progress <channel>")  (cli, renderer)
            human bar (tty stderr)   |   JSONL (--progress -, stdout, file)
            "\r[ 42%] ..."           |   {"event":"progress",...}
```

The ticker lives in `modules/util` (not `core`). `core`'s CMake already depends
on `util`, and every other module including `cli` links `util`, so placing the
ticker in `util` makes it the lowest common layer available to the whole
library — every public API that does heavy work could forward ticks through it
with no new dependency. The CLI only *renders*: a `Progress` object holds a
`ProgressTicker` and installs one sink that fans each event out to the bar and
to the JSONL channel. No clock, no counter, no throttle lives in the CLI anymore
that the library doesn't already own.

## The event contract (JSONL)

One JSON object per line, line-buffered, flushed on every emit. Fields are
stable so a client parses them without guessing units:

```
{"event":"begin","job":"sm","phase":"burst search","done":0,"total":3,"fraction":0.0000,"seconds":0.000}
{"event":"progress","job":"sm","phase":"write","done":3,"total":3,"fraction":1.0000,"seconds":0.038}
{"event":"finish","job":"sm","phase":"write","done":3,"total":3,"fraction":1.0000,"seconds":0.038}
```

* `event`: `begin` / `progress` / `finish`.
* `job`: the subcommand label.
* `phase`: a coarse sub-stage set by the command (`"load"`, `"search"`,
  `"write"`, `"extract"`...). Empty for the top-level `begin`.
* `done`/`total`: integer count and denominator; `fraction` is `done/total`
  rendered to 4 dp so clients don't recompute.
* `seconds`: wall time since the job's own `begin()`, in seconds with 3 dp.

## Throttle

`ProgressTicker::emit("tick")` is throttled inside the library: a `tick` is
dropped if fewer than 60 ms have elapsed since the last emit **and**
`done < total`. `begin`/`finish` are never throttled, and a `tick` on the
final step always goes through (the `done < total` guard). This keeps a 1 M
record PTO extraction from spamming a million lines while still delivering a
definite terminal line on completion.

## Channel selection (`--progress`)

| value                     | bar to stderr | JSON to ...          |
|---------------------------|---------------|----------------------|
| (absent)                  | tty only      | none                 |
| `-` / `stderr`            | tty           | stderr               |
| `stdout`                  | none          | stdout               |
| `<file path>`             | tty           | that file            |
| `none`                    | none          | none                 |

The console bar only draws when its stream is a tty; redirecting stderr to a
file silently stops redrawing the `\r...` in place, which is what you want for
a transcript. The JSON channel is independent of the console channel.

## Gotchas / measurements

* The original `cmd_convert` had no `set_total` and no `finish` — its events
  printed `total=0` and never closed, which is why no client could tell it had
  ended. Lifecycle is now `begin -> set_total -> tick/phase -> finish` on both
  the success and error paths.
* cxxopts reads `argv[0]` as the program name. After a subcommand is stripped,
  the remaining args must leave one slot for that `argv[0]`, i.e. advance by
  exactly one (`argc -= 1; argv += 1;`). `cmd_image` and `cmd_pto` shipped
  shipping `argc -= 2` and silently dropped the first positional (the input
  file), surfacing as "needs an input file".
