"""PTO container builder — wraps PtoFile and writes provenance tags automatically.

Every artifact added through this builder gets the full ``_mmfdb_*`` tag set.
Callers pass a :class:`~prototype.mfd_pipeline.provenance.ProvenanceNode`
describing what the artifact is and where it came from; the builder does the
rest.
"""

from __future__ import annotations

import json
from typing import Optional

import numpy as np
import tttrlib

from .provenance import ProvenanceNode, settings_hash

CONTAINER_PROFILE = "PTO.MFDB"
PROFILE_VERSION = "1.1"
PROFILE_READ_VERSION = "1"


def _add_tag(pto: tttrlib.PtoFile, target: int, name: str,
             pto_type: int, value) -> None:
    tag = tttrlib.PtoTag()
    tag.target = target
    tag.name = name
    tag.type = pto_type
    if pto_type == tttrlib.PtoType_Text:
        tag.text = str(value)
    elif pto_type in (tttrlib.PtoType_UID, tttrlib.PtoType_UInt):
        tag.u = int(value)
    elif pto_type == tttrlib.PtoType_Int:
        tag.i = int(value)
    elif pto_type == tttrlib.PtoType_Float:
        tag.d = float(value)
    else:
        raise ValueError(f"Unsupported PtoType: {pto_type}")
    pto.add_tag(tag)


def _write_provenance(pto: tttrlib.PtoFile, node: ProvenanceNode) -> None:
    """Write the full _mmfdb_* tag set for one artifact."""
    uid = node.uid
    if node.row_grain:
        _add_tag(pto, uid, "_mmfdb_artifact.row_grain",
                 tttrlib.PtoType_Text, node.row_grain)
    if node.data_format:
        _add_tag(pto, uid, "_mmfdb_artifact.data_format",
                 tttrlib.PtoType_Text, node.data_format)
    if node.operation_type:
        _add_tag(pto, uid, "_mmfdb_operation.operation_type",
                 tttrlib.PtoType_Text, node.operation_type)
    if node.settings:
        settings_json = json.dumps(node.settings, sort_keys=True)
        shash = settings_hash(node.settings)
        _add_tag(pto, uid, "_mmfdb_operation.settings_json",
                 tttrlib.PtoType_Text, settings_json)
        _add_tag(pto, uid, "_mmfdb_operation.settings_hash",
                 tttrlib.PtoType_Text, shash)
    for src_uid in node.source_uids:
        _add_tag(pto, uid, "_mmfdb_edge.source_uid",
                 tttrlib.PtoType_UID, src_uid)
        _add_tag(pto, uid, "_mmfdb_edge.source_node_id",
                 tttrlib.PtoType_UID, src_uid)
    if node.relationship:
        _add_tag(pto, uid, "_mmfdb_edge.relationship_type",
                 tttrlib.PtoType_Text, node.relationship)


class PtoBuilder:
    """Write-side container wrapper with automatic provenance tracking.

    Usage::

        builder = PtoBuilder()
        builder.create("out.pto", "My Container")
        stream_uid = builder.add_photon_stream(tttr_data, "data/run0.sm")
        burst_uid = builder.add_burst_table(
            store, "burstwise/abc/bi4_bur/run0.bur",
            ProvenanceNode(
                data_format="bur", row_grain="burst",
                operation_type="burst_selection",
                settings={"L": 20, "m": 5, "T": 5e-4},
                source_uids=[stream_uid],
                relationship="derived_from",
            ),
        )
        builder.commit()
        builder.close()
    """

    def __init__(self):
        self._pto: Optional[tttrlib.PtoFile] = None
        self._nodes: list[ProvenanceNode] = []

    @property
    def pto(self) -> tttrlib.PtoFile:
        if self._pto is None:
            raise RuntimeError("PtoBuilder not opened")
        return self._pto

    def create(self, path: str, title: str = "") -> None:
        self._pto = tttrlib.PtoFile()
        if not self._pto.create(path, title):
            raise RuntimeError(f"Failed to create PTO: {self._pto.error()}")
        _add_tag(self._pto, 0, "_mmfdb_container.profile",
                 tttrlib.PtoType_Text, CONTAINER_PROFILE)
        _add_tag(self._pto, 0, "_mmfdb_container.profile_version",
                 tttrlib.PtoType_Text, PROFILE_VERSION)
        _add_tag(self._pto, 0, "_mmfdb_container.profile_read_version",
                 tttrlib.PtoType_Text, PROFILE_READ_VERSION)

    def add_photon_stream(self, tttr_data: tttrlib.TTTR,
                          container_name: str,
                          temp_prefix: str = ".tmp_stream") -> int:
        """Write a TTTR photon stream into the container."""
        temp_file = f"{temp_prefix}_{container_name.replace('/', '_')}.sm"
        try:
            tttr_data.write(temp_file)
            uid = self.pto.add_file(
                "tttr_photon_stream", "sm", container_name, temp_file
            )
        finally:
            import os
            if os.path.exists(temp_file):
                os.unlink(temp_file)
        node = ProvenanceNode(
            uid=uid, kind="tttr_photon_stream",
            name=container_name,
            data_format="sm",
        )
        self._nodes.append(node)
        _write_provenance(self.pto, node)
        return uid

    def add_burst_table(self, store: tttrlib.DataStore,
                        container_name: str,
                        node: ProvenanceNode) -> int:
        """Write a burst-analysis DataStore with full provenance."""
        uid = tttrlib.pto_add_store(
            self.pto, "burst_table", container_name, store
        )
        node.uid = uid
        node.kind = "burst_table"
        node.name = container_name
        self._nodes.append(node)
        _write_provenance(self.pto, node)
        return uid

    def add_irf_curve(self, store: tttrlib.DataStore,
                      container_name: str,
                      node: ProvenanceNode) -> int:
        """Write an IRF curve DataStore with full provenance."""
        uid = tttrlib.pto_add_store(
            self.pto, "irf_curve", container_name, store
        )
        node.uid = uid
        node.kind = "irf_curve"
        node.name = container_name
        self._nodes.append(node)
        _write_provenance(self.pto, node)
        return uid

    def add_calibrated_by(self, derived_uid: int, irf_uid: int) -> None:
        """Add a calibrated_by edge from a burst table to an IRF."""
        _add_tag(self.pto, derived_uid, "_mmfdb_edge.source_node_id",
                 tttrlib.PtoType_UID, irf_uid)
        _add_tag(self.pto, derived_uid, "_mmfdb_edge.relationship_type",
                 tttrlib.PtoType_Text, "calibrated_by")

    def commit(self) -> None:
        if not self.pto.commit():
            raise RuntimeError(f"Failed to commit PTO: {self.pto.error()}")

    def close(self) -> None:
        if self._pto is not None:
            self._pto.close()
            self._pto = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_type is None:
            self.commit()
        self.close()
        return False
