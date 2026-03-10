import warnings
import numpy as np


class ExperimentalWarning(UserWarning):
    """Warning emitted when an experimental tttrlib feature is used."""


_CLSMISM_WARN = (
    "CLSMISM is experimental and may change or be removed in a future "
    "release. Use with caution."
)


# Attach Python-side convenience wrappers strictly under CLSMISM
try:  # pragma: no cover
    cls = globals().get('CLSMISM', None)

    def _as_detector_cube(data):
        """Return a contiguous (D, H, W) float64 cube from either:
        - a NumPy-like 3-D array, or
        - a tttrlib.CLSMImage (uses its intensity stack).
        """
        # Try duck-typing a CLSMImage first (intensity is exposed via __getattr__)
        try:
            if hasattr(data, 'intensity'):
                arr = np.asarray(getattr(data, 'intensity'), dtype=np.float64)
            elif hasattr(data, 'get_intensity'):
                arr = np.asarray(data.get_intensity(), dtype=np.float64)
            else:
                arr = np.asarray(data, dtype=np.float64)
        except Exception:
            arr = np.asarray(data, dtype=np.float64)
        if arr.ndim != 3:
            raise ValueError(f"Expected a 3-D detector cube or CLSMImage, got shape {arr.shape}")
        return np.ascontiguousarray(arr)

    def _apr_reconstruction(data, *, channels_last=False, usf=10, ref_idx=-1, filter_sigma=0.0, nz=1, n_det=-1):
        cube = _as_detector_cube(data)
        return cls._native_apr_reconstruction(
            cube,
            bool(channels_last),
            int(usf),
            int(ref_idx),
            float(filter_sigma),
            int(nz),
            int(n_det),
        )

    def _focus_reconstruction(
        data,
        *,
        channels_last=False,
        sigma_bound=2.0,
        threshold=0.1,
        calibration_size=10,
        parallelize=False,
        nz=1,
        n_det=-1,
        detector_coords=None,
    ):
        cube = _as_detector_cube(data)
        # SWIG stub expects a 1-D array for detector_coords; pass empty when None
        if detector_coords is None:
            coords1d = np.empty((0,), dtype=np.float64)
        else:
            coords1d = np.asarray(detector_coords, dtype=np.float64).ravel(order='C')
            coords1d = np.ascontiguousarray(coords1d, dtype=np.float64)
        return cls._native_focus_reconstruction(
            cube,
            bool(channels_last),
            float(sigma_bound),
            float(threshold),
            int(calibration_size),
            bool(parallelize),
            int(nz),
            int(n_det),
            coords1d,
        )

    # Bind as static methods with experimental warnings, avoid exporting
    # module-level functions.
    if cls is not None:
        # --- warn on instantiation ---
        _orig_init = cls.__init__

        def _warned_init(self, *args, **kwargs):
            warnings.warn(_CLSMISM_WARN, ExperimentalWarning, stacklevel=2)
            _orig_init(self, *args, **kwargs)

        try:
            cls.__init__ = _warned_init
        except (AttributeError, TypeError):
            pass

        # --- warn on static-method calls ---
        def _warned_apr(*args, **kwargs):
            warnings.warn(_CLSMISM_WARN, ExperimentalWarning, stacklevel=2)
            return _apr_reconstruction(*args, **kwargs)

        def _warned_focus(*args, **kwargs):
            warnings.warn(_CLSMISM_WARN, ExperimentalWarning, stacklevel=2)
            return _focus_reconstruction(*args, **kwargs)

        cls.apr_reconstruction = staticmethod(_warned_apr)
        cls.focus_reconstruction = staticmethod(_warned_focus)

        # Mark class as experimental so callers can introspect
        cls.__experimental__ = True

        # Prepend experimental note to the docstring
        _note = (
            ".. warning::\n\n"
            "   **Experimental** — this class is not yet stable API and may "
            "change or be removed without notice.\n\n"
        )
        cls.__doc__ = _note + (cls.__doc__ or "")

    # Clean up temporary names
    del _apr_reconstruction
    del _focus_reconstruction
    del _as_detector_cube
except Exception:
    pass
