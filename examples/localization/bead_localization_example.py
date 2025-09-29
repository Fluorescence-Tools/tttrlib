#!/usr/bin/env python3
"""
Bead Localization Example using tttrlib Image Localization

This example demonstrates how to use the newly enabled Image Localization functionality
in tttrlib to localize fluorescent beads in TTTR data from a confocal microscope.

The script:
1. Loads TTTR data from a .ptu file
2. Creates a CLSM image from the TTTR data
3. Applies peak detection to find bead candidates
4. Uses 2D Gaussian fitting to precisely localize the beads
5. Visualizes the results with detected bead positions

Data file: V:\tttr-data\imaging\pq\Microtime200_HH400 beads.ptu
"""

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import Circle
import tttrlib

def load_and_process_tttr_data(filename):
    """
    Load TTTR data and create a CLSM image
    
    Args:
        filename (str): Path to the .ptu file
        
    Returns:
        tuple: (tttr_data, clsm_image)
    """
    print(f"Loading TTTR data from: {filename}")
    
    # Load TTTR data
    tttr_data = tttrlib.TTTR(filename, 'PTU')
    
    print(f"Loaded {len(tttr_data)} photons")
    # Get measurement info (use available header attributes)
    try:
        n_records = len(tttr_data)
        macro_time_res = tttr_data.header.macro_time_resolution
        measurement_time = macro_time_res * n_records
        print(f"Measurement time: {measurement_time:.2f} seconds")
    except Exception as e:
        print(f"Could not determine measurement time: {e}")
    
    # Create CLSM image
    clsm_image = tttrlib.CLSMImage(tttr_data)
    
    print(f"CLSM image dimensions: {clsm_image.shape}")
    print(f"Number of frames: {len(clsm_image.frames)}")
    
    return tttr_data, clsm_image

def detect_peaks_simple(intensity_image, threshold_factor=3.0, min_distance=5):
    """
    Simple peak detection algorithm to find bead candidates
    
    Args:
        intensity_image (np.array): 2D intensity image
        threshold_factor (float): Threshold as multiple of background std
        min_distance (int): Minimum distance between peaks in pixels
        
    Returns:
        list: List of (x, y) peak coordinates
    """
    # Calculate background statistics
    background_mean = np.mean(intensity_image)
    background_std = np.std(intensity_image)
    threshold = background_mean + threshold_factor * background_std
    
    print(f"Background: mean={background_mean:.1f}, std={background_std:.1f}")
    print(f"Peak threshold: {threshold:.1f}")
    
    # Find pixels above threshold
    peak_candidates = np.where(intensity_image > threshold)
    
    if len(peak_candidates[0]) == 0:
        print("No peaks found above threshold")
        return []
    
    # Simple clustering to merge nearby pixels
    peaks = []
    used = set()
    
    for i in range(len(peak_candidates[0])):
        y, x = peak_candidates[0][i], peak_candidates[1][i]
        
        if (y, x) in used:
            continue
            
        # Find local maximum in neighborhood
        y_min = max(0, y - min_distance//2)
        y_max = min(intensity_image.shape[0], y + min_distance//2 + 1)
        x_min = max(0, x - min_distance//2)
        x_max = min(intensity_image.shape[1], x + min_distance//2 + 1)
        
        region = intensity_image[y_min:y_max, x_min:x_max]
        local_max_idx = np.unravel_index(np.argmax(region), region.shape)
        peak_y = y_min + local_max_idx[0]
        peak_x = x_min + local_max_idx[1]
        
        # Check if too close to existing peaks
        too_close = False
        for existing_x, existing_y in peaks:
            if np.sqrt((peak_x - existing_x)**2 + (peak_y - existing_y)**2) < min_distance:
                too_close = True
                break
        
        if not too_close:
            peaks.append((peak_x, peak_y))
            
        # Mark region as used
        for dy in range(-min_distance//2, min_distance//2 + 1):
            for dx in range(-min_distance//2, min_distance//2 + 1):
                used.add((y + dy, x + dx))
    
    print(f"Found {len(peaks)} peak candidates")
    return peaks

def fit_gaussian_peaks(intensity_image, peak_positions):
    """
    Fit 2D Gaussians to detected peaks using tttrlib localization
    
    Args:
        intensity_image (np.array): 2D intensity image
        peak_positions (list): List of (x, y) peak coordinates
        
    Returns:
        list: List of fitted peak parameters
    """
    print("Fitting 2D Gaussians to detected peaks...")
    
    fitted_peaks = []
    
    for i, (peak_x, peak_y) in enumerate(peak_positions):
        print(f"Fitting peak {i+1}/{len(peak_positions)} at ({peak_x}, {peak_y})")
        
        # Extract region around peak for fitting
        fit_size = 15  # Size of fitting region
        x_min = max(0, peak_x - fit_size//2)
        x_max = min(intensity_image.shape[1], peak_x + fit_size//2 + 1)
        y_min = max(0, peak_y - fit_size//2)
        y_max = min(intensity_image.shape[0], peak_y + fit_size//2 + 1)
        
        region = intensity_image[y_min:y_max, x_min:x_max]
        
        if region.size == 0:
            continue
            
        # Convert to format expected by localization using SWIG vector types
        data_2d = tttrlib.VectorDouble_2D()
        for row in region:
            row_vector = tttrlib.VectorDouble()
            for val in row:
                row_vector.append(float(val))
            data_2d.append(row_vector)
        
        # Initial parameters for 2D Gaussian fit
        # vars = [x0, y0, A, sigma, ellipticity, bg, x1, y1, A1, x2, y2, A2, 
        #         info, wi_nowi, fit_bg, ellipt_circ, model, twoIstar]
        vars = [0.0] * 18
        
        # Initial guess
        vars[0] = fit_size // 2  # x0 (center in local coordinates)
        vars[1] = fit_size // 2  # y0 (center in local coordinates)
        vars[2] = float(np.max(region) - np.min(region))  # Amplitude
        vars[3] = 2.0  # sigma
        vars[4] = 1.0  # ellipticity (1.0 = circular)
        vars[5] = float(np.min(region))  # background
        
        # Fitting options
        vars[14] = 0  # fit_bg (0 = fit background)
        vars[15] = 1  # ellipt_circ (1 = circular Gaussian)
        vars[16] = 0  # model (0 = single Gaussian)
        
        try:
            # Perform the fit using tttrlib localization
            localization = tttrlib.localization()
            result = localization.fit2DGaussian(vars, data_2d)
            
            if result > 0:  # Successful fit
                # Convert back to global coordinates
                fitted_x = x_min + vars[0]
                fitted_y = y_min + vars[1]
                amplitude = vars[2]
                sigma = vars[3]
                background = vars[5]
                chi2 = vars[17] if len(vars) > 17 else 0
                
                fitted_peaks.append({
                    'x': fitted_x,
                    'y': fitted_y,
                    'amplitude': amplitude,
                    'sigma': sigma,
                    'background': background,
                    'chi2': chi2,
                    'original_x': peak_x,
                    'original_y': peak_y
                })
                
                print(f"  Fitted: ({fitted_x:.2f}, {fitted_y:.2f}), "
                      f"σ={sigma:.2f}, A={amplitude:.1f}")
            else:
                print(f"  Fit failed for peak at ({peak_x}, {peak_y})")
                
        except Exception as e:
            print(f"  Error fitting peak at ({peak_x}, {peak_y}): {e}")
    
    print(f"Successfully fitted {len(fitted_peaks)} peaks")
    return fitted_peaks

def visualize_results(intensity_image, peak_positions, fitted_peaks):
    """
    Visualize the localization results
    
    Args:
        intensity_image (np.array): 2D intensity image
        peak_positions (list): Original peak positions
        fitted_peaks (list): Fitted peak parameters
    """
    fig, axes = plt.subplots(1, 2, figsize=(15, 6))
    
    # Plot 1: Original image with detected peaks
    ax1 = axes[0]
    im1 = ax1.imshow(intensity_image, cmap='hot', origin='lower')
    ax1.set_title('Detected Peak Positions')
    ax1.set_xlabel('X (pixels)')
    ax1.set_ylabel('Y (pixels)')
    
    # Mark detected peaks
    for x, y in peak_positions:
        circle = Circle((x, y), 3, fill=False, color='cyan', linewidth=2)
        ax1.add_patch(circle)
    
    plt.colorbar(im1, ax=ax1, label='Intensity (counts)')
    
    # Plot 2: Image with fitted peak positions
    ax2 = axes[1]
    im2 = ax2.imshow(intensity_image, cmap='hot', origin='lower')
    ax2.set_title('Fitted Peak Positions')
    ax2.set_xlabel('X (pixels)')
    ax2.set_ylabel('Y (pixels)')
    
    # Mark fitted peaks
    for peak in fitted_peaks:
        # Original position (red circle)
        circle_orig = Circle((peak['original_x'], peak['original_y']), 3, 
                           fill=False, color='red', linewidth=1, linestyle='--')
        ax2.add_patch(circle_orig)
        
        # Fitted position (green cross)
        ax2.plot(peak['x'], peak['y'], 'g+', markersize=10, markeredgewidth=2)
        
        # Sigma circle
        circle_sigma = Circle((peak['x'], peak['y']), peak['sigma'], 
                            fill=False, color='green', linewidth=1, alpha=0.7)
        ax2.add_patch(circle_sigma)
    
    plt.colorbar(im2, ax=ax2, label='Intensity (counts)')
    
    plt.tight_layout()
    plt.show()
    
    # Print summary statistics
    print("\n=== Localization Results ===")
    print(f"Total beads detected: {len(peak_positions)}")
    print(f"Successfully fitted: {len(fitted_peaks)}")
    
    if fitted_peaks:
        sigmas = [p['sigma'] for p in fitted_peaks]
        amplitudes = [p['amplitude'] for p in fitted_peaks]
        
        print(f"Average bead size (σ): {np.mean(sigmas):.2f} ± {np.std(sigmas):.2f} pixels")
        print(f"Average amplitude: {np.mean(amplitudes):.1f} ± {np.std(amplitudes):.1f} counts")
        
        print("\nIndividual bead results:")
        for i, peak in enumerate(fitted_peaks):
            print(f"Bead {i+1}: ({peak['x']:.2f}, {peak['y']:.2f}), "
                  f"σ={peak['sigma']:.2f}, A={peak['amplitude']:.1f}")

def main():
    """
    Main function to run the bead localization example
    """
    # Data file path
    data_file = r"V:\tttr-data\imaging\pq\Microtime200_HH400 beads.ptu"
    
    try:
        # Load and process TTTR data
        tttr_data, clsm_image = load_and_process_tttr_data(data_file)
        
        # Get intensity image (sum over all frames)
        intensity_image = clsm_image.get_intensity()
        
        print(f"Intensity image shape: {intensity_image.shape}")
        print(f"Intensity range: {np.min(intensity_image)} - {np.max(intensity_image)}")
        
        # Detect peaks
        peak_positions = detect_peaks_simple(intensity_image, threshold_factor=2.5)
        
        if not peak_positions:
            print("No peaks detected. Try adjusting the threshold_factor.")
            return
        
        # Fit Gaussian peaks
        fitted_peaks = fit_gaussian_peaks(intensity_image, peak_positions)
        
        # Visualize results
        visualize_results(intensity_image, peak_positions, fitted_peaks)
        
        print("\nBead localization completed successfully!")
        
    except FileNotFoundError:
        print(f"Error: Could not find data file: {data_file}")
        print("Please check that the file path is correct and the file exists.")
    except Exception as e:
        print(f"Error during processing: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
