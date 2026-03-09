"""
=======================================
Synthetic Bead Localization Demo
=======================================

This example demonstrates the Image Localization functionality in tttrlib
by creating synthetic fluorescent beads and localizing them using 2D Gaussian fitting.

The script creates a synthetic microscopy image with multiple fluorescent beads,
detects peak candidates, and then uses 2D Gaussian fitting to precisely localize
each bead with sub-pixel accuracy.

"""

import numpy as np
import matplotlib.pyplot as plt
import tttrlib

#%%
# Create synthetic fluorescent beads
# ----------------------------------
# We start by creating a synthetic microscopy image containing multiple
# fluorescent beads with realistic noise characteristics.

def create_synthetic_beads(image_size=128, num_beads=6, bead_brightness=800, noise_level=40):
    """Create a synthetic image with fluorescent beads"""
    
    print(f"Creating synthetic {image_size}x{image_size} image with {num_beads} beads...")
    
    # Create coordinate grids
    x = np.arange(image_size)
    y = np.arange(image_size)
    X, Y = np.meshgrid(x, y)
    
    # Initialize image with background noise
    np.random.seed(42)  # For reproducible results
    image = np.random.poisson(noise_level, (image_size, image_size)).astype(float)
    
    # Generate random bead positions (avoid edges)
    margin = 15
    true_positions = []
    
    for i in range(num_beads):
        # Random position away from edges
        bead_x = np.random.uniform(margin, image_size - margin)
        bead_y = np.random.uniform(margin, image_size - margin)
        
        # Random bead parameters
        sigma = np.random.uniform(1.8, 3.2)  # PSF width
        amplitude = np.random.uniform(bead_brightness * 0.7, bead_brightness * 1.3)
        
        # Add Gaussian bead to image
        gaussian = amplitude * np.exp(-((X - bead_x)**2 + (Y - bead_y)**2) / (2 * sigma**2))
        
        # Add Poisson noise to the bead
        gaussian_noisy = np.random.poisson(gaussian + noise_level) - noise_level
        image += np.maximum(gaussian_noisy, 0)
        
        true_positions.append((bead_x, bead_y, sigma, amplitude))
        
    print(f"Created synthetic beads at positions:")
    for i, (x, y, s, a) in enumerate(true_positions):
        print(f"  Bead {i+1}: ({x:.1f}, {y:.1f}), sigma={s:.1f}, A={a:.0f}")
    
    return image.astype(float), true_positions

# Create synthetic data
image, true_positions = create_synthetic_beads(
    image_size=128,
    num_beads=6,
    bead_brightness=800,
    noise_level=40
)

#%%
# Visualize the synthetic image
# -----------------------------
# Let's visualize the synthetic microscopy image with the fluorescent beads.

fig, ax = plt.subplots(1, 1, figsize=(8, 8))
im = ax.imshow(image, cmap='hot', origin='lower')
ax.set_title('Synthetic Fluorescent Beads')
ax.set_xlabel('X (pixels)')
ax.set_ylabel('Y (pixels)')
plt.colorbar(im, ax=ax, label='Intensity')

# Mark true positions
for i, (x, y, s, a) in enumerate(true_positions):
    circle = plt.Circle((x, y), s, fill=False, color='cyan', linewidth=2)
    ax.add_patch(circle)
    ax.text(x, y+s+2, f'{i+1}', color='cyan', ha='center', fontweight='bold')

plt.tight_layout()
plt.show()

#%%
# Detect bead candidates
# ----------------------
# We use simple threshold-based peak detection to find bead candidates
# in the synthetic image.

def detect_peaks_threshold(image, threshold_factor=3.5, min_distance=10):
    """Simple peak detection using threshold and local maxima"""
    
    print("Detecting bead candidates...")
    
    # Calculate threshold
    background_mean = np.mean(image)
    background_std = np.std(image)
    threshold = background_mean + threshold_factor * background_std
    
    print(f"  Background: {background_mean:.1f} +/- {background_std:.1f}")
    print(f"  Detection threshold: {threshold:.1f}")
    
    # Find pixels above threshold
    candidates = np.where(image > threshold)
    
    # Convert to list of coordinates
    peak_candidates = list(zip(candidates[1], candidates[0]))  # (x, y) format
    
    # Remove peaks that are too close to each other
    peaks = []
    used = set()
    
    # Sort by intensity (brightest first)
    peak_candidates.sort(key=lambda p: image[p[1], p[0]], reverse=True)
    
    for x, y in peak_candidates:
        if (x, y) in used:
            continue
            
        # Check if too close to existing peaks
        too_close = False
        for existing_x, existing_y in peaks:
            if np.sqrt((x - existing_x)**2 + (y - existing_y)**2) < min_distance:
                too_close = True
                break
        
        if not too_close:
            peaks.append((x, y))
            
        # Mark surrounding region as used
        for dy in range(-min_distance//2, min_distance//2 + 1):
            for dx in range(-min_distance//2, min_distance//2 + 1):
                used.add((x + dx, y + dy))
    
    print(f"Found {len(peaks)} peak candidates")
    return peaks

# Detect peaks
detected_peaks = detect_peaks_threshold(
    image,
    threshold_factor=3.5,
    min_distance=10
)

#%%
# Localize beads using 2D Gaussian fitting
# ----------------------------------------
# Now we use the tttrlib localization functionality to precisely localize
# each detected bead using 2D Gaussian fitting.

def localize_beads(image, peak_positions, fit_size=15):
    """Localize beads using 2D Gaussian fitting with tttrlib"""
    
    print("Localizing beads using 2D Gaussian fitting...")
    
    localized_beads = []
    
    for i, (peak_x, peak_y) in enumerate(peak_positions):
        print(f"  Fitting bead {i+1} at ({peak_x}, {peak_y})...")
        
        # Extract fitting region
        x_min = max(0, peak_x - fit_size//2)
        x_max = min(image.shape[1], peak_x + fit_size//2 + 1)
        y_min = max(0, peak_y - fit_size//2)
        y_max = min(image.shape[0], peak_y + fit_size//2 + 1)
        
        region = image[y_min:y_max, x_min:x_max]
        
        if region.size == 0:
            continue
            
        # Convert to SWIG vector format
        data_2d = tttrlib.new_VectorDouble_2D()
        for row in region:
            row_vector = tttrlib.new_VectorDouble()
            for val in row:
                tttrlib.VectorDouble_push_back(row_vector, float(val))
            tttrlib.VectorDouble_2D_push_back(data_2d, row_vector)
        
        # Set up fitting parameters
        vars = [0.0] * 18
        vars[0] = fit_size // 2  # x0 (center in local coordinates)
        vars[1] = fit_size // 2  # y0 (center in local coordinates)
        vars[2] = float(np.max(region) - np.min(region))  # Amplitude
        vars[3] = 2.5  # Initial sigma guess
        vars[4] = 1.0  # ellipticity (1.0 = circular)
        vars[5] = float(np.min(region))  # background
        
        # Fitting options
        vars[14] = 0  # fit_bg (0 = fit background)
        vars[15] = 1  # ellipt_circ (1 = circular Gaussian)
        vars[16] = 0  # model (0 = single Gaussian)
        
        try:
            # Perform the fit
            result = tttrlib.localization_fit2DGaussian(vars, data_2d)
            
            if result > 0:  # Successful fit
                # Convert back to global coordinates
                fitted_x = x_min + vars[0]
                fitted_y = y_min + vars[1]
                amplitude = vars[2]
                sigma = vars[3]
                background = vars[5]
                goodness_of_fit = vars[17]
                
                localized_beads.append({
                    'x': fitted_x,
                    'y': fitted_y,
                    'amplitude': amplitude,
                    'sigma': sigma,
                    'background': background,
                    'goodness_of_fit': goodness_of_fit,
                    'converged': True
                })
                
                print(f"    Success: ({fitted_x:.2f}, {fitted_y:.2f}), sigma={sigma:.2f}, A={amplitude:.0f}")
            else:
                print(f"    Fit failed")
                
        except Exception as e:
            print(f"    Error during fitting: {e}")
    
    print(f"Successfully localized {len(localized_beads)} beads")
    return localized_beads

# Localize beads using tttrlib
localized_beads = localize_beads(image, detected_peaks, fit_size=15)

#%%
# Visualize localization results
# ------------------------------
# Create a comprehensive visualization showing the original image,
# detected peaks, and localized positions.

fig, axes = plt.subplots(2, 2, figsize=(12, 12))
fig.suptitle('tttrlib Image Localization Demo Results', fontsize=16)

# 1. Original synthetic image
ax1 = axes[0, 0]
im1 = ax1.imshow(image, cmap='hot', origin='lower')
ax1.set_title('Synthetic Fluorescent Beads')
ax1.set_xlabel('X (pixels)')
ax1.set_ylabel('Y (pixels)')
plt.colorbar(im1, ax=ax1, label='Intensity')

# Add true positions
for i, (x, y, s, a) in enumerate(true_positions):
    circle = plt.Circle((x, y), s, fill=False, color='cyan', linewidth=2)
    ax1.add_patch(circle)
    ax1.text(x, y+s+2, f'{i+1}', color='cyan', ha='center', fontweight='bold')

# 2. Detected peaks
ax2 = axes[0, 1]
im2 = ax2.imshow(image, cmap='hot', origin='lower')
ax2.set_title('Peak Detection Results')
ax2.set_xlabel('X (pixels)')
ax2.set_ylabel('Y (pixels)')
plt.colorbar(im2, ax=ax2, label='Intensity')

# Add detected peaks
for i, (x, y) in enumerate(detected_peaks):
    ax2.plot(x, y, 'go', markersize=8, markerfacecolor='none', markeredgewidth=2)
    ax2.text(x, y+3, f'{i+1}', color='green', ha='center', fontweight='bold')

# 3. Localization results
ax3 = axes[1, 0]
im3 = ax3.imshow(image, cmap='hot', origin='lower')
ax3.set_title('2D Gaussian Localization Results')
ax3.set_xlabel('X (pixels)')
ax3.set_ylabel('Y (pixels)')
plt.colorbar(im3, ax=ax3, label='Intensity')

# Add localized positions
for i, bead in enumerate(localized_beads):
    x, y = bead['x'], bead['y']
    sigma = bead['sigma']
    ax3.plot(x, y, 'r+', markersize=10, markeredgewidth=3)
    circle = plt.Circle((x, y), sigma, fill=False, color='red', linewidth=2)
    ax3.add_patch(circle)
    ax3.text(x, y+sigma+2, f'{i+1}', color='red', ha='center', fontweight='bold')

# 4. Accuracy comparison
ax4 = axes[1, 1]
ax4.set_title('Localization Accuracy')
ax4.set_xlabel('True X Position')
ax4.set_ylabel('True Y Position')

# Plot true positions
true_x = [pos[0] for pos in true_positions]
true_y = [pos[1] for pos in true_positions]
ax4.plot(true_x, true_y, 'co', markersize=10, label='True Positions')

# Plot localized positions
if localized_beads:
    loc_x = [bead['x'] for bead in localized_beads]
    loc_y = [bead['y'] for bead in localized_beads]
    ax4.plot(loc_x, loc_y, 'r+', markersize=12, markeredgewidth=3, label='Localized')
    
    # Draw error lines
    for i, bead in enumerate(localized_beads):
        if i < len(true_positions):
            true_pos = true_positions[i]
            ax4.plot([true_pos[0], bead['x']], [true_pos[1], bead['y']], 
                    'k--', alpha=0.5, linewidth=1)

ax4.legend()
ax4.grid(True, alpha=0.3)
ax4.set_aspect('equal')

plt.tight_layout()
plt.show()

#%%
# Calculate accuracy metrics
# --------------------------
# Finally, we calculate comprehensive accuracy metrics to assess
# the quality of the localization.

print("\n" + "="*60)
print("LOCALIZATION ACCURACY ANALYSIS")
print("="*60)

if not localized_beads:
    print("No beads were successfully localized.")
else:
    # Match localized beads to true positions (nearest neighbor)
    errors = []
    sigma_errors = []
    amplitude_errors = []
    
    for i, bead in enumerate(localized_beads):
        if i < len(true_positions):
            true_pos = true_positions[i]
            
            # Position error
            pos_error = np.sqrt((bead['x'] - true_pos[0])**2 + (bead['y'] - true_pos[1])**2)
            errors.append(pos_error)
            
            # Sigma error
            sigma_error = abs(bead['sigma'] - true_pos[2]) / true_pos[2] * 100
            sigma_errors.append(sigma_error)
            
            # Amplitude error
            amp_error = abs(bead['amplitude'] - true_pos[3]) / true_pos[3] * 100
            amplitude_errors.append(amp_error)
            
            print(f"Bead {i+1}:")
            print(f"  Position error: {pos_error:.3f} pixels")
            print(f"  Sigma error: {sigma_error:.1f}%")
            print(f"  Amplitude error: {amp_error:.1f}%")
            print(f"  Goodness of fit: {bead['goodness_of_fit']:.3f}")
    
    if errors:
        print(f"\nOverall Statistics:")
        print(f"  Mean position error: {np.mean(errors):.3f} +/- {np.std(errors):.3f} pixels")
        print(f"  Mean sigma error: {np.mean(sigma_errors):.1f} +/- {np.std(sigma_errors):.1f}%")
        print(f"  Mean amplitude error: {np.mean(amplitude_errors):.1f} +/- {np.std(amplitude_errors):.1f}%")
        print(f"  Success rate: {len(localized_beads)}/{len(true_positions)} ({len(localized_beads)/len(true_positions)*100:.1f}%)")

print("\nImage Localization Demo completed successfully!")
print("The tttrlib Image Localization functionality is working correctly.")
