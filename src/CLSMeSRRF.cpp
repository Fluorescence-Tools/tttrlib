/*
 * CLSMeSRRF.cpp
 *
 * Implementation of photon-level eSRRF for CLSM data.
 *
 * See CLSMeSRRF.h for detailed documentation.
 */

#include "CLSMeSRRF.h"
#include "CLSMImage.h"
#include "TTTR.h"
#include "Random.h"  // centralized counter-based RNG (Philox, PCG, SplitMix64, MT19937)
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include <cctype>
#include <stdexcept>
#include <vector>
#include <utility>

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

    // Bicubic interpolation at continuous position (x, y)
    double interpolated_value(const double* img, int nx, int ny, double x, double y) {
        int u0 = static_cast<int>(std::floor(x));
        int v0 = static_cast<int>(std::floor(y));

        double q = 0.0;
        for (int j = 0; j <= 3; ++j) {
            int v = std::max(0, std::min(ny - 1, v0 - 1 + j));
            double p = 0.0;
            for (int i = 0; i <= 3; ++i) {
                int u = std::max(0, std::min(nx - 1, u0 - 1 + i));
                p += img[v * nx + u] * cubic(x - u);
            }
            q += p * cubic(y - v);
        }
        return q;
    }

    // Safe access with bounds checking
    double safe_get(const double* arr, int width, int height, int x, int y) {
        x = std::max(0, std::min(width - 1, x));
        y = std::max(0, std::min(height - 1, y));
        return arr[y * width + x];
    }
}

// ========================================================================
// RGC Map Computation
// ========================================================================

void CLSMeSRRF::rgc_map(
    const double* img,
    int nx,
    int ny,
    int magnification,
    double fwhm,
    int sensitivity,
    bool intensity_weighting,
    double** output,
    int* out_ny,
    int* out_nx
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

    // Compute gradient (2-point backward difference)
    int gx_size = 2 * nx;
    int gy_size = 2 * ny;
    std::vector<double> Gx(gx_size * gy_size, 0.0);
    std::vector<double> Gy(gx_size * gy_size, 0.0);

    for (int y = 0; y < ny; ++y) {
        for (int x = 0; x < nx; ++x) {
            int x0 = std::max(x - 1, 0);
            int x1 = std::min(x + 1, nx - 1);
            int y0 = std::max(y - 1, 0);
            int y1 = std::min(y + 1, ny - 1);

            // 2-point backward difference
            Gx[y * gx_size + x] = img[y * nx + x] - img[y * nx + x0];
            Gy[y * gx_size + x] = img[y * nx + x] - img[y0 * nx + x];
        }
    }

    // 2x bicubic upsampling of gradient
    std::vector<double> Gx_up(my * mx, 0.0);
    std::vector<double> Gy_up(my * mx, 0.0);

    for (int yM = 0; yM < my; ++yM) {
        for (int xM = 0; xM < mx; ++xM) {
            double x = xM / 2.0;
            double y = yM / 2.0;
            Gx_up[yM * mx + xM] = interpolated_value(Gx.data(), gx_size, gy_size, x, y);
            Gy_up[yM * mx + xM] = interpolated_value(Gy.data(), gx_size, gy_size, x, y);
        }
    }

    // Constants matching OpenCL
    double vxy_offset = 0.5;
    int vxy_ArrayShift = 1;
    double vxy_PixelShift = 0.0;  // drift correction, zero for static

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

                for (int i = i_min; i <= i_max; ++i) {
                    // vx position in continuous space
                    double vx = (static_cast<int>(gradient_mag * (xc - vxy_PixelShift)) + i)
                                / static_cast<double>(gradient_mag) + vxy_PixelShift;

                    double dx = vx - xc;
                    double dy = vy - yc;
                    double distance = std::sqrt(dx * dx + dy * dy);

                    if (distance == 0.0 || distance > tso) {
                        continue;
                    }

                    // Fetch gradient with half-pixel offset
                    int gx_idx = static_cast<int>(gradient_mag * (vx - vxy_offset) + vxy_ArrayShift);
                    int gy_idx = static_cast<int>(gradient_mag * (vy - vxy_offset));
                    gx_idx = std::max(0, std::min(2 * nx - 1, gx_idx));
                    gy_idx = std::max(0, std::min(2 * ny - 1, gy_idx));

                    double Gx_val = Gx_up[gy_idx * mx + gx_idx];
                    double Gy_val = Gy_up[gy_idx * mx + gx_idx];

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
    *out_ny = my;
    *out_nx = mx;
}

double* CLSMeSRRF::rgc_map(
    const double* img,
    int nx,
    int ny,
    int magnification,
    double fwhm,
    int sensitivity,
    bool intensity_weighting
) {
    double* output = nullptr;
    int out_ny, out_nx;
    rgc_map(img, nx, ny, magnification, fwhm, sensitivity, intensity_weighting,
            &output, &out_ny, &out_nx);
    return output;
}

// ========================================================================
// Photon Reassignment
// ========================================================================

TTTR* CLSMeSRRF::reassign_photons(
    CLSMImage* clsm,
    TTTR* tttr,
    int magnification,
    double fwhm,
    int sensitivity,
    double search_radius,
    const char* channel_mode,
    unsigned long long seed,
    const char* method
) {
    if (!clsm || !tttr) {
        throw std::invalid_argument("CLSMImage and TTTR must not be null");
    }

    // Resolve the super-resolution method (eSRRF is the default; others reserved)
    SuperResMethod sr_method = SuperResMethod::ESRRF;
    if (method) {
        std::string m(method);
        std::transform(m.begin(), m.end(), m.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (m == "uniform") sr_method = SuperResMethod::UNIFORM;
        else if (m == "sofi") sr_method = SuperResMethod::SOFI;
        else if (m == "ism") sr_method = SuperResMethod::ISM;
        // default: ESRRF
    }

    // SOFI and ISM reassignment are not yet implemented
    if (sr_method == SuperResMethod::SOFI) {
        throw std::runtime_error("SOFI reassignment not yet implemented (reserved)");
    }
    if (sr_method == SuperResMethod::ISM) {
        throw std::runtime_error("ISM reassignment not yet implemented (reserved)");
    }

    int n_channels = clsm->get_n_channels();
    int n_frames = clsm->get_n_frames();
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
        seed = global_rng_seed();
    }

    // 2. Compute intensity image and RGC field(s)
    //    For the UNIFORM method, the field is flat (all ones) — no spatial prior.
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
            // Sum only this channel's frames
            int ch_frames = clsm->get_channel_frame_count(ch);
            for (int f = 0; f < ch_frames; ++f) {
                CLSMFrame* frame = clsm->get_frame_for_channel(ch, f);
                if (!frame) continue;
                for (int l = 0; l < n_lines; ++l) {
                    for (int p = 0; p < n_pixel; ++p) {
                        // Access frame's intensity
                        int global_f = 0;  // computed below
                        // For split_by_channel, frame ordering is channel-major
                        (void)global_f;
                    }
                }
            }
            // Simplified: use the raw intensity array directly
            // In split_by_channel mode, dim1 = n_det * n_frames_per_det
            int frames_per_ch = dim1 / std::max(n_channels, 1);
            for (int f = ch * frames_per_ch; f < (ch + 1) * frames_per_ch && f < dim1; ++f) {
                for (int l = 0; l < dim2; ++l) {
                    for (int p = 0; p < dim3; ++p) {
                        img2d[l * n_pixel + p] += intensity_raw[f * dim2 * dim3 + l * dim3 + p];
                    }
                }
            }
        } else {
            // Merged: sum all frames
            for (int f = 0; f < dim1; ++f) {
                for (int l = 0; l < dim2; ++l) {
                    for (int p = 0; p < dim3; ++p) {
                        img2d[l * n_pixel + p] += intensity_raw[f * dim2 * dim3 + l * dim3 + p];
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
            rgc = static_cast<double*>(std::malloc(out_my * out_mx * sizeof(double)));
            for (int i = 0; i < out_my * out_mx; ++i) rgc[i] = 1.0;
        } else {
            // ESRRF: compute the Radial Gradient Convergence field
            rgc_map(img2d.data(), n_pixel, n_lines, magnification, fwhm, sensitivity,
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

    int sr_pixels = static_cast<int>(std::ceil(search_radius * magnification));
    int my = rgc_my[0];
    int mx = rgc_mx[0];

    #pragma omp parallel for schedule(dynamic) if(n_photons >= 10000)
    for (int i = 0; i < n_photons; ++i) {
        // Determine which RGC field to use
        int ch = 0;
        if (mode == "split" && n_channels > 1) {
            ch = std::min(photon_event[i] % n_channels, eff_channels - 1);  // simplified
        }
        double* rgc = rgc_fields[ch];

        // Current photon position in magnified pixel units
        double xM = photon_x[i] * magnification;
        double yM = photon_y[i] * magnification;

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
            x_new[i] = xM;
            y_new[i] = yM;
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

        // New macro time from magnified position:
        // macro_time = frame_offset + line * line_duration + subpixel_x * pixel_duration
        // For simplicity, encode as: y * mx + x (flat index in magnified grid)
        // The actual PTU writer will convert this to proper macro times
        new_macro_times[i] = static_cast<unsigned long long>(
            photon_frame[i] * my * mx +
            static_cast<int>(y_new[i]) * mx +
            static_cast<int>(x_new[i])
        );
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

void CLSMeSRRF::temporal_combine(
    const double* stack,
    int n_frames,
    int ny,
    int nx,
    const char* mode,
    double** output
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
        // TAC2[X] = E[X(t) X(t+1)] - E[X]^2
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
}

double* CLSMeSRRF::temporal_combine(
    const double* stack,
    int n_frames,
    int ny,
    int nx,
    const char* mode
) {
    double* output = nullptr;
    temporal_combine(stack, n_frames, ny, nx, mode, &output);
    return output;
}

// ========================================================================
// Public Photon-Position Seam
// ========================================================================

void CLSMeSRRF::get_photon_positions(
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
// PTU Output with Magnified Raster
// ========================================================================

bool CLSMeSRRF::write_ptu_magnified(
    TTTR* tttr_reassigned,
    int nx,
    int ny,
    int magnification,
    int pixel_duration,
    int line_duration,
    int frame_marker_delay,
    const char* output_filename
) {
    // TODO: Implement PTU output with magnified raster
    throw std::runtime_error("write_ptu_magnified not yet implemented");
}
