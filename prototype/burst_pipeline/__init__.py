"""MFD burst-analysis pipeline with full PTO provenance.

Provenance contract — every artifact in the .pto must carry:

    _mmfdb_artifact.row_grain         "burst" | "curve_point"
    _mmfdb_artifact.data_format       "bur" | "bg4" | "br4" | "bv4" | "2c4" | "irf"
    _mmfdb_operation.operation_type   "burst_selection" | "mle_green" | ...
    _mmfdb_operation.settings_json    JSON dict of parameters that produced it
    _mmfdb_operation.settings_hash    SHA256[:16] of settings_json
    _mmfdb_edge.source_uid            UID of the parent artifact
    _mmfdb_edge.relationship_type     "derived_from" | "companion_of" | "calibrated_by"

The pipeline is restorable: read the .pto, extract settings_json from each
artifact, re-run the operation, and compare the output. If settings and
source photon stream are identical, the output must be identical.
"""
