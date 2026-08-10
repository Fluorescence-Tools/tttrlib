"""A/B benchmark + correctness: PDA3c pieces, C++ (tttrlib) vs ChiSurf."""
import sys, time
sys.path.insert(0, '/Users/tpeulen/dev/chisurf')
import numpy as np
import tttrlib

from chisurf.core.fluorescence.pda3c.species import (
    gauss_hermite_grid as py_ghg,
    covariance_from_statistics, covariance_to_cholesky,
)
from chisurf.core.fluorescence.pda3c.physics import (
    transfer_matrix as py_transfer, distances_to_matrix,
    ThreeColorSetup,
)
from chisurf.core.fluorescence.crosstalk import apply_mixing

N_RUNS = 20
setup = ThreeColorSetup.from_scalars(r0_bg=47.0, r0_br=47.0, r0_gr=47.0)
means = np.array([40.0, 50.0, 60.0])
cov = covariance_from_statistics([3.0, 4.0, 5.0], [0.3, 0.2, 0.1])
L = covariance_to_cholesky(cov)
exc = setup.excitation[0]
emi = setup.emission

print(f"{'nodes':>5} {'M':>5} | {'ChiSurf(ms)':>12} {'C++(ms)':>9} {'speedup':>7} {'match':>6}")
for nnodes in [3, 5, 7]:
    nodes, w = np.polynomial.hermite.hermgauss(nnodes)
    py_pts, py_wts = py_ghg(means, L, n_nodes=nnodes)
    dist_mats = np.array([distances_to_matrix(pt, 3) for pt in py_pts])
    M = len(py_pts)

    t0 = time.perf_counter()
    for _ in range(N_RUNS):
        T = py_transfer(dist_mats, setup)
        ch = apply_mixing(emi, np.einsum('d,...dj->...j', exc, T).T).T
        avg = np.einsum('mc,m->c', ch, py_wts)
    t_py = (time.perf_counter() - t0) / N_RUNS

    t0 = time.perf_counter()
    for _ in range(N_RUNS):
        avg_cpp = np.asarray(tttrlib.species_forward_model(
            means.tolist(), L.flatten().tolist(), nodes.tolist(), w.tolist(),
            [47.0] * 3, exc.tolist(), emi.flatten().tolist(), 3, 3))
    t_cpp = (time.perf_counter() - t0) / N_RUNS

    ok = np.allclose(avg, avg_cpp, atol=1e-8)
    print(f"{nnodes:>5} {M:>5} | {1000*t_py:>12.3f} {1000*t_cpp:>9.3f} {t_py/t_cpp:>7.1f}x {str(ok):>6}")