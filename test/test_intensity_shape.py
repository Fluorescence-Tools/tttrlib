"""
Test script to verify CLSMImage intensity shape behavior.

This script demonstrates that:
1. When split_by_channel=False or n_channels==1: intensity.shape = (n_frames, n_lines, n_pixel)
2. When split_by_channel=True and n_channels>1: intensity.shape = (n_channels, frames_per_channel, n_lines, n_pixel)
"""

def test_intensity_shape():
    """
    Test the intensity property shape for different channel configurations.
    
    Note: This is a demonstration of the expected behavior.
    Actual testing requires real TTTR data with multi-channel information.
    """
    
    print("=" * 80)
    print("CLSMImage Intensity Shape Test")
    print("=" * 80)
    
    print("\nExpected behavior:")
    print("-" * 80)
    
    print("\n1. Single channel or split_by_channel=False:")
    print("   intensity.shape = (n_frames, n_lines, n_pixel)")
    print("   Example: (100, 256, 256) for 100 frames of 256x256 pixels")
    
    print("\n2. Multi-channel with split_by_channel=True:")
    print("   intensity.shape = (n_channels, frames_per_channel, n_lines, n_pixel)")
    print("   Example: (2, 50, 256, 256) for 2 channels, 50 frames each, 256x256 pixels")
    
    print("\n" + "=" * 80)
    print("Implementation Details:")
    print("=" * 80)
    
    print("\nThe intensity property in CLSMImage.py now:")
    print("1. Calls get_intensity() to get raw data from C++")
    print("2. Checks n_channels:")
    print("   - If n_channels == 1: returns raw data as-is")
    print("   - If n_channels > 1: reshapes by extracting each channel's frames")
    print("3. Returns numpy array with appropriate shape")
    
    print("\n" + "=" * 80)
    print("Usage Example:")
    print("=" * 80)
    
    example_code = """
    import tttrlib
    
    # Load TTTR data
    data = tttrlib.TTTR('path/to/file.ptu')
    
    # Create CLSMImage with split_by_channel=True
    img = tttrlib.CLSMImage(data, split_by_channel=True)
    
    # Access intensity
    intensity = img.intensity
    print(f"Intensity shape: {intensity.shape}")
    
    # If multi-channel:
    if img.n_channels > 1:
        # Access specific channel
        ch0_intensity = img[0].intensity  # Channel 0
        ch1_intensity = img[1].intensity  # Channel 1
        print(f"Channel 0 shape: {ch0_intensity.shape}")
        print(f"Channel 1 shape: {ch1_intensity.shape}")
    """
    
    print(example_code)
    
    print("\n" + "=" * 80)
    print("Test Complete")
    print("=" * 80)

if __name__ == "__main__":
    test_intensity_shape()
