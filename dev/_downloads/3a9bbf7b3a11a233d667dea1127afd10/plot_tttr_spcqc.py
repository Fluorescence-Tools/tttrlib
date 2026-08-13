"""
==========================================
Becker & Hickl SPC-QC files and conversion
==========================================

Read a measurement from an SPC-QC module (SPC-QC-104/004, SPC-QC-106/006),
inspect what its records carry, and convert it to and from the other TTTR
containers.

The QC generation writes ``.spc`` files like the classic SPC-130/600 cards, but
with a record layout of its own. Three differences matter in practice:

* The **micro time is not inverted**. Every classic SPC card stores a reversed
  ADC value, so tttrlib un-reverses it on read; the QC modules already store the
  micro time the way SPCM histograms it.
* A **detector is a router signal plus a module input**, not just a routing
  number, so a QC routing channel carries both (see below).
* The **macro time clock is in femtoseconds** in the file header — the QC
  counter runs at about 2 ns, far finer than the classic 0.1 ns unit could
  express — and it is *independent of the TAC*, so the micro time resolution
  comes from the companion ``.set`` file rather than from the header.

See the :ref:`file format guide <file_formats>` for the full record layout.
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

source = get_data_path("bh/bh_spcqc004.spc")
data = tttrlib.TTTR(str(source))

print(f"Container:   {data.get_tttr_container_type()}")
print(f"Events:      {len(data):,}")
print(f"Channels:    {np.unique(data.routing_channels)}")
print(f"Macro clock: {data.header.macro_time_resolution * 1e9:.6f} ns")
print(f"Micro bin:   {data.header.micro_time_resolution * 1e12:.3f} ps")
print(f"Duration:    {data.macro_times[-1] * data.header.macro_time_resolution:.3f} s")

# %%
# The container is detected from the file
# ---------------------------------------------------------------------------
# SPC-QC files share the ``.spc`` extension with the classic SPC-130 and
# SPC-600 flavours, so ``tttrlib.TTTR(filename)`` inspects the header flags and
# the record stream rather than the extension. Passing ``"SPC-QC"`` explicitly
# works too, and is worth doing when the file has an unusual name.

# %%
# Two clocks, not one
# ---------------------------------------------------------------------------
# On a classic SPC card the micro time resolution is the macro time clock
# divided by 4096. That does not hold here — the QC TAC runs independently, so
# the two are unrelated and the TAC range is only recorded in the ``.set``
# sidecar next to the ``.spc`` (``SP_TAC_R`` / ``SP_ADC_RE``). tttrlib picks
# that file up automatically. **Without it** a default range is assumed, so
# check ``micro_time_resolution`` when a ``.spc`` arrives on its own.
ratio = data.header.macro_time_resolution / data.header.micro_time_resolution
print(f"macro / micro = {ratio:.1f}  (would be 4096 on a classic SPC card)")

# %%
# A detector is a router signal *and* a module input
# ---------------------------------------------------------------------------
# Both are packed into ``routing_channels``: the router signal in the low bits
# and the module input directly above the routing width the header declares.
# With no router attached — the common case, and the case here — the channel is
# simply the module input, so the channels read 0, 1, 2.
for channel in np.unique(data.routing_channels):
    n = int((data.routing_channels == channel).sum())
    print(f"channel {channel}: {n:,} photons")

# %%
# Writing SPC-QC reproduces the instrument's own bytes
# ---------------------------------------------------------------------------
# Re-writing the measurement gives back SPCM's record stream verbatim, so a
# tttrlib-written file is not merely one that tttrlib can read again — SPCM and
# other readers see exactly what the module produced. Only the tail differs:
# SPCM keeps emitting macro time overflow records after the last photon, and
# those carry nothing worth preserving.
out_spc = get_output_path("spcqc_rewritten.spc")
out_spc.unlink(missing_ok=True)
assert data.write(str(out_spc))

written = np.fromfile(out_spc, dtype="<u4")
original = np.fromfile(source, dtype="<u4")
identical = np.array_equal(written, original[: len(written)])
print(f"records written: {len(written):,} of {len(original):,}")
print(f"byte-identical to the instrument's file: {identical}")
assert identical

# %%
# Converting to other containers
# ---------------------------------------------------------------------------
# ``TTTR.write`` picks the container from the output extension. Since all
# Becker & Hickl flavours share ``.spc``, writing an SPC-QC source back to
# ``.spc`` keeps it an SPC-QC file rather than downgrading it to SPC-130 —
# which is why the byte-for-byte rewrite above needed no arguments.
#
# .. note::
#
#    ``write`` **retargets the header** it writes through: after
#    ``data.write("out.ptu")`` the object's container type is PTU, so a
#    following ``data.write("out.spc")`` is a cross-family conversion and lands
#    on the default SPC flavour, SPC-130. Name the container explicitly
#    (``write(path, "SPC-QC")``) or re-read the source when converting the same
#    object several times.
out_ptu = get_output_path("spcqc_converted.ptu")
out_hdf5 = get_output_path("spcqc_converted.hdf5")
for path in (out_ptu, out_hdf5):
    path.unlink(missing_ok=True)

assert data.write(str(out_ptu))
as_ptu = tttrlib.TTTR(str(out_ptu), "PTU")
assert np.array_equal(data.macro_times, as_ptu.macro_times)
assert np.array_equal(data.micro_times, as_ptu.micro_times)
assert np.array_equal(data.routing_channels, as_ptu.routing_channels)
print("→ PTU: lossless (12-bit micro times fit the 15-bit HydraHarp T3 field)")

assert data.write(str(out_hdf5))
as_hdf5 = tttrlib.TTTR(str(out_hdf5), "PHOTON-HDF5")
assert np.array_equal(data.macro_times, as_hdf5.macro_times)
assert np.array_equal(data.micro_times, as_hdf5.micro_times)
print("→ Photon-HDF5: lossless, and readable by any Photon-HDF5 tool")

# %%
# Coming back the other way
# ---------------------------------------------------------------------------
# Converting *into* SPC-QC has one limit worth knowing: a QC-x04 record holds a
# 4-bit router signal and a 2-bit module input, so channels 0..63 survive and
# anything above that cannot be represented. Micro times clip to 12 bit.
#
# The macro time overflow record carries no count, so one word is written per
# 4096 macro time units of idle time. A long, sparse measurement therefore
# produces a large file — the same way it does on the instrument.
round_trip = tttrlib.TTTR(str(out_ptu), "PTU")
out_back = get_output_path("spcqc_from_ptu.spc")
out_back.unlink(missing_ok=True)
# name the container: the source is a PTU, so ".spc" alone would pick SPC-130
assert round_trip.write(str(out_back), "SPC-QC")
back = tttrlib.TTTR(str(out_back))
assert np.array_equal(back.macro_times, data.macro_times)
assert np.array_equal(back.routing_channels, data.routing_channels)
print("→ PTU → SPC-QC: macro times and channels preserved")

# %%
# The decay, per module input
# ---------------------------------------------------------------------------
# Histogramming the micro times per channel reproduces the decay curves SPCM
# writes into the companion ``.sdt`` bin for bin. Note that the curves start at
# low bins and fall: no un-reversing is applied, unlike for classic SPC data.
fig, ax = plt.subplots(figsize=(7, 4))
edges = np.arange(0, 4097, 8)
for channel in np.unique(data.routing_channels):
    selection = data.routing_channels == channel
    ax.hist(
        data.micro_times[selection], bins=edges, histtype="step",
        label=f"input {channel} ({int(selection.sum()):,} photons)",
    )
ax.set_yscale("log")
ax.set_xlabel(f"micro time channel ({data.header.micro_time_resolution * 1e12:.0f} ps each)")
ax.set_ylabel("counts")
ax.set_title("SPC-QC-004 decay per module input")
ax.legend()
plt.tight_layout()
plt.show()
