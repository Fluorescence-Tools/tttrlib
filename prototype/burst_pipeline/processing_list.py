"""Processing-list reader — extract and replay the pipeline from a .pto.

A .pto container written by the burst pipeline carries a provenance graph
(see :mod:`.provenance`). This module reads that graph and presents it as
an ordered **processing list**: step 1 was burst_selection with these settings,
step 2 was tcspc_calibration for green, and so on.

The user can then:

1. **List** — ``ProcessingList.from_pto(path)`` returns the ordered steps.
2. **Inspect** — each step shows its operation_type, settings, inputs, outputs.
3. **Replay** — re-run any step (or all steps) from the recovered settings,
   comparing results to the stored data.
4. **Jump** — skip to a specific step by name or index.

The operation definitions (inputs, outputs, column names) come from the
tttrlib registry's ``operation`` category, so the processing list is
self-describing: a consumer knows what each step consumes and produces
without hard-coding anything.
"""

from __future__ import annotations

import json
import pathlib
from dataclasses import dataclass, field
from typing import Optional

import numpy as np
import tttrlib

from .provenance import ProvenanceGraph, ProvenanceNode


def _load_operation_registry() -> dict:
    """Load the operation catalog from the tttrlib registry (Python binding)."""
    try:
        raw = tttrlib.registry_category_json("operation")
        return json.loads(raw)
    except Exception:
        return {}


@dataclass
class ProcessingStep:
    """One step in the processing list read from a .pto.

    Attributes
    ----------
    index : int
        Position in the processing order (0-based).
    uid : int
        PTO object UID.
    operation_type : str
        e.g. ``"burst_selection"``, ``"mle_green"``.
    name : str
        Container-internal object name.
    settings : dict
        The settings_json recovered from provenance.
    inputs : list of int
        Source UIDs this step consumed.
    outputs : dict
        Operation output spec from the registry (columns, data_format, …).
    spec : dict
        Full registry entry for this operation_type, or ``{}`` if unknown.
    """

    index: int
    uid: int
    operation_type: str
    name: str
    settings: dict = field(default_factory=dict)
    inputs: list[int] = field(default_factory=list)
    outputs: dict = field(default_factory=dict)
    spec: dict = field(default_factory=dict)

    def __repr__(self) -> str:
        return (
            f"ProcessingStep({self.index}: {self.operation_type} "
            f"uid={self.uid} name={self.name!r})"
        )

    def summary(self) -> str:
        """One-line human-readable summary."""
        cols = self.outputs.get("columns", [])
        n_cols = len(cols)
        return (
            f"[{self.index:2d}] {self.operation_type:20s} → "
            f"{self.name}  ({n_cols} cols, inputs: {self.inputs})"
        )


class ProcessingList:
    """Ordered processing steps read from a .pto container.

    Usage::

        plist = ProcessingList.from_pto("bursts.pto")
        for step in plist:
            print(step.summary())
        step3 = plist["mle_green"]
        data = plist.read_step_data(step3)
    """

    def __init__(self, steps: list[ProcessingStep],
                 graph: ProvenanceGraph,
                 pto: tttrlib.PtoFile):
        self.steps = steps
        self.graph = graph
        self._pto = pto

    @classmethod
    def from_pto(cls, path: str) -> "ProcessingList":
        """Read a .pto and build the processing list from provenance."""
        reader = tttrlib.PtoFile()
        if not reader.open(path):
            raise RuntimeError(f"Failed to open {path}")
        graph = ProvenanceGraph(reader)
        registry = _load_operation_registry()

        steps: list[ProcessingStep] = []
        idx = 0
        for node in graph.nodes.values():
            if node.kind == "tttr_photon_stream":
                continue
            spec = registry.get(node.operation_type, {})
            outputs = spec.get("outputs", {})
            step = ProcessingStep(
                index=idx,
                uid=node.uid,
                operation_type=node.operation_type,
                name=node.name,
                settings=node.settings,
                inputs=node.source_uids,
                outputs=outputs,
                spec=spec,
            )
            steps.append(step)
            idx += 1

        return cls(steps, graph, reader)

    def __iter__(self):
        return iter(self.steps)

    def __len__(self):
        return len(self.steps)

    def __getitem__(self, key) -> ProcessingStep:
        """Access by index (int) or by operation_type (str).

        If multiple steps have the same operation_type, returns the first.
        """
        if isinstance(key, int):
            return self.steps[key]
        if isinstance(key, str):
            for s in self.steps:
                if s.operation_type == key:
                    return s
            raise KeyError(f"No step with operation_type {key!r}")
        raise TypeError(f"Key must be int or str, not {type(key)}")

    def operation_types(self) -> list[str]:
        """List all operation types in processing order."""
        return [s.operation_type for s in self.steps]

    def read_step_data(self, step: ProcessingStep) -> dict[str, np.ndarray]:
        """Read the column data for one step's output artifact."""
        cols = tttrlib.pto_store_columns(self._pto, step.uid)
        store = tttrlib.pto_store(self._pto, step.uid, list(cols))
        return {c: np.asarray(store[c]) for c in cols}

    def replay_step(self, step: ProcessingStep,
                    tttr_data: tttrlib.TTTR,
                    burst_indices: Optional[np.ndarray] = None) -> dict[str, np.ndarray]:
        """Re-run one step's computation and return the output columns.

        Parameters
        ----------
        step : ProcessingStep
            The step to replay.
        tttr_data : TTTR
            The photon stream (must be the same one stored in the .pto).
        burst_indices : array, optional
            Burst [first, last] pairs, required for companion steps.
        """
        from .operations import COMPUTE_REGISTRY

        fn = COMPUTE_REGISTRY.get(step.operation_type)
        if fn is None:
            raise ValueError(
                f"Cannot replay {step.operation_type}: "
                f"not in COMPUTE_REGISTRY"
            )

        irf_stores = {}
        if step.operation_type in ("mle_green", "mle_red"):
            for s in self.steps:
                if s.operation_type == "tcspc_calibration":
                    data = self.read_step_data(s)
                    ch = s.settings.get("channel", 0)
                    store = tttrlib.DataStore(f"irf_ch{ch}")
                    store.set_n_rows(len(data["x_ns"]))
                    store.add("x_ns", data["x_ns"])
                    store.add("counts", data["counts"])
                    store.add("counts_raw", data["counts_raw"])
                    irf_stores[f"irf_ch{ch}"] = store

        if step.operation_type == "burst_selection":
            store, burst_indices = fn(
                tttr_data, settings=step.settings,
            )
        else:
            if burst_indices is None:
                burst_indices = self._read_burst_indices()
            store = fn(
                tttr_data, burst_indices,
                settings=step.settings, irf_stores=irf_stores,
            )

        result = {}
        for i in range(store.n_columns()):
            col = store.column(i)
            result[col.name()] = np.asarray(
                np.frombuffer(
                    col.data_ptr()[:col.size() * col.element_size()],
                    dtype=_col_dtype(col)
                ).copy()
            ) if hasattr(col, 'element_size') else np.array([])
        return result

    def _read_burst_indices(self) -> np.ndarray:
        """Read burst first/last photon indices from the .bur artifact."""
        for step in self.steps:
            if step.operation_type == "burst_selection":
                data = self.read_step_data(step)
                fp = data.get("First Photon")
                lp = data.get("Last Photon")
                if fp is not None and lp is not None:
                    return np.column_stack([fp.astype(np.int64),
                                            lp.astype(np.int64)])
        return np.zeros((0, 2), dtype=np.int64)

    def print_summary(self) -> None:
        """Print the full processing list for human inspection."""
        print(f"Processing list ({len(self.steps)} steps):")
        print(f"{'idx':>4s}  {'operation_type':22s}  "
              f"{'data_format':8s}  {'name':40s}  inputs")
        print("-" * 100)
        for s in self.steps:
            fmt = s.spec.get("data_format", "?")
            print(f"{s.index:4d}  {s.operation_type:22s}  "
                  f"{fmt:8s}  {s.name:40s}  {s.inputs}")

    def close(self) -> None:
        if self._pto is not None:
            self._pto.close()
            self._pto = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
        return False
