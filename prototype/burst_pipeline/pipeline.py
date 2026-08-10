"""Generic burst-analysis pipeline with full PTO provenance.

A :class:`BurstPipeline` runs a list of :class:`Operation` stages over a
TTTR photon stream, writes all results into a .pto container with complete
provenance tracking, and can be reconstructed from the container.

MFD is one configuration — see :func:`mfd_config` for the standard
multi-parameter fluorescence detection setup.

    pipeline = BurstPipeline(mfd_config())
    pipeline.pack("out.pto", [tttr1, tttr2])

    # read back
    graph = ProvenanceGraph(pto)
    pipeline2 = BurstPipeline.from_provenance(graph)
"""

from __future__ import annotations

import json
import sys
from typing import Optional

import numpy as np
import tttrlib

from .operations import (
    Operation, COMPUTE_REGISTRY, compute_burst_search,
)
from .provenance import ProvenanceNode, settings_hash, ProvenanceGraph
from .pto_builder import PtoBuilder


def mfd_config() -> list[Operation]:
    """Standard MFD burst-analysis configuration.

    Returns the operation list for a two-colour MFD experiment:
    burst search, green/red IRF, green/red MLE, BVA, and KDE-CDE.
    """
    return [
        Operation(
            name="burst_search",
            operation_type="burst_selection",
            data_format="bur", row_grain="burst",
            settings={
                "threshold_khz": 30.0, "l_min": 30, "m_min": 5,
                "t_window_ms": 0.5, "routing_channels": [0, 1],
                "microtime_ranges": [[0, 4096]],
            },
        ),
        Operation(
            name="irf_green",
            operation_type="tcspc_calibration",
            data_format="irf", row_grain="curve_point", kind="irf_curve",
            settings={"channel": 0, "color": "green",
                      "source": "non_burst_photons", "baseline_quantile": 0.2},
        ),
        Operation(
            name="irf_red",
            operation_type="tcspc_calibration",
            data_format="irf", row_grain="curve_point", kind="irf_curve",
            settings={"channel": 1, "color": "red",
                      "source": "non_burst_photons", "baseline_quantile": 0.2},
        ),
        Operation(
            name="mle_green",
            operation_type="mle_green",
            data_format="bg4", row_grain="burst",
            companion_of="burst_selection",
            calibrated_by="tcspc_calibration",
            settings={"model": "fit23", "n_bins": 4096, "period_ns": 32.0,
                      "g_factor": 1.0, "l1": 0.0, "l2": 0.0,
                      "channel": 0, "color": "green",
                      "tau_init_ns": 3.8, "gamma": 1.0, "r0": 0.38, "rho": 1.2},
        ),
        Operation(
            name="mle_red",
            operation_type="mle_red",
            data_format="br4", row_grain="burst",
            companion_of="burst_selection",
            calibrated_by="tcspc_calibration",
            settings={"model": "fit23", "n_bins": 4096, "period_ns": 32.0,
                      "g_factor": 1.0, "l1": 0.0, "l2": 0.0,
                      "channel": 1, "color": "red",
                      "tau_init_ns": 1.6, "gamma": 1.0, "r0": 0.38, "rho": 1.2},
        ),
        Operation(
            name="bva",
            operation_type="bva",
            data_format="bv4", row_grain="burst",
            companion_of="burst_selection",
            settings={"win_size": 5, "n_subbursts": 10},
        ),
        Operation(
            name="kde_cde",
            operation_type="kde_cde",
            data_format="2c4", row_grain="burst",
            companion_of="burst_selection",
            settings={"kernel": "gaussian", "bandwidth": 0.05},
        ),
    ]


class BurstPipeline:
    """Generic burst-analysis pipeline.

    Parameters
    ----------
    operations : list of Operation
        The analysis stages to run, in dependency order. The first must be
        a ``burst_selection`` operation.
    """

    def __init__(self, operations: list[Operation]):
        self.operations = operations
        self._burst_op = self._find_burst_op()

    def _find_burst_op(self) -> Operation:
        for op in self.operations:
            if op.operation_type == "burst_selection":
                return op
        raise ValueError("Pipeline must contain a burst_selection operation")

    # ── analysis ────────────────────────────────────────────────────

    def analyze_one(self, tttr_data: tttrlib.TTTR) -> dict:
        """Run all operations on one TTTR.

        Returns a dict with keys:
            ``stores``: {op.name: DataStore}
            ``burst_indices``: (N, 2) array of [first_ph, last_ph]
        """
        burst_store, burst_indices = compute_burst_search(
            tttr_data, settings=self._burst_op.settings,
        )

        stores = {"burst_search": burst_store}
        irf_stores = {}

        for op in self.operations:
            if op.operation_type == "burst_selection":
                continue
            fn = COMPUTE_REGISTRY.get(op.operation_type)
            if fn is None:
                continue
            store = fn(
                tttr_data, burst_indices,
                settings=op.settings, irf_stores=irf_stores,
            )
            stores[op.name] = store
            if op.kind == "irf_curve":
                ch = op.settings.get("channel", 0)
                irf_stores[f"irf_ch{ch}"] = store

        return {"stores": stores, "burst_indices": burst_indices}

    # ── PTO packaging ───────────────────────────────────────────────

    def pack(self, output_path: str, tttr_files: list,
             title: str = "Burst Analysis Container") -> None:
        """Pack all TTTR files and their analysis into a .pto."""
        bhash = settings_hash(self._burst_op.settings)

        with PtoBuilder() as builder:
            builder.create(output_path, title)

            for idx, tttr_data in enumerate(tttr_files):
                stem = f"run_{idx}"
                result = self.analyze_one(tttr_data)

                stream_uid = builder.add_photon_stream(
                    tttr_data, f"data/{stem}.sm"
                )

                bur_name = f"burstwise_{bhash}/bi4_bur/{stem}.bur"
                burst_uid = builder.add_burst_table(
                    result["stores"]["burst_search"], bur_name,
                    ProvenanceNode(
                        data_format="bur", row_grain="burst",
                        operation_type="burst_selection",
                        settings=self._burst_op.settings,
                        source_uids=[stream_uid],
                        relationship="derived_from",
                    ),
                )

                irf_uids: dict[str, int] = {}

                for op in self.operations:
                    if op.operation_type == "burst_selection":
                        continue
                    store = result["stores"].get(op.name)
                    if store is None:
                        continue

                    container_name = self._container_name(op, stem, bhash)

                    source_uids = [burst_uid]
                    if op.kind == "irf_curve":
                        source_uids = [stream_uid]

                    node = ProvenanceNode(
                        data_format=op.data_format,
                        row_grain=op.row_grain,
                        operation_type=op.operation_type,
                        settings=op.settings,
                        source_uids=source_uids,
                        relationship=op.relationship,
                    )

                    if op.kind == "irf_curve":
                        uid = builder.add_irf_curve(store, container_name, node)
                        ch = op.settings.get("channel", 0)
                        irf_uids[f"irf_ch{ch}"] = uid
                    else:
                        uid = builder.add_burst_table(store, container_name, node)
                        if op.calibrated_by:
                            ch = op.settings.get("channel", 0)
                            irf_uid = irf_uids.get(f"irf_ch{ch}")
                            if irf_uid is not None:
                                builder.add_calibrated_by(uid, irf_uid)

    def _container_name(self, op: Operation, stem: str, bhash: str) -> str:
        fmt_dir = {
            "bur": "bi4_bur", "bg4": "bg4", "br4": "br4",
            "bv4": "bv4", "2c4": "2c4", "irf": "irf",
        }.get(op.data_format, op.data_format)
        if op.kind == "irf_curve":
            color = op.settings.get("color", "unk")
            return f"irf/{color}_{stem}.irf"
        return f"burstwise_{bhash}/{fmt_dir}/{stem}.{op.data_format}"

    # ── reconstruction ──────────────────────────────────────────────

    @classmethod
    def from_provenance(cls, graph: ProvenanceGraph) -> "BurstPipeline":
        """Reconstruct a pipeline from a provenance graph read from a .pto.

        Extracts operation types and settings from each artifact and rebuilds
        the operation list.
        """
        ops: list[Operation] = []
        for node in graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            op = Operation(
                name=node.name,
                operation_type=node.operation_type,
                data_format=node.data_format,
                row_grain=node.row_grain,
                settings=node.settings,
                kind=node.kind,
            )
            if node.relationship == "companion_of":
                op.companion_of = "burst_selection"
            elif node.relationship == "calibrated_by":
                op.calibrated_by = "tcspc_calibration"
            ops.append(op)
        return cls(ops)

    # ── CLI ─────────────────────────────────────────────────────────

    @classmethod
    def main(cls):
        config_path = sys.argv[1] if len(sys.argv) > 1 \
            else "examples/simulation/configs/alex.json"
        output = sys.argv[2] if len(sys.argv) > 2 else "burst_output.pto"
        n_runs = int(sys.argv[3]) if len(sys.argv) > 3 else 2

        with open(config_path) as f:
            config_text = f.read()
        files = []
        for i in range(n_runs):
            sim = tttrlib.SimEngine.from_json(config_text)
            sim.run()
            files.append(sim.to_tttr(0.01, 2))

        pipeline = cls(mfd_config())
        pipeline.pack(output, files)
        print(f"Written: {output} ({len(files)} files)")


if __name__ == "__main__":
    BurstPipeline.main()
