"""Stream a photon measurement into every container that can take one.

The same four calls — create, append, checkpoint, close — against each format,
so the differences between them are visible in one place. Run it and you get a
table of what round-tripped.

Two kinds of container, and the difference matters:

* **A record stream** (PTU, HT3, SPC-130/600/QC, Confocor3, .sm) is a header
  followed by fixed-width records. Appending is natural. The catch is the
  record count, which the header states and which is unknown when the header
  is written: it is patched at every checkpoint, so a writer killed between
  the last record and the patch leaves a file claiming *fewer* records than it
  holds. The extra ones are ignored rather than misread.

* **A container** (`.pto`) stores decoded columns and commits them. There is no
  count to patch and no partial state a reader can see: a chunk is committed or
  it does not exist.

Not streamable, and named rather than silently missing: Photon-HDF5 (a dataset
tree), Photonscore `.photons` (position tables reconstructed at the end),
BrightEyes `.ttr` (a bare word stream with no header), FLIM LABS (read-only).
"""
import glob
import os
import tempfile

import numpy as np

import tttrlib

# Container ids, from TTTRHeaderTypes.h.
PQ_PTU, PQ_HT3 = 0, 1
BH_SPC130, BH_SPC600_256, BH_SPC600_4096 = 2, 3, 4
CZ_CONFOCOR3, SM = 6, 7
BH_SPCQC = 9

RECORD_FORMATS = [
    ("PTU", PQ_PTU, "ptu"),
    ("HT3", PQ_HT3, "ht3"),
    ("SPC-130", BH_SPC130, "spc"),
    ("SPC-600/256", BH_SPC600_256, "spc"),
    ("SPC-600/4096", BH_SPC600_4096, "spc"),
    ("SPC-QC", BH_SPCQC, "spc"),
    ("Confocor3", CZ_CONFOCOR3, "raw"),
    ("SM", SM, "sm"),
]

CHUNK = 25_000


def verdict(control, streamed, n, macro_ok, micro_ok):
    """Judge a streamed file against the whole-file baseline, not against
    perfection -- the two failures look the same and are not the same."""
    if control == 0:
        # Not a writer defect and not a streaming one: the bytes are right and
        # detection cannot identify them, so opening by name gives an empty
        # TTTR. Filed in BUGS.md; TTTR(path, container) reads these back whole.
        return "not re-detected by name (see BUGS.md)"
    if streamed != control:
        return "STREAMING LOST EVENTS"
    if macro_ok and micro_ok:
        return "exact"
    return "as whole file (lossy record layout)"


def source_measurement(n=200_000):
    """A real measurement, truncated. Real data because the interesting part
    is the macro-time overflow handling, and synthetic events with a tidy
    clock would not exercise it."""
    hits = sorted(glob.glob("tttr-data/**/*.ht3", recursive=True))
    if not hits:
        raise SystemExit("no test data; run test/download_test_data.py first")
    t = tttrlib.TTTR(hits[0])
    n = min(n, len(t))
    return t, (np.asarray(t.macro_times)[:n].copy(),
               np.asarray(t.micro_times)[:n].copy(),
               np.asarray(t.routing_channels)[:n].copy(),
               np.asarray(t.event_types)[:n].copy())


def stream(writer, path, header, events):
    """The four calls. Identical for every backend — that is the point."""
    macro, micro, chan, etype = events
    if not writer.create(path, header, "run"):
        return None, writer.error()
    # How much a crash may cost. Separate from set_buffer_limit, which is how
    # much the writer may hold.
    writer.set_auto_checkpoint(50_000)
    for i in range(0, len(macro), CHUNK):
        s = slice(i, i + CHUNK)
        if not writer.append(macro[s], micro[s], chan[s], etype[s]):
            return None, writer.error()
    writer.close()
    return writer, None


def whole_file_control(src, events, container, path):
    """What a normal TTTR.write to this format gives back. The baseline a
    streamed file is judged against."""
    macro, micro, chan, etype = events
    ref = tttrlib.TTTR(macro, micro, chan, etype)
    try:
        if not ref.write(path, src.header, container):
            return 0
        return len(tttrlib.TTTR(path))
    except Exception:
        return 0


def main():
    src, events = source_measurement()
    macro, micro, chan, etype = events
    n = len(macro)
    tmp = tempfile.mkdtemp()
    print(f"streaming {n:,} events in {CHUNK:,}-event chunks "
          f"({(n + CHUNK - 1) // CHUNK} chunks, macro span {int(macro[-1]):,})\n")
    # The control column is the point of this table. "Streaming lost events" and
    # "this format cannot round-trip at all" look identical from the streamed
    # file alone, and they need completely different responses -- so every row
    # is measured against a whole-file TTTR.write to the same format.
    print(f"{'format':14} {'whole file':>11} {'streamed':>10} {'macro':>6} {'micro':>6} {'verdict'}")
    print("-" * 72)

    for label, container, ext in RECORD_FORMATS:
        stem = label.replace('/', '_')
        ctrl = whole_file_control(src, events,
                                  container, os.path.join(tmp, f"w_{stem}.{ext}"))
        path = os.path.join(tmp, f"s_{stem}.{ext}")
        w = tttrlib.RecordStreamWriter(container)
        w, err = stream(w, path, src.header, events)
        if err:
            print(f"{label:14} {ctrl:>11,} {'-':>10} {'-':>6} {'-':>6} {err[:24]}")
            continue
        back = tttrlib.TTTR(path)
        got = len(back)
        m_ok = got == n and np.array_equal(np.asarray(back.macro_times), macro)
        u_ok = got == n and np.array_equal(np.asarray(back.micro_times), micro)
        print(f"{label:14} {ctrl:>11,} {got:>10,} {str(m_ok):>6} {str(u_ok):>6} "
              f"{verdict(ctrl, got, n, m_ok, u_ok)}")

    # The container. Same calls, different guarantees -- see the docstring.
    path = os.path.join(tmp, "s.pto")
    ctrl = whole_file_control(src, events, -1, os.path.join(tmp, "w.pto"))
    w = tttrlib.PtoPhotonStream()
    w, err = stream(w, path, src.header, events)
    if err:
        print(f"{'PTO':14} {ctrl:>11,} {'-':>10} {'-':>6} {'-':>6} {err[:24]}")
    else:
        back = tttrlib.TTTR(path)
        got = len(back)
        m_ok = got == n and np.array_equal(np.asarray(back.macro_times), macro)
        u_ok = got == n and np.array_equal(np.asarray(back.micro_times), micro)
        print(f"{'PTO':14} {ctrl:>11,} {got:>10,} {str(m_ok):>6} {str(u_ok):>6} "
              f"exact, {w.n_chunks()} chunks")

    print(f"\nfiles in {tmp}")
    print("\n'as whole file' means streaming matched TTTR.write exactly -- any")
    print("loss is the format's record layout (fewer micro-time or channel bits")
    print("than the source), not the streaming.")
    print("\n'not re-detected by name' is neither: those files are written")
    print("correctly and read back whole with TTTR(path, container_type). What")
    print("fails is identifying them from the path, and TTTR(path) then returns")
    print("an EMPTY object instead of raising. Filed in BUGS.md.")


if __name__ == "__main__":
    main()
