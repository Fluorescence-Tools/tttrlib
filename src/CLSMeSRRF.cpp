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
#include <cmath>
#include <algorithm>
#include <cstring>
#include <stdexcept>

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
    unsigned long long seed
) {
    // TODO: Implement photon reassignment
    // This will require:
    // 1. Get photon positions via get_photon_positions
    // 2. Compute RGC field(s)
    // 3. For each photon, sample new position from RGC-weighted distribution
    // 4. Build new TTTR with reassigned positions (preserving micro_time, channel)
    throw std::runtime_error("reassign_photons not yet implemented");
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
    // TODO: Implement photon position extraction
    // This needs to expose for_each_mask_photon functionality
    throw std::runtime_error("get_photon_positions not yet implemented");
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
