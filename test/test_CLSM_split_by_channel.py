"""
Test for split_by_channel functionality in CLSMImage.

This test verifies that when split_by_channel=True with multiple frames and channels,
the shape is correctly reported as (n_channels, frames_per_channel, n_lines, n_pixel)
instead of (n_channels * frames_per_channel, n_lines, n_pixel).
"""

from __future__ import division

import unittest
import os
import tttrlib
import numpy as np

# Centralized test settings
from test_settings import settings, DATA_AVAILABLE  # type: ignore

# Test files with multiple channels
ht3_filename = settings.get('clsm_ht3_img_filename')

ht3_reading_parameter = {
    "marker_frame_start": [4],
    "marker_line_start": 1,
    "marker_line_stop": 2,
    "marker_event_type": 1,
    "n_pixel_per_line": 256,
    "reading_routine": 'default',
    "skip_before_first_frame_marker": True
}


@unittest.skipIf(not DATA_AVAILABLE, "Data directory not found, skipping CLSM tests")
class TestCLSMSplitByChannel(unittest.TestCase):

    def test_split_by_channel_shape(self):
        """Test that split_by_channel correctly reports shape with multiple frames and channels."""
        if not os.path.exists(ht3_filename):
            self.skipTest(f"Data file not found: {ht3_filename}")
        
        # Load data
        data = tttrlib.TTTR(ht3_filename, 'HT3')
        
        # Create CLSM image WITHOUT split_by_channel
        clsm_no_split = tttrlib.CLSMImage(
            tttr_data=data,
            split_by_channel=False,
            **ht3_reading_parameter
        )
        clsm_no_split.fill(data, channels=[0, 2])
        
        # Shape should be (n_frames, n_lines, n_pixel)
        shape_no_split = clsm_no_split.shape
        print(f"Shape without split_by_channel: {shape_no_split}")
        self.assertEqual(len(shape_no_split), 3, "Shape should have 3 dimensions without split")
        self.assertEqual(clsm_no_split.n_channels, 1, "Should have 1 channel without split")
        
        # Create CLSM image WITH split_by_channel
        clsm_split = tttrlib.CLSMImage(
            tttr_data=data,
            split_by_channel=True,
            **ht3_reading_parameter
        )
        clsm_split.fill(data, channels=[0, 2])
        
        # Shape should be (n_channels, frames_per_channel, n_lines, n_pixel)
        shape_split = clsm_split.shape
        print(f"Shape with split_by_channel: {shape_split}")
        print(f"Number of channels: {clsm_split.n_channels}")
        print(f"Total frames: {clsm_split.n_frames}")
        
        # Verify shape has 4 dimensions when split_by_channel is enabled
        if clsm_split.n_channels > 1:
            self.assertEqual(len(shape_split), 4, 
                           "Shape should have 4 dimensions with split_by_channel=True and multiple channels")
            
            # Verify the dimensions are correct
            n_channels, frames_per_channel, n_lines, n_pixel = shape_split
            self.assertEqual(n_channels, clsm_split.n_channels, 
                           "First dimension should be number of channels")
            self.assertEqual(n_lines, clsm_split.n_lines, 
                           "Third dimension should be number of lines")
            self.assertEqual(n_pixel, clsm_split.n_pixel, 
                           "Fourth dimension should be number of pixels")
            
            # Total frames should equal n_channels * frames_per_channel
            self.assertEqual(clsm_split.n_frames, n_channels * frames_per_channel,
                           "Total frames should equal n_channels * frames_per_channel")
            
            # Verify __repr__ also shows correct shape
            repr_str = str(clsm_split)
            print(f"String representation: {repr_str}")
            self.assertIn(str(n_channels), repr_str, "Repr should include number of channels")
        else:
            # If only one channel detected, shape should still be 3D
            self.assertEqual(len(shape_split), 3,
                           "Shape should have 3 dimensions when only one channel is present")

    def test_channel_indexing(self):
        """Test that channel indexing works correctly with split_by_channel."""
        if not os.path.exists(ht3_filename):
            self.skipTest(f"Data file not found: {ht3_filename}")
        
        # Load data
        data = tttrlib.TTTR(ht3_filename, 'HT3')
        
        # Create CLSM image WITH split_by_channel
        clsm_split = tttrlib.CLSMImage(
            tttr_data=data,
            split_by_channel=True,
            **ht3_reading_parameter
        )
        clsm_split.fill(data, channels=[0, 2])
        
        if clsm_split.n_channels > 1:
            # Test channel indexing: clsm[ch][frame][line][pixel]
            for ch_idx in range(clsm_split.n_channels):
                channel_slice = clsm_split[ch_idx]
                n_frames_in_channel = len(channel_slice)
                print(f"Channel {ch_idx} has {n_frames_in_channel} frames")
                
                # Verify we can access frames within the channel
                if n_frames_in_channel > 0:
                    frame = channel_slice[0]
                    self.assertIsNotNone(frame, f"Should be able to access frame 0 in channel {ch_idx}")


if __name__ == '__main__':
    unittest.main()
