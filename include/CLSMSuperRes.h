/*
 * CLSMSuperRes.h
 *
 * Photon-level eSRRF (enhanced Super-Resolution Radial Fluctuations) for CLSM data.
 *
 * This module implements the Henriques lab eSRRF algorithms ported from camera-frame
 * data to single-photon TTTR data. The key idea is to use the Radial Gradient
 * Convergence (RGC) field as a spatial probability density and redistribute individual
 * photons onto a finer raster.
 *
 * Ported from:
 *   - junk/NanoJ-eSRRF/resources/liveSRRF.cl (calculateRadialGradientConvergence)
 *   - junk/NanoJ-eSRRF/resources/RadialGradientConvergence.cl
 *   - junk/NanoJ-eSRRF/src/nanoj/liveSRRF/LiveSRRF_CL.java
 *
 * Author: tttrlib development team
 * Date: 2025-01-31
 * License: Same as tttrlib (see LICENSE)
 */

#ifndef TTTRLIB_CLSMSUPERRES_H
#define TTTRLIB_CLSMSUPERRES_H

#include <vector>
#include <memory>

// Forward declarations
class TTTR;
class CLSMImage;

/**
 * @brief Super-resolution method selector.
 *
 * eSRRF (Radial Gradient Convergence reassignment) is one of a family of
 * photon-level super-resolution approaches. This enum selects which spatial
 * prior is used to redistribute photons. New methods (SOFI, ISM, etc.) are
 * added here as they are implemented.
 *
 *   - "esrrf"    : Radial Gradient Convergence (RGC) reassignment [default]
 *                  Ported from NanoJ-eSRRF (Laine & Heil, Nat. Methods 2023)
 *   - "uniform"  : No spatial prior — uniform upsampling within search radius
 *                  (baseline; equivalent to esrrf with sensitivity=0)
 *   - "sofi"     : SOFI-style reweighting [not yet implemented]
 *   - "ism"      : Image Scanning Microscopy shift [not yet implemented]
 */
enum class SuperResMethod {
    ESRRF,    // Radial Gradient Convergence reassignment
    UNIFORM,  // Uniform upsampling (no spatial prior)
    SOFI,     // SOFI-style (reserved)
    ISM       // Image Scanning Microscopy (reserved)
};

/**
 * @brief Photon-level super-resolution reassignment for CLSM data
 *
 * This class provides static methods for:
 *   - Computing RGC maps on magnified grids (the eSRRF spatial prior)
 *   - Reassigning photons to sub-pixel positions using a spatial prior
 *   - Temporal combination (AVG/VAR/TAC2) of reassigned frames
 *   - PTU output with magnified raster
 *
 * eSRRF is the first and default method; the framework supports adding more
 * (SOFI, ISM variants) via SuperResMethod.
 *
 * All methods are static and work on plain C arrays for SWIG compatibility.
 * Output arrays are malloc()ed and ownership is transferred to the caller
 * (numpy will free() them via ARGOUTVIEWM typemaps).
 */
class CLSMSuperRes {
public:
    CLSMSuperRes() = default;
    ~CLSMSuperRes() = default;

    // ========================================================================
    // RGC Map Computation
    // ========================================================================

    /**
     * @brief Compute Radial Gradient Convergence (RGC) map on magnified grid
     *
     * @param img Input intensity image (ny, nx) in row-major order
     * @param nx Native image width (pixels)
     * @param ny Native image height (pixels)
     * @param magnification Magnification factor M (output is M*ny by M*nx)
     * @param fwhm PSF FWHM in native pixels (NanoJ calls this "Radius")
     * @param sensitivity Exponent applied to normalized RGC (higher = sharper)
     * @param intensity_weighting If true, multiply RGC by interpolated intensity
     * @param output Output RGC array (M*ny * M*nx), caller must free()
     * @param n_output1 Output height (= M*ny)
     * @param n_output2 Output width (= M*nx)
     */
    static void rgc_map(
        const double* img,
        int nx,
        int ny,
        int magnification,
        double fwhm,
        int sensitivity,
        bool intensity_weighting,
        double** output,
        int* n_output1,
        int* n_output2
    );

    // Overload returning the array directly (for SWIG)
    static double* rgc_map(
        const double* img,
        int nx,
        int ny,
        int magnification,
        double fwhm,
        int sensitivity,
        bool intensity_weighting
    );

    // ========================================================================
    // Photon Reassignment
    // ========================================================================

    /**
     * @brief Reassign photons to new positions using RGC as spatial prior
     *
     * For each photon, sample a new (x, y) position from the RGC-weighted
     * distribution within the search radius. Micro times and routing channels
     * are preserved unchanged.
     *
     * @param clsm CLSMImage containing the photons (must be filled)
     * @param tttr TTTR object for macro time access
     * @param magnification Magnification factor M
     * @param fwhm PSF FWHM in native pixels
     * @param sensitivity RGC sensitivity exponent
     * @param search_radius Search radius in native pixels (default: fwhm/2)
     * @param channel_mode "merged" or "split" (see below)
     * @param method Super-resolution method (default "esrrf"); see SuperResMethod
     * @param seed RNG seed for reproducibility (controlled by TTTR_RNG_SEED if 0)
     *
     * channel_mode:
     *   - "merged": one RGC field from sum over all channels, all photons sample from it
     *   - "split": one RGC field per channel, photons use only their own channel's field
     *
     * Returns:
     *   New TTTR object with reassigned photons (caller owns the memory)
     */
    static TTTR* reassign_photons(
        CLSMImage* clsm,
        TTTR* tttr,
        int magnification,
        double fwhm,
        int sensitivity,
        double search_radius,
        const char* channel_mode,  // "merged" or "split"
        unsigned long long seed,
        const char* method = "esrrf"  // super-res method: "esrrf", "uniform", ...
    );

    // ========================================================================
    // Temporal Combination (AVG/VAR/TAC2)
    // ========================================================================

    /**
     * @brief Combine RGC-weighted frames with SOFI-like temporal statistics
     *
     * @param stack Stack of per-frame RGC (or intensity-weighted RGC) images
     *               (n_frames, ny, nx) in row-major order
     * @param n_frames Number of frames
     * @param ny Frame height
     * @param nx Frame width
     * @param mode Combination mode: "AVG", "VAR", "TAC2", or "INT"
     * @param output Output combined image (ny, nx), caller must free()
     */
    static void temporal_combine(
        const double* stack,
        int n_frames,
        int ny,
        int nx,
        const char* mode,
        double** output
    );

    // Overload returning the array directly
    static double* temporal_combine(
        const double* stack,
        int n_frames,
        int ny,
        int nx,
        const char* mode
    );

    // ========================================================================
    // Public Photon-Position Seam
    // ========================================================================

    /**
     * @brief Get per-photon positions from a CLSMImage (exact fractional x)
     *
     * This exposes the photon position stream that for_each_mask_photon uses
     * internally, making exact-x photon positions available for reassignment.
     *
     * @param clsm CLSMImage containing the photons (must be filled)
     * @param tttr TTTR object for macro time access
     * @param out_frame Output frame indices (n_photons), caller must free()
     * @param out_line Output line indices (n_photons), caller must free()
     * @param out_x_exact Output exact fractional x coordinates (n_photons), caller must free()
     * @param out_y_line Output y line coordinates (n_photons), caller must free()
     * @param out_event_idx Output TTTR event indices (n_photons), caller must free()
     * @param n_photons Output number of photons
     */
    static void get_photon_positions(
        CLSMImage* clsm,
        TTTR* tttr,
        int** out_frame,
        int** out_line,
        double** out_x_exact,
        double** out_y_line,
        int** out_event_idx,
        int* n_photons
    );

    // ========================================================================
    // PTU Output with Magnified Raster
    // ========================================================================

    /**
     * @brief Build a PTU file with magnified raster from reassigned photons
     *
     * Synthesizes frame/line markers for an (M*nx) x (M*ny) raster and places
     * each photon at line_start + subpixel_index * dwell'. Micro times and routing
     * channels are copied through.
     *
     * Tradeoff: position is faithfully preserved, but macro time is synthetic.
     * Correlation/FCS on the raster timescale is not meaningful on the output.
     *
     * @param tttr_reassigned Reassigned TTTR object
     * @param nx Native width
     * @param ny Native height
     * @param magnification Magnification factor M
     * @param pixel_duration Dwell time per magnified pixel (macro time units)
     * @param line_duration Total time per magnified line
     * @param frame_marker_delay Delay between frames
     * @param output_filename Path for output PTU file
     *
     * @return true on success
     */
    static bool write_ptu_magnified(
        TTTR* tttr_reassigned,
        int nx,
        int ny,
        int magnification,
        int pixel_duration,
        int line_duration,
        int frame_marker_delay,
        const char* output_filename
    );
};

#endif // TTTRLIB_CLSMSUPERRES_H
