# SPDX-License-Identifier: BSD-3-Clause
"""Python conveniences over the decay-fit interface.

The C++ side takes flat ``double`` arrays: parameters, setup and results. That
is deliberate — it keeps the hot path free of string handling and gives every
language binding the same wire — but a flat array is only usable if something
turns names into slots. That something is the registry, and this module is the
thin layer that reads it.

So there is no hand-written table here mapping models to their parameters. Ask
for a setup block and you get one built from the model's schema with its
documented defaults; hand back a result vector and you get a dict keyed by the
column names the schema declares. A model added to the registry is usable from
Python the moment it is registered, with no change to this file.
"""
import json as _json


def fit_names():
    """Names of every registered decay fit.

    Returns:
        list[str]: registry keys accepted by :class:`DecayFit2`.

    Example:
        >>> import tttrlib
        >>> "fit23" in tttrlib.fit_names()   # doctest: +SKIP
        True
    """
    return list(decay_fit_names())


def _fit_entry(name):
    """Registry entry for ``name``, or a ValueError naming the alternatives."""
    entries = _json.loads(registry_category_json("fit"))
    if name not in entries:
        raise ValueError(
            f"unknown fit {name!r}; available: {sorted(entries)}"
        )
    return entries[name]


def _schema_slots(properties, counts):
    """Expand a schema's properties into flat slots, per the flattening rule.

    Properties in declaration order; a scalar takes one slot; an array takes
    ``count`` slots named ``prop[i]``; a string with an ``enum`` takes one slot
    holding the index of its value.
    """
    slots = []
    for prop_name, prop in properties.items():
        if prop.get("type") == "array":
            n = int(counts.get(prop["count_from"], 0))
            slots.extend((f"{prop_name}[{i}]", prop["items"]) for i in range(n))
        else:
            slots.append((prop_name, prop))
    return slots


def setup_vector(name, **values):
    """Build a model's flat setup vector from its registry schema.

    Every slot takes its documented default unless overridden here, so a caller
    supplies only what it cares about and cannot get the *order* wrong — which is
    the failure the flat wire would otherwise invite.

    Args:
        name: registry key of the fit, e.g. ``"fit23"``.
        **values: setup values by name, e.g. ``dt=0.032, period=13.5``. A string
            value for an enumerated property (such as ``objective``) is
            converted to its index.

    Returns:
        list[float]: the flat setup vector.

    Raises:
        ValueError: unknown fit, unknown setup name, or an enum value that is
            not one of the declared choices.

    Example:
        >>> import tttrlib
        >>> tttrlib.setup_vector("fit23", dt=0.032, period=13.5)   # doctest: +SKIP
        [0.032, 13.5, 1.0, 0.0, 0.0, -1.0, 1.0, 0.0, 0.0, -1.0]
    """
    # Delegates to the C++ builder so there is one implementation of the
    # flattening rule rather than one per language binding.
    try:
        return list(decay_fit_setup_vector(name, _json.dumps(values)))
    except Exception as exc:                       # native errors are plain
        raise ValueError(str(exc)) from None


def parameter_vector(name, n=None, **values):
    """Build a model's flat parameter vector from its registry schema.

    Args:
        name: registry key of the fit.
        n: entry count for a model whose parameters are variable-length arrays
            (the value its ``count_from`` names). Ignored by fixed-arity models.
        **values: starting values by name. An array property is set with a
            sequence, e.g. ``lifetimes=[0.5, 3.0]``.

    Returns:
        list[float]: the flat parameter vector, in schema order.

    Example:
        >>> import tttrlib
        >>> tttrlib.parameter_vector("fit23", tau=2.0)   # doctest: +SKIP
        [2.0, 0.1, 0.38, 1.22]
    """
    entry = _fit_entry(name)
    properties = entry["params_schema"]["properties"]

    unknown = set(values) - set(properties)
    if unknown:
        raise ValueError(
            f"{name} does not take {sorted(unknown)}; available: {sorted(properties)}"
        )

    out = []
    for prop_name, prop in properties.items():
        if prop.get("type") == "array":
            count = int(n if n is not None else 0)
            supplied = values.get(prop_name)
            if supplied is not None:
                count = len(supplied) if n is None else count
                out.extend(float(v) for v in list(supplied)[:count])
                out.extend(float(prop["items"].get("default", 0.0))
                           for _ in range(count - len(supplied)))
            else:
                out.extend(float(prop["items"].get("default", 0.0)) for _ in range(count))
        else:
            out.append(float(values.get(prop_name, prop.get("default", 0.0))))
    return out


def default_links(name, n=None, free=None):
    """Build a link vector holding everything the registry marks fixed.

    ``fixed_default`` exists because some parameters are not identifiable from a
    short decay — scatter and anisotropy against an auto-extracted background —
    so freeing them by default produces confident nonsense.

    Args:
        name: registry key of the fit.
        n: entry count for variable-length parameter blocks.
        free: names to free regardless of their default, e.g. ``["gamma"]``.

    Returns:
        list[int]: link codes, ``-1`` fixed and ``0`` free, one per slot.
    """
    codes = list(decay_fit_default_links(name, 0 if n is None else int(n)))
    if free:
        names = list(decay_fit_parameter_names(name, 0 if n is None else int(n)))
        free = set(free)
        codes = [
            0 if slot.split("[")[0] in free else code
            for slot, code in zip(names, codes)
        ]
    return codes


def result_names(name, n=None):
    """Column names of a model's flat result vector, in order.

    Returns:
        list[str]: one name per result slot.
    """
    return list(decay_fit_result_names(name, 0 if n is None else int(n)))


def fit_image(fit, problem, cube, x0, constraints, min_counts=1):
    """Fit every pixel of an image stack, masking the ones with too few photons.

    A pixel with a handful of photons has no lifetime to measure, and fitting it
    anyway returns a confident number drawn from noise. Masking is therefore part
    of the operation rather than something a caller is trusted to remember: the
    returned ``valid`` array says which pixels were fitted, and the parameters of
    the rest are ``nan`` instead of plausible-looking values.

    Args:
        fit: a :class:`DecayFit2`.
        problem: prototype problem describing one pixel's decay.
        cube: ``(ny, nx, n_channels * n_bins)`` array of per-pixel decays.
        x0: starting parameters (shared by every pixel).
        constraints: applied to every pixel.
        min_counts: fewest photons a pixel needs to be fitted.

    Returns:
        dict: ``parameters`` ``(ny, nx, n_parameters)``, ``results``
        ``(ny, nx, n_results)``, ``objective`` ``(ny, nx)``, ``intensity``
        ``(ny, nx)`` and ``valid`` ``(ny, nx)``. Masked pixels are ``nan``.
    """
    import numpy as np

    cube = np.ascontiguousarray(cube, dtype=np.float64)
    ny, nx, n_cols = cube.shape
    flat = cube.reshape(ny * nx, n_cols)
    intensity = flat.sum(axis=1)
    valid = intensity >= float(min_counts)

    n_par = fit.n_parameters(problem)
    n_res = fit.n_results(problem)
    parameters = np.full((ny * nx, n_par), np.nan)
    results = np.full((ny * nx, n_res), np.nan)
    objective = np.full(ny * nx, np.nan)

    rows = np.flatnonzero(valid)
    if rows.size:
        batch = fit.fit_many(
            problem, flat[rows].ravel(), int(rows.size), int(n_cols),
            list(x0), constraints)
        parameters[rows] = np.asarray(batch.parameters).reshape(rows.size, n_par)
        results[rows] = np.asarray(batch.results).reshape(rows.size, n_res)
        objective[rows] = np.asarray(batch.objective)

    return {
        "parameters": parameters.reshape(ny, nx, n_par),
        "results": results.reshape(ny, nx, n_res),
        "objective": objective.reshape(ny, nx),
        "intensity": intensity.reshape(ny, nx),
        "valid": valid.reshape(ny, nx),
    }


def results_as_dict(name, results, n=None):
    """Name a flat result vector's columns.

    A batch returns an ``(n_rows, n_results)`` matrix whose columns mean whatever
    the schema says; this is what turns one row of it into something readable.

    Args:
        name: registry key of the fit.
        results: one row of results.
        n: entry count for variable-length blocks.

    Returns:
        dict: result name to value, with booleans and integers restored to their
        declared types rather than left as the doubles they travelled as.

    Example:
        >>> import tttrlib
        >>> tttrlib.results_as_dict("fit23", [1.03, 1.0, 12.0, 0.21, 0.19])
        {'twoIstar': 1.03, 'converged': True, 'iterations': 12, 'r_scatter': 0.21, 'r_experimental': 0.19}
    """
    entry = _fit_entry(name)
    counts = {}
    for prop in entry["results_schema"]["properties"].values():
        if prop.get("type") == "array":
            counts[prop["count_from"]] = 0 if n is None else int(n)
    slots = _schema_slots(entry["results_schema"]["properties"], counts)

    out = {}
    for (slot_name, prop), value in zip(slots, results):
        if prop.get("type") == "boolean":
            out[slot_name] = bool(value)
        elif prop.get("type") == "integer":
            out[slot_name] = int(value)
        else:
            out[slot_name] = float(value)
    return out
