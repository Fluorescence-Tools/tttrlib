"""
====================================
TTTR file conversion between formats
====================================

Convert a TTTR measurement between all writable container formats and verify
what survives each trip.

Every TTTR file decodes into the same four arrays (macro times, micro times,
routing channels, event types). Converting between formats is therefore a
matter of writing these arrays with a header that points at the target
container. Some containers cannot represent all fields — see the
conversion-fidelity table in the :ref:`file format guide <file_formats>`.

The conversions below are also covered by the unit tests in
``test/python/tttr/test_TTTR_roundtrip.py``.
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

# Container type identifiers (see TTTRHeaderTypes.h)
PTU, HT3, SPC130, SPC600_256, SPC600_4096, PHOTON_HDF5, CZ_RAW, SM = range(8)
SPCQC = 9
# Record type identifiers used below
HHT2v2, HHT3v2, RECORD_SPC130, RECORD_SM = 1, 4, 7, 11
RECORD_SPCQC_X04 = 15

source = get_data_path("pq/ptu/pq_ptu_hh_t3.ptu")
data = tttrlib.TTTR(str(source), "PTU")
print(f"Source: {len(data):,} events (HydraHarp T3 PTU)")


# %%
# Choosing the output format
# ---------------------------------------------------------------------------
# ``TTTR.write`` picks the container from the output filename extension, so the
# common case needs no extra arguments — ``write("out.spc")`` writes a Becker &
# Hickl SPC file and ``write("out.ptu")`` a PicoQuant PTU file. The format can
# also be forced explicitly, either by name or by container id, which is handy
# for unusual extensions:
#
# .. code-block:: python
#
#     data.write("out.ptu")           # inferred from the ".ptu" extension
#     data.write("out.dat", "PTU")     # forced by container name
#     data.write("out.dat", None, 0)   # forced by container id (see TTTRHeaderTypes.h)
#
# The ``convert`` helper below instead edits the header's record type as well,
# so it can also select the record encoding (e.g. T2 vs T3) within a container.


def convert(data, container_type, record_type, out_name, read_as):
    """Write ``data`` as another container format and read it back."""
    header = data.header
    header.tttr_container_type = container_type
    header.tttr_record_type = record_type
    out = get_output_path(out_name)
    out.unlink(missing_ok=True)
    assert data.write(str(out))
    return tttrlib.TTTR(str(out), read_as)


# %%
# PTU (T3) → Photon-HDF5 — the open interchange format; lossless
# ---------------------------------------------------------------------------
as_hdf5 = convert(data, PHOTON_HDF5, -1, "converted.hdf5", "PHOTON-HDF5")
assert np.array_equal(data.macro_times, as_hdf5.macro_times)
assert np.array_equal(data.micro_times, as_hdf5.micro_times)
assert np.array_equal(data.routing_channels, as_hdf5.routing_channels)
print("→ Photon-HDF5: macro, micro and channels preserved exactly")

# %%
# PTU (T3) → Becker & Hickl SPC-130 — micro times clip to 12 bit
# ---------------------------------------------------------------------------
as_spc = convert(data, SPC130, RECORD_SPC130, "converted.spc", "SPC-130")
photons = np.array(data.event_types) == 0
assert np.array_equal(
    np.minimum(data.micro_times[photons], 4095),
    np.array(as_spc.micro_times)[photons],
)
print("→ SPC-130: micro times clipped to 12 bit, macro times preserved")

# %%
# PTU (T3) → HT3 — the legacy HydraHarp container
# ---------------------------------------------------------------------------
as_ht3 = convert(data, HT3, HHT3v2, "converted.ht3", "HT3")
assert np.array_equal(data.macro_times, as_ht3.macro_times)
assert np.array_equal(data.micro_times, as_ht3.micro_times)
print("→ HT3: lossless for HydraHarp T3 payloads")

# %%
# PTU T3 → PTU T2 — drops the micro times
# ---------------------------------------------------------------------------
as_t2 = convert(data, PTU, HHT2v2, "converted_t2.ptu", "PTU")
assert np.array_equal(data.macro_times, as_t2.macro_times)
assert as_t2.micro_times.max(initial=0) == 0
print("→ PTU T2: micro times dropped, time tags and channels preserved")

# %%
# PTU (T3) → Becker & Hickl SPC-QC — the QC generation of SPC modules
# ---------------------------------------------------------------------------
# The QC record has 12 micro time bits like SPC-130, but splits the detector
# into a 4-bit router signal and a 2-bit module input (3 bits on QC-x06), so
# channels up to 63 survive — wider than SPC-130's 4-bit routing field.
#
# One thing to expect: the QC macro time overflow record carries no count, so
# one word is emitted per 4096 macro time units of idle time. Sparse, long
# measurements therefore produce large files — exactly as they do on the
# instrument.
as_qc = convert(data, SPCQC, RECORD_SPCQC_X04, "converted_qc.spc", "SPC-QC")
assert np.array_equal(data.macro_times, as_qc.macro_times)
assert np.array_equal(data.routing_channels, as_qc.routing_channels)
print("→ SPC-QC: macro times and channels preserved, micro times clipped to 12 bit")

# %%
# PTU (T3) → SM — macro times and channels only
# ---------------------------------------------------------------------------
as_sm = convert(data, SM, RECORD_SM, "converted.sm", "SM")
assert np.array_equal(data.macro_times, as_sm.macro_times)
assert np.array_equal(data.routing_channels, as_sm.routing_channels)
print("→ SM: macro times and channels preserved, micro times dropped")

# %%
# Visual check: the micro time histogram survives the SPC-130 trip
# ---------------------------------------------------------------------------
fig, ax = plt.subplots(1, 2, figsize=(9, 3.5), sharey=True)
ax[0].hist(data.micro_times[photons], bins=128, color="0.3")
ax[0].set_title("Source PTU (T3)")
ax[0].set_xlabel("micro time channel")
ax[0].set_ylabel("counts")
ax[1].hist(np.array(as_spc.micro_times)[photons], bins=128, color="tab:blue")
ax[1].set_title("After PTU → SPC-130 → read back")
ax[1].set_xlabel("micro time channel")
plt.tight_layout()
plt.show()
