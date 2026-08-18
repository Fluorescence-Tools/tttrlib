# SPDX-License-Identifier: BSD-3-Clause
"""A data-processing pipeline as a document: compose, save, reload, replay.

A pipeline is a list of steps, each naming a registry entry plus its
parameters. Because the name and the parameters are *data*, the pipeline is a
document -- JSON, a ``.pto`` tag, or a Python object -- and the same document
runs on another machine, another tttrlib version, or in another tool.

The document is deliberately the **mmfdb workflow schema** (``version: 1``:
``sources`` + ``steps`` with ``id`` / ``operation_type`` / ``software`` /
``inputs`` / ``params`` / ``python`` / ``outputs``), so a tttrlib pipeline is a
valid mmfdb workflow and an mmfdb workflow's tttrlib steps load here --
``Pipeline.to_mmfdb()`` / ``Pipeline.from_mmfdb()`` are the same dict with the
tttrlib-specific keys added or dropped. `../mmfdb/src/mmfdb/workflow/spec.py`
is the reference for the schema, and mmfdb's controlled vocabulary supplies
``operation_type``.

Reproducibility is what the extra keys are for: every document records the
format and its version, the tttrlib version that wrote it, and per step the
software package and version -- so a file can say "this was produced by
tttrlib 0.27.0" and a reader can refuse, warn, or adapt.
"""

import copy
import json as _json


#: The document format this module reads and writes.
PIPELINE_FORMAT = "tttrlib.pipeline"
#: Bumped only when a document written by an older tttrlib stops loading.
PIPELINE_FORMAT_VERSION = 1
#: The mmfdb workflow schema version the document conforms to.
MMFDB_WORKFLOW_VERSION = 1


def _tttrlib_version():
    import tttrlib
    return getattr(tttrlib, "__version__", "") or ""


class Pipeline:
    """A sequence of registered operations, as a document.

    Steps are added with :meth:`then` (or the ``|`` operator), the document is
    read and written with :meth:`to_dict` / :meth:`to_json` / :meth:`save` /
    :meth:`to_pto` and their ``from_``/``load`` counterparts, and it is
    executed with :meth:`run`.

        >>> import tttrlib
        >>> p = (tttrlib.Pipeline("segmentation", description="regions and outlines")
        ...      .then("region_segmentation")
        ...      .then("iso_contours", level=1.5, vertex_connect_high=False))
        >>> p.steps[1]["operation"]
        'iso_contours'
        >>> tttrlib.Pipeline.from_json(p.to_json()) == p
        True

    Args:
        name: identifier of the pipeline, carried into the document.
        description: free text, for the person who reads the file later.
        sources: optional ``{name: {"path": ..., "kind": ...}}`` describing the
            raw inputs, in the mmfdb ``sources`` shape.
    """

    def __init__(self, name="pipeline", description="", sources=None, steps=None):
        self.name = name
        self.description = description
        self.sources = dict(sources or {})
        self.steps = [dict(s) for s in (steps or [])]

    # ------------------------------------------------------------ compose --

    def then(self, operation, _id=None, _inputs=None, _outputs=None, **params):
        """Append a step and return a NEW pipeline (pipelines are values).

        Args:
            operation: a registered name (:func:`describe` must find it).
            _id: step id; defaults to the operation name, made unique.
            _inputs: ``{local_name: ref}`` where a ref is a source name or
                ``"step_id.output"`` -- the mmfdb wiring. Defaults to the
                previous step's output, which is the common case.
            _outputs: ``{name: {"kind": ..., "path": ...}}``.
            **params: the operation's parameters.
        """
        import tttrlib
        tttrlib.describe(operation)          # raises here, not at run time
        step_id = _id or self._unique_id(operation)
        if _inputs is None:
            _inputs = {"input": f"{self.steps[-1]['id']}.output"} if self.steps else {}
        step = {"id": step_id, "operation": operation, "params": dict(params),
                "inputs": dict(_inputs), "outputs": dict(_outputs or {})}
        return Pipeline(self.name, self.description, self.sources, self.steps + [step])

    def __or__(self, other):
        """``p | "iso_contours"`` or ``p | ("iso_contours", {"level": 1.5})``."""
        if isinstance(other, str):
            return self.then(other)
        if isinstance(other, Pipeline):
            return Pipeline(self.name, self.description,
                            {**self.sources, **other.sources}, self.steps + other.steps)
        name, params = other
        return self.then(name, **(params or {}))

    def _unique_id(self, base):
        taken = {s["id"] for s in self.steps}
        if base not in taken:
            return base
        i = 2
        while f"{base}_{i}" in taken:
            i += 1
        return f"{base}_{i}"

    def __eq__(self, other):
        return isinstance(other, Pipeline) and self.to_dict(stamp=False) == other.to_dict(stamp=False)

    def __repr__(self):
        return (f"Pipeline({self.name!r}, steps=["
                + ", ".join(repr(s["operation"]) for s in self.steps) + "])")

    def __len__(self):
        return len(self.steps)

    # ------------------------------------------------------------ document --

    def to_dict(self, stamp=True):
        """The pipeline as a plain dict -- the document.

        Args:
            stamp: record the writing tttrlib version. ``False`` gives the
                version-free document two runs can be compared with.
        """
        doc = {
            "format": PIPELINE_FORMAT,
            "format_version": PIPELINE_FORMAT_VERSION,
            "version": MMFDB_WORKFLOW_VERSION,      # the mmfdb workflow schema
            "name": self.name,
            "description": self.description,
            "sources": copy.deepcopy(self.sources),
            "steps": [],
        }
        if stamp:
            doc["software"] = {"package": "tttrlib", "version": _tttrlib_version()}
        for step in self.steps:
            entry = _describe_or_none(step["operation"])
            out = {
                "id": step["id"],
                "operation": step["operation"],
                # mmfdb's controlled vocabulary term for what the step does.
                "operation_type": (entry or {}).get("operation_type", step["operation"]),
                "params": copy.deepcopy(step.get("params", {})),
                "inputs": copy.deepcopy(step.get("inputs", {})),
                "outputs": copy.deepcopy(step.get("outputs", {})),
                # run_kind "python" with a module:callable target -- how mmfdb
                # runs a step it did not write.
                "python": "tttrlib.pipeline:run_step",
            }
            if stamp:
                out["software"] = {"package": "tttrlib", "version": _tttrlib_version()}
            doc["steps"].append(out)
        return doc

    @classmethod
    def from_dict(cls, doc, strict=True):
        """Rebuild a pipeline from a document.

        Args:
            strict: refuse a document of an unknown format or a newer format
                version, and refuse a step whose operation is not registered
                in *this* build. ``False`` warns instead, which is what a
                reader wants when it only means to display the pipeline.
        """
        import warnings
        fmt = doc.get("format", PIPELINE_FORMAT)
        if fmt != PIPELINE_FORMAT:
            message = f"not a {PIPELINE_FORMAT} document (format={fmt!r})"
            if strict:
                raise ValueError(message)
            warnings.warn(message)
        version = int(doc.get("format_version", PIPELINE_FORMAT_VERSION))
        if version > PIPELINE_FORMAT_VERSION:
            message = (f"pipeline format version {version} is newer than this "
                       f"tttrlib understands ({PIPELINE_FORMAT_VERSION}); "
                       f"written by tttrlib "
                       f"{doc.get('software', {}).get('version', 'unknown')}")
            if strict:
                raise ValueError(message)
            warnings.warn(message)
        steps = []
        for step in doc.get("steps", []):
            operation = step.get("operation") or step.get("operation_type")
            if operation is None:
                raise ValueError(f"step {step.get('id')!r} names no operation")
            entry = _describe_or_none(operation)
            if entry is None:
                message = (f"step {step.get('id', operation)!r} names {operation!r}, "
                           f"which is not registered in this build")
                if strict:
                    raise ValueError(message)
                warnings.warn(message)
            steps.append({"id": step.get("id", operation), "operation": operation,
                          "params": dict(step.get("params", {})),
                          "inputs": dict(step.get("inputs", {})),
                          "outputs": dict(step.get("outputs", {}))})
        return cls(doc.get("name", "pipeline"), doc.get("description", ""),
                   doc.get("sources", {}), steps)

    def to_json(self, indent=2, stamp=True):
        """The document as a JSON string."""
        return _json.dumps(self.to_dict(stamp=stamp), indent=indent)

    @classmethod
    def from_json(cls, text, strict=True):
        """Read a pipeline from a JSON string."""
        return cls.from_dict(_json.loads(text), strict=strict)

    def save(self, path, indent=2):
        """Write the document to a ``.json`` file."""
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(self.to_json(indent=indent))
        return path

    @classmethod
    def load(cls, path, strict=True):
        """Read a pipeline from a ``.json`` file, or from a ``.pto`` container."""
        text = str(path)
        if text.endswith(".pto"):
            return cls.from_pto(text, strict=strict)
        with open(path, encoding="utf-8") as handle:
            return cls.from_json(handle.read(), strict=strict)

    # ----------------------------------------------------------------- pto --

    #: The tag a ``.pto`` carries the pipeline document under.
    PTO_TAG = "_mmfdb_workflow.definition"
    #: Companion tags, so a reader sees the identity without parsing the JSON.
    PTO_NAME_TAG = "_mmfdb_workflow.name"
    PTO_VERSION_TAG = "_mmfdb_workflow.version"

    def to_pto(self, path, target=0, title=None):
        """Store the document in a ``.pto`` container, as mmfdb names it.

        The document goes into ``_mmfdb_workflow.definition`` -- the item
        mmfdb defines for "the verbatim workflow definition, so the pipeline
        can be re-run" -- next to ``_mmfdb_workflow.name`` and
        ``.version``. An existing file is extended; a missing one is created.

        Args:
            path: the ``.pto`` file.
            target: uid of the object the workflow produced, or 0 for the
                file itself.
        """
        import os
        import tttrlib
        f = tttrlib.PtoFile()
        exists = os.path.exists(path)
        ok = f.open(path, True) if exists else f.create(path, title or self.name)
        if not ok:
            raise IOError(f"cannot write {path}: {f.error()}")
        if not exists:
            f.set_writing_app(f"tttrlib {_tttrlib_version()}")
        for name, text in ((self.PTO_TAG, self.to_json(indent=None)),
                           (self.PTO_NAME_TAG, self.name),
                           (self.PTO_VERSION_TAG, str(PIPELINE_FORMAT_VERSION))):
            tag = tttrlib.PtoTag()
            tag.name = name
            tag.type = tttrlib.PtoType_Text
            tag.target = target
            tag.text = text
            f.set_tag(tag)
        if not f.commit():
            raise IOError(f"cannot commit {path}: {f.error()}")
        f.close()
        return path

    @classmethod
    def from_pto(cls, path, strict=True):
        """Read the pipeline a ``.pto`` container carries."""
        import tttrlib
        f = tttrlib.PtoFile()
        if not f.open(path):
            raise IOError(f"cannot read {path}: {f.error()}")
        try:
            for tag in f.tags():
                if tag.name == cls.PTO_TAG:
                    return cls.from_json(tag.text, strict=strict)
        finally:
            f.close()
        raise ValueError(f"{path} carries no {cls.PTO_TAG}")

    # --------------------------------------------------------------- mmfdb --

    def to_mmfdb(self, sources=None):
        """The pipeline as an **mmfdb workflow** document (schema version 1).

        The same steps in mmfdb's own key names, ready for
        ``mmfdb workflow run`` or to be written into a deposit CIF:
        ``operation_type`` from the registry, ``software`` naming tttrlib and
        its version, ``python`` as the run target, and the ``sources`` /
        ``inputs`` wiring. See ``../mmfdb/src/mmfdb/workflow/spec.py``.

        Args:
            sources: ``{name: {"path": ...}}`` to write into the document; the
                pipeline's own sources are used when omitted.
        """
        doc = self.to_dict()
        out = {
            "version": MMFDB_WORKFLOW_VERSION,
            "name": self.name,
            "description": self.description or f"tttrlib pipeline {self.name}",
            "sources": copy.deepcopy(sources if sources is not None else self.sources),
            "steps": [],
        }
        for step in doc["steps"]:
            out["steps"].append({
                "id": step["id"],
                "operation_type": step["operation_type"],
                "software": step.get("software", {"package": "tttrlib",
                                                  "version": _tttrlib_version()}),
                "inputs": step["inputs"],
                # The tttrlib operation name travels in params, because mmfdb's
                # step key is the *operation type* and two tttrlib operations
                # can share one (mle_green and mle_red are both
                # burst_lifetime_fitting).
                "params": {**step["params"], "tttrlib_operation": step["operation"]},
                "python": step["python"],
                "outputs": step["outputs"],
            })
        return out

    @classmethod
    def from_mmfdb(cls, doc, strict=True):
        """Read an mmfdb workflow document, keeping the steps tttrlib can run.

        A step is tttrlib's when its ``software.package`` is ``tttrlib`` or it
        carries a ``tttrlib_operation`` parameter; other steps (a subprocess,
        another package) are skipped, since only their owner can run them.
        """
        steps = []
        for step in doc.get("steps", []):
            params = dict(step.get("params", {}))
            operation = params.pop("tttrlib_operation", None)
            package = (step.get("software") or {}).get("package", "")
            if operation is None:
                if package != "tttrlib":
                    continue
                operation = step.get("operation_type")
            steps.append({"id": step.get("id", operation), "operation": operation,
                          "params": params,
                          "inputs": dict(step.get("inputs", {})),
                          "outputs": dict(step.get("outputs", {}))})
        return cls.from_dict({"format": PIPELINE_FORMAT,
                              "format_version": PIPELINE_FORMAT_VERSION,
                              "name": doc.get("name", "pipeline"),
                              "description": doc.get("description", ""),
                              "sources": doc.get("sources", {}),
                              "steps": steps}, strict=strict)

    # ------------------------------------------------------------- execute --

    def run(self, value=None, adapters=None):
        """Execute the steps in order, threading the value through.

        Args:
            value: the input of the first step.
            adapters: ``{step_id: lambda previous: (args, kwargs)}`` for the
                steps whose input is not simply the previous output. A step
                without an adapter is called with the previous value as its
                first argument and its ``params`` as keyword arguments.

        Returns:
            The last step's result.
        """
        import tttrlib
        adapters = dict(adapters or {})
        composed = tttrlib.compose(*[
            (s["operation"],
             # `tttrlib_operation` is how the mmfdb form carries the operation
             # name inside `params`; it is a marker, never an argument.
             {k: v for k, v in s.get("params", {}).items() if k != "tttrlib_operation"},
             adapters.get(s["id"]))
            for s in self.steps])
        try:
            return composed(value)
        except (TypeError, ValueError) as error:
            # A step whose input is not simply the previous output needs an
            # adapter, and saying which one is the difference between a
            # fixable message and a traceback out of a SWIG proxy.
            raise type(error)(
                f"{error}\n\nA step of {self.name!r} could not be called with the "
                f"previous value alone. Give it an adapter: "
                f"run(value, adapters={{'<step id>': lambda previous: ((args...), {{}})}}). "
                f"Steps: {[s['id'] for s in self.steps]}") from error

    def describe(self):
        """One line per step: what it is, and what implements it."""
        import tttrlib
        lines = []
        for step in self.steps:
            entry = tttrlib.describe(step["operation"])
            params = ", ".join(f"{k}={v!r}" for k, v in step.get("params", {}).items())
            lines.append(f"{step['id']}: {entry['label']}"
                         + (f" ({params})" if params else ""))
        return "\n".join(lines)


def _describe_or_none(name):
    import tttrlib
    try:
        return tttrlib.describe(name)
    except ValueError:
        return None


def run_step(ctx):
    """Run one tttrlib step of an mmfdb workflow.

    This is the ``python`` target every step of :meth:`Pipeline.to_mmfdb`
    names (``tttrlib.pipeline:run_step``), so mmfdb's runner can execute a
    tttrlib step without importing anything else. ``ctx`` carries the step's
    ``params`` (including ``tttrlib_operation``) and its resolved ``inputs``.
    """
    import tttrlib
    params = dict(getattr(ctx, "params", None) or ctx["params"])
    operation = params.pop("tttrlib_operation")   # the mmfdb marker, not a parameter
    inputs = getattr(ctx, "inputs", None) or ctx.get("inputs", {})
    value = next(iter(inputs.values())) if inputs else None
    entry = tttrlib.describe(operation)
    leading = [params.pop(key) for key in (entry.get("positional") or []) if key in params]
    target = tttrlib.resolve(operation)
    if isinstance(target, tuple):
        cls, attr = target
        return getattr(value, attr)(*leading, **params)
    if value is not None:
        return target(value, *leading, **params)
    return target(*leading, **params)
