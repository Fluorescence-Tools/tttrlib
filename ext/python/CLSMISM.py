import numpy as np


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

    # Bind as static methods and avoid exporting module-level functions
    if cls is not None:
        cls.apr_reconstruction = staticmethod(_apr_reconstruction)
        cls.focus_reconstruction = staticmethod(_focus_reconstruction)

    # Clean up temporary names
    del _apr_reconstruction
    del _focus_reconstruction
    del _as_detector_cube
except Exception:
    pass
