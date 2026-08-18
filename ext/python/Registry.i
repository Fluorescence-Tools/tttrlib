// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Registry.h"
%}

%include "Registry.h"

#ifdef SWIGPYTHON
%pythoncode "./ext/python/pipeline_support.py"

%pythoncode %{

def registry(category=None):
    """Machine-readable description of what tttrlib can do, as a dict.

    This answers the questions the compiled API cannot: which burst searches
    exist, which file containers can be read, what parameters each takes and what
    their defaults, units and ranges are. Callers previously hard-coded those
    lists and drifted whenever tttrlib gained a feature.

    Parameter descriptions are standard JSON Schema, so anything that can already
    render a JSON Schema can offer a tttrlib feature without tttrlib-specific
    code, and picks up new entries on upgrade.

    This covers the parts of the library that carry such semantics. For a
    complete list of *every* class and method, see :func:`api_index`.

    Args:
        category: return only this category (e.g. ``"burst_search"``,
            ``"file_container"``). ``None`` returns every category.

    Returns:
        dict: ``{category: {name: entry}}``, or ``{name: entry}`` for one category.

    Raises:
        ValueError: unknown category, naming the available ones.

    Example:
        >>> import tttrlib
        >>> sorted(tttrlib.registry())
        ['burst_search', 'file_container']
        >>> sorted(tttrlib.registry("burst_search"))       # doctest: +ELLIPSIS
        [...'maxtree'...]
    """
    import json as _json
    # The category list is taken from the parsed JSON rather than from
    # registry_categories(), whose std::vector<std::string> return has no Python
    # typemap; the JSON is the same source of truth either way.
    everything = _json.loads(registry_json())
    if category is None:
        return everything
    if category not in everything:
        raise ValueError(
            f"unknown registry category {category!r}; "
            f"available: {sorted(everything)}"
        )
    return everything[category]


def describe(name):
    """One registry entry by name, whatever category it is in.

        >>> tttrlib.describe("phasor")["label"]
        'Phasor analysis of decays and FLIM images'

    Raises:
        ValueError: naming the closest matches, if the name is not registered.
    """
    everything = registry()
    hits = [(c, e[name]) for c, e in everything.items() if name in e]
    for category, entry in hits:
        # The declaring category, not the `operation` view: a replayable entry
        # appears in both, and its own capability is the informative one.
        if entry.get("capability", category) == category:
            entry = dict(entry)
            entry["category"] = category
            return entry
    if hits:
        category, entry = hits[0]
        entry = dict(entry)
        entry["category"] = category
        return entry
    import difflib as _difflib
    known = sorted({k for entries in everything.values() for k in entries})
    close = _difflib.get_close_matches(name, known, n=5)
    raise ValueError(f"{name!r} is not registered"
                     + (f"; did you mean: {', '.join(close)}?" if close else ""))


def resolve(name):
    """The callable a registry entry names, ready to call.

    An entry says how it is reached: ``method`` for a method of an object
    (``"get_mean_lifetime"``), otherwise the first ``api`` symbol. Reading the
    registry and calling what it names is what makes a pipeline *composable* --
    a step is a name plus parameters, so it can come from a ``.pto`` file, a
    UI, or a config, and tttrlib does not need to know it in advance.

        >>> fn = tttrlib.resolve("watershed")
        >>> callable(fn)
        True

    Returns:
        For a free function, the function. For a method, a ``(class, method
        name)`` pair -- the caller supplies the instance, since only it knows
        which object the step applies to. For an entry whose interface *is* a
        class (``BVA``, ``Correlator``, ``HMM``), the class: constructing it is
        how that step is run.
    """
    import inspect as _inspect
    import tttrlib as _t
    entry = describe(name)
    api = entry.get("api") or []
    method = entry.get("method")
    # 1. "Class.method" in api -- the most explicit form
    for symbol in api:
        if "." in symbol:
            cls_name, attr = symbol.split(".", 1)
            if method and attr != method:
                continue
            cls = getattr(_t, cls_name, None)
            if cls is not None and hasattr(cls, attr):
                return cls, attr
    # 2. a `method` on the first api class that has it
    if method:
        for symbol in api:
            cls = getattr(_t, symbol.split(".")[0], None)
            if _inspect.isclass(cls) and callable(getattr(cls, method, None)):
                return cls, method
        obj = getattr(_t, method, None)
        if callable(obj) and not _inspect.isclass(obj):
            return obj
    # 3. the first api symbol that is a free function
    for symbol in api:
        if "." in symbol:
            continue
        obj = getattr(_t, symbol, None)
        if callable(obj) and not _inspect.isclass(obj):
            return obj
    # 4. the entry's interface is a class: constructing it runs the step
    for symbol in api:
        cls = getattr(_t, symbol.split(".")[0], None)
        if _inspect.isclass(cls):
            return cls
    raise ValueError(f"registry entry {name!r} names no callable "
                     f"(api={api!r}, method={method!r})")


def defaults(name):
    """The parameter defaults of a registry entry, as a dict.

        >>> tttrlib.defaults("photon_reassignment")["method"]
        'esrrf'
    """
    schema = describe(name).get("params_schema") or {}
    out = {}
    for key, spec in (schema.get("properties") or {}).items():
        if isinstance(spec, dict) and "default" in spec:
            out[key] = spec["default"]
    return out


def compose(*steps):
    """Chain registry entries into one callable pipeline.

    Each step is ``name`` or ``(name, parameters_dict)`` or ``(name, params,
    adapter)``. The pipeline calls each step's resolved callable in turn,
    passing the previous result as the first argument and the step's parameters
    as keyword arguments, and returns the last result. Parameters are the ones
    given here; :func:`defaults` supplies an entry's schema defaults when a
    caller wants them (they are not applied silently, because an entry whose
    schema covers a family of calls would otherwise pass a keyword the chosen
    one does not take). An ``adapter`` -- ``lambda previous: (args, kwargs)`` -- is how a
    step whose input is not simply the previous output is wired.

    This is composition *through the registry*: nothing here knows what a
    watershed or a burst search is, only that the registry names one.

        >>> import numpy as np
        >>> pipeline = tttrlib.compose(("histogram", {"bins": 8}))
        >>> callable(pipeline)
        True

    Returns:
        callable: takes the pipeline's input, returns the last step's output.
    """
    prepared = []
    for step in steps:
        if isinstance(step, str):
            name, params, adapter = step, {}, None
        elif len(step) == 2:
            (name, params), adapter = step, None
        else:
            name, params, adapter = step
        entry = describe(name)               # raises if the name is not registered
        prepared.append((name, entry, dict(params or {}), adapter))

    def run(value=None):
        for name, entry, params, adapter in prepared:
            target = resolve(name)
            if adapter is not None:
                args, kwargs = adapter(value)
                kwargs = dict(kwargs or {})
            else:
                args, kwargs = ((value,) if value is not None else ()), {}
            if isinstance(target, tuple):        # (class, method): value is the instance
                cls, attr = target
                if args and isinstance(args[0], cls):
                    instance, args = args[0], args[1:]
                else:
                    raise TypeError(
                        f"step {name!r} is a method of {cls.__name__}; the pipeline's "
                        f"value must be a {cls.__name__} instance (got {type(args[0]).__name__ if args else 'nothing'})")
                value = getattr(instance, attr)(*args, **{**params, **kwargs})
            else:
                value = target(*args, **{**params, **kwargs})
        return value

    run.steps = [name for name, _, _, _ in prepared]
    return run


def _api_describe_parameter(parameter):
    """One argument: name, kind, and default when there is one.

    Defaults are kept only when they survive a JSON round trip. A SWIG proxy
    object as a default is meaningful in Python but cannot be transported to
    another language, so it is reported as a repr rather than coerced into
    something a scripting language would misread as a value.
    """
    import inspect as _inspect, json as _json
    entry = {"name": parameter.name, "kind": parameter.kind.name}
    if parameter.default is not _inspect.Parameter.empty:
        try:
            # Round-trip rather than just dump: this normalises Python-only
            # shapes (a tuple default becomes a list) so the emitted index is
            # already what another language will receive, and re-reading the
            # JSON reproduces it exactly.
            entry["default"] = _json.loads(_json.dumps(parameter.default))
        except (TypeError, ValueError):
            entry["default_repr"] = repr(parameter.default)
    return entry


def _api_describe_callable(obj):
    """Signature, parameters and docstring of one function or method."""
    import inspect as _inspect
    entry = {}
    doc = _inspect.getdoc(obj)
    if doc:
        entry["doc"] = doc
    try:
        signature = _inspect.signature(obj)
    except (TypeError, ValueError):
        # Some SWIG builtins expose no introspectable signature; the docstring
        # SWIG writes still carries one, so the entry stays useful.
        return entry
    entry["signature"] = str(signature)
    entry["parameters"] = [
        _api_describe_parameter(p) for name, p in signature.parameters.items()
        if name != "self"
    ]
    return entry


def _api_describe_class(cls):
    """Methods, properties, static methods and attributes of one class."""
    import inspect as _inspect
    entry = {"methods": {}, "properties": [], "static_methods": []}
    doc = _inspect.getdoc(cls)
    if doc and doc.strip():
        entry["doc"] = doc
    for name, member in sorted(vars(cls).items()):
        if name.startswith("_"):
            continue
        if isinstance(member, property):
            prop = {"name": name}
            if member.__doc__:
                prop["doc"] = _inspect.cleandoc(member.__doc__)
            entry["properties"].append(prop)
        elif isinstance(member, staticmethod):
            entry["static_methods"].append(name)
            entry["methods"][name] = _api_describe_callable(member.__func__)
        elif _inspect.isroutine(member):
            entry["methods"][name] = _api_describe_callable(member)
    # SWIG puts instance attributes behind property-like descriptors that are not
    # `property` instances, so anything public left over is reported rather than
    # silently dropped.
    described = set(entry["methods"]) | {p["name"] for p in entry["properties"]}
    entry["attributes"] = sorted(
        name for name in dir(cls)
        if not name.startswith("_") and name not in described
        and not _inspect.isroutine(getattr(cls, name, None))
    )
    return entry


def api_index():
    """A complete index of every tttrlib class, method and function, as a dict.

    Where :func:`registry` describes the curated subset that carries semantics —
    what an algorithm is, which parameters are meaningful, their units and ranges
    — this describes *everything*: around 110 classes and 2000 methods, with
    signatures, defaults and docstrings. A scripting language can use it to
    discover and call any part of the library without parsing C++ headers.

    It is derived from the built module rather than authored, so it cannot drift
    out of step with the code — which is the only way coverage this wide stays
    correct. ``tools/generate_api_index.py`` is a thin CLI that dumps it to a
    JSON file.

    Only objects tttrlib itself defines are indexed: a wrapper module re-exports
    whatever it imported, and advertising those would describe an API tttrlib
    does not own.

    Returns:
        dict: ``{"classes": {...}, "functions": {...}, "n_classes": int,
        "n_methods": int, "n_functions": int}``

    Example:
        >>> import tttrlib
        >>> index = tttrlib.api_index()
        >>> index["n_classes"] > 50
        True
        >>> "burst_search_maxtree" in index["classes"]["TTTR"]["methods"]
        True
    """
    import inspect as _inspect
    import tttrlib as _module

    index = {
        "tttrlib_version": getattr(_module, "__version__", None),
        "classes": {},
        "functions": {},
    }
    for name, obj in sorted(vars(_module).items()):
        if name.startswith("_"):
            continue
        origin = getattr(obj, "__module__", None)
        if origin is not None and not origin.startswith(_module.__name__):
            continue
        if _inspect.isclass(obj):
            index["classes"][name] = _api_describe_class(obj)
        elif _inspect.isroutine(obj):
            index["functions"][name] = _api_describe_callable(obj)

    index["n_classes"] = len(index["classes"])
    index["n_functions"] = len(index["functions"])
    index["n_methods"] = sum(len(c["methods"]) for c in index["classes"].values())
    return index
%}
#endif
