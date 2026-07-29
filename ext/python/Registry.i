// SPDX-License-Identifier: BSD-3-Clause
%{
#include "Registry.h"
%}

%include "Registry.h"

#ifdef SWIGPYTHON
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
