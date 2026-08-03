/*
 * CLSMSuperRes.cpp
 *
 * Photon-level eSRRF and the array-detector (ISM) reconstructions.
 *
 * See CLSMSuperRes.h for detailed documentation.
 */

#include "CLSMSuperRes.h"
#include "CLSMImage.h"
#include "TTTR.h"
#include "TTTRHeader.h"  // TTTRHeader::add_tag / set_*_resolution / tyInt8 etc.
#include "FileCheck.h"   // inferTTTRContainerTypeFromExtension
#include "Random.h"      // centralized counter-based RNG (Philox, PCG, SplitMix64, MT19937)
#include "info.h"        // cpu_features::configure_openmp, AVX/NEON dispatch macros
#include <cmath>
#include <algorithm>
#include <complex>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cctype>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <utility>

// Single-header FFT used by the ISM reconstructions
#include "pocketfft/pocketfft_hdronly.h"

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884L
#endif

// ========================================================================
// Internal helper: cubic interpolation kernel (Catmull-Rom)
// ========================================================================

namespace {
    double cubic(double x) {
        double a = 0.5;  // Catmull-Rom
        if (x < 0.0) x = -x;
        if (x < 1.0)
            return x * x * (x * (-a + 2.0) + (a - 3.0)) + 1.0;
        else if (x < 2.0)
            return -a * x * x * x + 5.0 * a * x * x - 8.0 * a * x + 4.0 * a;
        return 0.0;
    }

    // Interpolation at a continuous position (x, y), matching NanoJ's
    // getInterpolatedValue (liveSRRF.cl:38-129): bicubic in the interior,
    // bilinear extrapolation everywhere the 4x4 support does not fit.
    double interpolated_value(const double* img, int nx, int ny, double x, double y) {
        const int u0 = static_cast<int>(std::floor(x));
        const int v0 = static_cast<int>(std::floor(y));

        if (u0 > 0 && u0 < nx - 2 && v0 > 0 && v0 < ny - 2) {
            double q = 0.0;
            for (int j = 0; j <= 3; ++j) {
                const int v = std::max(0, std::min(ny - 1, v0 - 1 + j));
                double p = 0.0;
                for (int i = 0; i <= 3; ++i) {
                    const int u = std::max(0, std::min(nx - 1, u0 - 1 + i));
                    p += img[v * nx + u] * cubic(x - u);
                }
                q += p * cubic(y - v);
            }
            return q;
        }

        // Bilinear extrapolation on the last interior cell.
        const int xbase = std::max(0, std::min(nx - 2,
                static_cast<int>(std::min(static_cast<double>(nx - 2), std::max(x, 0.0)))));
        const int ybase = std::max(0, std::min(ny - 2,
                static_cast<int>(std::min(static_cast<double>(ny - 2), std::max(y, 0.0)))));
        const int xbase1 = std::min(nx - 1, xbase + 1);
        const int ybase1 = std::min(ny - 1, ybase + 1);
        const double xf = x - xbase;
        const double yf = y - ybase;
        const double lower_left  = img[ybase  * nx + xbase];
        const double lower_right = img[ybase  * nx + xbase1];
        const double upper_right = img[ybase1 * nx + xbase1];
        const double upper_left  = img[ybase1 * nx + xbase];
        const double upper = upper_left + xf * (upper_right - upper_left);
        const double lower = lower_left + xf * (lower_right - lower_left);
        return lower + yf * (upper - lower);
    }

    // Safe access with bounds checking (NanoJ getVBoundaryCheck)
    double safe_get(const double* arr, int width, int height, int x, int y) {
        x = std::max(0, std::min(width - 1, x));
        y = std::max(0, std::min(height - 1, y));
        return arr[y * width + x];
    }
}

// ========================================================================
// RGC Map Computation
// ========================================================================

void CLSMSuperRes::rgc_map(
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
) {
    // Derived parameters (from LiveSRRF_CL.java:266-345)
    double sigma = fwhm / 2.354;
    int gradient_mag = 2;
    double radius = (std::floor(gradient_mag * 2.0 * sigma) / gradient_mag) + 1.0;
    double tss = 2.0 * sigma * sigma;      // two sigma squared
    double tso = 2.0 * sigma + 1.0;         // two sigma plus one

    int mx = magnification * nx;
    int my = magnification * ny;

    // Allocate output
    double* rgc = static_cast<double*>(std::malloc(my * mx * sizeof(double)));
    if (!rgc) {
        throw std::runtime_error("Failed to allocate RGC output array");
    }
    std::memset(rgc, 0, my * mx * sizeof(double));

    // Compute gradient (2-point backward difference) at native resolution
    std::vector<double> Gx(static_cast<size_t>(nx) * ny, 0.0);
    std::vector<double> Gy(static_cast<size_t>(nx) * ny, 0.0);

    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            int x0 = std::max(x - 1, 0);
            int y0 = std::max(y - 1, 0);

            // 2-point backward difference
            Gx[y * nx + x] = img[y * nx + x] - img[y * nx + x0];
            Gy[y * nx + x] = img[y * nx + x] - img[y0 * nx + x];
        }
    }

    // 2x bicubic upsampling of the gradient (NanoJ calculateGradientInterpolation).
    // The source is the native-resolution gradient, so the interpolation clamps
    // against nx/ny -- reading a padded buffer here would blend in zeros along
    // the right and bottom borders.
    const int gx_size = gradient_mag * nx;
    const int gy_size = gradient_mag * ny;
    std::vector<double> Gx_up(static_cast<size_t>(gx_size) * gy_size, 0.0);
    std::vector<double> Gy_up(static_cast<size_t>(gx_size) * gy_size, 0.0);

    for (int yM = 0; yM < gy_size; ++yM) {
        for (int xM = 0; xM < gx_size; ++xM) {
            double x = xM / static_cast<double>(gradient_mag);
            double y = yM / static_cast<double>(gradient_mag);
            Gx_up[yM * gx_size + xM] = interpolated_value(Gx.data(), nx, ny, x, y);
            Gy_up[yM * gx_size + xM] = interpolated_value(Gy.data(), nx, ny, x, y);
        }
    }

    // Constants matching OpenCL
    double vxy_offset = 0.5;
    int vxy_ArrayShift = 1;
    double vxy_PixelShift = 0.0;  // drift correction, zero for static

    // OpenMP thread configuration (same idiom as CLSMImage.cpp)
#ifndef _WIN32
    int num_threads = tttrlib::cpu_features::configure_openmp(false);
    bool use_openmp = (num_threads > 1);
#else
    bool use_openmp = false;
#endif
    (void) use_openmp;

    // Main RGC loop (parallel over magnified pixels)
    #pragma omp parallel for schedule(dynamic) if(use_openmp && my * mx >= 10000)
    for (int yM = 0; yM < my; ++yM) {
        for (int xM = 0; xM < mx; ++xM) {
            // Continuous position in native space, centre of magnified pixel
            double xc = (xM + 0.5) / magnification + vxy_PixelShift;
            double yc = (yM + 0.5) / magnification + vxy_PixelShift;

            double CGLH = 0.0;
            double wSum = 0.0;

            // Sample gradient neighbourhood
            int j_min = -static_cast<int>(gradient_mag * radius);
            int j_max = static_cast<int>(gradient_mag * radius + 1);
            int i_min = j_min;
            int i_max = j_max;

            for (int j = j_min; j <= j_max; ++j) {
                // vy position in continuous space (matching liveSRRF.cl:170)
                double vy = (static_cast<int>(gradient_mag * (yc - vxy_PixelShift)) + j)
                            / static_cast<double>(gradient_mag) + vxy_PixelShift;

                // Samples outside the frame contribute to neither CGLH nor wSum
                if (!(vy > 0.0 && vy < ny)) continue;

                for (int i = i_min; i <= i_max; ++i) {
                    // vx position in continuous space
                    double vx = (static_cast<int>(gradient_mag * (xc - vxy_PixelShift)) + i)
                                / static_cast<double>(gradient_mag) + vxy_PixelShift;

                    if (!(vx > 0.0 && vx < nx)) continue;

                    double dx = vx - xc;
                    double dy = vy - yc;
                    double distance = std::sqrt(dx * dx + dy * dy);

                    if (distance == 0.0 || distance > tso) {
                        continue;
                    }

                    // Fetch the gradient with the half-pixel offset. Gx and Gy sit
                    // on grids shifted against each other by one sub-pixel along
                    // their own axis (liveSRRF.cl:191-192) -- Gx is shifted in x,
                    // Gy in y.
                    const double vx_g = gradient_mag * (vx - vxy_offset);
                    const double vy_g = gradient_mag * (vy - vxy_offset);
                    double Gx_val = safe_get(Gx_up.data(), gx_size, gy_size,
                                             static_cast<int>(vx_g + vxy_ArrayShift),
                                             static_cast<int>(vy_g));
                    double Gy_val = safe_get(Gy_up.data(), gx_size, gy_size,
                                             static_cast<int>(vx_g),
                                             static_cast<int>(vy_g + vxy_ArrayShift));

                    // dGauss^4 distance weight
                    double distanceWeight = distance * std::exp(-(distance * distance) / tss);
                    distanceWeight = std::pow(distanceWeight, 4);
                    wSum += distanceWeight;

                    // Convergence test: gradient must point inward
                    double GdotR = Gx_val * dx + Gy_val * dy;
                    if (GdotR < 0.0) {
                        double GMag = std::sqrt(Gx_val * Gx_val + Gy_val * Gy_val);
                        double Dk = (GMag == 0.0) ? distance :
                                   std::abs(Gy_val * dx - Gx_val * dy) / GMag;

                        // Linear angular kernel: 1 - sin(theta)
                        Dk = 1.0 - Dk / distance;  // in [0, 1]

                        CGLH += Dk * distanceWeight;
                    }
                }
            }

            // Normalize and apply sensitivity exponent
            if (wSum > 0.0) {
                CGLH /= wSum;
                if (CGLH >= 0.0) {
                    CGLH = std::pow(CGLH, sensitivity);
                } else {
                    CGLH = 0.0;
                }
            }

            // Intensity weighting (optional)
            if (intensity_weighting) {
                // Interpolate original image at magnified pixel centre
                // Note the -0.5 grid convention
                double v = interpolated_value(
                    img, nx, ny,
                    xM / static_cast<double>(magnification) - 0.5,
                    yM / static_cast<double>(magnification) - 0.5
                );
                rgc[yM * mx + xM] = v * CGLH;
            } else {
                rgc[yM * mx + xM] = CGLH;
            }
        }
    }

    *output = rgc;
    *n_output1 = my;
    *n_output2 = mx;
}

double* CLSMSuperRes::rgc_map(
    const double* img,
    int ny,
    int nx,
    int magnification,
    double fwhm,
    int sensitivity,
    bool intensity_weighting
) {
    double* output = nullptr;
    int out_ny, out_nx;
    rgc_map(img, ny, nx, magnification, fwhm, sensitivity, intensity_weighting,
            &output, &out_ny, &out_nx);
    return output;
}

// ========================================================================
// Photon Reassignment
// ========================================================================

TTTR* CLSMSuperRes::reassign_photons(
    CLSMImage* clsm,
    TTTR* tttr,
    int magnification,
    double fwhm,
    int sensitivity,
    double search_radius,
    const char* channel_mode,
    unsigned long long seed,
    const char* method,
    const double* detector_offsets,
    int n_detector_offsets,
    double ism_shift_factor
) {
    if (!clsm || !tttr) {
        throw std::invalid_argument("CLSMImage and TTTR must not be null");
    }

    // Resolve the super-resolution method (eSRRF is default; UNIFORM, ISM supported)
    SuperResMethod sr_method = SuperResMethod::ESRRF;
    if (method) {
        std::string m(method);
        std::transform(m.begin(), m.end(), m.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (m == "uniform") sr_method = SuperResMethod::UNIFORM;
        else if (m == "sofi") sr_method = SuperResMethod::SOFI;
        else if (m == "ism") sr_method = SuperResMethod::ISM;
        else if (m == "esrrf+ism" || m == "esrrf_ism" || m == "ism+esrrf" || m == "ism_esrrf") sr_method = SuperResMethod::ESRRF_ISM;
    }

    if (sr_method == SuperResMethod::SOFI) {
        throw std::runtime_error("SOFI reassignment not yet implemented (reserved)");
    }

    int n_channels = clsm->get_n_channels();
    int n_lines = clsm->get_n_lines();
    int n_pixel = clsm->get_n_pixel();

    if (n_lines <= 0 || n_pixel <= 0) {
        throw std::runtime_error("CLSMImage has no pixels");
    }

    // 1. Get photon positions (exact fractional x from macro times)
    int* photon_frame = nullptr;
    int* photon_line = nullptr;
    double* photon_x = nullptr;
    double* photon_y = nullptr;
    int* photon_event = nullptr;
    int n_photons = 0;

    get_photon_positions(clsm, tttr,
        &photon_frame, &photon_line, &photon_x, &photon_y,
        &photon_event, &n_photons);

    if (n_photons == 0) {
        // No photons — return empty TTTR
        if (photon_frame) std::free(photon_frame);
        if (photon_line) std::free(photon_line);
        if (photon_x) std::free(photon_x);
        if (photon_y) std::free(photon_y);
        if (photon_event) std::free(photon_event);
        auto empty = std::make_shared<TTTR>();
        return new TTTR(*empty);
    }

    // Resolve seed: 0 means "use the global TTTR_RNG_SEED"
    if (seed == 0) {
        seed = tttrlib::global_rng_seed();
    }

    // 2. Compute intensity image and RGC field(s)
    //    For UNIFORM, flat prior; for ESRRF, ISM, or ESRRF_ISM, compute RGC field.
    std::string mode(channel_mode);
    std::vector<double*> rgc_fields;  // one per channel (or one for merged)
    std::vector<int> rgc_my, rgc_mx;

    // Get intensity: shape (n_frames_total, n_lines, n_pixel)
    unsigned short* intensity_raw = nullptr;
    int dim1, dim2, dim3;
    clsm->get_intensity(&intensity_raw, &dim1, &dim2, &dim3);
    // dim1 = n_frames_total (may include channel splitting), dim2 = n_lines, dim3 = n_pixel

    // Determine effective channels for RGC computation
    int eff_channels = (mode == "split") ? std::max(n_channels, 1) : 1;

    for (int ch = 0; ch < eff_channels; ++ch) {
        // Sum intensity over frames for this channel into a 2D image (n_lines, n_pixel)
        std::vector<double> img2d(n_lines * n_pixel, 0.0);

        if (mode == "split" && n_channels > 1) {
            // Frames of one channel form a contiguous block of flat frame
            // indices, which is the same indexing get_intensity uses.
            const int f0 = clsm->get_channel_frame_offset(ch);
            const int ch_frames = clsm->get_channel_frame_count(ch);
            for (int f = f0; f >= 0 && f < std::min(f0 + ch_frames, dim1); ++f) {
                for (int l = 0; l < std::min(n_lines, dim2); ++l) {
                    for (int p = 0; p < std::min(n_pixel, dim3); ++p) {
                        img2d[l * n_pixel + p] +=
                            intensity_raw[static_cast<size_t>(f) * dim2 * dim3 + l * dim3 + p];
                    }
                }
            }
        } else {
            // Merged: sum all frames
            for (int f = 0; f < dim1; ++f) {
                for (int l = 0; l < std::min(n_lines, dim2); ++l) {
                    for (int p = 0; p < std::min(n_pixel, dim3); ++p) {
                        img2d[l * n_pixel + p] +=
                            intensity_raw[static_cast<size_t>(f) * dim2 * dim3 + l * dim3 + p];
                    }
                }
            }
        }

        // Compute RGC field (or flat field for UNIFORM method)
        int out_my, out_mx;
        double* rgc = nullptr;
        if (sr_method == SuperResMethod::UNIFORM) {
            // UNIFORM: flat prior (all ones) — uniform upsampling within search radius
            out_my = magnification * n_lines;
            out_mx = magnification * n_pixel;
            rgc = static_cast<double*>(std::malloc(
                    static_cast<size_t>(out_my) * out_mx * sizeof(double)));
            if (!rgc) {
                for (double* f : rgc_fields) std::free(f);
                std::free(intensity_raw);
                std::free(photon_frame); std::free(photon_line);
                std::free(photon_x); std::free(photon_y); std::free(photon_event);
                throw std::runtime_error("Failed to allocate the uniform prior");
            }
            for (int i = 0; i < out_my * out_mx; ++i) rgc[i] = 1.0;
        } else {
            // ESRRF, ISM, or ESRRF_ISM: compute the Radial Gradient Convergence field
            rgc_map(img2d.data(), n_lines, n_pixel, magnification, fwhm, sensitivity,
                    true, &rgc, &out_my, &out_mx);
        }
        rgc_fields.push_back(rgc);
        rgc_my.push_back(out_my);
        rgc_mx.push_back(out_mx);
    }

    std::free(intensity_raw);

    // 3. For each photon, sample new position from RGC-weighted distribution
    double* x_new = static_cast<double*>(std::malloc(n_photons * sizeof(double)));
    double* y_new = static_cast<double*>(std::malloc(n_photons * sizeof(double)));

    if (!x_new || !y_new) {
        std::free(x_new);
        std::free(y_new);
        for (double* f : rgc_fields) std::free(f);
        std::free(photon_frame);
        std::free(photon_line);
        std::free(photon_x);
        std::free(photon_y);
        std::free(photon_event);
        throw std::runtime_error("Failed to allocate reassignment output arrays");
    }

    int my = magnification * n_lines;
    int mx = magnification * n_pixel;
    double sr_pixels = search_radius * magnification;

    // A photon at native position x sits at the centre of the magnified pixel
    // covering it: the RGC grid samples native coordinate (xM + 0.5) / M, so the
    // inverse of that mapping is xM = x * M - 0.5.
    const double subpixel_origin = 0.5;

    #pragma omp parallel for schedule(dynamic) if(n_photons >= 10000)
    for (int i = 0; i < n_photons; ++i) {
        // Determine which RGC field to use. Frames of one channel form a
        // contiguous block, so the photon's flat frame index names its channel.
        int ch = 0;
        if (mode == "split" && n_channels > 1) {
            const int c = clsm->get_channel_of_frame(photon_frame[i]);
            ch = std::max(0, std::min(c, eff_channels - 1));
        }
        double* rgc = rgc_fields[ch];

        // Current photon position in magnified pixel units
        double xM = photon_x[i] * magnification - subpixel_origin;
        double yM = photon_y[i] * magnification - subpixel_origin;

        // Apply ISM shift vector if method is ISM or ESRRF_ISM or detector offsets are provided
        if (sr_method == SuperResMethod::ISM || sr_method == SuperResMethod::ESRRF_ISM || (detector_offsets && n_detector_offsets >= 2)) {
            int event_idx = photon_event[i];
            int route_ch = tttr->get_routing_channel_at(event_idx);
            double dx_ch = 0.0;
            double dy_ch = 0.0;

            if (detector_offsets && 2 * route_ch + 1 < n_detector_offsets) {
                dx_ch = detector_offsets[2 * route_ch];
                dy_ch = detector_offsets[2 * route_ch + 1];
            } else if (n_channels > 1) {
                int grid_side = static_cast<int>(std::ceil(std::sqrt(n_channels)));
                int col = route_ch % grid_side;
                int row = route_ch / grid_side;
                dx_ch = col - (grid_side - 1.0) / 2.0;
                dy_ch = row - (grid_side - 1.0) / 2.0;
            }

            xM = (photon_x[i] + ism_shift_factor * dx_ch) * magnification - subpixel_origin;
            yM = (photon_y[i] + ism_shift_factor * dy_ch) * magnification - subpixel_origin;
        }

        if (sr_method == SuperResMethod::ISM && sensitivity <= 0) {
            // Direct ISM shift reassignment with subpixel jitter [-0.5, +0.5]
            double jitter_x = (tttrlib::Random::deterministic(seed + 1000, i) - 0.5);
            double jitter_y = (tttrlib::Random::deterministic(seed + 2000, i) - 0.5);
            x_new[i] = std::max(0.0, std::min(xM + jitter_x, static_cast<double>(mx - 1)));
            y_new[i] = std::max(0.0, std::min(yM + jitter_y, static_cast<double>(my - 1)));
            continue;
        }

        // Search window
        int x0 = std::max(static_cast<int>(std::floor(xM - sr_pixels)), 0);
        int x1 = std::min(static_cast<int>(std::ceil(xM + sr_pixels)), mx - 1);
        int y0 = std::max(static_cast<int>(std::floor(yM - sr_pixels)), 0);
        int y1 = std::min(static_cast<int>(std::ceil(yM + sr_pixels)), my - 1);

        // Collect weights and positions
        std::vector<double> weights;
        std::vector<std::pair<int, int>> positions;

        for (int yi = y0; yi <= y1; ++yi) {
            for (int xi = x0; xi <= x1; ++xi) {
                double dx = xi - xM;
                double dy = yi - yM;
                double dist = std::sqrt(dx * dx + dy * dy);
                if (dist <= search_radius * magnification) {
                    double w = rgc[yi * mx + xi];
                    if (w > 0) {  // Only consider positive weights
                        weights.push_back(w);
                        positions.emplace_back(xi, yi);
                    }
                }
            }
        }

        if (positions.empty()) {
            // No valid sub-pixels — keep original position
            x_new[i] = std::max(0.0, std::min(xM, static_cast<double>(mx - 1)));
            y_new[i] = std::max(0.0, std::min(yM, static_cast<double>(my - 1)));
            continue;
        }

        // Normalize weights
        double w_sum = 0.0;
        for (double w : weights) w_sum += w;

        if (w_sum <= 0.0) {
            // Uniform fallback
            double u = tttrlib::Random::deterministic(seed, i);
            int idx = static_cast<int>(u * positions.size());
            idx = std::min(idx, static_cast<int>(positions.size()) - 1);
            x_new[i] = positions[idx].first;
            y_new[i] = positions[idx].second;
            continue;
        }

        // Sample from categorical distribution
        double u = tttrlib::Random::deterministic(seed, i) * w_sum;
        double cumsum = 0.0;
        int sample_idx = static_cast<int>(positions.size()) - 1;  // default to last
        for (size_t k = 0; k < weights.size(); ++k) {
            cumsum += weights[k];
            if (u <= cumsum) {
                sample_idx = static_cast<int>(k);
                break;
            }
        }

        x_new[i] = positions[sample_idx].first;
        y_new[i] = positions[sample_idx].second;

        // ALWAYS add subpixel jitter [-0.5, +0.5] to prevent discrete grid artifacts
        double jitter_x = (tttrlib::Random::deterministic(seed + 1000, i) - 0.5);
        double jitter_y = (tttrlib::Random::deterministic(seed + 2000, i) - 0.5);
        x_new[i] = std::max(0.0, std::min(x_new[i] + jitter_x, static_cast<double>(mx - 1)));
        y_new[i] = std::max(0.0, std::min(y_new[i] + jitter_y, static_cast<double>(my - 1)));
    }

    // 4. Build new TTTR with reassigned positions
    // Macro time encodes position in the magnified raster.
    // We preserve micro_time and routing_channel from the original.
    std::vector<unsigned long long> new_macro_times(n_photons);
    std::vector<unsigned short> new_micro_times(n_photons);
    std::vector<signed char> new_routing(n_photons);
    std::vector<signed char> new_event_types(n_photons);

    for (int i = 0; i < n_photons; ++i) {
        int event_idx = photon_event[i];
        // Preserve micro_time and routing_channel
        new_micro_times[i] = tttr->get_micro_time_at(event_idx);
        new_routing[i] = tttr->get_routing_channel_at(event_idx);
        new_event_types[i] = tttr->get_event_type_at(event_idx);

        // The position in the magnified raster is carried in the macro time as
        // the flat index frame * my * mx + y * mx + x; CLSMSuperRes::write turns
        // it back into frame/line markers and dwell-time offsets. 64-bit
        // throughout: a 512x512 frame at magnification 8 overflows int at ~128
        // frames.
        new_macro_times[i] =
            static_cast<unsigned long long>(photon_frame[i]) *
                static_cast<unsigned long long>(my) * static_cast<unsigned long long>(mx) +
            static_cast<unsigned long long>(static_cast<int>(y_new[i])) *
                static_cast<unsigned long long>(mx) +
            static_cast<unsigned long long>(static_cast<int>(x_new[i]));
    }

    // Clean up intermediate arrays
    for (double* f : rgc_fields) std::free(f);
    std::free(photon_frame);
    std::free(photon_line);
    std::free(photon_x);
    std::free(photon_y);
    std::free(photon_event);
    std::free(x_new);
    std::free(y_new);

    // Build the new TTTR
    auto* result = new TTTR(
        new_macro_times.data(), static_cast<int>(new_macro_times.size()),
        new_micro_times.data(), static_cast<int>(new_micro_times.size()),
        new_routing.data(), static_cast<int>(new_routing.size()),
        new_event_types.data(), static_cast<int>(new_event_types.size())
    );

    return result;
}

// ========================================================================
// Temporal Combination
// ========================================================================

void CLSMSuperRes::temporal_combine(
    const double* stack,
    int n_frames,
    int ny,
    int nx,
    const char* mode,
    double** output,
    int* n_output1,
    int* n_output2
) {
    if (n_frames <= 0 || ny <= 0 || nx <= 0) {
        throw std::invalid_argument("Invalid stack dimensions");
    }

    double* result = static_cast<double*>(std::malloc(ny * nx * sizeof(double)));
    if (!result) {
        throw std::runtime_error("Failed to allocate temporal combine output");
    }

    std::string mode_str(mode);

    if (mode_str == "AVG") {
        // Mean over frames
        for (int i = 0; i < ny * nx; ++i) {
            double sum = 0.0;
            for (int f = 0; f < n_frames; ++f) {
                sum += stack[f * ny * nx + i];
            }
            result[i] = sum / n_frames;
        }
    } else if (mode_str == "VAR") {
        // Var[X] = E[X^2] - E[X]^2
        for (int i = 0; i < ny * nx; ++i) {
            double sum = 0.0;
            double sum_sq = 0.0;
            for (int f = 0; f < n_frames; ++f) {
                double val = stack[f * ny * nx + i];
                sum += val;
                sum_sq += val * val;
            }
            double mean = sum / n_frames;
            result[i] = (sum_sq / n_frames) - (mean * mean);
        }
    } else if (mode_str == "TAC2") {
        // TAC2[X] = E[X(t) X(t+1)] - E[X]^2. A lag-1 cumulant needs two frames.
        if (n_frames < 2) {
            std::free(result);
            throw std::invalid_argument("TAC2 needs at least two frames");
        }
        for (int i = 0; i < ny * nx; ++i) {
            double sum = 0.0;
            double lag1_sum = 0.0;
            for (int f = 0; f < n_frames; ++f) {
                double val = stack[f * ny * nx + i];
                sum += val;
                if (f < n_frames - 1) {
                    lag1_sum += val * stack[(f + 1) * ny * nx + i];
                }
            }
            double mean = sum / n_frames;
            double mean_lag1 = lag1_sum / (n_frames - 1);
            result[i] = mean_lag1 - (mean * mean);
        }
    } else if (mode_str == "INT") {
        // Interpolated intensity (just mean)
        for (int i = 0; i < ny * nx; ++i) {
            double sum = 0.0;
            for (int f = 0; f < n_frames; ++f) {
                sum += stack[f * ny * nx + i];
            }
            result[i] = sum / n_frames;
        }
    } else {
        std::free(result);
        throw std::invalid_argument("Unknown mode: " + mode_str);
    }

    *output = result;
    *n_output1 = ny;
    *n_output2 = nx;
}

double* CLSMSuperRes::temporal_combine(
    const double* stack,
    int n_frames,
    int ny,
    int nx,
    const char* mode
) {
    double* output = nullptr;
    int dummy1, dummy2;
    temporal_combine(stack, n_frames, ny, nx, mode, &output, &dummy1, &dummy2);
    return output;
}

// ========================================================================
// Public Photon-Position Seam
// ========================================================================

void CLSMSuperRes::get_photon_positions(
    CLSMImage* clsm,
    TTTR* tttr,
    int** out_frame,
    int** out_line,
    double** out_x_exact,
    double** out_y_line,
    int** out_event_idx,
    int* n_photons
) {
    // Delegate to CLSMImage's own photon-position extraction (exact fractional x
    // from macro times). This keeps the timing arithmetic in one place.
    clsm->get_photon_positions(
        tttr,
        out_frame,
        out_line,
        out_x_exact,
        out_y_line,
        out_event_idx,
        n_photons
    );
}

// ========================================================================
// Write Reassigned Photons to a Magnified-Raster TTTR File
// ========================================================================

namespace {

struct SRDecodedPhoton {
    int frame;
    int line;
    int x;
    size_t event;
};

} // namespace

bool CLSMSuperRes::write(
    TTTR* tttr_reassigned,
    int nx,
    int ny,
    int magnification,
    const char* output_filename,
    int pixel_duration,
    int line_duration,
    int frame_marker_delay
) {
    if (tttr_reassigned == nullptr || output_filename == nullptr) return false;
    if (nx <= 0 || ny <= 0 || magnification <= 0) return false;

    const std::string filename(output_filename);
    const int container_type = inferTTTRContainerTypeFromExtension(filename);
    if (container_type < 0) {
        std::cerr << "CLSMSuperRes::write: cannot infer container type from '"
                  << filename << "'" << std::endl;
        return false;
    }
    // Magnified-raster output is defined for the marker- and photonscore-based
    // containers only; the others have no canonical frame/line representation.
    if (container_type != PQ_PTU_CONTAINER &&
        container_type != PQ_HT3_CONTAINER &&
        container_type != BH_SPC130_CONTAINER &&
        container_type != PS_PHOTONS_CONTAINER) {
        std::cerr << "CLSMSuperRes::write: container type " << container_type
                  << " not supported for magnified-raster output" << std::endl;
        return false;
    }

    // Magnified raster geometry.
    const int mx = magnification * nx;
    const int my = magnification * ny;
    const long long n_pixels_per_frame = static_cast<long long>(my) * mx;

    // Timing model: frame f starts at f * frame_stride, line l at
    // frame_base + l * line_duration, a photon at subpixel x at
    // line_base + x * pixel_duration.
    if (pixel_duration <= 0) pixel_duration = 1;
    if (line_duration <= 0) line_duration = mx * pixel_duration;
    if (frame_marker_delay < 0) frame_marker_delay = 0;
    const long long frame_stride =
        static_cast<long long>(my) * line_duration + frame_marker_delay;

    // Decode each photon's magnified flat position (frame*my*mx + y*mx + x)
    // from the reassigned macro time.
    const size_t n_events = tttr_reassigned->get_n_valid_events();
    std::vector<SRDecodedPhoton> photons;
    photons.reserve(n_events);
    int max_frame = 0;
    for (size_t i = 0; i < n_events; ++i) {
        if (tttr_reassigned->get_event_type_at(i) != RECORD_PHOTON) continue;
        const unsigned long long flat = tttr_reassigned->get_macro_time_at(i);
        const int frame = static_cast<int>(flat / n_pixels_per_frame);
        const long long rem = static_cast<long long>(flat % n_pixels_per_frame);
        const int line = static_cast<int>(rem / mx);
        const int x = static_cast<int>(rem % mx);
        if (line < 0 || line >= my || x < 0 || x >= mx) continue;
        photons.push_back({frame, line, x, i});
        if (frame > max_frame) max_frame = frame;
    }
    const int n_frames = std::max(1, max_frame + 1);

    // Raster order keeps the synthesized macro times non-decreasing, which the
    // record writers require for their overflow accounting.
    std::sort(photons.begin(), photons.end(),
        [](const SRDecodedPhoton& a, const SRDecodedPhoton& b) {
            if (a.frame != b.frame) return a.frame < b.frame;
            if (a.line != b.line) return a.line < b.line;
            return a.x < b.x;
        });

    // Photonscore position markers carry the raster coordinate in the micro
    // time, which is 16-bit; the magnified coordinate must fit.
    if (container_type == PS_PHOTONS_CONTAINER && (mx >= 65536 || my >= 65536)) {
        std::cerr << "CLSMSuperRes::write: magnified raster exceeds the 16-bit "
                     "position-coordinate range" << std::endl;
        return false;
    }

    // Marker routing channels. PicoQuant files use line start / line stop /
    // frame at 1 / 2 / 4; Becker & Hickl SPC-130 files use start-only line
    // markers at 2 and frame markers at 4.
    const signed char line_start_ch = (container_type == BH_SPC130_CONTAINER) ? 2 : 1;
    const signed char line_stop_ch  = (container_type == BH_SPC130_CONTAINER) ? -1 : 2;
    const signed char frame_ch      = 4;

    // Build the output event stream in raster order.
    std::vector<unsigned long long> out_ms;
    std::vector<unsigned short> out_mt;
    std::vector<signed char> out_routing;
    std::vector<signed char> out_events;
    out_ms.reserve(n_events + static_cast<size_t>(n_frames) * (2 * my + 2));

    auto push_marker = [&](unsigned long long ms, unsigned short micro,
                           signed char routing) {
        out_ms.push_back(ms);
        out_mt.push_back(micro);
        out_routing.push_back(routing);
        out_events.push_back(RECORD_MARKER);
    };
    auto push_photon = [&](unsigned long long ms, unsigned short micro,
                           signed char routing) {
        out_ms.push_back(ms);
        out_mt.push_back(micro);
        out_routing.push_back(routing);
        out_events.push_back(RECORD_PHOTON);
    };

    if (container_type == PS_PHOTONS_CONTAINER) {
        // Position-marker stream consumed by write_ps_file/read_ps_file: each
        // photon is preceded by an X and a Y position marker whose micro time
        // holds the raster coordinate.
        for (const auto& p : photons) {
            const unsigned long long ms =
                static_cast<unsigned long long>(p.frame) * frame_stride +
                static_cast<unsigned long long>(p.line) * line_duration +
                static_cast<unsigned long long>(p.x) * pixel_duration;
            push_marker(ms, static_cast<unsigned short>(p.x), MARKER_POSITION_X);
            push_marker(ms, static_cast<unsigned short>(p.line), MARKER_POSITION_Y);
            push_photon(ms,
                        tttr_reassigned->get_micro_time_at(p.event),
                        tttr_reassigned->get_routing_channel_at(p.event));
        }
    } else {
        size_t cursor = 0;
        for (int f = 0; f < n_frames; ++f) {
            const unsigned long long frame_base =
                static_cast<unsigned long long>(f) * frame_stride;
            push_marker(frame_base, 0, frame_ch);
            // SPC-130 line markers are start-only: N lines need N+1 starts so
            // get_line_edges can pair them into N (start, stop) ranges.
            const int n_line_markers = (line_stop_ch < 0) ? my + 1 : my;
            for (int l = 0; l < n_line_markers; ++l) {
                const unsigned long long line_base =
                    frame_base + static_cast<unsigned long long>(l) * line_duration;
                push_marker(line_base, 0, line_start_ch);
                // Photons of (frame f, line l), already ordered by x.
                while (cursor < photons.size() &&
                       photons[cursor].frame == f &&
                       photons[cursor].line == l) {
                    const auto& p = photons[cursor];
                    push_photon(
                        line_base + static_cast<unsigned long long>(p.x) * pixel_duration,
                        tttr_reassigned->get_micro_time_at(p.event),
                        tttr_reassigned->get_routing_channel_at(p.event));
                    ++cursor;
                }
                if (line_stop_ch >= 0) {
                    push_marker(line_base + static_cast<unsigned long long>(line_duration),
                                0, line_stop_ch);
                }
            }
        }
    }

    // Wrap the stream in a TTTR and hand it to the container writer (which
    // auto-fills the record-type/header metadata it needs).
    auto* out = new TTTR(
        out_ms.data(), static_cast<int>(out_ms.size()),
        out_mt.data(), static_cast<int>(out_mt.size()),
        out_routing.data(), static_cast<int>(out_routing.size()),
        out_events.data(), static_cast<int>(out_events.size())
    );

    // Header: imaging geometry and marker layout. The macro time is synthetic
    // (1 ns per tick); micro times are preserved from the reassigned stream.
    auto* header = new TTTRHeader(container_type);
    header->set_macro_time_resolution(1e-9);
    header->set_micro_time_resolution(1e-9);
    unsigned short max_micro = 0;
    for (const auto& p : photons)
        max_micro = std::max(max_micro, tttr_reassigned->get_micro_time_at(p.event));
    int n_micro = 1;
    while (n_micro <= static_cast<int>(max_micro)) n_micro <<= 1;
    header->set_number_of_micro_time_channels(n_micro);

    header->set_int_tag("ImgHdr_PixX", mx);
    header->set_int_tag("ImgHdr_PixY", my);
    header->set_int_tag("ImgHdr_MaxFrames", n_frames);
    header->set_int_tag("ImgHdr_Dimensions", 2);
    header->set_int_tag("ImgHdr_BiDirect", 0);
    header->set_int_tag("ImgHdr_Ident", 1);
    header->set_float_tag("ImgHdr_TimePerPixel",
                          static_cast<double>(pixel_duration) * 1e-9);

    if (container_type == PQ_PTU_CONTAINER) {
        // PTU stores marker *indices*: 1 / 2 / 3 decode to channels 1 / 2 / 4.
        header->set_string_tag("Tag Version", "0      ");
        header->set_int_tag("ImgHdr_LineStart", 1);
        header->set_int_tag("ImgHdr_LineStop", 2);
        header->set_int_tag("ImgHdr_Frame", 3);
    } else if (container_type == PQ_HT3_CONTAINER) {
        // HT3 stores raw routing channels and carries the imaging blob that
        // reconstructs the ImgHdr_* tags on read (v[2]=Frame-1, v[3]/v[4]=
        // line start/stop, v[6]/v[7]=PixX/PixY).
        header->set_int_tag("ImgHdr_LineStart", 1);
        header->set_int_tag("ImgHdr_LineStop", 2);
        header->set_int_tag("ImgHdr_Frame", 4);
        std::vector<int32_t> blob = {0, 0, 3, 1, 2, 0, mx, my};
        header->set_blob_tag("ImgHdr", blob);
    } else if (container_type == BH_SPC130_CONTAINER) {
        header->set_int_tag("ImgHdr_LineStart", 2);
        header->set_int_tag("ImgHdr_LineStop", 255);
        header->set_int_tag("ImgHdr_Frame", 4);
        header->set_int_tag("BH_UsePixelClock", 0);
    }

    bool ok = out->write(filename, header, container_type);
    delete out;
    delete header;
    return ok;
}

// ========================================================================
// Image Scanning Microscopy (ISM) -- BrightEyes-ISM algorithms
// ========================================================================

// Runtime AVX/FMA control using CPU feature detection from info.h
// Set TTTRLIB_USE_AVX=0 to disable AVX optimizations at runtime
// Set TTTRLIB_USE_FMA=0 to disable FMA optimizations at runtime
static bool g_use_avx = tttrlib::cpu_features::get_avx_enabled();
static bool g_use_fma = tttrlib::cpu_features::get_fma_enabled();
static bool g_use_neon = tttrlib::cpu_features::get_neon_enabled();

namespace {

// -----------------------------------------------------------------------------
// AVX kernels (compiled in on x86; only called when g_use_avx / g_use_fma are
// true at runtime). The per-function target attribute lets these use AVX/FMA
// even when the translation unit is built without -mavx, keeping the scalar
// code paths portable to non-AVX CPUs.
// -----------------------------------------------------------------------------
#if TTTRLIB_COMPILE_AVX
// out[i] = exp(-radial2[i]/denom); returns sum(out).
TTTRLIB_TARGET_AVX
static double normalized_gaussian_exp_avx(const double* radial2, size_t n, double denom, double* out) {
    const size_t n_vec = (n / 4) * 4;
    __m256d v_denom = _mm256_set1_pd(denom);
    __m256d v_sum = _mm256_setzero_pd();
    for (size_t i = 0; i < n_vec; i += 4) {
        __m256d v_r2 = _mm256_loadu_pd(&radial2[i]);
        __m256d v_arg = _mm256_div_pd(_mm256_sub_pd(_mm256_setzero_pd(), v_r2), v_denom);
        double tmp[4];
        _mm256_storeu_pd(tmp, v_arg);
        for (int j = 0; j < 4; ++j) tmp[j] = std::exp(tmp[j]);
        __m256d v_val = _mm256_loadu_pd(tmp);
        _mm256_storeu_pd(&out[i], v_val);
        v_sum = _mm256_add_pd(v_sum, v_val);
    }
    double sum_arr[4];
    _mm256_storeu_pd(sum_arr, v_sum);
    double sum = sum_arr[0] + sum_arr[1] + sum_arr[2] + sum_arr[3];
    for (size_t i = n_vec; i < n; ++i) {
        double val = std::exp(-radial2[i] / denom);
        out[i] = val;
        sum += val;
    }
    return sum;
}

// out[i] *= s
TTTRLIB_TARGET_AVX
static void scale_inplace_avx(double* out, size_t n, double s) {
    const size_t n_vec = (n / 4) * 4;
    __m256d v_s = _mm256_set1_pd(s);
    for (size_t i = 0; i < n_vec; i += 4) {
        __m256d v = _mm256_loadu_pd(&out[i]);
        v = _mm256_mul_pd(v, v_s);
        _mm256_storeu_pd(&out[i], v);
    }
    for (size_t i = n_vec; i < n; ++i) out[i] *= s;
}

// acc[i] += b[i]
TTTRLIB_TARGET_AVX
static void add_inplace_avx(double* acc, const double* b, size_t n) {
    const size_t n_vec = (n / 4) * 4;
    for (size_t i = 0; i < n_vec; i += 4) {
        __m256d v_acc = _mm256_loadu_pd(&acc[i]);
        __m256d v_b = _mm256_loadu_pd(&b[i]);
        v_acc = _mm256_add_pd(v_acc, v_b);
        _mm256_storeu_pd(&acc[i], v_acc);
    }
    for (size_t i = n_vec; i < n; ++i) acc[i] += b[i];
}

// Least-squares B estimate accumulators: di = gauss - gaussA, resid = micro - gaussA;
// num += di*resid, den += di*di. Needs FMA (gate with g_use_avx && g_use_fma).
TTTRLIB_TARGET_AVX_FMA
static void fit_num_den_avx(const double* gauss, const double* gaussA, const double* micro,
                            size_t n, double& num_out, double& den_out) {
    const size_t n_vec = (n / 4) * 4;
    __m256d v_num = _mm256_setzero_pd();
    __m256d v_den = _mm256_setzero_pd();
    for (size_t i = 0; i < n_vec; i += 4) {
        __m256d v_gauss  = _mm256_loadu_pd(&gauss[i]);
        __m256d v_gaussA = _mm256_loadu_pd(&gaussA[i]);
        __m256d v_micro  = _mm256_loadu_pd(&micro[i]);
        __m256d v_di    = _mm256_sub_pd(v_gauss, v_gaussA);
        __m256d v_resid = _mm256_sub_pd(v_micro, v_gaussA);
        v_num = _mm256_fmadd_pd(v_di, v_resid, v_num);
        v_den = _mm256_fmadd_pd(v_di, v_di, v_den);
    }
    double num_arr[4], den_arr[4];
    _mm256_storeu_pd(num_arr, v_num);
    _mm256_storeu_pd(den_arr, v_den);
    double num = num_arr[0] + num_arr[1] + num_arr[2] + num_arr[3];
    double den = den_arr[0] + den_arr[1] + den_arr[2] + den_arr[3];
    for (size_t i = n_vec; i < n; ++i) {
        double di = gauss[i] - gaussA[i];
        double resid = micro[i] - gaussA[i];
        num += di * resid;
        den += di * di;
    }
    num_out = num;
    den_out = den;
}

// SSE cost of model = gaussA + B*(gauss - gaussA) vs micro. Needs FMA.
TTTRLIB_TARGET_AVX_FMA
static double fit_sse_avx(const double* gauss, const double* gaussA, const double* micro,
                          size_t n, double B) {
    const size_t n_vec = (n / 4) * 4;
    __m256d v_sse = _mm256_setzero_pd();
    __m256d v_B = _mm256_set1_pd(B);
    for (size_t i = 0; i < n_vec; i += 4) {
        __m256d v_gauss  = _mm256_loadu_pd(&gauss[i]);
        __m256d v_gaussA = _mm256_loadu_pd(&gaussA[i]);
        __m256d v_micro  = _mm256_loadu_pd(&micro[i]);
        __m256d v_di    = _mm256_sub_pd(v_gauss, v_gaussA);
        __m256d v_model = _mm256_fmadd_pd(v_B, v_di, v_gaussA);
        __m256d v_diff  = _mm256_sub_pd(v_model, v_micro);
        v_sse = _mm256_fmadd_pd(v_diff, v_diff, v_sse);
    }
    double sse_arr[4];
    _mm256_storeu_pd(sse_arr, v_sse);
    double sse = sse_arr[0] + sse_arr[1] + sse_arr[2] + sse_arr[3];
    for (size_t i = n_vec; i < n; ++i) {
        double di = gauss[i] - gaussA[i];
        double model = gaussA[i] + B * di;
        double diff = model - micro[i];
        sse += diff * diff;
    }
    return sse;
}
#endif // TTTRLIB_COMPILE_AVX

// -----------------------------------------------------------------------------
// NEON kernels (AArch64). Wired only for the compute-dense reductions where
// 2-wide NEON beats the autovectorized scalar loop on Apple Silicon
// (fit num/den & sse ~1.5x). Memory-bound ops (add/scale) are left to the
// autovectorizer, which is faster there.
// -----------------------------------------------------------------------------
#if TTTRLIB_COMPILE_NEON
static void fit_num_den_neon(const double* g, const double* gA, const double* m,
                             size_t n, double& num_out, double& den_out) {
    float64x2_t vn = vdupq_n_f64(0.0), vd = vdupq_n_f64(0.0);
    size_t nv = (n / 2) * 2, i = 0;
    for (; i < nv; i += 2) {
        float64x2_t vg = vld1q_f64(&g[i]), vgA = vld1q_f64(&gA[i]), vm = vld1q_f64(&m[i]);
        float64x2_t di = vsubq_f64(vg, vgA), resid = vsubq_f64(vm, vgA);
        vn = vfmaq_f64(vn, di, resid);
        vd = vfmaq_f64(vd, di, di);
    }
    double num = vgetq_lane_f64(vn, 0) + vgetq_lane_f64(vn, 1);
    double den = vgetq_lane_f64(vd, 0) + vgetq_lane_f64(vd, 1);
    for (; i < n; ++i) {
        double di = g[i] - gA[i], resid = m[i] - gA[i];
        num += di * resid;
        den += di * di;
    }
    num_out = num;
    den_out = den;
}

static double fit_sse_neon(const double* g, const double* gA, const double* m,
                           size_t n, double B) {
    float64x2_t v_sse = vdupq_n_f64(0.0), v_B = vdupq_n_f64(B);
    size_t nv = (n / 2) * 2, i = 0;
    for (; i < nv; i += 2) {
        float64x2_t vg = vld1q_f64(&g[i]), vgA = vld1q_f64(&gA[i]), vm = vld1q_f64(&m[i]);
        float64x2_t di = vsubq_f64(vg, vgA);
        float64x2_t model = vfmaq_f64(vgA, v_B, di);   // gaussA + B*di
        float64x2_t diff = vsubq_f64(model, vm);
        v_sse = vfmaq_f64(v_sse, diff, diff);
    }
    double sse = vgetq_lane_f64(v_sse, 0) + vgetq_lane_f64(v_sse, 1);
    for (; i < n; ++i) {
        double di = g[i] - gA[i];
        double model = gA[i] + B * di;
        double diff = model - m[i];
        sse += diff * diff;
    }
    return sse;
}
#endif // TTTRLIB_COMPILE_NEON

// AVX-optimized Gaussian normalization
static void normalized_gaussian(const std::vector<double>& radial2, double sigma, std::vector<double>& out) {
    const size_t n = radial2.size();
    out.resize(n);
    if (sigma <= 1e-8) {
        double inv = 1.0 / static_cast<double>(n);
        std::fill(out.begin(), out.end(), inv);
        return;
    }
    double denom = 2.0 * sigma * sigma;
    double sum = 0.0;

#if TTTRLIB_COMPILE_AVX
    if (g_use_avx) {
        sum = normalized_gaussian_exp_avx(radial2.data(), n, denom, out.data());
    } else
#endif
    {
        for (size_t i = 0; i < n; ++i) {
            double val = std::exp(-radial2[i] / denom);
            out[i] = val;
            sum += val;
        }
    }

    if (sum <= std::numeric_limits<double>::min()) {
        double inv = 1.0 / static_cast<double>(n);
        std::fill(out.begin(), out.end(), inv);
    } else {
        double inv_sum = 1.0 / sum;
#if TTTRLIB_COMPILE_AVX
        if (g_use_avx) {
            scale_inplace_avx(out.data(), n, inv_sum);
        } else
#endif
        {
            for (double& v : out) v *= inv_sum;
        }
    }
}
// Simple 2D image
struct Image {
    size_t W{0}, H{0};
    std::vector<double> data; // row-major

    Image() = default;
    Image(size_t w, size_t h, double v=0.0): W(w), H(h), data(w*h, v) {}

    inline double& operator()(size_t y, size_t x) { return data[y*W + x]; }
    inline const double& operator()(size_t y, size_t x) const { return data[y*W + x]; }
};

// Complex image helpers used throughout this file
using cpx = std::complex<double>;
struct CImage {
    size_t W{0}, H{0};
    std::vector<cpx> data; // row-major

    CImage() = default;
    CImage(size_t w, size_t h, cpx v=cpx(0.0,0.0)) : W(w), H(h), data(w*h, v) {}

    inline cpx& operator()(size_t y, size_t x) { return data[y*W + x]; }
    inline const cpx& operator()(size_t y, size_t x) const { return data[y*W + x]; }
};

// ---------- pocketfft helpers (2D) ----------
//
// pocketfft never normalizes on its own -- the trailing `fct` argument is the
// only scaling applied. These two wrappers reproduce numpy's fft2 / ifft2
// exactly (forward unscaled, inverse scaled by 1/(W*H)), and they carry the
// *full* complex spectrum rather than the half-spectrum r2c writes, so that
// every consumer below can index any frequency it likes.
namespace fft2d {
    static void fft2(const Image& in, CImage& out) {
        if (out.W != in.W || out.H != in.H) out = CImage(in.W, in.H);
        CImage tmp(in.W, in.H);
        for (size_t i = 0; i < in.data.size(); ++i) tmp.data[i] = cpx(in.data[i], 0.0);
        using pocketfft::shape_t; using pocketfft::stride_t;
        shape_t shape{in.H, in.W};
        stride_t s(2); s[0] = (ptrdiff_t)(in.W*sizeof(cpx)); s[1] = (ptrdiff_t)sizeof(cpx);
        shape_t axes{0, 1};
        pocketfft::c2c(shape, s, s, axes, true, tmp.data.data(), out.data.data(), 1.0);
    }

    // Inverse transform of a full spectrum, keeping the real part. The inputs
    // used here are Hermitian by construction, so the imaginary part is noise.
    static void ifft2_real(const CImage& in, Image& out) {
        if (out.W != in.W || out.H != in.H) out = Image(in.W, in.H);
        CImage tmp(in.W, in.H);
        using pocketfft::shape_t; using pocketfft::stride_t;
        shape_t shape{in.H, in.W};
        stride_t s(2); s[0] = (ptrdiff_t)(in.W*sizeof(cpx)); s[1] = (ptrdiff_t)sizeof(cpx);
        shape_t axes{0, 1};
        const double fct = 1.0 / static_cast<double>(in.W * in.H);
        pocketfft::c2c(shape, s, s, axes, false, in.data.data(), tmp.data.data(), fct);
        for (size_t i = 0; i < tmp.data.size(); ++i) out.data[i] = tmp.data[i].real();
    }
}

// Signed frequency of storage index k in an n-point FFT: 0, 1, ... , -2, -1.
static inline double fft_freq(size_t k, size_t n) {
    return (2 * k < n + 1) ? static_cast<double>(k)
                           : static_cast<double>(k) - static_cast<double>(n);
}

// ---------- Fourier shift (subpixel) ----------
// Multiply the spectrum by exp(-2πi (fx*dx/W + fy*dy/H)), which translates the
// image content by +(dx, dy).
static void fourier_shift_inplace(CImage& F, double dx, double dy) {
    const size_t W = F.W, H = F.H;
    const double two_pi = 2.0 * M_PI;
    const double dx_norm = dx / (double)W;
    const double dy_norm = dy / (double)H;

    for (size_t v = 0; v < H; ++v) {
        const double ky_dy = fft_freq(v, H) * dy_norm;
        for (size_t u = 0; u < W; ++u) {
            const double phase = -two_pi * (fft_freq(u, W) * dx_norm + ky_dy);
            F(v,u) *= cpx(std::cos(phase), std::sin(phase));
        }
    }
}

// Shift an image by (dx, dy) via FFT
// Shift an image by (dx, dy).
//
// The transform is periodic, so it is applied on a zero-padded canvas twice the
// size and cropped back. Without that, content leaving one edge reappears at
// the opposite one: a bright structure at the left of the frame lands in the
// right margin, which is not merely cosmetic -- eSRRF run on the result reads
// those wrapped tails as converging gradients and renders them as puncta. The
// padding also confines the interpolation's ringing, matching the compact
// boundary behaviour of the reference implementation's default shift.
static Image subpixel_shift(const Image& in, double dx, double dy) {
    const size_t W = in.W, H = in.H;
    const size_t pad_x = W / 2, pad_y = H / 2;
    const size_t W2 = W + 2 * pad_x, H2 = H + 2 * pad_y;

    Image canvas(W2, H2, 0.0);
    for (size_t y = 0; y < H; ++y)
        std::copy(in.data.begin() + y * W, in.data.begin() + (y + 1) * W,
                  canvas.data.begin() + (y + pad_y) * W2 + pad_x);

    CImage F;
    fft2d::fft2(canvas, F);
    fourier_shift_inplace(F, dx, dy);
    Image shifted(W2, H2);
    fft2d::ifft2_real(F, shifted);

    Image out(W, H, 0.0);
    for (size_t y = 0; y < H; ++y)
        std::copy(shifted.data.begin() + (y + pad_y) * W2 + pad_x,
                  shifted.data.begin() + (y + pad_y) * W2 + pad_x + W,
                  out.data.begin() + y * W);
    return out;
}

// ---------- Windowing and smoothing (BrightEyes APR_lib pre-processing) ----------

// Separable 2D Hann window, matching APR_lib.hann2d.
static Image hann2d(size_t W, size_t H) {
    Image w(W, H, 0.0);
    std::vector<double> wy(H), wx(W);
    for (size_t y = 0; y < H; ++y)
        wy[y] = (H > 1) ? 0.5 * (1.0 - std::cos(2.0 * M_PI * y / (double)(H - 1))) : 1.0;
    for (size_t x = 0; x < W; ++x)
        wx[x] = (W > 1) ? 0.5 * (1.0 - std::cos(2.0 * M_PI * x / (double)(W - 1))) : 1.0;
    for (size_t y = 0; y < H; ++y)
        for (size_t x = 0; x < W; ++x)
            w(y, x) = wy[y] * wx[x];
    return w;
}

// Separable Gaussian blur with edge clamping, matching
// scipy.ndimage.gaussian_filter(mode='nearest', truncate=4.0) -- the default
// behind skimage.filters.gaussian, which APR_lib uses for denoising.
static void gaussian_blur_inplace(Image& im, double sigma) {
    if (!(sigma > 0.0)) return;
    const int radius = static_cast<int>(4.0 * sigma + 0.5);
    if (radius < 1) return;

    std::vector<double> k(2 * radius + 1);
    double ksum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-0.5 * (i * i) / (sigma * sigma));
        k[i + radius] = v;
        ksum += v;
    }
    for (double& v : k) v /= ksum;

    const size_t W = im.W, H = im.H;
    Image tmp(W, H, 0.0);
    for (size_t y = 0; y < H; ++y) {
        for (size_t x = 0; x < W; ++x) {
            double acc = 0.0;
            for (int i = -radius; i <= radius; ++i) {
                const size_t xx = (size_t)std::clamp((int)x + i, 0, (int)W - 1);
                acc += k[i + radius] * im(y, xx);
            }
            tmp(y, x) = acc;
        }
    }
    for (size_t y = 0; y < H; ++y) {
        for (size_t x = 0; x < W; ++x) {
            double acc = 0.0;
            for (int i = -radius; i <= radius; ++i) {
                const size_t yy = (size_t)std::clamp((int)y + i, 0, (int)H - 1);
                acc += k[i + radius] * tmp(yy, x);
            }
            im(y, x) = acc;
        }
    }
}

// ---------- Phase cross-correlation with subpixel upsampling ----------
//
// Equivalent to skimage.registration.phase_cross_correlation(reference, moving,
// upsample_factor=usf, normalization=None), which is what BrightEyes-ISM's
// ShiftVectors calls. The return value is the shift that has to be *applied to
// `moving`* to register it onto `reference`, as (dx, dy) with dx along the width.
static std::pair<double,double> phase_cross_correlation(
        const Image& reference, const Image& moving, int usf)
{
    if (reference.W != moving.W || reference.H != moving.H)
        throw std::runtime_error("phase_cross_correlation: size mismatch");
    const size_t W = reference.W, H = reference.H;

    CImage Fr, Fm;
    fft2d::fft2(reference, Fr);
    fft2d::fft2(moving, Fm);

    CImage product(W, H);
    for (size_t i = 0; i < product.data.size(); ++i)
        product.data[i] = Fr.data[i] * std::conj(Fm.data[i]);

    // Integer-precision peak of the cross-correlation
    Image corr;
    fft2d::ifft2_real(product, corr);
    size_t peak_x = 0, peak_y = 0;
    double peak = -std::numeric_limits<double>::infinity();
    for (size_t y = 0; y < H; ++y) {
        for (size_t x = 0; x < W; ++x) {
            const double v = std::abs(corr(y, x));
            if (v > peak) { peak = v; peak_x = x; peak_y = y; }
        }
    }
    // Indices past the midpoint are negative shifts
    double dx = (peak_x > W / 2) ? (double)peak_x - (double)W : (double)peak_x;
    double dy = (peak_y > H / 2) ? (double)peak_y - (double)H : (double)peak_y;

    if (usf <= 1) return {dx, dy};

    // Subpixel refinement: evaluate the inverse DFT of the cross-power spectrum
    // on a small grid of 1/usf steps around the integer peak (the matrix-multiply
    // DFT of skimage's _upsampled_dft). Done separably, so the cost is
    // O(M*(H*W + M*W)) rather than O(M^2*H*W).
    dx = std::round(dx * usf) / usf;
    dy = std::round(dy * usf) / usf;
    const int M = static_cast<int>(std::ceil(usf * 1.5));
    const double dft_shift = std::floor(M / 2.0);
    const double offset_x = dft_shift - dx * usf;
    const double offset_y = dft_shift - dy * usf;

    // rows: tmp[a, kx] = sum_ky product[ky, kx] * exp(+2πi (a - offset_y) fy / (H*usf))
    std::vector<cpx> tmp(static_cast<size_t>(M) * W, cpx(0.0, 0.0));
    const double row_scale = 2.0 * M_PI / (static_cast<double>(H) * usf);
    for (int a = 0; a < M; ++a) {
        const double ra = (a - offset_y) * row_scale;
        for (size_t ky = 0; ky < H; ++ky) {
            const double phase = ra * fft_freq(ky, H);
            const cpx w(std::cos(phase), std::sin(phase));
            const cpx* row = &product.data[ky * W];
            cpx* dst = &tmp[static_cast<size_t>(a) * W];
            for (size_t kx = 0; kx < W; ++kx) dst[kx] += w * row[kx];
        }
    }
    // cols: cc[a, b] = sum_kx tmp[a, kx] * exp(+2πi (b - offset_x) fx / (W*usf))
    const double col_scale = 2.0 * M_PI / (static_cast<double>(W) * usf);
    int best_a = 0, best_b = 0;
    double best = -std::numeric_limits<double>::infinity();
    for (int a = 0; a < M; ++a) {
        for (int b = 0; b < M; ++b) {
            const double rb = (b - offset_x) * col_scale;
            cpx acc(0.0, 0.0);
            for (size_t kx = 0; kx < W; ++kx) {
                const double phase = rb * fft_freq(kx, W);
                acc += tmp[static_cast<size_t>(a) * W + kx] * cpx(std::cos(phase), std::sin(phase));
            }
            const double v = std::abs(acc);
            if (v > best) { best = v; best_a = a; best_b = b; }
        }
    }

    dx += (best_b - dft_shift) / usf;
    dy += (best_a - dft_shift) / usf;
    return {dx, dy};
}

// ---------- Simple utilities ----------
static Image sum_images(const std::vector<Image>& imgs) {
    if (imgs.empty()) return Image();
    Image acc(imgs[0].W, imgs[0].H, 0.0);
    for (const auto& im: imgs) {
        if (im.W!=acc.W || im.H!=acc.H) throw std::runtime_error("sum_images: size mismatch");
        const size_t n = acc.data.size();
#if TTTRLIB_COMPILE_AVX
        if (g_use_avx) {
            add_inplace_avx(acc.data.data(), im.data.data(), n);
        } else
#endif
        {
            for (size_t i=0;i<n;++i) acc.data[i]+=im.data[i];
        }
    }
    return acc;
}

static void clamp_non_negative(Image& img) {
    for (double& v : img.data) if (v < 0.0) v = 0.0;
}

static std::vector<Image> detector_images_from_array(const double* data, size_t dim0, size_t dim1, size_t dim2, bool channels_last) {
    if (!data) throw std::runtime_error("CLSMSuperRes: null input array");

    size_t D = 0, H = 0, W = 0;
    if (channels_last) {
        H = dim0;
        W = dim1;
        D = dim2;
    } else {
        D = dim0;
        H = dim1;
        W = dim2;
    }

    if (D == 0 || H == 0 || W == 0) {
        throw std::runtime_error("CLSMSuperRes: invalid array dimensions (zero sized)");
    }

    std::vector<Image> det_imgs;
    det_imgs.reserve(D);
    for (size_t d = 0; d < D; ++d) {
        det_imgs.emplace_back(W, H, 0.0);
    }

    if (channels_last) {
        for (size_t y = 0; y < H; ++y) {
            for (size_t x = 0; x < W; ++x) {
                const size_t base = ((size_t)y * W + x) * D;
                for (size_t d = 0; d < D; ++d) {
                    det_imgs[d](y, x) = data[base + d];
                }
            }
        }
    } else {
        const size_t plane = H * W;
        for (size_t d = 0; d < D; ++d) {
            const double* src = data + d * plane;
            Image& im = det_imgs[d];
            for (size_t y = 0; y < H; ++y) {
                for (size_t x = 0; x < W; ++x) {
                    im(y, x) = src[y * W + x];
                }
            }
        }
    }

    return det_imgs;
}

static void ensure_geometry(const std::vector<Image>& det_imgs) {
    if (det_imgs.empty()) {
        throw std::runtime_error("CLSMSuperRes: no detector images available");
    }
    const size_t refW = det_imgs[0].W;
    const size_t refH = det_imgs[0].H;
    for (const auto& im : det_imgs) {
        if (im.W != refW || im.H != refH) {
            throw std::runtime_error("CLSMSuperRes: detector images have inconsistent shapes");
        }
    }
}

// ---------- Adaptive pixel reassignment ----------

// Shift vectors of every detector element against a reference element, matching
// BrightEyes-ISM APR_lib.ShiftVectors: apodize, optionally denoise, then phase
// cross-correlate at 1/usf precision. The returned vectors are the shifts to
// apply to each channel image.
static std::vector<std::pair<double,double>> apr_shift_vectors(
        const std::vector<Image>& det_imgs, int usf, size_t ref_idx,
        bool apodize, double filter_sigma)
{
    const size_t D = det_imgs.size();
    if (D == 0) return {};
    if (ref_idx >= D) ref_idx = 0;
    if (usf < 1) usf = 1;

    std::vector<Image> work;
    work.reserve(D);
    const Image window = apodize ? hann2d(det_imgs[0].W, det_imgs[0].H) : Image();
    for (size_t i = 0; i < D; ++i) {
        Image im = det_imgs[i];
        if (apodize)
            for (size_t k = 0; k < im.data.size(); ++k) im.data[k] *= window.data[k];
        if (filter_sigma > 0.0) gaussian_blur_inplace(im, filter_sigma);
        work.emplace_back(std::move(im));
    }

    std::vector<std::pair<double,double>> shifts(D, {0.0, 0.0});
    for (size_t i = 0; i < D; ++i) {
        if (i == ref_idx) continue;
        shifts[i] = phase_cross_correlation(work[ref_idx], work[i], usf);
    }
    return shifts;
}

// Register every channel with its shift vector and sum, matching
// APR_lib.Reassignment(mode='fourier') followed by the channel sum.
static std::vector<Image> apr_register(
        const std::vector<Image>& det_imgs,
        const std::vector<std::pair<double,double>>& shifts)
{
    std::vector<Image> out;
    out.reserve(det_imgs.size());
    for (size_t i = 0; i < det_imgs.size(); ++i) {
        const auto [dx, dy] = shifts[i];
        Image shifted = (std::abs(dx) > 1e-9 || std::abs(dy) > 1e-9)
                ? subpixel_shift(det_imgs[i], dx, dy)
                : det_imgs[i];
        clamp_non_negative(shifted);
        out.emplace_back(std::move(shifted));
    }
    return out;
}

static void apr_reconstruction_core(
    std::vector<Image>& det_imgs,
    double** output, int* dim1, int* dim2, int* dim3,
    int usf, int ref_idx, double filter_sigma)
{
    ensure_geometry(det_imgs);

    const size_t W = det_imgs[0].W;
    const size_t H = det_imgs[0].H;
    const size_t D = det_imgs.size();

    // Default reference is the central detector element
    const size_t ref = (ref_idx >= 0 && static_cast<size_t>(ref_idx) < D)
            ? static_cast<size_t>(ref_idx) : D / 2;

    const auto shifts = apr_shift_vectors(det_imgs, usf, ref, true, filter_sigma);
    const Image sum_img = sum_images(apr_register(det_imgs, shifts));

    *dim1 = 1;
    *dim2 = static_cast<int>(H);
    *dim3 = static_cast<int>(W);
    double* buf = static_cast<double*>(std::malloc(H * W * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    std::copy(sum_img.data.begin(), sum_img.data.end(), buf);
    *output = buf;
}

// ---------- Focus-ISM ----------

// Detector-element coordinates on the square lattice the fingerprint model is
// defined on: linspace(-(side/2), side/2, side) along each axis, element c at
// (row = c / side, col = c % side) -- the layout FocusISM_lib reshapes into.
static std::vector<std::pair<double,double>> detector_lattice(size_t n_det) {
    const size_t side = static_cast<size_t>(std::llround(std::sqrt((double)n_det)));
    if (side * side != n_det) {
        throw std::runtime_error(
            "CLSMSuperRes: focus-ISM needs a square detector array, or explicit "
            "detector_coords; got " + std::to_string(n_det) + " channels");
    }
    const double half = static_cast<double>(side / 2);
    std::vector<std::pair<double,double>> coords(n_det);
    for (size_t c = 0; c < n_det; ++c) {
        const size_t row = c / side, col = c % side;
        const double x = (side > 1)
                ? -half + 2.0 * half * (double)col / (double)(side - 1) : 0.0;
        const double y = (side > 1)
                ? -half + 2.0 * half * (double)row / (double)(side - 1) : 0.0;
        coords[c] = {x, y};
    }
    return coords;
}

// Width of the in-focus fingerprint: least squares fit of A*exp(-r^2/(2*sigma^2))
// to the normalized fingerprint (FocusISM_lib.FitFingerprint). A enters
// linearly, so only sigma needs searching.
static double fit_fingerprint_sigma(const std::vector<double>& fingerprint,
                                    const std::vector<double>& radial2) {
    const size_t n = fingerprint.size();
    double r2max = 0.0;
    for (double r2 : radial2) r2max = std::max(r2max, r2);
    if (!(r2max > 0.0)) return 1.0;

    const double lo = 0.05 * std::sqrt(r2max);
    const double hi = 20.0 * std::sqrt(r2max);
    double best_sigma = 1.0, best_cost = std::numeric_limits<double>::infinity();

    auto sweep = [&](double a, double b, int steps) {
        for (int s = 0; s < steps; ++s) {
            const double t = (steps == 1) ? 0.5 : (double)s / (steps - 1);
            const double sigma = a * std::pow(b / a, t);
            const double denom = 2.0 * sigma * sigma;
            double fg = 0.0, gg = 0.0;
            for (size_t i = 0; i < n; ++i) {
                const double g = std::exp(-radial2[i] / denom);
                fg += fingerprint[i] * g;
                gg += g * g;
            }
            if (!(gg > 0.0)) continue;
            const double A = fg / gg;
            double sse = 0.0;
            for (size_t i = 0; i < n; ++i) {
                const double d = A * std::exp(-radial2[i] / denom) - fingerprint[i];
                sse += d * d;
            }
            if (sse < best_cost) { best_cost = sse; best_sigma = sigma; }
        }
    };

    sweep(lo, hi, 128);
    sweep(best_sigma * 0.8, best_sigma * 1.25, 32);
    return best_sigma;
}

static void focus_ism_core(
    std::vector<Image>& det_imgs,
    double sigma_bound, double threshold, int calibration_size,
    bool parallelize,
    double** output, int* dim1, int* dim2, int* dim3,
    const double* detector_coords, int detector_coords_len)
{
    ensure_geometry(det_imgs);
    const size_t W = det_imgs[0].W;
    const size_t H = det_imgs[0].H;

    // Detectors the caller supplied coordinates for bound the channel count
    size_t n_det = det_imgs.size();
    if (detector_coords && detector_coords_len > 1)
        n_det = std::min(n_det, static_cast<size_t>(detector_coords_len / 2));
    det_imgs.resize(n_det);

    std::vector<std::pair<double,double>> detector_positions;
    if (detector_coords && detector_coords_len >= 2 * static_cast<int>(n_det)) {
        detector_positions.resize(n_det);
        for (size_t i = 0; i < n_det; ++i)
            detector_positions[i] = {detector_coords[2*i], detector_coords[2*i + 1]};
    } else {
        detector_positions = detector_lattice(n_det);
    }

    std::vector<double> radial2(n_det, 0.0);
    for (size_t i = 0; i < n_det; ++i) {
        const double x = detector_positions[i].first;
        const double y = detector_positions[i].second;
        radial2[i] = x*x + y*y;
    }

    // Step 1: adaptive pixel reassignment, as focusISM() does before fitting.
    const auto shifts = apr_shift_vectors(det_imgs, 10, n_det / 2, true, 1.0);
    const std::vector<Image> ism_imgs = apr_register(det_imgs, shifts);
    const Image ism_sum = sum_images(ism_imgs);

    // Step 2: calibrate the in-focus fingerprint width on a central patch of
    // the raw data (focusISM() asks the user to drag out this region).
    size_t patch = static_cast<size_t>(std::max(1, calibration_size));
    patch = std::min({patch, W, H});
    const size_t y0 = (H > patch) ? (H - patch) / 2 : 0;
    const size_t x0 = (W > patch) ? (W - patch) / 2 : 0;

    std::vector<double> fingerprint(n_det, 0.0);
    for (size_t d = 0; d < n_det; ++d) {
        double acc = 0.0;
        const Image& det = det_imgs[d];
        for (size_t y = 0; y < patch; ++y)
            for (size_t x = 0; x < patch; ++x)
                acc += det(y0 + y, x0 + x);
        fingerprint[d] = acc;
    }
    const double max_fp = *std::max_element(fingerprint.begin(), fingerprint.end());
    if (max_fp > 0.0) for (double& v : fingerprint) v /= max_fp;

    double sigma_A = fit_fingerprint_sigma(fingerprint, radial2);
    if (!std::isfinite(sigma_A) || sigma_A < 1e-3) sigma_A = 1.0;

    // sigma_B is bounded below at sigma_bound * sigma_A (focusISM's
    // sigma_B_bound, default 2). The upper end is where the Gaussian is flat
    // across the array to within rounding, so B has stopped changing.
    if (!(sigma_bound > 0.0)) sigma_bound = 2.0;
    const double sigma_lower = sigma_bound * sigma_A;
    const double sigma_upper = std::max(sigma_lower * 1.01, 40.0 * sigma_A);

    std::vector<double> gaussA;
    normalized_gaussian(radial2, sigma_A, gaussA);

    Image focus_img(W, H, 0.0);
    Image background_img(W, H, 0.0);

    const double eps_norm = 1e-12;
    const int coarse_steps = 32;
    const int fine_steps = 16;

    // Per pixel: fit the micro-image to (1-B)*g(sigma_A) + B*g(sigma_B), i.e.
    // FocusISM_lib.pixel_fit_2. B enters the model linearly once sigma_B is
    // fixed, so a search over sigma_B with the exact least-squares B at each
    // step replaces the two-parameter curve_fit. Both Gaussians are normalized
    // over the array, so summing the per-channel signal and background images
    // over channels leaves exactly N*(1-B) and N*B.
    const int n_rows = static_cast<int>(H);
    #pragma omp parallel for schedule(static) if(parallelize && H * W >= 4096)
    for (int yy = 0; yy < n_rows; ++yy) {
        const size_t y = static_cast<size_t>(yy);
        std::vector<double> micro(n_det, 0.0), micro_norm(n_det, 0.0), gauss(n_det, 0.0);
        for (size_t x = 0; x < W; ++x) {
            double total = 0.0;
            for (size_t d = 0; d < n_det; ++d) {
                const double v = ism_imgs[d](y, x);
                micro[d] = v;
                total += v;
            }

            // Below the photon threshold everything counts as background
            if (!(total > threshold) || total <= eps_norm) {
                focus_img(y, x) = 0.0;
                background_img(y, x) = total;
                continue;
            }

            const double inv_tot = 1.0 / total;
            for (size_t i = 0; i < n_det; ++i) micro_norm[i] = micro[i] * inv_tot;

            double best_B = 0.0;
            double best_sigma = sigma_lower;
            double best_cost = std::numeric_limits<double>::infinity();

            auto sweep_sigma = [&](double min_sigma, double max_sigma, int steps) {
                for (int si = 0; si < steps; ++si) {
                    const double t = (steps == 1) ? 0.5 : (double)si / (steps - 1);
                    double ratio = max_sigma / std::max(min_sigma, 1e-6);
                    if (ratio < 1.0) ratio = 1.0;
                    const double sigmaB = min_sigma * std::pow(ratio, t);
                    normalized_gaussian(radial2, sigmaB, gauss);

                    double num = 0.0, den = 0.0;
#if TTTRLIB_COMPILE_AVX
                    if (g_use_avx && g_use_fma) {
                        fit_num_den_avx(gauss.data(), gaussA.data(), micro_norm.data(),
                                        n_det, num, den);
                    } else
#elif TTTRLIB_COMPILE_NEON
                    if (g_use_neon) {
                        fit_num_den_neon(gauss.data(), gaussA.data(), micro_norm.data(),
                                         n_det, num, den);
                    } else
#endif
                    {
                        for (size_t i = 0; i < n_det; ++i) {
                            const double di = gauss[i] - gaussA[i];
                            const double resid = micro_norm[i] - gaussA[i];
                            num += di * resid;
                            den += di * di;
                        }
                    }
                    double B = (den > eps_norm) ? (num / den) : 0.0;
                    if (!std::isfinite(B)) B = 0.0;
                    B = std::clamp(B, 0.0, 1.0);

                    double sse = 0.0;
#if TTTRLIB_COMPILE_AVX
                    if (g_use_avx && g_use_fma) {
                        sse = fit_sse_avx(gauss.data(), gaussA.data(), micro_norm.data(),
                                          n_det, B);
                    } else
#elif TTTRLIB_COMPILE_NEON
                    if (g_use_neon) {
                        sse = fit_sse_neon(gauss.data(), gaussA.data(), micro_norm.data(),
                                           n_det, B);
                    } else
#endif
                    {
                        for (size_t i = 0; i < n_det; ++i) {
                            const double di = gauss[i] - gaussA[i];
                            const double diff = gaussA[i] + B * di - micro_norm[i];
                            sse += diff * diff;
                        }
                    }

                    if (sse < best_cost) {
                        best_cost = sse;
                        best_B = B;
                        best_sigma = sigmaB;
                    }
                }
            };

            sweep_sigma(sigma_lower, sigma_upper, coarse_steps);
            const double refine_lo = std::max(sigma_lower, best_sigma * 0.8);
            const double refine_hi = std::min(sigma_upper,
                    std::max(refine_lo * 1.01, best_sigma * 1.25));
            sweep_sigma(refine_lo, refine_hi, fine_steps);

            background_img(y, x) = total * best_B;
            focus_img(y, x) = total * (1.0 - best_B);
        }
    }

    // Output planes: in-focus signal, out-of-focus background, APR sum --
    // the three arrays focusISM() returns.
    *dim1 = 3;
    *dim2 = static_cast<int>(H);
    *dim3 = static_cast<int>(W);
    const size_t plane = H * W;
    double* buf = static_cast<double*>(std::malloc(3 * plane * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    std::copy(focus_img.data.begin(), focus_img.data.end(), buf);
    std::copy(background_img.data.begin(), background_img.data.end(), buf + plane);
    std::copy(ism_sum.data.begin(), ism_sum.data.end(), buf + 2 * plane);
    *output = buf;
}

// ---------- SOFISM ----------
//
// Super-resolution optical fluctuation image scanning microscopy: Sroda et al.,
// Optica 7, 1308 (2020); the formulation followed here is the one restated by
// Beck et al., arXiv:2606.16508.
//
// At every scan position the array detector records a short time series, and
// the contrast comes from the temporal *fluctuations* of independently blinking
// emitters rather than from their mean brightness. For every pair of detector
// elements the fluctuation cross-correlation is formed,
//
//     C_ij(r, tau) = 1/(N_t - tau) * sum_t dI_i(r,t) dI_j(r,t+tau)          (1)
//
// Cross-terms between different emitters average away, so what survives is a
// per-emitter quantity whose effective PSF is the *product* of the two
// elements' PSFs -- narrower than either, by sqrt(2) for Gaussians.
//
// The pair behaves as one virtual detector sitting midway between the two
// physical elements, so it is reassigned by the mean of their two ISM shifts,
//
//     v_ij = (v_i + v_j) / 2                                                (2)
//
// and the shifted correlation images are summed. Autocorrelation terms (i == j)
// are excluded by default: their shot noise does not cancel and rides straight
// into the result as a bias.
static void sofism_core(
    const double* data, size_t n_time, size_t n_det, size_t ny, size_t nx,
    int lag, int usf, int ref_idx, double filter_sigma, bool include_auto,
    double** output, int* out_dim1, int* out_dim2)
{
    if (n_time < 2) throw std::runtime_error(
        "CLSMSuperRes: SOFISM needs at least two time bins per scan position");
    if (lag < 0 || static_cast<size_t>(lag) >= n_time) throw std::runtime_error(
        "CLSMSuperRes: SOFISM lag must be within the time series");

    const size_t n_pixel = ny * nx;
    const size_t n_pairs_time = n_time - static_cast<size_t>(lag);

    // Time-averaged detector images: the ISM cube the shift vectors come from,
    // and the means the fluctuations are taken about.
    std::vector<Image> mean_imgs;
    mean_imgs.reserve(n_det);
    for (size_t d = 0; d < n_det; ++d) {
        Image m(nx, ny, 0.0);
        for (size_t t = 0; t < n_time; ++t) {
            const double* src = data + (t * n_det + d) * n_pixel;
            for (size_t p = 0; p < n_pixel; ++p) m.data[p] += src[p];
        }
        const double inv = 1.0 / static_cast<double>(n_time);
        for (double& v : m.data) v *= inv;
        mean_imgs.push_back(std::move(m));
    }

    const size_t ref = (ref_idx >= 0 && static_cast<size_t>(ref_idx) < n_det)
            ? static_cast<size_t>(ref_idx) : n_det / 2;
    const auto shifts = apr_shift_vectors(mean_imgs, usf, ref, true, filter_sigma);

    Image accumulator(nx, ny, 0.0);
    Image correlation(nx, ny, 0.0);

    // C_ij = <I_i(t) I_j(t+lag)> - <I_i><I_j>, symmetrized over the pair order
    // so that one shifted image serves both (i,j) and (j,i).
    auto correlate = [&](size_t i, size_t j, Image& out) {
        std::fill(out.data.begin(), out.data.end(), 0.0);
        const bool symmetrise = (i != j) && (lag != 0);
        for (size_t t = 0; t + static_cast<size_t>(lag) < n_time; ++t) {
            const double* a = data + (t * n_det + i) * n_pixel;
            const double* b = data + ((t + lag) * n_det + j) * n_pixel;
            if (symmetrise) {
                const double* a2 = data + (t * n_det + j) * n_pixel;
                const double* b2 = data + ((t + lag) * n_det + i) * n_pixel;
                for (size_t p = 0; p < n_pixel; ++p)
                    out.data[p] += 0.5 * (a[p] * b[p] + a2[p] * b2[p]);
            } else {
                for (size_t p = 0; p < n_pixel; ++p) out.data[p] += a[p] * b[p];
            }
        }
        const double inv = 1.0 / static_cast<double>(n_pairs_time);
        const Image& mi = mean_imgs[i];
        const Image& mj = mean_imgs[j];
        for (size_t p = 0; p < n_pixel; ++p)
            out.data[p] = out.data[p] * inv - mi.data[p] * mj.data[p];
    };

    for (size_t i = 0; i < n_det; ++i) {
        for (size_t j = include_auto ? i : i + 1; j < n_det; ++j) {
            correlate(i, j, correlation);
            const double dx = 0.5 * (shifts[i].first + shifts[j].first);
            const double dy = 0.5 * (shifts[i].second + shifts[j].second);
            const Image shifted = (std::abs(dx) > 1e-9 || std::abs(dy) > 1e-9)
                    ? subpixel_shift(correlation, dx, dy) : correlation;
            for (size_t p = 0; p < n_pixel; ++p)
                accumulator.data[p] += shifted.data[p];
        }
    }

    *out_dim1 = static_cast<int>(ny);
    *out_dim2 = static_cast<int>(nx);
    double* buf = static_cast<double*>(std::malloc(n_pixel * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    std::copy(accumulator.data.begin(), accumulator.data.end(), buf);
    *output = buf;
}

// ---------- s2ISM ----------
//
// Adaptive maximum-likelihood deconvolution of an array-detector dataset over a
// stack of axial planes: Zunino et al., "Structured detection for simultaneous
// super-resolution and optical sectioning in laser scanning microscopy",
// Nat. Photonics (2025). Ported from the reference implementation
// s2ism/s2ism.py (amd_update_fft, amd_stop, max_likelihood_reconstruction).
//
// Unlike APR this is not a reassignment: the detector array is treated as a set
// of images of one object seen through Nch different PSFs, and a multi-image
// Richardson-Lucy iteration inverts them jointly. Giving the object several
// axial planes with their own PSFs is what buys the sectioning -- out-of-focus
// signal is explained by the out-of-focus planes rather than smeared into the
// focal one.
//
//   est_ch   = sum_z  obj_z (*) psf_z,ch
//   frac_ch  = data_ch / est_ch          (0 where est_ch < eps)
//   upd_z    = sum_ch frac_ch (corr) psf_z,ch
//   obj_z   <- obj_z * upd_z
//
// The PSF of each plane is normalized over (channel, y, x) so the update needs
// no further scaling, and the correlation uses the conjugate spectrum, which on
// the origin-centred PSF is exactly the reference's flipped array.
static void s2ism_core(
    const double* data,          // (n_ch, ny, nx)
    const double* psf,           // (nz, n_ch, ny, nx)
    size_t nz, size_t n_ch, size_t ny, size_t nx,
    int max_iter, double threshold, bool auto_stop, bool init_from_sum,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3)
{
    if (nz == 0 || n_ch == 0 || ny == 0 || nx == 0)
        throw std::runtime_error("CLSMSuperRes: s2ISM given an empty array");
    if (max_iter < 1)
        throw std::runtime_error("CLSMSuperRes: s2ISM needs at least one iteration");

    const size_t n_pixel = ny * nx;
    const double eps = std::numeric_limits<float>::epsilon();

    // Per-plane PSF normalization over (channel, y, x), and its spectrum with
    // the centre rolled to the origin so a delta PSF is the identity.
    // Two spectra per (plane, element): the PSF for the forward model, and the
    // PSF flipped in x and y for the correlation. The flip is taken literally
    // from the reference (numpy flip, i.e. about index (N-1)/2); conjugating
    // the spectrum instead would be a circular flip about index 0 and lands one
    // sample off in each axis.
    std::vector<CImage> psf_fft(nz * n_ch), psf_m_fft(nz * n_ch);
    for (size_t z = 0; z < nz; ++z) {
        double norm = 0.0;
        for (size_t i = 0; i < n_ch * n_pixel; ++i) norm += psf[(z * n_ch) * n_pixel + i];
        if (!(norm > 0.0)) norm = 1.0;
        for (size_t c = 0; c < n_ch; ++c) {
            Image kernel(nx, ny, 0.0), mirrored(nx, ny, 0.0);
            const double* src = psf + ((z * n_ch) + c) * n_pixel;
            for (size_t y = 0; y < ny; ++y) {
                for (size_t x = 0; x < nx; ++x) {
                    const size_t yy = (y + ny - ny / 2) % ny;   // ifftshift
                    const size_t xx = (x + nx - nx / 2) % nx;
                    kernel.data[yy * nx + xx] = src[y * nx + x] / norm;
                    mirrored.data[yy * nx + xx] =
                            src[(ny - 1 - y) * nx + (nx - 1 - x)] / norm;
                }
            }
            fft2d::fft2(kernel, psf_fft[z * n_ch + c]);
            fft2d::fft2(mirrored, psf_m_fft[z * n_ch + c]);
        }
    }

    // Object initialization: a flat object carrying the measured flux, or the
    // channel sum shared out over the planes.
    double total = 0.0;
    for (size_t i = 0; i < n_ch * n_pixel; ++i) total += data[i];

    std::vector<Image> obj(nz, Image(nx, ny, 0.0));
    if (init_from_sum) {
        for (size_t z = 0; z < nz; ++z)
            for (size_t c = 0; c < n_ch; ++c)
                for (size_t p = 0; p < n_pixel; ++p)
                    obj[z].data[p] += data[c * n_pixel + p] / static_cast<double>(nz);
    } else {
        const double flat = total / static_cast<double>(nz * n_pixel);
        for (auto& o : obj) std::fill(o.data.begin(), o.data.end(), flat);
    }

    std::vector<CImage> obj_fft(nz), frac_fft(n_ch), work(std::max(nz, n_ch));
    Image scratch(nx, ny, 0.0);
    std::vector<Image> fraction(n_ch, Image(nx, ny, 0.0));

    bool pre_flag = true, running = true;
    for (int k = 1; k <= max_iter && running; ++k) {
        for (size_t z = 0; z < nz; ++z) fft2d::fft2(obj[z], obj_fft[z]);

        // forward model, then the data/model ratio
        for (size_t c = 0; c < n_ch; ++c) {
            CImage acc(nx, ny, cpx(0.0, 0.0));
            for (size_t z = 0; z < nz; ++z) {
                const CImage& h = psf_fft[z * n_ch + c];
                for (size_t i = 0; i < acc.data.size(); ++i)
                    acc.data[i] += obj_fft[z].data[i] * h.data[i];
            }
            fft2d::ifft2_real(acc, scratch);
            const double* obs = data + c * n_pixel;
            for (size_t p = 0; p < n_pixel; ++p)
                fraction[c].data[p] = (scratch.data[p] < eps) ? 0.0 : obs[p] / scratch.data[p];
            fft2d::fft2(fraction[c], frac_fft[c]);
        }

        // correlate the ratio back through each plane's PSFs and update
        double focal_before = obj[nz / 2].data.size()
                ? std::accumulate(obj[nz / 2].data.begin(), obj[nz / 2].data.end(), 0.0) : 0.0;
        double all_before = 0.0;
        for (const auto& o : obj)
            all_before += std::accumulate(o.data.begin(), o.data.end(), 0.0);

        for (size_t z = 0; z < nz; ++z) {
            CImage acc(nx, ny, cpx(0.0, 0.0));
            for (size_t c = 0; c < n_ch; ++c) {
                const CImage& hm = psf_m_fft[z * n_ch + c];
                for (size_t i = 0; i < acc.data.size(); ++i)
                    acc.data[i] += frac_fft[c].data[i] * hm.data[i];
            }
            fft2d::ifft2_real(acc, scratch);
            for (size_t p = 0; p < n_pixel; ++p) {
                obj[z].data[p] *= scratch.data[p];
                if (!(obj[z].data[p] >= 0.0)) obj[z].data[p] = 0.0;
            }
        }

        // Adaptive halt: stop once the focal-plane photon count stops moving,
        // for two consecutive iterations (amd_stop's two flags).
        if (auto_stop && total > 0.0) {
            double focal_after = std::accumulate(
                    obj[nz / 2].data.begin(), obj[nz / 2].data.end(), 0.0);
            const double d_focal = (focal_after - focal_before) / total;
            (void) all_before;
            if (std::abs(d_focal) < threshold) {
                if (!pre_flag) running = false;   // second consecutive quiet step
                else pre_flag = false;
            } else {
                pre_flag = true;
            }
        }
    }

    *out_dim1 = static_cast<int>(nz);
    *out_dim2 = static_cast<int>(ny);
    *out_dim3 = static_cast<int>(nx);
    double* buf = static_cast<double*>(std::malloc(nz * n_pixel * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    for (size_t z = 0; z < nz; ++z)
        std::copy(obj[z].data.begin(), obj[z].data.end(), buf + z * n_pixel);
    *output = buf;
}

} // anonymous namespace

void CLSMSuperRes::shift_vectors(
    const double* data, int dim0, int dim1, int dim2,
    bool channels_last,
    double** output, int* out_dim1, int* out_dim2,
    int usf, int ref_idx, double filter_sigma,
    int n_det)
{
    if (!output || !out_dim1 || !out_dim2) {
        throw std::runtime_error("shift_vectors: null output/dims");
    }
    if (dim0 <= 0 || dim1 <= 0 || dim2 <= 0) {
        throw std::runtime_error("shift_vectors: invalid dimensions");
    }

    std::vector<Image> det_imgs = detector_images_from_array(
        data,
        static_cast<size_t>(dim0),
        static_cast<size_t>(dim1),
        static_cast<size_t>(dim2),
        channels_last);
    if (n_det > 0 && static_cast<size_t>(n_det) < det_imgs.size()) {
        det_imgs.resize(static_cast<size_t>(n_det));
    }
    ensure_geometry(det_imgs);

    const size_t D = det_imgs.size();
    const size_t ref = (ref_idx >= 0 && static_cast<size_t>(ref_idx) < D)
            ? static_cast<size_t>(ref_idx) : D / 2;
    const auto shifts = apr_shift_vectors(det_imgs, usf, ref, true, filter_sigma);

    double* buf = static_cast<double*>(std::malloc(2 * D * sizeof(double)));
    if (!buf) throw std::bad_alloc();
    for (size_t i = 0; i < D; ++i) {
        buf[2 * i]     = shifts[i].second;  // dy, row axis first
        buf[2 * i + 1] = shifts[i].first;   // dx
    }
    *output = buf;
    *out_dim1 = static_cast<int>(D);
    *out_dim2 = 2;
}

void CLSMSuperRes::apr_reconstruction(
    const double* data, int dim0, int dim1, int dim2,
    bool channels_last,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3,
    int usf, int ref_idx, double filter_sigma,
    int n_det)
{
    if (!output || !out_dim1 || !out_dim2 || !out_dim3) {
        throw std::runtime_error("apr_reconstruction: null output/dims");
    }
    if (dim0 <= 0 || dim1 <= 0 || dim2 <= 0) {
        throw std::runtime_error("apr_reconstruction: invalid dimensions");
    }

    std::vector<Image> det_imgs = detector_images_from_array(
        data,
        static_cast<size_t>(dim0),
        static_cast<size_t>(dim1),
        static_cast<size_t>(dim2),
        channels_last);

    if (n_det > 0 && static_cast<size_t>(n_det) < det_imgs.size()) {
        det_imgs.resize(static_cast<size_t>(n_det));
    }

    apr_reconstruction_core(det_imgs, output, out_dim1, out_dim2, out_dim3,
                            usf, ref_idx, filter_sigma);
}

void CLSMSuperRes::s2ism_reconstruction(
    const double* data, int n_ch, int ny, int nx,
    const double* psf, int psf_nz, int psf_nch, int psf_ny, int psf_nx,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3,
    int max_iter, double threshold, bool auto_stop, bool init_from_sum)
{
    if (!output || !out_dim1 || !out_dim2 || !out_dim3)
        throw std::runtime_error("s2ism_reconstruction: null output/dims");
    if (psf_nch != n_ch || psf_ny != ny || psf_nx != nx)
        throw std::runtime_error(
            "s2ism_reconstruction: the PSF must be (nz, n_ch, ny, nx) matching the data");
    s2ism_core(data, psf, static_cast<size_t>(psf_nz), static_cast<size_t>(n_ch),
               static_cast<size_t>(ny), static_cast<size_t>(nx),
               max_iter, threshold, auto_stop, init_from_sum,
               output, out_dim1, out_dim2, out_dim3);
}

void CLSMSuperRes::sofism_reconstruction(
    const double* data, int n_time, int n_det, int ny, int nx,
    double** output, int* out_dim1, int* out_dim2,
    int lag, int usf, int ref_idx, double filter_sigma, bool include_auto)
{
    if (!output || !out_dim1 || !out_dim2) {
        throw std::runtime_error("sofism_reconstruction: null output/dims");
    }
    if (n_time <= 0 || n_det <= 0 || ny <= 0 || nx <= 0) {
        throw std::runtime_error("sofism_reconstruction: invalid dimensions");
    }
    sofism_core(data, static_cast<size_t>(n_time), static_cast<size_t>(n_det),
                static_cast<size_t>(ny), static_cast<size_t>(nx),
                lag, usf, ref_idx, filter_sigma, include_auto,
                output, out_dim1, out_dim2);
}

void CLSMSuperRes::focus_reconstruction(
    const double* data,
    int dim0, int dim1, int dim2,
    bool channels_last,
    double** output, int* out_dim1, int* out_dim2, int* out_dim3,
    double sigma_bound, double threshold, int calibration_size,
    bool parallelize,
    int n_det,
    const double* detector_coords, int detector_coords_len)
{
    if (!output || !out_dim1 || !out_dim2 || !out_dim3) {
        throw std::runtime_error("focus_reconstruction: null output/dims");
    }
    if (dim0 <= 0 || dim1 <= 0 || dim2 <= 0) {
        throw std::runtime_error("focus_reconstruction: invalid dimensions");
    }

    std::vector<Image> det_imgs = detector_images_from_array(
        data,
        static_cast<size_t>(dim0),
        static_cast<size_t>(dim1),
        static_cast<size_t>(dim2),
        channels_last);

    if (n_det > 0 && static_cast<size_t>(n_det) < det_imgs.size()) {
        det_imgs.resize(static_cast<size_t>(n_det));
    }

    focus_ism_core(det_imgs, sigma_bound, threshold, calibration_size, parallelize,
                   output, out_dim1, out_dim2, out_dim3,
                   detector_coords, detector_coords_len);
}
