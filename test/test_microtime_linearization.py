"""
Comprehensive tests for MicrotimeLinearization class and TTTR integration.
Tests both the MicrotimeLinearization class directly and its integration with TTTR objects.
"""

import pytest
import numpy as np
import tttrlib


class TestMicrotimeLinearizationClass:
    """Test MicrotimeLinearization class functionality"""

    def test_single_card_creation(self):
        """Test creating a linearizer with a single card"""
        lt1 = tttrlib.VectorFloat()
        for i in range(256):
            lt1.append(float(i))
        
        tables = tttrlib.MapIntVectorFloat()
        tables[0] = lt1
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        assert linearizer.is_valid()
        assert linearizer.get_num_cards() == 1
        assert list(linearizer.get_card_ids()) == [0]

    def test_two_card_creation(self):
        """Test creating a linearizer with two cards"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(2):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        for ch in range(8, 16):
            channel_map[ch] = 1
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        assert linearizer.is_valid()
        assert linearizer.get_num_cards() == 2
        assert set(linearizer.get_card_ids()) == {0, 1}

    def test_ncard_creation_with_custom_mapping(self):
        """Test creating a linearizer with n cards and custom channel mapping"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(4):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        # Card 0: channels 0-3
        for ch in range(0, 4):
            channel_map[ch] = 0
        # Card 1: channels 4-7
        for ch in range(4, 8):
            channel_map[ch] = 1
        # Card 2: channels 8-11
        for ch in range(8, 12):
            channel_map[ch] = 2
        # Card 3: channels 12-15
        for ch in range(12, 16):
            channel_map[ch] = 3
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        assert linearizer.is_valid()
        assert linearizer.get_num_cards() == 4
        assert set(linearizer.get_card_ids()) == {0, 1, 2, 3}

    def test_channel_to_card_mapping_query(self):
        """Test querying which card handles a specific channel"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(2):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        # Non-contiguous mapping: Card 0 handles channels 0-3 and 12-15
        for ch in range(0, 4):
            channel_map[ch] = 0
        for ch in range(12, 16):
            channel_map[ch] = 0
        # Card 1 handles channels 4-11
        for ch in range(4, 12):
            channel_map[ch] = 1
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Test channel-to-card queries
        assert linearizer.get_card_for_channel(0) == 0
        assert linearizer.get_card_for_channel(3) == 0
        assert linearizer.get_card_for_channel(4) == 1
        assert linearizer.get_card_for_channel(11) == 1
        assert linearizer.get_card_for_channel(12) == 0
        assert linearizer.get_card_for_channel(15) == 0
        assert linearizer.get_card_for_channel(100) == -1  # Unmapped channel

    def test_channels_for_card_query(self):
        """Test querying all channels assigned to a specific card"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(2):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        # Card 0: channels 0-3 and 12-15 (non-contiguous)
        for ch in [0, 1, 2, 3, 12, 13, 14, 15]:
            channel_map[ch] = 0
        # Card 1: channels 4-11
        for ch in range(4, 12):
            channel_map[ch] = 1
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Query channels for each card
        card0_channels = set(linearizer.get_channels_for_card(0))
        card1_channels = set(linearizer.get_channels_for_card(1))
        
        assert card0_channels == {0, 1, 2, 3, 12, 13, 14, 15}
        assert card1_channels == {4, 5, 6, 7, 8, 9, 10, 11}

    def test_tacstart_tacshift_parameters(self):
        """Test setting and retrieving TAC start and shift parameters"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(2):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        for ch in range(8, 16):
            channel_map[ch] = 1
        
        tacstart_map = tttrlib.MapIntInt()
        tacstart_map[0] = 10
        tacstart_map[1] = 20
        
        tacshift_map = tttrlib.MapIntInt()
        tacshift_map[0] = 5
        tacshift_map[1] = 15
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tacstart_map, tacshift_map
        )
        
        # Check card 0 parameters
        assert linearizer.get_tacstart(0) == 10
        assert linearizer.get_tacshift(0) == 5
        
        # Check card 1 parameters
        assert linearizer.get_tacstart(1) == 20
        assert linearizer.get_tacshift(1) == 15

    def test_get_linearization_table(self):
        """Test retrieving linearization tables"""
        tables = tttrlib.MapIntVectorFloat()
        
        lt1 = tttrlib.VectorFloat()
        for i in range(256):
            lt1.append(float(i))
        tables[0] = lt1
        
        lt2 = tttrlib.VectorFloat()
        for i in range(256):
            lt2.append(float(i + 100))
        tables[1] = lt2
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        for ch in range(8, 16):
            channel_map[ch] = 1
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Get tables using getter methods
        retrieved_lt1 = linearizer.get_linearization_table(0)
        retrieved_lt2 = linearizer.get_linearization_table(1)
        
        assert len(retrieved_lt1) == 256
        assert len(retrieved_lt2) == 256
        assert abs(retrieved_lt1[0] - 0.0) < 0.1
        assert abs(retrieved_lt2[0] - 100.0) < 0.1

    def test_empty_linearizer_invalid(self):
        """Test that empty linearizer is invalid"""
        tables = tttrlib.MapIntVectorFloat()
        channel_map = tttrlib.MapIntInt()
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        assert not linearizer.is_valid()
        assert linearizer.get_num_cards() == 0

    def test_add_card_method(self):
        """Test adding a card dynamically"""
        linearizer = tttrlib.MicrotimeLinearization(
            tttrlib.MapIntVectorFloat(), 
            tttrlib.MapIntInt(), 
            tttrlib.MapIntInt(), 
            tttrlib.MapIntInt()
        )
        
        # Initially empty
        assert linearizer.get_num_cards() == 0
        
        # Add a card
        lt = tttrlib.VectorFloat()
        for i in range(256):
            lt.append(float(i))
        
        linearizer.add_card(0, lt)
        
        assert linearizer.get_num_cards() == 1
        retrieved = linearizer.get_linearization_table(0)
        assert len(retrieved) == 256

    def test_set_card_for_channel_method(self):
        """Test setting channel-to-card mapping dynamically"""
        tables = tttrlib.MapIntVectorFloat()
        lt = tttrlib.VectorFloat()
        for i in range(256):
            lt.append(float(i))
        tables[0] = lt
        
        channel_map = tttrlib.MapIntInt()
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Initially no mapping
        assert linearizer.get_card_for_channel(5) == -1
        
        # Set mapping
        linearizer.set_card_for_channel(5, 0)
        
        assert linearizer.get_card_for_channel(5) == 0

    def test_set_tacstart_tacshift_methods(self):
        """Test setting TAC parameters dynamically"""
        tables = tttrlib.MapIntVectorFloat()
        lt = tttrlib.VectorFloat()
        for i in range(256):
            lt.append(float(i))
        tables[0] = lt
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Default values
        assert linearizer.get_tacstart(0) == 0
        assert linearizer.get_tacshift(0) == 0
        
        # Set new values
        linearizer.set_tacstart(0, 10)
        linearizer.set_tacshift(0, 5)
        
        assert linearizer.get_tacstart(0) == 10
        assert linearizer.get_tacshift(0) == 5


class TestMicrotimeLinearizationWithTTTR:
    """Test MicrotimeLinearization integration with TTTR objects"""

    def _create_linearizer_single_card(self):
        """Helper to create a single-card linearizer"""
        lt1 = tttrlib.VectorFloat()
        for i in range(256):
            lt1.append(float(i))
        
        tables = tttrlib.MapIntVectorFloat()
        tables[0] = lt1
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        
        return tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )

    def _create_linearizer_two_cards(self):
        """Helper to create a two-card linearizer"""
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(2):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        channel_map = tttrlib.MapIntInt()
        for ch in range(8):
            channel_map[ch] = 0
        for ch in range(8, 16):
            channel_map[ch] = 1
        
        return tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )

    def test_linearize_microtimes_basic(self):
        """Test linearizing microtimes in a TTTR object"""
        tttr = tttrlib.TTTR()
        linearizer = self._create_linearizer_single_card()
        
        # Create test data using append_event
        n_photons = 100
        for i in range(n_photons):
            macro_time = i
            micro_time = i % 256
            channel = 0
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Store original microtimes
        original_microtimes = np.array(tttr.get_micro_times())
        
        # Linearize microtimes
        result = tttr.linearize_microtimes(linearizer, seed=42)
        
        assert result == 1  # Success
        
        # Get linearized microtimes
        linearized_microtimes = np.array(tttr.get_micro_times())
        assert len(linearized_microtimes) == n_photons

    def test_linearize_microtimes_two_cards(self):
        """Test linearizing microtimes with two-card setup"""
        tttr = tttrlib.TTTR()
        linearizer = self._create_linearizer_two_cards()
        
        # Create test data with channels from both cards
        n_photons = 100
        for i in range(n_photons):
            macro_time = i
            micro_time = i % 256
            channel = i % 16  # Mix channels from both cards
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Linearize
        result = tttr.linearize_microtimes(linearizer, seed=42)
        
        assert result == 1
        assert len(tttr.get_micro_times()) == n_photons

    def test_linearize_preserves_data_structure(self):
        """Test that linearization preserves TTTR data structure"""
        tttr = tttrlib.TTTR()
        linearizer = self._create_linearizer_single_card()
        
        # Create test data
        n_photons = 50
        for i in range(n_photons):
            macro_time = i * 100
            micro_time = i % 256
            channel = 0
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Store original data
        original_size = len(tttr)
        original_macro = np.array(tttr.get_macro_times())
        
        # Linearize
        result = tttr.linearize_microtimes(linearizer, seed=42)
        
        # Verify data structure is preserved
        assert result == 1
        assert len(tttr) == original_size
        assert np.array_equal(original_macro, np.array(tttr.get_macro_times()))

    def test_linearize_multiple_times(self):
        """Test that linearization can be applied multiple times"""
        tttr = tttrlib.TTTR()
        linearizer = self._create_linearizer_single_card()
        
        # Create test data
        n_photons = 50
        for i in range(n_photons):
            macro_time = i
            micro_time = i % 256
            channel = 0
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Apply linearization multiple times
        result1 = tttr.linearize_microtimes(linearizer, seed=42)
        result2 = tttr.linearize_microtimes(linearizer, seed=43)
        
        assert result1 == 1
        assert result2 == 1
        assert len(tttr) == n_photons

    def test_linearize_ncard_custom_mapping(self):
        """Test linearizing microtimes with n cards and custom channel mapping"""
        tttr = tttrlib.TTTR()
        
        # Create 4 linearization tables
        tables = tttrlib.MapIntVectorFloat()
        for card_id in range(4):
            lt = tttrlib.VectorFloat()
            for i in range(256):
                lt.append(float(i))
            tables[card_id] = lt
        
        # Create custom channel-to-card mapping
        channel_map = tttrlib.MapIntInt()
        for ch in range(0, 4):
            channel_map[ch] = 0
        for ch in range(4, 8):
            channel_map[ch] = 1
        for ch in range(8, 12):
            channel_map[ch] = 2
        for ch in range(12, 16):
            channel_map[ch] = 3
        
        linearizer = tttrlib.MicrotimeLinearization(
            tables, channel_map, tttrlib.MapIntInt(), tttrlib.MapIntInt()
        )
        
        # Create test data with channels from all 4 cards
        n_photons = 100
        for i in range(n_photons):
            macro_time = i
            micro_time = i % 256
            channel = i % 16
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Linearize
        result = tttr.linearize_microtimes(linearizer, seed=42)
        
        assert result == 1
        assert len(tttr.get_micro_times()) == n_photons

    def test_random_number_consistency(self):
        """Test that using same seed produces consistent results"""
        tttr1 = tttrlib.TTTR()
        tttr2 = tttrlib.TTTR()
        
        linearizer = self._create_linearizer_single_card()
        
        # Create identical test data
        n_photons = 50
        for i in range(n_photons):
            macro_time = i
            micro_time = i % 256
            channel = 0
            event_type = 0
            tttr1.append_event(macro_time, micro_time, channel, event_type)
            tttr2.append_event(macro_time, micro_time, channel, event_type)
        
        # Linearize both with same seed
        seed = 12345
        result1 = tttr1.linearize_microtimes(linearizer, seed=seed)
        result2 = tttr2.linearize_microtimes(linearizer, seed=seed)
        
        assert result1 == 1
        assert result2 == 1
        
        # Results should be identical
        micro1 = np.array(tttr1.get_micro_times())
        micro2 = np.array(tttr2.get_micro_times())
        
        assert np.array_equal(micro1, micro2)

    def test_large_dataset_linearization(self):
        """Test linearization on larger dataset"""
        tttr = tttrlib.TTTR()
        linearizer = self._create_linearizer_two_cards()
        
        # Create larger dataset
        n_photons = 10000
        rng = np.random.RandomState(42)
        for i in range(n_photons):
            macro_time = i * 10
            micro_time = rng.randint(0, 256)
            channel = rng.randint(0, 16)
            event_type = 0
            tttr.append_event(macro_time, micro_time, channel, event_type)
        
        # Linearize
        result = tttr.linearize_microtimes(linearizer, seed=42)
        
        assert result == 1
        assert len(tttr) == n_photons


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
