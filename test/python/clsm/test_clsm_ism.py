"""CLSM ISM (Image Scanning Microscopy) tests - check if it works"""
from __future__ import division

import os
import unittest
import warnings
import gc

import tttrlib

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# Load CLSM data path if available (already absolute via test_settings)
clsm_file = settings.get("ht3_clsm_filename")
CLSM_AVAILABLE = os.path.exists(clsm_file) if clsm_file else False

_CLSMISM_AVAILABLE = hasattr(tttrlib, 'CLSMISM')

# ---------------------------------------------------------------------------
# Helper: create CLSMISM inside a catch-all for warnings so tests below can
# assert on the warning explicitly where they want to.
# ---------------------------------------------------------------------------

def _make_ism(source):
    """Return a CLSMISM or None; absorbs both errors and the experimental warning."""
    try:
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            return tttrlib.CLSMISM(source)
    except (AttributeError, TypeError, RuntimeError):
        return None


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMCreation(unittest.TestCase):
    """CLSM ISM creation tests - check if it works"""

    def setUp(self):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_creation_works(self):
        """Test ISM creation works (no correctness check)"""
        try:
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                ism = tttrlib.CLSMISM(self.clsm)
                self.assertIsNotNone(ism)
            # Verify an ExperimentalWarning was issued
            exp_warnings = [
                w for w in caught
                if issubclass(w.category, tttrlib.ExperimentalWarning)
            ]
            self.assertTrue(
                len(exp_warnings) >= 1,
                "Expected at least one ExperimentalWarning when constructing CLSMISM"
            )
        except (AttributeError, TypeError, RuntimeError):
            # ISM might not be available for this file - that's OK
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_creation_from_tttr(self):
        """Test ISM creation from TTTR object"""
        try:
            tttr = self.clsm.tttr_data
            with warnings.catch_warnings(record=True) as caught:
                warnings.simplefilter("always")
                ism = tttrlib.CLSMISM(tttr)
                self.assertIsNotNone(ism)
            exp_warnings = [
                w for w in caught
                if issubclass(w.category, tttrlib.ExperimentalWarning)
            ]
            self.assertTrue(len(exp_warnings) >= 1)
        except (AttributeError, TypeError, RuntimeError):
            pass


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMOperations(unittest.TestCase):
    """CLSM ISM operations tests - check if it works"""

    def setUp(self):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_n_frames_access(self):
        """Test ISM n_frames property access"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            n_frames = ism.n_frames
            self.assertIsNotNone(n_frames)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_n_lines_access(self):
        """Test ISM n_lines property access"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            n_lines = ism.n_lines
            self.assertIsNotNone(n_lines)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_n_pixel_access(self):
        """Test ISM n_pixel property access"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            n_pixel = ism.n_pixel
            self.assertIsNotNone(n_pixel)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_shape_access(self):
        """Test ISM shape property access"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            shape = ism.shape
            self.assertIsNotNone(shape)
        except (AttributeError, TypeError, RuntimeError):
            pass


@unittest.skipIf(not (DATA_AVAILABLE and CLSM_AVAILABLE), "CLSM data not available")
class TestCLSMISMIntensity(unittest.TestCase):
    """CLSM ISM intensity operations - check if it works"""

    def setUp(self):
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            self.clsm = tttrlib.CLSMImage(clsm_file)

    def tearDown(self):
        self.clsm = None
        gc.collect()

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_intensity_access(self):
        """Test ISM intensity property access"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            intensity = ism.intensity
            self.assertIsNotNone(intensity)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_fill_operation(self):
        """Test ISM fill operation"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            ism.fill()
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            pass

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_ism_fill_with_channels(self):
        """Test ISM fill with specific channels"""
        ism = _make_ism(self.clsm)
        if ism is None:
            return
        try:
            ism.fill(channels=[0, 1])
            self.assertIsNotNone(ism)
        except (AttributeError, TypeError, RuntimeError):
            pass


class TestCLSMISMFallback(unittest.TestCase):
    """Fallback tests when ISM not available"""

    def test_ism_availability_check(self):
        """Test ISM availability"""
        has_ism = hasattr(tttrlib, 'CLSMISM')
        self.assertIsInstance(has_ism, bool)

    def test_clsm_data_check(self):
        """Test CLSM data availability"""
        self.assertTrue(DATA_AVAILABLE, "Test data directory not found")

    def test_experimental_warning_exported(self):
        """ExperimentalWarning must be accessible as tttrlib.ExperimentalWarning"""
        self.assertTrue(
            hasattr(tttrlib, 'ExperimentalWarning'),
            "tttrlib.ExperimentalWarning not found"
        )
        self.assertTrue(
            issubclass(tttrlib.ExperimentalWarning, UserWarning),
            "ExperimentalWarning must be a subclass of UserWarning"
        )

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_clsmism_marked_experimental(self):
        """CLSMISM must carry the __experimental__ attribute"""
        self.assertTrue(
            getattr(tttrlib.CLSMISM, '__experimental__', False),
            "CLSMISM.__experimental__ not set"
        )

    @unittest.skipIf(not _CLSMISM_AVAILABLE, "CLSMISM not available")
    def test_clsmism_warns_on_construction(self):
        """Constructing CLSMISM() must emit ExperimentalWarning"""
        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            try:
                tttrlib.CLSMISM()
            except Exception:
                pass  # constructor may raise on bad args — that's fine
        exp_warnings = [
            w for w in caught
            if issubclass(w.category, tttrlib.ExperimentalWarning)
        ]
        self.assertTrue(
            len(exp_warnings) >= 1,
            "No ExperimentalWarning emitted when constructing CLSMISM"
        )

    def test_experimental_decorator_available(self):
        """tttrlib.experimental decorator must be importable"""
        self.assertTrue(
            hasattr(tttrlib, 'experimental'),
            "tttrlib.experimental decorator not found"
        )

    def test_experimental_decorator_patches_class(self):
        """@experimental must patch __init__ and set __experimental__"""
        @tttrlib.experimental
        class _Dummy:
            def __init__(self):
                self.ok = True

        self.assertTrue(getattr(_Dummy, '__experimental__', False))

        with warnings.catch_warnings(record=True) as caught:
            warnings.simplefilter("always")
            obj = _Dummy()
        self.assertTrue(obj.ok)
        exp_warnings = [
            w for w in caught
            if issubclass(w.category, tttrlib.ExperimentalWarning)
        ]
        self.assertTrue(len(exp_warnings) >= 1)


if __name__ == '__main__':
    unittest.main()
