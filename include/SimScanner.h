/*!
 * \file SimScanner.h
 * \brief Discrete CLSM raster scanner driven by a per-pixel dwell-time array (PRD-005).
 *
 * Beam-scan model: the excitation + detection fields translate together over a fixed
 * fluorophore field along a raster path; per pixel the fields are positioned at the
 * pixel centre and the simulation advances for that pixel's dwell time, emitting
 * photons plus frame/line/pixel markers into the TTTR stream. The dwell-time array is
 * the inverse of the pixel-wise dwell times a CLSM reader derives. Marker values match
 * tttrlib.CLSMImage defaults so the output reconstructs directly. Additive; existing
 * tttrlib untouched.
 */
#ifndef TTTRLIB_SIMSCANNER_H
#define TTTRLIB_SIMSCANNER_H

#include <cstdint>
#include <vector>

namespace tttrlib {

/// Marker channel/type conventions (defaults match tttrlib::CLSMSettings).
struct SimMarkerConfig {
    int marker_event_type = 1;                 ///< event_type of marker events (photons use 0)
    std::vector<int> marker_frame = {4};       ///< routing channel(s) of frame markers
    int marker_line_start = 1;                 ///< routing channel of line-start markers
    int marker_line_stop = 2;                  ///< routing channel of line-stop markers
    int marker_pixel = 8;                      ///< routing channel of pixel markers
    bool emit_pixel_markers = true;            ///< emit a marker at each pixel start
    bool emit_line_stop = true;                ///< emit a line-stop marker at line end
};

/// A discrete raster scan: geometry + per-pixel dwell times + marker convention.
class SimScanner {
public:
    bool enabled = false;                      ///< false => stationary focus (FCS/point mode)
    int nx = 0, ny = 0;                        ///< pixels per line, lines per frame
    std::vector<double> dwell;                 ///< ny*nx dwell times (row-major, same units as dt)
    double pixel_dx = 0.1, pixel_dy = 0.1;     ///< µm per pixel step
    double origin_x = 0.0, origin_y = 0.0;     ///< world position of pixel (0,0)
    bool bidirectional = false;                ///< serpentine line direction
    SimMarkerConfig markers;

    SimScanner() = default;

    /// Stationary focus (no scanning).
    static SimScanner none() { return SimScanner{}; }

    /// Raster with an explicit per-pixel dwell array (length ny*nx).
    static SimScanner raster(int nx, int ny, const std::vector<double>& dwell,
                             double pixel_dx, double pixel_dy,
                             double origin_x, double origin_y,
                             const SimMarkerConfig& markers = SimMarkerConfig(),
                             bool bidirectional = false) {
        SimScanner s;
        s.enabled = true; s.nx = nx; s.ny = ny; s.dwell = dwell;
        s.pixel_dx = pixel_dx; s.pixel_dy = pixel_dy;
        s.origin_x = origin_x; s.origin_y = origin_y;
        s.markers = markers; s.bidirectional = bidirectional;
        return s;
    }

    /// Raster with a single uniform dwell time for every pixel (convenience).
    static SimScanner uniform(int nx, int ny, double dwell,
                              double pixel_dx, double pixel_dy,
                              double origin_x, double origin_y,
                              const SimMarkerConfig& markers = SimMarkerConfig(),
                              bool bidirectional = false) {
        return raster(nx, ny, std::vector<double>(size_t(nx) * ny, dwell),
                      pixel_dx, pixel_dy, origin_x, origin_y, markers, bidirectional);
    }
};

} // namespace tttrlib

#endif // TTTRLIB_SIMSCANNER_H
