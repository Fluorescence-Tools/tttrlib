"""
Experimental feature marking infrastructure for tttrlib.

Classes and functions marked with ``@experimental`` are available for use but
are not considered stable API.  They may change or be removed without notice
between minor versions.

Usage
-----
Mark a class as experimental::

    from tttrlib._experimental import experimental

    @experimental
    class MyFeature:
        ...

A ``tttrlib.ExperimentalWarning`` is emitted the first time the class is
instantiated *or* one of its static/class methods is called directly.

Users can silence the warnings in the usual Python way::

    import warnings
    warnings.filterwarnings("ignore", category=tttrlib.ExperimentalWarning)
"""
from __future__ import annotations

import functools
import warnings


class ExperimentalWarning(UserWarning):
    """Warning emitted when an experimental tttrlib feature is used.

    Experimental features are functional but not yet stable API — their
    interface may change or be removed in a future release.

    To suppress::

        import warnings
        import tttrlib
        warnings.filterwarnings("ignore", category=tttrlib.ExperimentalWarning)
    """


def _make_experimental_warning(name: str) -> str:
    return (
        f"{name} is experimental and may change or be removed in a future "
        "release. Use with caution."
    )


def mark_experimental(cls, message: str | None = None) -> None:
    """Patch an existing class in-place to emit :class:`ExperimentalWarning`.

    Use this when you cannot apply :func:`experimental` as a decorator — for
    example to mark SWIG extension types after import.

    Parameters
    ----------
    cls : type
        The class to patch.
    message : str, optional
        Custom warning message.  Defaults to a generic one based on the class name.
    """
    _warn_msg = message or _make_experimental_warning(cls.__name__)

    _orig_init = cls.__init__

    @functools.wraps(_orig_init)
    def _warned_init(self, *args, **kwargs):
        warnings.warn(_warn_msg, ExperimentalWarning, stacklevel=2)
        _orig_init(self, *args, **kwargs)

    try:
        cls.__init__ = _warned_init
    except (AttributeError, TypeError):
        pass

    for _attr, _val in list(vars(cls).items()):
        if not _attr.startswith('_') and isinstance(_val, staticmethod):
            _orig_fn = getattr(cls, _attr)

            def _make_warned_static(fn, msg):
                @functools.wraps(fn)
                def _warned(*args, **kwargs):
                    warnings.warn(msg, ExperimentalWarning, stacklevel=2)
                    return fn(*args, **kwargs)
                return staticmethod(_warned)

            try:
                setattr(cls, _attr, _make_warned_static(_orig_fn, _warn_msg))
            except (AttributeError, TypeError):
                pass

    cls.__experimental__ = True
    _note = (
        ".. warning::\n\n"
        "   **Experimental** — this class is not yet stable API and may "
        "change or be removed without notice.\n\n"
    )
    cls.__doc__ = _note + (cls.__doc__ or "")


def experimental(cls):
    """Class decorator that marks a class as experimental.

    On instantiation (``__init__``) and on direct static/class-method calls,
    a :class:`ExperimentalWarning` is issued.  The warning is emitted with
    ``stacklevel=2`` so it points to the caller's code, not to tttrlib
    internals.

    Works with both pure-Python classes and SWIG-generated extension types
    (the latter do not support subclassing, so the original class is patched
    in-place rather than wrapped in a subclass).

    Parameters
    ----------
    cls : type
        The class to mark as experimental.

    Returns
    -------
    type
        The same class object, patched to emit warnings on use.
    """
    mark_experimental(cls)
    return cls
