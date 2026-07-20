"""
Basic Example 4: Writing TTTR data and converting formats
--------------------------------------------------------
This example demonstrates three common write operations:

1. Writing a subset of events to a new PTU file.
2. Making a full copy of a TTTR dataset in its native format.
3. Re-using a header from another file to *convert* a dataset between
   vendor formats (borrowed from the unit tests in
   ``test/test_TTTR_transcode.py``).

The example uses the real demo data bundle; set ``TTTRLIB_DATA`` as described
in ``test/test_settings.py`` before building the documentation.
"""

from __future__ import annotations

import tttrlib

from examples._example_data import get_data_path, get_output_path


# ---------------------------------------------------------------------------
# Locate source data and prepare writable paths in the temporary examples dir.
# ---------------------------------------------------------------------------
ptu_source = get_data_path("pq/ptu/pq_ptu_hh_t3.ptu")
spc_source = get_data_path("bh/bh_spc132.spc")

subset_output = get_output_path("output_subset.ptu")
subset_output.unlink(missing_ok=True)

copy_output = get_output_path("output_spc_copy.spc")
copy_output.unlink(missing_ok=True)

converted_output = get_output_path("output_spc_as_ptu.ptu")
converted_output.unlink(missing_ok=True)


# ---------------------------------------------------------------------------
# 1) Write a subset of events to a new PTU file
# ---------------------------------------------------------------------------
print("Loading PTU demo data …")
tttr_ptu = tttrlib.TTTR(str(ptu_source), "PTU")
print(f"Total PTU events: {len(tttr_ptu):,}")

subset = tttr_ptu[:100]
subset.write(str(subset_output))
print(f"Wrote first 100 events to {subset_output}")


# ---------------------------------------------------------------------------
# 2) Copy a TTTR dataset in its native format (SPC-130 → SPC-130)
# ---------------------------------------------------------------------------
print("Loading SPC-130 demo data …")
tttr_spc = tttrlib.TTTR(str(spc_source), "SPC-130")
print(f"Total SPC events: {len(tttr_spc):,}")

tttr_spc.write(str(copy_output))
print(f"Wrote full SPC dataset copy to {copy_output}")


# ---------------------------------------------------------------------------
# 3) Convert SPC-130 data to a PTU container by re-using a PTU header
# ---------------------------------------------------------------------------
# The unit tests demonstrate that we can borrow a header from another TTTR file
# to change container/record metadata during the write step. The header will be
# adapted to match the SPC payload on write.
print("Borrowing PTU header for conversion …")
ptu_header_template = tttrlib.TTTRHeader(str(ptu_source), 0)

tttr_spc.write(str(converted_output), ptu_header_template)
print(f"Converted SPC data to PTU container at {converted_output}")

# Quick sanity check: read the converted file back.
converted = tttrlib.TTTR(str(converted_output))
print(
    "Converted file payload:"
    f" {len(converted):,} photons,"
    f" {len(converted.micro_times):,} micro times,"
    f" {len(converted.routing_channels):,} routing channels",
)
