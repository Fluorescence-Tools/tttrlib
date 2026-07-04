"""PSF fillers (PRD-008): Gaussian-Lorentzian and numeric/measured radial PSFs.

A numeric radial PSF sampled from a Gaussian must reproduce the analytic
``gaussian3d`` field (and hence its count rate); the Gaussian-Lorentzian MDF
must run end-to-end. See ``doc/simulator-guide.rst`` (PSF gallery).
"""

from __future__ import annotations

import numpy as np
import pytest

tttrlib = pytest.importorskip("tttrlib")
pytestmark = pytest.mark.skipif(
    not hasattr(tttrlib, "SimEngine"), reason="tttrlib built without the Sim* simulator"
)


def _count_rate(field):
    """Photons/window from a single immobile emitter at the focus centre."""
    sample = tttrlib.SimSystem()
    sp = tttrlib.SimSpecies(); sp.D = 0.0
    sp.q = tttrlib.VectorDouble([50.0, 50.0]); sample.add_species(sp)
    sample.set_rate_matrices(tttrlib.VectorDouble([0.0]), tttrlib.VectorDouble([0.0]))
    sample.set_background(tttrlib.VectorDouble([0.0, 0.0]))
    sample.add_fluorophore(0.0, 0.0, 0.0, 0, False)
    st = tttrlib.SimIntegrator(); st.dt = 0.01; st.n_channels = 2; st.n_ph_max = 100000
    e = tttrlib.SimEngine(sample, field, tttrlib.VectorSimGrid([]), st)
    e.run()
    return e.n_photons() / e.current_window()


def test_numeric_radial_psf_reproduces_gaussian():
    """A radial PSF sampled from a Gaussian gives the same count rate as gaussian3d."""
    w0, z0 = 0.3, 2.0
    nr, nz = 80, 80
    r_step, z_step = 0.05, 0.1
    r = np.arange(nr) * r_step
    z = (np.arange(nz) - nz / 2) * z_step
    rz = np.exp(-2.0 * ((r[None, :] ** 2) / w0 ** 2 + (z[:, None] ** 2) / z0 ** 2))

    numeric = tttrlib.SimGrid.numeric_from_numpy(rz, r_step, z_step,
                                                 extent_xy=2.0, extent_z=4.0, spacing=0.1)
    analytic = tttrlib.SimGrid.gaussian3d(w0, z0, 2.0, 4.0, 0.1, 1.0)

    cr_num = _count_rate(numeric)
    cr_an = _count_rate(analytic)
    assert abs(cr_num - cr_an) / cr_an < 0.02, f"numeric {cr_num:.3f} vs analytic {cr_an:.3f}"


def test_gaussian_lorentzian_runs_and_peaks_at_centre():
    """The Gaussian-Lorentzian MDF is peak-1 at the focus centre and simulates end-to-end."""
    gl = tttrlib.SimGrid.gaussian_lorentzian(0.3, 1.0, 2.0, 4.0, 0.1, 1.0)
    assert abs(gl.at(0.0, 0.0, 0.0) - 1.0) < 1e-6
    assert gl.at(0.3, 0.0, 0.0) < gl.at(0.0, 0.0, 0.0)     # decays laterally
    assert _count_rate(gl) > 0.0
