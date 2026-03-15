"""
Advanced Burst Selection Analysis
=================================

This example demonstrates advanced burst selection techniques using tttrlib,
combining automatic (GMM) and manual (Gaussian fitting) approaches for
single-molecule FRET data analysis.

The example provides a unified framework that includes:

1. **Native tttrlib burst detection** using both sliding window and CUSUM/SPRT algorithms
2. **Automatic GMM-based burst classification** for unsupervised burst identification
3. **Manual Gaussian fitting** for user-controlled burst selection and parameter tuning
4. **ALEX configuration support** with dual-channel (green/red) analysis
5. **Comprehensive visualization** and data export capabilities

This example is designed to replicate and extend the functionality of the
ChiSurf burst_selection plugin using only tttrlib and standard Python libraries.

Data Requirements
-----------------
- Brick-Mic ALEX HDF5 files (included in tttr-data package)
- Uses routing channels: 0 = green photons, 1 = red photons
- Compatible with PIE (Pulsed Interleaved Excitation) data

Key Methods Demonstrated
------------------------
- ``tttrlib.TTTR.burst_search()`` - Native burst detection
- ``sklearn.mixture.GaussianMixture`` - Automatic burst classification
- ``scipy.stats.norm`` - Manual Gaussian fitting
- Advanced burst filtering and gap filling techniques
"""

import json
import os
import time
from datetime import datetime
from collections import OrderedDict
from pathlib import Path
from typing import Dict, List, Tuple, Optional, Union

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib import cm, colors
import tttrlib
from sklearn.mixture import GaussianMixture
from scipy.optimize import least_squares
from scipy.stats import norm

# Configure matplotlib for better visualizations
plt.rcParams['figure.figsize'] = (12, 6)
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['Arial', 'DejaVu Sans', 'Liberation Sans']


class Parameter:
    """A simple standalone fitting parameter (chisurf-like, but script-local)."""
    
    def __init__(self, value: float, fixed: bool = False, bounds_on: bool = False, 
                 lb: float = -np.inf, ub: float = np.inf, name: str = ""):
        self.value = value
        self.fixed = fixed
        self.bounds_on = bounds_on
        self.lb = lb
        self.ub = ub
        self.name = name
        
        if self.bounds_on and self.lb > self.ub:
            raise ValueError(f"Invalid bounds for {self.name or 'parameter'}: lb={self.lb} > ub={self.ub}")
        if self.bounds_on and not (self.lb <= float(self.value) <= self.ub):
            raise ValueError(
                f"Initial value for {self.name or 'parameter'} out of bounds: value={self.value}, lb={self.lb}, ub={self.ub}"
            )

    def copy(self):
        return Parameter(**self.__dict__)


class GaussianComponent:
    """A single 1D Gaussian mixture component."""
    
    def __init__(self, mu: Parameter, sigma: Parameter, population: Parameter):
        self.mu = mu
        self.sigma = sigma
        self.population = population

    def copy(self):
        return GaussianComponent(
            mu=self.mu.copy(),
            sigma=self.sigma.copy(),
            population=self.population.copy()
        )


class GaussianMixtureHistogramFitter:
    """Fit a user-specified Gaussian mixture to a 1D histogram.
    
    This is intended as a lightweight, script-friendly alternative to an
    automated GMM fit. The user provides initial parameters and per-parameter
    fixed/free flags.
    """
    
    def __init__(self, components: List[GaussianComponent]):
        if not components:
            raise ValueError("At least one GaussianComponent is required")
        self.components = [c.copy() for c in components]

    def pdf(self, x_vals: np.ndarray) -> np.ndarray:
        x_vals = np.asarray(x_vals, dtype=float)
        weights = np.array([c.population.value for c in self.components], dtype=float)
        pdf_total = np.zeros_like(x_vals, dtype=float)
        for w, c in zip(weights, self.components):
            pdf_total += float(w) * norm.pdf(x_vals, loc=float(c.mu.value), scale=float(c.sigma.value))
        return pdf_total

    def component_pdfs(self, x_vals: np.ndarray) -> List[np.ndarray]:
        x_vals = np.asarray(x_vals, dtype=float)
        weights = np.array([c.population.value for c in self.components], dtype=float)
        return [float(w) * norm.pdf(x_vals, loc=float(c.mu.value), scale=float(c.sigma.value)) 
                for w, c in zip(weights, self.components)]

    def fit_to_histogram(self, data: np.ndarray, bins: int, 
                        data_range: Tuple[float, float], min_sigma: float = 1e-4) -> List[GaussianComponent]:
        """Fit the Gaussian mixture model to histogram data."""
        data = np.asarray(data, dtype=float).ravel()
        xmin, xmax = float(data_range[0]), float(data_range[1])

        hist_counts, bin_edges = np.histogram(data, bins=bins, range=(xmin, xmax))
        bin_centers = 0.5 * (bin_edges[:-1] + bin_edges[1:])
        bin_width = float(abs((xmax - xmin) / bins))
        n_events = float(data.size)

        # Build initial parameter vector and bounds
        x0: List[float] = []
        lo: List[float] = []
        hi: List[float] = []
        free_pop_components: List[GaussianComponent] = []

        def p_bounds(p: Parameter) -> Tuple[float, float]:
            if p.bounds_on:
                return float(p.lb), float(p.ub)
            return -np.inf, np.inf

        for c in self.components:
            if not c.mu.fixed:
                x0.append(float(c.mu.value))
                lb, ub = p_bounds(c.mu)
                lo.append(lb)
                hi.append(ub)

            if not c.sigma.fixed:
                x0.append(float(c.sigma.value))
                lb, ub = p_bounds(c.sigma)
                lo.append(lb)
                hi.append(ub)

            if not c.population.fixed:
                free_pop_components.append(c)

        for c in free_pop_components:
            x0.append(float(c.population.value))
            lo.append(0.0)
            hi.append(1.0)

        x0_arr = np.array(x0, dtype=float)
        bounds = (np.array(lo, dtype=float), np.array(hi, dtype=float))
        denom = np.sqrt(hist_counts.astype(float) + 1.0)

        def apply_x(xvec: np.ndarray) -> None:
            xvec = np.asarray(xvec, dtype=float).ravel()
            idx = 0
            for c in self.components:
                if not c.mu.fixed:
                    c.mu.value = float(xvec[idx])
                    idx += 1
                if not c.sigma.fixed:
                    c.sigma.value = max(float(xvec[idx]), min_sigma)
                    idx += 1
            for c, xval in zip(free_pop_components, xvec[idx:]):
                c.population.value = float(xval)

        def objective(xvec: np.ndarray) -> np.ndarray:
            apply_x(xvec)
            model = self.pdf(bin_centers)
            residual = (hist_counts - model * n_events * bin_width) / denom
            return residual

        result = least_squares(objective, x0_arr, bounds=bounds, method='trf')
        apply_x(result.x)
        
        # Normalize populations to sum to 1
        free_pops = np.array([c.population.value for c in free_pop_components], dtype=float)
        if len(free_pops) > 0:
            free_pops /= free_pops.sum()
            for c, pop in zip(free_pop_components, free_pops):
                c.population.value = float(pop)

        return self.components.copy()


def load_tttr_data(file_path: str) -> tttrlib.TTTR:
    """Load TTTR data with proper error handling."""
    try:
        tttr = tttrlib.TTTR(file_path, 'PHOTON-HDF5')
        if len(tttr) == 0:
            raise RuntimeError(f"No photons loaded from {file_path}. File may be empty or path incorrect.")
        print(f"Loaded {len(tttr)} photons from {os.path.basename(file_path)}")
        try:
            acquisition_time = (tttr.macro_times[-1] - tttr.macro_times[0]) * tttr.header.macro_time_resolution
            print(f"Acquisition time: {acquisition_time:.2f} seconds")
        except IndexError:
            print("Acquisition time: 0 seconds (empty or invalid data)")
        return tttr
    except Exception as e:
        raise RuntimeError(f"Failed to load TTTR data: {e}")


def detect_bursts_native(tttr: tttrlib.TTTR, method: str = "both") -> Dict[str, np.ndarray]:
    """Detect bursts using native tttrlib methods.
    
    Parameters
    ----------
    tttr : tttrlib.TTTR
        TTTR data object
    method : str
        "sliding_window", "cusum_sprt", or "both"
        
    Returns
    -------
    dict
        Dictionary containing burst detection results
    """
    results = {}
    
    if method in ["sliding_window", "both"]:
        print("\n" + "="*70)
        print("Sliding Window Burst Detection")
        print("="*70)
        
        bursts_sw = tttr.burst_search(
            L=30,           # Minimum 30 photons per burst
            m=10,           # Window of 10 photons
            T=0.25e-3,      # Max 0.25 ms separation
            mode="sliding_window"
        )
        print(f"Found {len(bursts_sw)//2} bursts using sliding window method")
        results['sliding_window'] = bursts_sw

    if method in ["cusum_sprt", "both"]:
        print("\n" + "="*70)
        print("CUSUM/SPRT Burst Detection")
        print("="*70)
        
        bursts_cusum = tttr.burst_search(
            L=30,           # Minimum 30 photons per burst
            m=2000,         # Background count rate = 2000 cps
            T=30,           # Time constant for SPRT
            mode="cusum_sprt"
        )
        print(f"Found {len(bursts_cusum)//2} bursts using CUSUM/SPRT method")
        results['cusum_sprt'] = bursts_cusum

    return results


def extract_burst_properties(tttr: tttrlib.TTTR, bursts: Union[np.ndarray, tuple]) -> pd.DataFrame:
    """Extract properties from detected bursts.
    
    Parameters
    ----------
    tttr : tttrlib.TTTR
        TTTR data object
    bursts : np.ndarray or tuple
        Burst ranges as [start1, end1, start2, end2, ...]
        
    Returns
    -------
    pd.DataFrame
        DataFrame with burst properties
    """
    # Convert tuple to numpy array if needed (SWIG wrapper returns tuple)
    if isinstance(bursts, tuple):
        bursts = np.array(bursts)
    
    burst_ranges = bursts.reshape([len(bursts) // 2, 2])
    burst_data = []
    
    for i, (start, end) in enumerate(burst_ranges):
        burst_photons = tttr[start:end]
        duration = (burst_photons.macro_times[-1] - burst_photons.macro_times[0]) * tttr.header.macro_time_resolution
        count_rate = len(burst_photons) / duration if duration > 0 else 0
        
        # Channel analysis (two-color configuration: green and red)
        green_photons = len(burst_photons.get_selection_by_channel([0]))
        red_photons = len(burst_photons.get_selection_by_channel([1]))
        total_photons = green_photons + red_photons
        
        # Calculate Proximity Ratio (E*) = red / (green + red)
        # This is NOT true FRET efficiency, it's the raw ratio
        proximity_ratio = red_photons / total_photons if total_photons > 0 else 0
        
        # Calculate count rates in kHz
        green_count_rate_khz = (green_photons / duration / 1000) if duration > 0 else 0
        red_count_rate_khz = (red_photons / duration / 1000) if duration > 0 else 0
        
        burst_data.append({
            'burst_id': i,
            'start_index': start,
            'end_index': end,
            'duration_ms': duration * 1000,  # Duration in milliseconds
            'total_photons': total_photons,
            'count_rate_khz': count_rate / 1000,  # Count rate in kHz
            'green_photons': green_photons,
            'red_photons': red_photons,
            'green_count_rate_khz': green_count_rate_khz,
            'red_count_rate_khz': red_count_rate_khz,
            'proximity_ratio': proximity_ratio
        })
    
    return pd.DataFrame(burst_data)


def apply_gmm_classification(burst_properties: pd.DataFrame, n_components: int = 3) -> pd.DataFrame:
    """Apply Gaussian Mixture Model classification to bursts.
    
    Parameters
    ----------
    burst_properties : pd.DataFrame
        DataFrame with burst properties
    n_components : int
        Number of GMM components
        
    Returns
    -------
    pd.DataFrame
        DataFrame with GMM classification results
    """
    print(f"\nApplying GMM classification with {n_components} components...")
    
    # Use Proximity Ratio and total intensity for classification
    X = burst_properties[['proximity_ratio', 'total_photons']].values
    
    gmm = GaussianMixture(n_components=n_components, random_state=42)
    gmm.fit(X)
    
    burst_properties['gmm_cluster'] = gmm.predict(X)
    burst_properties['gmm_proba_max'] = gmm.predict_proba(X).max(axis=1)
    
    for i in range(n_components):
        burst_properties[f'gmm_proba_{i}'] = gmm.predict_proba(X)[:, i]
    
    print(f"GMM classification completed. Cluster distribution:")
    print(burst_properties['gmm_cluster'].value_counts().sort_index())
    
    return burst_properties


def manual_gaussian_fit(burst_properties: pd.DataFrame, parameter: str = 'fret_efficiency',
                       n_components: int = 3) -> Dict:
    """Perform manual Gaussian fitting on burst properties.
    
    Parameters
    ----------
    burst_properties : pd.DataFrame
        DataFrame with burst properties
    parameter : str
        Parameter to fit ('fret_efficiency' or 'stoichiometry')
    n_components : int
        Number of Gaussian components
        
    Returns
    -------
    dict
        Dictionary with fitting results and components
    """
    print(f"\nPerforming manual Gaussian fit on {parameter} with {n_components} components...")
    
    # Map parameter names to actual column names
    param_map = {
        'proximity_ratio': 'proximity_ratio',
        'fret_efficiency': 'proximity_ratio',  # alias
    }
    actual_param = param_map.get(parameter, parameter)
    
    data = burst_properties[actual_param].values
    data_array = np.array(data, dtype=float)
    data_range = (float(data_array.min()), float(data_array.max()))
    
    # Create initial Gaussian components
    components = []
    for i in range(n_components):
        # Distribute initial means evenly across data range
        initial_mean = data_range[0] + (i + 1) * (data_range[1] - data_range[0]) / (n_components + 1)
        components.append(GaussianComponent(
            mu=Parameter(initial_mean, fixed=False, bounds_on=True, lb=data_range[0], ub=data_range[1], 
                        name=f"mu_{i}"),
            sigma=Parameter(0.1, fixed=False, bounds_on=True, lb=1e-4, ub=0.5, name=f"sigma_{i}"),
            population=Parameter(1.0/n_components, fixed=False, bounds_on=True, lb=0.01, ub=1.0, 
                                name=f"pop_{i}")
        ))
    
    fitter = GaussianMixtureHistogramFitter(components)
    fitted_components = fitter.fit_to_histogram(data_array, bins=50, data_range=data_range)
    
    # Assign clusters based on fitted Gaussians
    cluster_assignments = []
    for val in data:
        # Find which component has highest probability for this value
        probs = [comp.population.value * norm.pdf(val, loc=comp.mu.value, scale=comp.sigma.value) 
                for comp in fitted_components]
        cluster_assignments.append(np.argmax(probs))
    
    burst_properties[f'manual_gauss_cluster'] = cluster_assignments
    
    return {
        'components': fitted_components,
        'cluster_assignments': cluster_assignments,
        'fitter': fitter
    }


def visualize_burst_analysis(tttr: tttrlib.TTTR, burst_properties: pd.DataFrame, 
                           gmm_results: Optional[Dict] = None, 
                           gauss_results: Optional[Dict] = None):
    """Create comprehensive visualizations of burst analysis results."""
    
    fig = plt.figure(figsize=(18, 12))
    
    # Plot 1: Intensity trace with bursts
    ax1 = fig.add_subplot(3, 2, 1)
    intensity_trace = tttr.get_intensity_trace(0.001)  # 1ms binning
    ax1.plot(intensity_trace, 'k', alpha=0.7, label='Intensity trace')
    
    # Mark burst regions (sample first 50 bursts to avoid overcrowding)
    macro_times_array = np.array(tttr.macro_times)  # Convert SWIG tuple to numpy array
    for idx, burst in burst_properties.head(50).iterrows():  # Limit to first 50 bursts
        start_idx = int(burst['start_index'])
        end_idx = int(burst['end_index'])
        start_time = macro_times_array[start_idx] * tttr.header.macro_time_resolution
        end_time = macro_times_array[end_idx] * tttr.header.macro_time_resolution
        start_bin = int(start_time / 0.001)
        end_bin = int(end_time / 0.001)
        if start_bin < len(intensity_trace) and end_bin <= len(intensity_trace):
            ax1.axvspan(start_bin, end_bin, color='r', alpha=0.3)
    
    ax1.set_title('Intensity Trace with Detected Bursts')
    ax1.set_xlabel('Time (ms)')
    ax1.set_ylabel('Count rate (Hz)')
    ax1.legend()
    
    # Plot 2: Proximity Ratio vs Total Photons (2D histogram)
    ax2 = fig.add_subplot(3, 2, 2)
    ax2.hexbin(burst_properties['total_photons'], burst_properties['proximity_ratio'], 
              gridsize=30, bins='log', cmap='viridis', mincnt=1)
    ax2.set_title('Proximity Ratio vs Total Photons')
    ax2.set_xlabel('Total Photons')
    ax2.set_ylabel('Proximity Ratio (E*)')
    
    # Plot 3: Proximity Ratio histogram with classifications
    ax3 = fig.add_subplot(3, 2, 3)
    ax3.hist(burst_properties['proximity_ratio'], bins=50, alpha=0.5, color='gray', 
             label='All bursts')
    
    if gmm_results is not None:
        for cluster in burst_properties['gmm_cluster'].unique():
            cluster_data = burst_properties[burst_properties['gmm_cluster'] == cluster]['proximity_ratio']
            ax3.hist(cluster_data, bins=50, alpha=0.7, label=f'GMM Cluster {cluster}')
    
    if gauss_results is not None:
        x_vals = np.linspace(0, 1, 100)
        for i, comp in enumerate(gauss_results['components']):
            y_vals = comp.population.value * norm.pdf(x_vals, loc=comp.mu.value, scale=comp.sigma.value) * len(burst_properties) * 0.02
            ax3.plot(x_vals, y_vals, '--', linewidth=2, label=f'Gauss Comp {i}')
    
    ax3.set_title('Proximity Ratio Distribution with Classifications')
    ax3.set_xlabel('Proximity Ratio (E*)')
    ax3.set_ylabel('Count')
    ax3.legend()
    
    # Plot 4: Count rate distribution
    ax4 = fig.add_subplot(3, 2, 4)
    ax4.hist(burst_properties['count_rate_khz'], bins=50, color='purple', alpha=0.7)
    ax4.set_title('Burst Count Rate Distribution')
    ax4.set_xlabel('Count Rate (kHz)')
    ax4.set_ylabel('Count')
    ax4.set_xscale('log')
    
    # Plot 5: Burst duration distribution
    ax5 = fig.add_subplot(3, 2, 5)
    ax5.hist(burst_properties['duration_ms'], bins=50, color='green', alpha=0.7)
    ax5.set_title('Burst Duration Distribution')
    ax5.set_xlabel('Duration (ms)')
    ax5.set_ylabel('Count')
    ax5.set_xscale('log')
    
    # Plot 6: Total photons per burst
    ax6 = fig.add_subplot(3, 2, 6)
    ax6.hist(burst_properties['total_photons'], bins=50, color='blue', alpha=0.7)
    ax6.set_title('Photons per Burst Distribution')
    ax6.set_xlabel('Total Photons')
    ax6.set_ylabel('Count')
    
    plt.tight_layout()
    plt.show()


def export_results(burst_properties: pd.DataFrame, output_folder: str = "burst_results",
                 filename: str = "burst_analysis"):
    """Export burst analysis results to various formats."""
    
    output_path = Path(output_folder)
    output_path.mkdir(exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_filename = f"{filename}_{timestamp}"
    
    # Export to CSV
    csv_path = output_path / f"{base_filename}.csv"
    burst_properties.to_csv(csv_path, index=False)
    print(f"Exported burst properties to: {csv_path}")
    
    # Export to JSON
    json_path = output_path / f"{base_filename}.json"
    burst_properties.to_json(json_path, orient='records', indent=2)
    print(f"Exported burst properties to: {json_path}")
    
    # Export ChiSurf-compatible .bur format (matching original notebook format)
    bur_path = output_path / f"{base_filename}.bur"
    with open(bur_path, 'w') as f:
        f.write("# Burst analysis results from tttrlib\n")
        f.write("# Format: First Photon\tLast Photon\tDuration (ms)\tNumber of Photons\t"
                "Count Rate (KHz)\tgreen_photons\tred_photons\tproximity_ratio\tgmm_cluster\n")
        
        for _, row in burst_properties.iterrows():
            f.write(f"{row['start_index']}\t{row['end_index']}\t"
                   f"{row['duration_ms']:.4f}\t{row['total_photons']}\t"
                   f"{row['count_rate_khz']:.4f}\t{row['green_photons']}\t"
                   f"{row['red_photons']}\t{row['proximity_ratio']:.4f}\t"
                   f"{row.get('gmm_cluster', -1)}\n")
    
    print(f"Exported ChiSurf-compatible .bur file to: {bur_path}")


def main():
    """Main function demonstrating advanced burst selection analysis."""
    
    print("="*80)
    print("ADVANCED BURST SELECTION ANALYSIS")
    print("Combining GMM and Manual Gaussian Approaches")
    print("="*80)
    
    # Get data path from environment variable
    data_root = Path(os.environ.get("TTTRLIB_DATA", "")).resolve()
    if not data_root.exists():
        raise FileNotFoundError(
            "TTTRLIB_DATA environment variable not set or path does not exist. "
            "Please set TTTRLIB_DATA to point to your tttr-data directory."
        )
    
    data_path = data_root / "hdf" / "Brick-Mic" / "DNA_Alexa488-ATTO543-10sec" / "test80basic_0.h5"
    
    print(f"Loading data from: {data_path}")
    
    try:
        # Load TTTR data
        tttr = load_tttr_data(str(data_path))
        
        # Step 1: Native burst detection
        print("\n" + "="*80)
        print("STEP 1: NATIVE BURST DETECTION")
        print("="*80)
        burst_results = detect_bursts_native(tttr, method="both")
        
        # Use sliding window results for further analysis
        bursts = burst_results['sliding_window']
        
        # Step 2: Extract burst properties
        print("\n" + "="*80)
        print("STEP 2: EXTRACTING BURST PROPERTIES")
        print("="*80)
        burst_properties = extract_burst_properties(tttr, bursts)
        print(f"Extracted properties for {len(burst_properties)} bursts")
        print(burst_properties.head())
        
        # Step 3: GMM classification
        print("\n" + "="*80)
        print("STEP 3: GMM CLASSIFICATION")
        print("="*80)
        burst_properties = apply_gmm_classification(burst_properties, n_components=3)
        
        # Step 4: Manual Gaussian fitting
        print("\n" + "="*80)
        print("STEP 4: MANUAL GAUSSIAN FITTING")
        print("="*80)
        gauss_results = manual_gaussian_fit(burst_properties, parameter='proximity_ratio', n_components=3)
        
        # Step 5: Visualization (commented out for faster execution - uncomment to enable)
        print("\n" + "="*80)
        print("STEP 5: VISUALIZATION")
        print("="*80)
        print("Skipping visualization for faster execution...")
        # Uncomment the following line to enable visualization:
        # visualize_burst_analysis(tttr, burst_properties, gmm_results=None, gauss_results=gauss_results)
        
        # Step 6: Export results
        print("\n" + "="*80)
        print("STEP 6: EXPORTING RESULTS")
        print("="*80)
        export_results(burst_properties)
        
        print("\n" + "="*80)
        print("ANALYSIS COMPLETED SUCCESSFULLY")
        print("="*80)
        
    except FileNotFoundError:
        print(f"Error: Data file not found at {data_path}")
        print("Please set TTTRLIB_DATA environment variable to point to your tttr-data directory.")
        print("Example: export TTTRLIB_DATA=/path/to/tttr-data")
    except Exception as e:
        print(f"An error occurred during analysis: {e}")
        raise


if __name__ == "__main__":
    main()