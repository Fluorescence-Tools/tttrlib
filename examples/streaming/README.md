# Streaming acquisition

Writing photons to disk **as they arrive**, for a measurement that does not fit
in memory and may be killed before it ends.

`TTTR.write` is the other case: it stores a measurement that is already
finished, and to do that it needs the whole thing in RAM. These examples are
the acquisition path.

| Example | What it shows |
|---|---|
| [`stream_to_any_format.py`](stream_to_any_format.py) | The same four calls against **every** streamable container, each judged against a whole-file `TTTR.write` to the same format |
| [`stream_to_many_consumers.py`](stream_to_many_consumers.py) | One stream feeding a file *and* live analysis, in one pass |

Bounded memory, live reading during acquisition and survival of a killed writer
are pinned as tests rather than examples, in
`test/python/test_pto_stream.py` — they need subprocesses and a `SIGKILL`,
which makes them awkward to read as example code and precise as tests.

## The four calls

```python
w = tttrlib.RecordStreamWriter(container_type)   # or PtoPhotonStream()
w.create(path, header, "run")
w.append(macro, micro, routing_channel, event_type)   # as often as you like
w.checkpoint()                                        # make it durable
w.close()
```

`append` copies and returns — it does no file I/O, so an instrument thread is
never blocked by a slow disk. A writer thread drains the buffer. When the
buffer is full `append` **blocks**; it never drops. `n_dropped()` is zero by
construction and `n_stalls()` tells you the disk could not keep up.

## Two knobs, two questions

* `set_buffer_limit(n)` — how many events may sit in memory. This is the memory
  bound and the no-loss guarantee in one number.
* `set_auto_checkpoint(n)` — how much a crash may cost.

They are separate because a caller usually wants the second much smaller than
the first.
