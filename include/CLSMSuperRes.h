/*
 * CLSMSuperRes.h
 *
 * Super-resolution reconstructions for CLSM data, in two families:
 *
 *  - Photon-level eSRRF (enhanced Super-Resolution Radial Fluctuations). The
 *    Henriques-lab algorithm, ported from camera frames to single-photon TTTR
 *    data: the Radial Gradient Convergence (RGC) field is used as a spatial
 *    probability density, and individual photons are redistributed onto a finer
 *    raster. Reference: NanoJ-eSRRF (liveSRRF.cl,
 *    RadialGradientConvergence.cl, LiveSRRF_CL.java); Laine et al.,
 *    Nat. Methods 20, 1949 (2023).
 *
 *  - Array-detector (ISM) reconstructions: shift-vector estimation by phase
 *    cross-correlation, adaptive pixel reassignment, and focus-ISM background
 *    rejection. Reference: BrightEyes-ISM (APR_lib.py, FocusISM_lib.py);
 *    Tortarolo et al., Nat. Commun. 13, 7929 (2022).
 *
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
 * Selects the spatial prior reassign_photons() samples a photon's new position
 * from.
 *
 *   - "esrrf"     : Radial Gradient Convergence (RGC) reassignment [default]
 *   - "uniform"   : No spatial prior -- uniform sampling within the search
 *                   radius. The baseline to compare eSRRF against.
 *   - "ism"       : Image Scanning Microscopy shift-reassignment. Each photon
 *                   is moved by half its detector element's offset before
 *                   sampling; with sensitivity <= 0 the shift is applied
 *                   directly, without an RGC prior.
 *   - "esrrf+ism" : the ISM shift, then RGC sampling around the shifted position
 *   - "sofi"      : reserved, not implemented -- requesting it raises
 */
enum class SuperResMethod {
    ESRRF,      // Radial Gradient Convergence reassignment
    UNIFORM,    // Uniform sampling (no spatial prior)
    SOFI,       // SOFI-style (reserved, not implemented)
    ISM,        // Image Scanning Microscopy shift-reassignment
    ESRRF_ISM   // Combined eSRRF + ISM (ISM shift + RGC radial convergence sampling)
};

/**
 * @brief Super-resolution reconstructions for CLSM data
 *
 * Photon-level eSRRF:
 *   - rgc_map: the Radial Gradient Convergence field on a magnified grid
 *   - reassign_photons: redistribute photons onto a finer raster
 *   - temporal_combine: AVG/VAR/TAC2 over a stack of per-frame fields
 *   - get_photon_positions: the exact fractional photon positions both build on
 *   - write: the reassigned stream as a magnified-raster TTTR file
 *
 * Array-detector (ISM) reconstructions:
 *   - shift_vectors: per-element registration shifts
 *   - apr_reconstruction: adaptive pixel reassignment
 *   - focus_reconstruction: in-focus / out-of-focus separation
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
        int ny,
        int nx,
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
        int ny,
        int nx,
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
        const char* method = "esrrf",  // super-res method: "esrrf", "uniform", "ism"
        const double* detector_offsets = nullptr,
        int n_detector_offsets = 0,
        double ism_shift_factor = 0.5
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
     * @param n_output1 Output height (= ny)
     * @param n_output2 Output width (= nx)
     */
    static void temporal_combine(
        const double* stack,
        int n_frames,
        int ny,
        int nx,
        const char* mode,
        double** output,
        int* n_output1,
        int* n_output2
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
    // Write Reassigned Photons to a Magnified-Raster TTTR File
    // ========================================================================

    /**
     * @brief Write reassigned photons to a TTTR file with magnified raster
     *
     * Decodes each photon's magnified flat position from the reassigned TTTR
     * macro times, synthesizes frame/line markers for the (M*nx) x (M*ny)
     * raster, and writes the stream to disk. The container type is inferred
     * from the output filename extension (PTU, HT3, ...); ImgHdr geometry tags
     * are emitted so the file reads back as a CLSMImage.
     *
     * Timing model:
     *   - frame f starts at frame_base = f * (my*line_duration + frame_marker_delay)
     *   - line l starts at line_base = frame_base + l * line_duration
     *   - a photon at subpixel x sits at line_base + x * pixel_duration
     *
     * Macro time is synthetic: position is faithfully preserved, but
     * correlation/FCS on the raster timescale is not meaningful on the output.
     *
     * @param tttr_reassigned Reassigned TTTR object (macro times encode the
     *                        magnified flat position frame*my*mx + y*mx + x)
     * @param nx Native width (pixels)
     * @param ny Native height (pixels)
     * @param magnification Magnification factor M
     * @param output_filename Path for the output file; the container type is
     *                        inferred from its extension
     * @param pixel_duration Dwell time per magnified pixel (macro time units);
     *                        if <= 0, defaults to 1
     * @param line_duration Total time per magnified line; if <= 0, defaults to
     *                      M*nx * pixel_duration
     * @param frame_marker_delay Extra macro-time gap inserted between frames
     *
     * @return true on success, false if the container type is unsupported
     */
    static bool write(
        TTTR* tttr_reassigned,
        int nx,
        int ny,
        int magnification,
        const char* output_filename,
        int pixel_duration = -1,
        int line_duration = -1,
        int frame_marker_delay = 0
    );

    // ========================================================================
    // Image Scanning Microscopy (ISM) - BrightEyes-ISM Algorithms
    // ========================================================================

    /**
     * @brief Shift vectors of an array-detector cube (BrightEyes-ISM ShiftVectors)
     *
     * Phase cross-correlates every detector element against a reference element
     * and returns the shift that registers it onto that reference. The images
     * are Hann-apodized and optionally denoised first, and the correlation peak
     * is refined to 1/usf of a pixel.
     *
     * @param data Detector cube, (n_det, ny, nx) or (ny, nx, n_det)
     * @param channels_last True when the detector axis is last
     * @param usf Upsampling factor of the peak refinement (1 = integer pixels)
     * @param ref_idx Reference detector element; < 0 selects the centre
     * @param filter_sigma Gaussian denoising before the correlation, in pixels
     * @param n_det Use only the first n_det channels; < 0 uses all
     * @param output (n_det, 2) shifts as (dy, dx) in pixels, row axis first,
     *               matching skimage/scipy. Caller owns the memory (std::free).
     */
    static void shift_vectors(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double** output, int* out_dim1, int* out_dim2,
        int usf = 10, int ref_idx = -1, double filter_sigma = 0.0,
        int n_det = -1
    );

    /**
     * @brief Adaptive Pixel Reassignment (APR-ISM) reconstruction
     *
     * Estimates a shift vector per detector element (see shift_vectors),
     * registers every channel image with its vector and sums them. Mirrors
     * BrightEyes-ISM APR_lib.APR.
     *
     * @param output (1, ny, nx) reassigned sum; caller owns the memory.
     */
    static void apr_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double** output, int* out_dim1, int* out_dim2, int* out_dim3,
        int usf = 10, int ref_idx = -1, double filter_sigma = 0.0,
        int n_det = -1
    );

    /**
     * @brief s2ISM: joint super-resolution and optical sectioning
     *
     * Adaptive maximum-likelihood deconvolution of an array-detector dataset
     * over a stack of axial planes (Zunino et al., Nat. Photonics 2025). The
     * detector array is treated as Nch images of one object seen through Nch
     * different PSFs, and a multi-image Richardson-Lucy iteration inverts them
     * jointly:
     *
     *   est_ch = sum_z obj_z (*) psf_z,ch ;  frac_ch = data_ch / est_ch ;
     *   obj_z <- obj_z * sum_ch frac_ch (corr) psf_z,ch
     *
     * Giving the object several axial planes with their own PSFs is what buys
     * the sectioning: out-of-focus signal is explained by the out-of-focus
     * planes instead of being smeared into the focal one. Unlike APR, this
     * needs a PSF model -- it does not derive one from the data.
     *
     * @param data Detector cube, (n_ch, ny, nx)
     * @param psf Per-plane, per-element PSF, (nz, n_ch, ny, nx), same ny/nx as
     *            the data and centred in the frame. Normalized internally.
     * @param max_iter Maximum Richardson-Lucy iterations
     * @param threshold Halt when the focal-plane photon count changes by less
     *                  than this fraction of the total, twice in a row
     * @param auto_stop Use that adaptive rule; otherwise run max_iter
     * @param init_from_sum Start from the channel sum shared over the planes
     *                      rather than a flat object
     * @param output (nz, ny, nx) object estimate; the focal plane is nz/2.
     *               Caller owns the memory (std::free).
     */
    static void s2ism_reconstruction(
        const double* data, int n_ch, int ny, int nx,
        const double* psf, int psf_nz, int psf_nch, int psf_ny, int psf_nx,
        double** output, int* out_dim1, int* out_dim2, int* out_dim3,
        int max_iter = 100, double threshold = 1e-3, bool auto_stop = false,
        bool init_from_sum = false
    );

    /**
     * @brief SOFISM: super-resolution optical fluctuation image scanning microscopy
     *
     * Combines the two independent resolution mechanisms of ISM and SOFI. At
     * every scan position the array detector records a short time series; for
     * each pair of detector elements the temporal cross-correlation of the
     * fluctuations is formed,
     *
     *   C_ij(r, tau) = 1/(N_t - tau) * sum_t dI_i(r,t) dI_j(r,t+tau),
     *
     * whose effective PSF is the product of the two elements' PSFs (narrower by
     * sqrt(2) for Gaussians). The pair acts as one virtual detector midway
     * between the two elements, so it is reassigned by the mean of their ISM
     * shifts, v_ij = (v_i + v_j)/2, and the shifted correlation images are
     * summed. Sroda et al., Optica 7, 1308 (2020).
     *
     * The contrast comes from emitters blinking *independently*: cross-terms
     * between different emitters average away, which is what leaves a
     * per-emitter, PSF-squared response. Data without genuine fluctuations
     * (a static sample, or shot noise alone) carries no SOFI signal.
     *
     * @param data Photon counts, (n_time, n_det, ny, nx): the time series
     *             recorded at each scan position, one plane per detector element
     * @param lag Correlation delay tau, in time bins. 0 is the variance-like
     *            zero-lag cumulant; a small non-zero lag suppresses the
     *            uncorrelated shot-noise spike at tau = 0.
     * @param usf Upsampling factor of the shift estimate (see shift_vectors)
     * @param ref_idx Reference detector element; < 0 selects the centre
     * @param filter_sigma Gaussian denoising before the shift correlation only
     * @param include_auto Include the i == j autocorrelation terms. Off by
     *                     default: their shot noise does not cancel and enters
     *                     the result as a bias.
     * @param output (ny, nx) SOFISM image; caller owns the memory (std::free)
     */
    static void sofism_reconstruction(
        const double* data,
        int n_time, int n_det, int ny, int nx,
        double** output, int* out_dim1, int* out_dim2,
        int lag = 0, int usf = 10, int ref_idx = -1, double filter_sigma = 0.0,
        bool include_auto = false
    );

    /**
     * @brief Focus-ISM: separate in-focus signal from out-of-focus background
     *
     * After an APR pass, every pixel's micro-image (its distribution over the
     * detector array) is fitted with two Gaussians sharing the array centre --
     * a narrow in-focus one whose width is calibrated from the fingerprint of a
     * central patch, and a wider free one for the background. Mirrors
     * BrightEyes-ISM FocusISM_lib.focusISM / pixel_fit_2.
     *
     * @param sigma_bound Lower bound of the background width, in units of the
     *                    in-focus width
     * @param threshold Pixels whose total photon count does not exceed this are
     *                  assigned wholly to the background
     * @param calibration_size Side of the central patch the in-focus
     *                         fingerprint is calibrated on, in pixels
     * @param parallelize Fit pixel rows in parallel
     * @param detector_coords Optional (n_det, 2) element coordinates. Without
     *                        them a square lattice is assumed, and a non-square
     *                        channel count is an error.
     * @param output (3, ny, nx): in-focus signal, background, APR sum
     */
    static void focus_reconstruction(
        const double* data,
        int dim0, int dim1, int dim2,
        bool channels_last,
        double** output, int* out_dim1, int* out_dim2, int* out_dim3,
        double sigma_bound = 2.0, double threshold = 0.0, int calibration_size = 10,
        bool parallelize = false,
        int n_det = -1,
        const double* detector_coords = nullptr, int detector_coords_len = 0
    );
};

#endif // TTTRLIB_CLSMSUPERRES_H
