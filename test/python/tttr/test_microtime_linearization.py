"""
Comprehensive tests for MicrotimeLinearization class and TTTR integration.
Tests both the MicrotimeLinearization class directly and its integration with TTTR objects.
"""

import pytest
import numpy as np
import tttrlib


class TestMicrotimeLinearizationClass:
    """Test MicrotimeLinearization class functionality"""

    def test_default_constructor(self):
        """Test creating a linearizer with default constructor"""
        linearizer = tttrlib.MicrotimeLinearization()

        # Initially no LUTs configured
        assert not linearizer.has_luts()

    def test_set_channel_lut_basic(self):
        """Test setting LUT for a single channel"""
        linearizer = tttrlib.MicrotimeLinearization()

        # Create a simple LUT (identity mapping)
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))

        # Set LUT for channel 0
        linearizer.set_channel_lut(0, lut)

        assert linearizer.has_luts()

    def test_set_channel_shift(self):
        """Test setting shift for a channel"""
        linearizer = tttrlib.MicrotimeLinearization()

        # Set shift for channel 5
        linearizer.set_channel_shift(5, 10)

        # Note: has_luts() only checks for LUTs, not shifts
        # Shifts without LUTs are allowed

    def test_lut_size_validation(self):
        """Test that all LUTs must have the same size"""
        linearizer = tttrlib.MicrotimeLinearization()

        # Set first LUT with 256 elements
        lut256 = tttrlib.VectorFloat()
        for i in range(256):
            lut256.append(float(i))
        linearizer.set_channel_lut(0, lut256)

        # Try to set second LUT with different size - should throw exception
        lut128 = tttrlib.VectorFloat()
        for i in range(128):
            lut128.append(float(i))

        with pytest.raises(Exception):  # Should throw std::invalid_argument
            linearizer.set_channel_lut(1, lut128)

    def test_multiple_channels_same_lut_size(self):
        """Test setting multiple channels with same LUT size"""
        linearizer = tttrlib.MicrotimeLinearization()

        # Set first LUT
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))
        linearizer.set_channel_lut(0, lut)

        # Set second LUT with same size - should work
        lut2 = tttrlib.VectorFloat()
        for i in range(256):
            lut2.append(float(i + 100))
        linearizer.set_channel_lut(1, lut2)

        assert linearizer.has_luts()


class TestMicrotimeLinearizationWithTTTR:
    """Test MicrotimeLinearization integration with TTTR objects"""

    def _create_simple_tttr_data(self, n_photons=100, n_channels=8):
        """Helper to create TTTR object with test data"""
        tttr = tttrlib.TTTR()

        for i in range(n_photons):
            macro_time = i * 10
            micro_time = i % 256  # Simple pattern
            channel = i % n_channels
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)

        return tttr

    def test_apply_channel_luts_basic(self):
        """Test configuring LUTs and shifts in TTTR object"""
        tttr = self._create_simple_tttr_data()

        # Create LUT for channel 0
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))  # Identity LUT

        # Configure LUTs and shifts
        channel_luts = tttrlib.MapIntVectorFloat()
        channel_luts[0] = lut

        channel_shifts = tttrlib.MapSignedCharInt()
        channel_shifts[0] = 5  # Shift channel 0 by 5

        # Apply configuration
        result = tttr.apply_channel_luts(channel_luts, channel_shifts)
        assert result >= 0  # Returns number of modified events

    def test_apply_luts_and_shifts_basic(self):
        """Test applying linearization and shifts"""
        tttr = self._create_simple_tttr_data()

        # Configure LUT for channel 0
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))  # Identity LUT

        channel_luts = tttrlib.MapIntVectorFloat()
        channel_luts[0] = lut

        channel_shifts = tttrlib.MapSignedCharInt()
        channel_shifts[0] = 0  # No shift

        # Configure and apply
        tttr.apply_channel_luts(channel_luts, channel_shifts)
        result = tttr.apply_luts_and_shifts(42)  # With seed

        assert result >= 0  # Should succeed

    def test_apply_luts_and_shifts_with_shifts_only(self):
        """Test applying only shifts (no LUTs)"""
        tttr = self._create_simple_tttr_data()

        # Only configure shifts, no LUTs
        channel_luts = tttrlib.MapIntVectorFloat()  # Empty
        channel_shifts = tttrlib.MapSignedCharInt()
        channel_shifts[0] = 10  # Shift channel 0

        tttr.apply_channel_luts(channel_luts, channel_shifts)
        result = tttr.apply_luts_and_shifts(-1)  # Use environment variable

        assert result >= 0

    def test_linearization_preserves_data_structure(self):
        """Test that linearization preserves TTTR data structure"""
        tttr = self._create_simple_tttr_data(50)

        # Store original data
        original_size = len(tttr)
        original_macro = np.array(tttr.get_macro_times())
        original_channels = np.array(tttr.routing_channels)

        # Apply identity linearization (should not change much)
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))  # Identity

        channel_luts = tttrlib.MapIntVectorFloat()
        channel_luts[0] = lut

        channel_shifts = tttrlib.MapSignedCharInt()

        tttr.apply_channel_luts(channel_luts, channel_shifts)
        result = tttr.apply_luts_and_shifts(42)

        # Verify data structure is preserved
        assert result >= 0
        assert len(tttr) == original_size
        assert np.array_equal(original_macro, np.array(tttr.get_macro_times()))
        assert np.array_equal(original_channels, np.array(tttr.routing_channels))

    def test_multiple_channels_different_luts(self):
        """Test linearization with different LUTs for different channels"""
        tttr = tttrlib.TTTR()

        # Create data with multiple channels
        n_photons = 200
        for i in range(n_photons):
            macro_time = i * 10
            micro_time = i % 256
            channel = i % 4  # Channels 0-3
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)

        # Create different LUTs for each channel
        channel_luts = tttrlib.MapIntVectorFloat()
        for ch in range(4):
            lut = tttrlib.VectorFloat()
            for i in range(256):
                lut.append(float(i + ch * 10))  # Different offset per channel
            channel_luts[ch] = lut

        channel_shifts = tttrlib.MapSignedCharInt()

        # Apply
        tttr.apply_channel_luts(channel_luts, channel_shifts)
        result = tttr.apply_luts_and_shifts(42)

        assert result >= 0
        assert len(tttr) == n_photons

    def test_reproducible_results_with_seed(self):
        """Test that using same seed produces consistent results"""
        # Create two identical TTTR objects
        tttr1 = self._create_simple_tttr_data(50)
        tttr2 = self._create_simple_tttr_data(50)

        # Configure identical LUTs
        lut = tttrlib.VectorFloat()
        for i in range(256):
            lut.append(float(i))

        channel_luts = tttrlib.MapIntVectorFloat()
        channel_luts[0] = lut

        channel_shifts = tttrlib.MapSignedCharInt()

        # Apply to both with same seed
        seed = 12345
        tttr1.apply_channel_luts(channel_luts, channel_shifts)
        tttr2.apply_channel_luts(channel_luts, channel_shifts)

        result1 = tttr1.apply_luts_and_shifts(seed)
        result2 = tttr2.apply_luts_and_shifts(seed)

        assert result1 >= 0
        assert result2 >= 0

        # Results should be identical
        micro1 = np.array(tttr1.get_micro_times())
        micro2 = np.array(tttr2.get_micro_times())

        assert np.array_equal(micro1, micro2)

    def test_environment_variable_seed(self):
        """Test using environment variable for seed"""
        import os

        tttr = self._create_simple_tttr_data(30)

        # Set environment variable
        os.environ['TTTR_RND_SEED'] = '999'

        try:
            # Configure and apply with default seed (should use env var)
            lut = tttrlib.VectorFloat()
            for i in range(256):
                lut.append(float(i))

            channel_luts = tttrlib.MapIntVectorFloat()
            channel_luts[0] = lut

            channel_shifts = tttrlib.MapSignedCharInt()

            tttr.apply_channel_luts(channel_luts, channel_shifts)
            result = tttr.apply_luts_and_shifts(-1)  # -1 means use env var

            assert result >= 0

        finally:
            # Clean up environment
            if 'TTTR_RND_SEED' in os.environ:
                del os.environ['TTTR_RND_SEED']

    def test_large_dataset_performance(self):
        """Test linearization on larger dataset"""
        tttr = self._create_simple_tttr_data(10000, 16)  # 10k photons, 16 channels

        # Create LUTs for all channels
        channel_luts = tttrlib.MapIntVectorFloat()
        for ch in range(16):
            lut = tttrlib.VectorFloat()
            for i in range(256):
                lut.append(float(i))  # Identity
            channel_luts[ch] = lut

        channel_shifts = tttrlib.MapSignedCharInt()

        # Apply
        tttr.apply_channel_luts(channel_luts, channel_shifts)
        result = tttr.apply_luts_and_shifts(42)

        assert result >= 0
        assert len(tttr) == 10000


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
