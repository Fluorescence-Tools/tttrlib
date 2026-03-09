from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, MutableSequence, Optional, Sequence, Tuple, TYPE_CHECKING
import weakref
import atexit

import numpy as np

try:  # pragma: no cover - fallback when extension is unavailable
    from . import _tttrlib as _backend
except ImportError:  # pragma: no cover - keeps imports working during docs/type checking
    _backend = None  # type: ignore[assignment]

if TYPE_CHECKING:  # pragma: no cover - imported only for type checkers
    from ._tttrlib import CLSMImage, VectorDouble, VectorDouble_2D, localization

__all__ = ["GaussianFitResult", "ImageLocalizer"]


def _get_swig_symbol(name: str):
    """Return a SWIG generated symbol from the compiled backend."""

    if _backend is None:
        raise RuntimeError(
            "The tttrlib extension module is not available. "
            "Ensure tttrlib is built before importing ImageLocalizer."
        )

    try:
        return getattr(_backend, name)
    except AttributeError as exc:  # pragma: no cover - defensive guard
        raise RuntimeError(
            f"The SWIG symbol '{name}' is not available in the tttrlib backend."
        ) from exc


def _vector_double_from_sequence(values: Sequence[float]):
    """Convert a Python sequence into a ``tttrlib.VectorDouble``.
    
    Note: This function creates SWIG vectors that may cause memory leaks.
    Use numpy arrays with the NumPy-friendly methods when possible.
    """

    vector_type = _get_swig_symbol("VectorDouble")
    vec = vector_type()
    for value in values:
        vec.append(float(value))
    return vec


def _vector_2d_from_array(array: np.ndarray):
    """Convert a 2-D numpy array into ``tttrlib.VectorDouble_2D``.
    
    Note: This function creates SWIG vectors that may cause memory leaks.
    Use numpy arrays with the NumPy-friendly methods when possible.
    """

    if array.ndim != 2:
        raise ValueError("Image data must be two-dimensional")

    vector_2d_type = _get_swig_symbol("VectorDouble_2D")
    rows = vector_2d_type()
    for row in array:
        rows.append(_vector_double_from_sequence(row))
    return rows


# Global registry for tracking localization instances for cleanup
_localization_instances = weakref.WeakSet()


def _cleanup_all_localizations():
    """Clean up all remaining localization instances at exit."""
    for instance in list(_localization_instances):
        try:
            instance._cleanup()
        except Exception:
            pass  # Ignore errors during cleanup


# Register cleanup function to run at exit
atexit.register(_cleanup_all_localizations)


def _normalise_roi(
    roi: Optional[Tuple[Optional[slice | Tuple[int, int] | int], Optional[slice | Tuple[int, int] | int]]],
    shape: Tuple[int, int],
) -> Tuple[Tuple[slice, slice], Tuple[int, int]]:
    """Normalise ROI specifications to slices and return the offset."""

    if roi is None:
        return (slice(0, shape[0], 1), slice(0, shape[1], 1)), (0, 0)

    if not isinstance(roi, tuple) or len(roi) != 2:
        raise TypeError("ROI must be a tuple of (rows, cols) specifications")

    def _to_slice(spec, size: int) -> slice:
        if spec is None:
            return slice(0, size, 1)
        if isinstance(spec, slice):
            start, stop, step = spec.indices(size)
            if step != 1:
                raise ValueError("ROI slices must not use a step")
            return slice(start, stop, 1)
        if isinstance(spec, int):
            index = spec if spec >= 0 else size + spec
            if not 0 <= index < size:
                raise ValueError("ROI index out of bounds")
            return slice(index, index + 1, 1)
        if isinstance(spec, tuple) and len(spec) == 2:
            start, stop = spec
            if start is None:
                start = 0
            if stop is None:
                stop = size
            start = start if start >= 0 else size + start
            stop = stop if stop >= 0 else size + stop
            if not (0 <= start <= size and 0 <= stop <= size):
                raise ValueError("ROI range out of bounds")
            if stop <= start:
                raise ValueError("ROI stop must be greater than start")
            return slice(start, stop, 1)
        raise TypeError(
            "ROI entries must be slices, index pairs, integers or None"
        )

    row_slice = _to_slice(roi[0], shape[0])
    col_slice = _to_slice(roi[1], shape[1])

    def _offset(slc: slice, size: int) -> int:
        start = slc.start if slc.start is not None else 0
        if start < 0:
            start += size
        return start

    offset = (_offset(row_slice, shape[0]), _offset(col_slice, shape[1]))
    return (row_slice, col_slice), offset


def _ensure_array(
    image: np.ndarray | "CLSMImage",
    *,
    frame: Optional[int] = None,
) -> np.ndarray:
    """Return a 2-D ``np.ndarray`` for the provided image source."""

    if isinstance(image, np.ndarray):
        array = np.asarray(image, dtype=np.float64)
    else:
        if hasattr(image, "get_intensity"):
            intensity = image.get_intensity()
            array = np.asarray(intensity, dtype=np.float64)
        else:  # pragma: no cover - defensive guard for unknown types
            raise TypeError(
                "Image data must be a numpy array or a tttrlib.CLSMImage instance"
            )

        if array.ndim == 3:
            if frame is None:
                if array.shape[0] != 1:
                    raise ValueError(
                        "The CLSM image contains multiple frames; provide the "
                        "frame index to localise a single plane."
                    )
                frame_index = 0
            else:
                frame_index = frame
            array = np.asarray(array[frame_index], dtype=np.float64)

    if array.ndim != 2:
        raise ValueError("Image data must be two-dimensional")

    return np.ascontiguousarray(array, dtype=np.float64)


def _prepare_parameters(
    image: np.ndarray,
    base: Optional[Sequence[float]] = None,
    *,
    fit_background: bool,
    allow_elliptical: bool,
    model: int,
    overrides: Optional[Mapping[str, float | Tuple[float, float]]] = None,
) -> np.ndarray:
    """Create a full 18 parameter vector for the Gaussian fit."""

    if base is None:
        params = np.zeros(18, dtype=np.float64)
        y_peak, x_peak = np.unravel_index(np.argmax(image), image.shape)
        params[0] = float(x_peak)
        params[1] = float(y_peak)
        amplitude = float(np.max(image) - np.min(image))
        params[2] = amplitude if amplitude > 0 else float(np.max(image))
        params[3] = max(float(min(image.shape)) / 6.0, 1.0)
        params[4] = 1.0
        params[5] = float(np.min(image))
    else:
        if len(base) != 18:
            raise ValueError("Initial parameter sequence must contain 18 elements")
        params = np.asarray(base, dtype=np.float64).copy()

    params[14] = 0 if fit_background else 1
    params[15] = 0 if allow_elliptical else 1
    params[16] = int(model)

    if overrides:
        if "center" in overrides:
            cx, cy = overrides["center"]  # type: ignore[misc]
            params[0] = float(cx)
            params[1] = float(cy)
        if "amplitude" in overrides:
            params[2] = float(overrides["amplitude"])  # type: ignore[arg-type]
        if "sigma" in overrides:
            params[3] = float(overrides["sigma"])  # type: ignore[arg-type]
        if "background" in overrides:
            params[5] = float(overrides["background"])  # type: ignore[arg-type]
        if "ellipticity" in overrides:
            params[4] = float(overrides["ellipticity"])  # type: ignore[arg-type]

    return params


@dataclass(frozen=True)
class GaussianFitResult:
    """Container describing the outcome of a Gaussian localisation fit."""

    status: int
    parameters: np.ndarray
    roi_offset: Tuple[int, int]
    roi_shape: Tuple[int, int]
    model: Optional[np.ndarray] = None

    @property
    def success(self) -> bool:
        """Return ``True`` when the fit converged successfully."""

        return self.status > 0

    @property
    def center(self) -> Tuple[float, float]:
        """Return the fitted centre position (x, y) within the ROI."""

        return float(self.parameters[0]), float(self.parameters[1])

    @property
    def amplitude(self) -> float:
        return float(self.parameters[2])

    @property
    def sigma(self) -> float:
        return float(self.parameters[3])

    @property
    def ellipticity(self) -> float:
        return float(self.parameters[4])

    @property
    def background(self) -> float:
        return float(self.parameters[5])

    @property
    def chi2(self) -> float:
        return float(self.parameters[17])

    def to_global(self) -> Tuple[float, float]:
        """Return the fitted centre in global coordinates."""

        x, y = self.center
        return x + self.roi_offset[1], y + self.roi_offset[0]


class ImageLocalizer:
    """High level helper exposing numpy friendly localisation methods.
    
    This class provides a memory-safe NumPy-friendly interface to tttrlib's
    localization functionality. It automatically manages SWIG object cleanup
    to prevent memory leaks.
    
    Can be used as a context manager for explicit resource management:
    
        with ImageLocalizer() as localizer:
            result = localizer.fit(image)
    """

    def __init__(
        self,
        *,
        model: int = 0,
        fit_background: bool = True,
        allow_elliptical: bool = False,
    ) -> None:
        # Initialize cleanup flag first to prevent __del__ issues
        self._cleaned_up = False
        
        localization_type = _get_swig_symbol("localization")
        self._impl = localization_type()
        self.model = model
        self.fit_background = fit_background
        self.allow_elliptical = allow_elliptical
        
        # Register this instance for cleanup tracking
        _localization_instances.add(self)
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit with cleanup."""
        self._cleanup()
    
    def __del__(self):
        """Destructor with cleanup."""
        self._cleanup()
    
    def _cleanup(self):
        """Clean up SWIG resources to prevent memory leaks."""
        if not self._cleaned_up and hasattr(self, '_impl'):
            try:
                # SWIG objects don't have explicit destructors in Python,
                # but we can at least clear the reference
                del self._impl
                self._cleaned_up = True
            except Exception:
                pass  # Ignore cleanup errors

    def fit(
        self,
        image: np.ndarray | "CLSMImage",
        initial: Optional[Sequence[float] | MutableSequence[float]] = None,
        *,
        frame: Optional[int] = None,
        roi: Optional[
            Tuple[
                Optional[slice | Tuple[int, int] | int],
                Optional[slice | Tuple[int, int] | int],
            ]
        ] = None,
        guess: Optional[Mapping[str, float | Tuple[float, float]]] = None,
        return_model: bool = False,
    ) -> GaussianFitResult:
        """Fit a two-dimensional Gaussian to the provided image data.
        
        This method uses NumPy-friendly SWIG methods to avoid memory leaks
        from SWIG vector objects.
        """
        
        if self._cleaned_up:
            raise RuntimeError("ImageLocalizer has been cleaned up and cannot be used")

        array = _ensure_array(image, frame=frame)
        roi_slices, offset = _normalise_roi(roi, array.shape)
        sub_image = array[roi_slices[0], roi_slices[1]]

        if sub_image.size == 0:
            raise ValueError("The selected ROI does not contain any pixels")

        params = _prepare_parameters(
            sub_image,
            initial,
            fit_background=self.fit_background,
            allow_elliptical=self.allow_elliptical,
            model=self.model,
            overrides=guess,
        )

        # Use NumPy-friendly SWIG method to avoid memory leaks
        # Create a mutable copy of parameters for in-place modification
        param_vec = _vector_double_from_sequence(params)
        
        # Ensure sub_image is contiguous for NumPy SWIG interface
        sub_image_contiguous = np.ascontiguousarray(sub_image, dtype=np.float64)
        
        # Use the NumPy-friendly fit method
        status = self._impl.fit2DGaussian_numpy(
            param_vec, 
            sub_image_contiguous, 
            sub_image_contiguous.shape[0], 
            sub_image_contiguous.shape[1]
        )

        # Extract fitted parameters from the modified vector
        fitted = np.fromiter(
            (float(param_vec[i]) for i in range(len(param_vec))),
            dtype=np.float64,
            count=len(param_vec),
        )

        if isinstance(initial, MutableSequence):
            initial[:] = fitted.tolist()
        elif isinstance(initial, np.ndarray):
            initial[...] = fitted

        model_array = None
        if return_model:
            model_array = self.model_image(fitted, sub_image.shape)

        return GaussianFitResult(status, fitted, offset, sub_image.shape, model_array)

    def model_image(
        self,
        parameters: Sequence[float],
        shape: Tuple[int, int],
    ) -> np.ndarray:
        """Generate a Gaussian model for the provided parameters.
        
        This method uses NumPy-friendly SWIG methods to avoid memory leaks
        from SWIG vector objects.
        """
        
        if self._cleaned_up:
            raise RuntimeError("ImageLocalizer has been cleaned up and cannot be used")

        if len(parameters) != 18:
            raise ValueError("Parameter vector must contain 18 elements")

        # Use NumPy-friendly SWIG method to avoid memory leaks
        params_vec = _vector_double_from_sequence(parameters)
        rows, cols = map(int, shape)
        
        # Use the NumPy-friendly model generation method
        model_array = self._impl.model2DGaussian_numpy(params_vec, rows, cols)
        return np.asarray(model_array, dtype=np.float64)
    
    def fit_numpy(
        self,
        image: np.ndarray,
        parameters: Optional[np.ndarray] = None,
        *,
        roi: Optional[
            Tuple[
                Optional[slice | Tuple[int, int] | int],
                Optional[slice | Tuple[int, int] | int],
            ]
        ] = None,
        return_model: bool = False,
    ) -> Tuple[int, np.ndarray, Optional[np.ndarray]]:
        """NumPy-optimized fit method that works directly with NumPy arrays.
        
        This is a lower-level interface that avoids some overhead and provides
        direct access to the NumPy-friendly SWIG methods.
        
        Parameters
        ----------
        image : np.ndarray
            2D image data to fit
        parameters : np.ndarray, optional
            18-element parameter array. If None, automatic initial guess is made.
        roi : tuple, optional
            Region of interest specification
        return_model : bool, optional
            Whether to return the fitted model image
            
        Returns
        -------
        status : int
            Fit status (>0 for success)
        fitted_params : np.ndarray
            18-element array of fitted parameters
        model : np.ndarray or None
            Model image if return_model=True, else None
        """
        
        if self._cleaned_up:
            raise RuntimeError("ImageLocalizer has been cleaned up and cannot be used")
            
        if image.ndim != 2:
            raise ValueError("Image must be 2-dimensional")
            
        # Apply ROI if specified
        if roi is not None:
            roi_slices, _ = _normalise_roi(roi, image.shape)
            sub_image = image[roi_slices[0], roi_slices[1]]
        else:
            sub_image = image
            
        if sub_image.size == 0:
            raise ValueError("The selected ROI does not contain any pixels")
            
        # Prepare parameters
        if parameters is None:
            params = _prepare_parameters(
                sub_image,
                None,
                fit_background=self.fit_background,
                allow_elliptical=self.allow_elliptical,
                model=self.model,
                overrides=None,
            )
        else:
            if len(parameters) != 18:
                raise ValueError("Parameter array must contain 18 elements")
            params = np.asarray(parameters, dtype=np.float64).copy()
            # Set control parameters
            params[14] = 0 if self.fit_background else 1
            params[15] = 0 if self.allow_elliptical else 1
            params[16] = int(self.model)
            
        # Use NumPy-friendly SWIG method
        param_vec = _vector_double_from_sequence(params)
        sub_image_contiguous = np.ascontiguousarray(sub_image, dtype=np.float64)
        
        status = self._impl.fit2DGaussian_numpy(
            param_vec,
            sub_image_contiguous,
            sub_image_contiguous.shape[0],
            sub_image_contiguous.shape[1]
        )
        
        # Extract fitted parameters
        fitted = np.fromiter(
            (float(param_vec[i]) for i in range(len(param_vec))),
            dtype=np.float64,
            count=len(param_vec),
        )
        
        # Generate model if requested
        model_array = None
        if return_model:
            model_array = self.model_image(fitted, sub_image.shape)
            
        return status, fitted, model_array

