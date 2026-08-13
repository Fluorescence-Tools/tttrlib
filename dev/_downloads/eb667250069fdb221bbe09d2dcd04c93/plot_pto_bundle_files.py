"""
==========================================
Bundling a measurement's files into a .pto
==========================================

A measurement is rarely one file. There is the instrument file, the settings
sidecar it cannot be read without, the burst table somebody computed from it,
the protocol, and a note about what went wrong on the third repeat. They travel
as a folder, get zipped to be sent, and arrive with the sidecar missing.

A PTO container carries all of them in one file, and — unlike a zip — the
photon stream inside it stays *readable where it lies*: you can open the
container and analyse the measurement without unpacking anything.

This example bundles a folder into a container, reads the photon stream back
out of it, and takes the folder apart again.
"""

# %%
import sys
from pathlib import Path
import numpy as np
import pylab as plt
import tttrlib
# Make the `examples` package importable when this script is run directly,
# from any working directory.
sys.path[:0] = [str(_p) for _p in Path(__file__).resolve().parents
                if (_p / "examples" / "_example_data.py").is_file()][:1]

from examples._example_data import get_data_path, get_output_path

# %%
# A measurement as it arrives: a folder
# ---------------------------------------------------------------------------
# A Becker & Hickl ``.spc`` and its ``.set``, a table, and a note. The ``.set``
# holds half the header: a reader handed the ``.spc`` alone silently reads the
# wrong one, which is why these two must never be separated.
folder = get_output_path("measurement")
(folder / "raw").mkdir(parents=True, exist_ok=True)
(folder / "notes").mkdir(parents=True, exist_ok=True)

for suffix in (".spc", ".set"):
    source = get_data_path("bh/bh_spcqc004" + suffix)
    (folder / "raw" / source.name).write_bytes(source.read_bytes())
(folder / "notes" / "protocol.txt").write_text("sample: DNA ruler\nbuffer: PBS\n")
(folder / "notes" / "bursts.csv").write_text("burst,duration_ms\n1,2.5\n2,0.8\n")

for path in sorted(folder.rglob("*")):
    if path.is_file():
        print(f"{path.relative_to(folder)!s:32s} {path.stat().st_size:>9,} bytes")

# %%
# Bundling it
# ---------------------------------------------------------------------------
# :func:`tttrlib.pto_bundle` takes paths and files. A directory means everything
# under it, and each object is named by its path *relative to that directory*,
# so two files with the same name in different folders stay two files.
#
# Nothing is written to disk permanently until :meth:`PtoFile.commit`: a crash
# before it leaves the container exactly as it was.
container = get_output_path("measurement.pto")

pto = tttrlib.PtoFile()
pto.create(str(container), "DNA ruler, run 4")
pto.set_writing_app("plot_pto_bundle_files.py")
objects = tttrlib.pto_bundle(pto, folder)
pto.commit()

for o in objects:
    print(f"{o.name:32s} {o.kind:11s} {o.encoding:8s} {o.media_type}")

# %%
# What the container worked out for itself
# ---------------------------------------------------------------------------
# Nothing above said what any of those files *were*. Each object's kind and
# encoding come from :func:`tttrlib.pto_classify_path`, and a photon stream is
# recognised by its **contents**, not its extension — which is the only way to
# tell the four different formats claiming ``.spc`` apart:
for name in ("bh/bh_spcqc004.spc", "pq/ptu/pq_ptu_hh_t3.ptu", "hdf/1a_1b_Mix.hdf5"):
    guess = tttrlib.pto_classify_path(str(get_data_path(name)))
    print(f"{name:30s} -> {guess.kind:10s} {guess.encoding}")

# %%
# Reading the measurement without unpacking it
# ---------------------------------------------------------------------------
# The payoff. ``container|object`` names a photon stream inside the container,
# and it decodes where it lies — no temporary file, no copy of the eight
# gigabytes. The ``.set`` was tied to the ``.spc`` when they were bundled
# (:data:`tttrlib.kPtoSidecarTag`), so the reader still gets the whole header.
data = tttrlib.pto_events(f"{container}|raw/bh_spcqc004.spc")
print(f"{len(data):,} events read out of the container")

direct = tttrlib.TTTR(str(folder / "raw" / "bh_spcqc004.spc"))
print(f"{len(direct):,} events read from the file on its own")
assert np.array_equal(data.macro_times, direct.macro_times)

# %%
# ...and the folder back again
# ---------------------------------------------------------------------------
# :meth:`PtoFile.disassemble` writes every object out under the name it was
# bundled with, directories included, for a tool that reads only ``.spc`` files.
unpacked = get_output_path("unpacked")
opened = tttrlib.PtoFile()
opened.open(str(container))
for written in opened.disassemble(str(unpacked)):
    print(Path(written).relative_to(unpacked))

# %%
# One file, and still a measurement
# ---------------------------------------------------------------------------
# The micro-time histogram of the stream that never left the container, beside
# what each object costs inside it.
fig, (ax_decay, ax_size) = plt.subplots(1, 2, figsize=(11, 4))

counts = data.get_microtime_histogram()[0]
ax_decay.semilogy(counts)
ax_decay.set_xlabel("micro time channel")
ax_decay.set_ylabel("counts")
ax_decay.set_title("read from inside measurement.pto")

names = [o.name for o in objects]
ax_size.barh(names, [max(o.size, 1) for o in objects], color="#4878cf")
ax_size.set_xscale("log")
ax_size.set_xlabel("payload (bytes)")
ax_size.set_title(f"{container.stat().st_size / 1e6:.1f} MB in one file")
ax_size.invert_yaxis()

plt.tight_layout()
plt.show()

# %%
# The same thing from the command line
# ---------------------------------------------------------------------------
# .. code-block:: bash
#
#     tttr pto pack -o measurement.pto --title "DNA ruler, run 4" measurement/
#     tttr pto add measurement.pto late-note.md   # bundle more into an existing one
#     tttr pto ls measurement.pto                 # what is in it
#     tttr pto extract-all measurement.pto out/   # the folder back again
#
# .. note::
#
#    Bundling copies bytes: the container holds the files, and the originals
#    can be deleted. Objects can also be added, replaced and removed in place
#    afterwards — recomputing a burst table beside an 8 GiB photon stream
#    rewrites the burst table, not the file. See :doc:`/formats/pto`.
