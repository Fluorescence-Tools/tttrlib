"""Record a PyBroMo photon stream for the SimEngine FCS A/B.

Run this in the isolated PyBroMo environment (it is *not* a dependency of the
test suite):

    benchmarks/.venvs/pybromo/bin/python test/python/simulation/gen_pybromo_reference.py

It writes ``test/data/reference/sim_fcs_pybromo_reference.npz`` holding PyBroMo's
photon timestamps for a single-species free-diffusion experiment through a 3-D
Gaussian PSF, plus every parameter needed to configure tttrlib's SimEngine to
the same physics: D, the PSF 1/e^2 radii, the peak brightness and the
concentration.  ``test_ab_simulation_reference.py`` correlates both streams
with the same tttrlib Correlator and compares the two G(tau) curves with each
other and with the analytic 3-D diffusion curve.

Geometry note: PyBroMo diffuses in a *reflecting rectangular box*, tttrlib in
an *absorbing ellipsoid held at steady state by surface injection*; only the
concentration is matched (N_box / V_box = N_ellipsoid / V_ellipsoid), which is
what G(0) = 1/N_eff and the diffusion time depend on.
"""
import os
import shutil
import tempfile

import numpy as np
import pybromo as pbm

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "data", "reference", "sim_fcs_pybromo_reference.npz")

# --- physics shared with the tttrlib side (SI here, um/ms in tttrlib) --------
D_um2_ms = 1.0            # -> 1e-9 m^2/s
w0_um, wz_um = 0.3, 1.5   # 1/e^2 radii of exp(-2 r^2/w0^2 - 2 z^2/wz^2)
peak_kcps = 50.0          # brightness at the PSF maximum
box_half_xy_um, box_half_z_um = 2.0, 4.0     # PyBroMo box: +-2 x +-2 x +-4 um
n_particles = 172         # 172 / (4*4*8 um^3) = 1.34 um^-3, = 90 in tttrlib's ellipsoid 4/3 pi 2^2 4
t_step_s = 0.5e-6
t_max_s = 6.0
seed = 1

D = D_um2_ms * 1e-9
box = pbm.Box(x1=-box_half_xy_um * 1e-6, x2=box_half_xy_um * 1e-6,
              y1=-box_half_xy_um * 1e-6, y2=box_half_xy_um * 1e-6,
              z1=-box_half_z_um * 1e-6, z2=box_half_z_um * 1e-6)
P = pbm.Particles.from_specs(num_particles=(n_particles,), D=(D,), box=box, seed=seed)
# PyBroMo's GaussianPSF is exp(-x^2/(2 s^2)) and its emission is the PSF *squared*
# (excitation x detection, see ParticlesSimulation._sim_trajectories), i.e.
# exp(-x^2/s^2). tttrlib's molecule-detection function is exp(-2 x^2/w0^2), so the
# same physics needs s = w0/sqrt(2) -- NOT w0/2, which is the sigma of a single
# Gaussian of 1/e^2 radius w0 and gives a volume 2^1.5 too small and a
# diffusion time 2x too short (measured on the first fixture: G(0) 2.7x, D 1.9x).
psf = pbm.GaussianPSF(sx=w0_um * 1e-6 / 2 ** 0.5, sy=w0_um * 1e-6 / 2 ** 0.5,
                      sz=wz_um * 1e-6 / 2 ** 0.5)

tmp = tempfile.mkdtemp(prefix="pybromo_ref_")
try:
    S = pbm.ParticlesSimulation(t_step=t_step_s, t_max=t_max_s, particles=P, box=box, psf=psf)
    S.simulate_diffusion(total_emission=False, save_pos=False, path=tmp, verbose=False)
    S.simulate_timestamps_mix(max_rates=[peak_kcps * 1e3], populations=[slice(0, n_particles)],
                              bg_rate=0.0, seed=seed, scale=10, path=tmp)
    ts = np.asarray(S._timestamps[:], dtype=np.int64)
    ts_unit_s = S.timestamps_unit if hasattr(S, "timestamps_unit") else t_step_s / 10.0
    np.savez_compressed(
        OUT,
        timestamps=ts,
        timestamps_unit_s=float(ts_unit_s),
        D_um2_ms=D_um2_ms, w0_um=w0_um, wz_um=wz_um, peak_kcps=peak_kcps,
        box_half_xy_um=box_half_xy_um, box_half_z_um=box_half_z_um,
        n_particles=n_particles, t_step_s=t_step_s, t_max_s=t_max_s, seed=seed,
        pybromo_version=str(pbm.__version__),
    )
    print("wrote", OUT, ts.size, "timestamps, unit", ts_unit_s, "s")
finally:
    try:
        import tables
        tables.file._open_files.close_all()
    except Exception:
        pass
    shutil.rmtree(tmp, ignore_errors=True)
