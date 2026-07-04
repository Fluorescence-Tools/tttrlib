/*!
 * \file SimGrid.h
 * \brief 3D voxel grid with trilinear interpolation — the field representation for
 *        excitation and detection profiles in the photon simulator (PRD-005).
 *
 * The simulator is fully grid-based: excitation and each detector's detection
 * profile are `SimGrid`s sampled at continuous molecule positions by trilinear
 * interpolation. Analytic PSF/CEF shapes exist only as **static fillers** that
 * build a `SimGrid`; they are never evaluated at runtime. Additive; does not
 * modify any existing tttrlib class.
 */
#ifndef TTTRLIB_SIMGRID_H
#define TTTRLIB_SIMGRID_H

#include <cstddef>
#include <vector>

namespace tttrlib {

/*!
 * \brief A regular 3D scalar grid in physical (µm) coordinates.
 *
 * Storage is row-major with x fastest: `data[((iz*ny)+iy)*nx + ix]`. `(x0,y0,z0)`
 * is the physical position of voxel-centre `[0,0,0]`; `(dx,dy,dz)` the spacing.
 * Sampling outside the grid returns 0.
 */
class SimGrid {
public:
    std::vector<double> data;
    int nx = 0, ny = 0, nz = 0;
    double dx = 1, dy = 1, dz = 1;   ///< voxel spacing (µm)
    double x0 = 0, y0 = 0, z0 = 0;   ///< physical centre of voxel [0,0,0] (µm)

    SimGrid() = default;
    SimGrid(int nx_, int ny_, int nz_,
            double dx_, double dy_, double dz_,
            double x0_, double y0_, double z0_)
        : data(size_t(nx_) * ny_ * nz_, 0.0),
          nx(nx_), ny(ny_), nz(nz_), dx(dx_), dy(dy_), dz(dz_),
          x0(x0_), y0(y0_), z0(z0_) {}

    inline size_t index(int ix, int iy, int iz) const {
        return (size_t(iz) * ny + iy) * nx + ix;
    }
    inline double at_voxel(int ix, int iy, int iz) const { return data[index(ix, iy, iz)]; }
    inline void set_voxel(int ix, int iy, int iz, double v) { data[index(ix, iy, iz)] = v; }

    // --- optional two-step lookup: cheap bounding-box reject before the trilinear ----
    // Physical AABB enclosing all voxels above the build threshold (padded ±1 voxel so
    // interpolation at the box edge is unaffected). When active (`bb_x1_ >= bb_x0_`),
    // `at()` first tests the query point against this box — 6 comparisons — and returns
    // 0 immediately if outside, skipping the floor/8-load/trilinear work. Inactive by
    // default, so `at()` is exact unless a bbox is built. Building with a nonzero
    // threshold drops the sub-threshold field tail (a speed/accuracy knob, like the
    // engine's focus_threshold); building with threshold 0 is exact for grids that have
    // genuinely-zero regions (e.g. masked/finite-support detection profiles).
    double bb_x0_ = 0, bb_x1_ = -1, bb_y0_ = 0, bb_y1_ = 0, bb_z0_ = 0, bb_z1_ = 0;

    /// Build the reject box around voxels with value > `threshold_frac`·peak (abs).
    /// `threshold_frac <= 0` bounds only strictly-nonzero voxels (exact early-out).
    /// A grid with no qualifying voxels, or `threshold_frac >= 1`, disables the box.
    void build_bbox(double threshold_frac);
    /// Disable the two-step reject (restore exact `at()`).
    void clear_bbox() { bb_x1_ = bb_x0_ - 1; }
    bool has_bbox() const { return bb_x1_ >= bb_x0_; }

    // --- optional analytic Gaussian mode (no voxels) --------------------------------
    // When `analytic_` is set, `at()` evaluates A·exp(-2((x²+y²)/w0² + z²/z0²)) directly
    // instead of interpolating a grid. This matches the legacy `focus_3dgauss` shape
    // exactly (no discretization error), needs no memory or construction, and is faster
    // than a trilinear lookup on a large grid. Built via `analytic_gaussian3d(...)`.
    bool analytic_ = false;
    double an_amp_ = 1.0, an_cxy_ = 0.0, an_cz_ = 0.0;   ///< amp, -2/w0², -2/z0²

    /// Trilinearly interpolated value at physical position (x,y,z); 0 outside the grid
    /// (or the analytic Gaussian value when in analytic mode).
    double at(double x, double y, double z) const;

    /// Analytic 3D-Gaussian excitation field A·exp(-2((x²+y²)/w0² + z²/z0²)) — evaluated
    /// on the fly, no voxels. Drop-in replacement for `gaussian3d` in the engine.
    static SimGrid analytic_gaussian3d(double w0, double z0, double amplitude = 1.0);

    /*!
     * \brief Fill a grid from a 3D-Gaussian intensity `A·exp(-2((x²+y²)/w0² + z²/z0²))`.
     *
     * Convenience only (matches the legacy `focus_3dgauss` shape, amplitude `A`). The
     * grid spans ±extent about the origin at the given isotropic `spacing`.
     */
    static SimGrid gaussian3d(double w0, double z0,
                              double extent_xy, double extent_z,
                              double spacing, double amplitude = 1.0);

    /*!
     * \brief Fill a grid from the Gaussian–Lorentzian confocal MDF.
     *
     * The lateral profile is Gaussian with a beam waist that expands along z as
     * `w(z) = w0·sqrt(1 + (z/zR)²)` (Rayleigh range `zR`), giving the standard FCS
     * molecule-detection function `A·(w0/w(z))²·exp(-2·(x²+y²)/w(z)²)`. This is the
     * physically-motivated confocal focus (vs. the separable 3D Gaussian above);
     * for `zR → ∞` it reduces to a cylinder, and near the focus it matches `gaussian3d`.
     */
    static SimGrid gaussian_lorentzian(double w0, double zR,
                                       double extent_xy, double extent_z,
                                       double spacing, double amplitude = 1.0);

    /*!
     * \brief Fill a 3D grid from a radially-symmetric numeric/measured PSF sampled on the
     *        half-plane `(r, z)` with `r = sqrt(x²+y²) ≥ 0` (PyBroMo `NumericPSF` convention).
     *
     * `rz` is row-major `[iz*nr + ir]`, `nr`×`nz`, with spacings `r_step`/`z_step` (µm) and
     * `z` centred (`z ∈ [-(nz-1)/2·z_step, …]`). Each output voxel is filled by bilinear
     * interpolation of `rz` at `(sqrt(x²+y²), z)` (cylindrical symmetry), so a measured or
     * vectorial-diffraction PSF becomes a `SimGrid` usable as excitation or detection.
     * Values outside the tabulated range clamp to 0.
     */
    static SimGrid from_radial(const std::vector<double>& rz, int nr, int nz,
                               double r_step, double z_step,
                               double extent_xy, double extent_z,
                               double spacing, double amplitude = 1.0);

    /// A grid of constant value (e.g. a uniform detection profile / CEF = 1).
    static SimGrid uniform(double value,
                           double extent_xy, double extent_z, double spacing);
};

} // namespace tttrlib

#endif // TTTRLIB_SIMGRID_H
