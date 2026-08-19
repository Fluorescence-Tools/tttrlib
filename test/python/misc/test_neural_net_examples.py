"""Smoke tests for the NeuralNet gallery examples in ``examples/miscellaneous``.

Each runs the script end to end (headless) and checks the number the example
itself reports -- the finite-difference agreement of the derivatives, and the
error of the physics-informed fits against their analytic solutions -- so a
change to the derivative API cannot break the tutorials silently.
"""
import runpy
from pathlib import Path

import numpy as np
import pytest

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:  # pragma: no cover
    pytest.skip("the gallery examples need matplotlib", allow_module_level=True)
pytest.importorskip("scipy")

_EXAMPLES = Path(__file__).resolve().parents[3] / "examples" / "miscellaneous"


def _run(name):
    plt.close("all")
    show = plt.show
    plt.show = lambda *a, **k: None
    try:
        return runpy.run_path(str(_EXAMPLES / name), run_name="__main__")
    finally:
        plt.show = show
        plt.close("all")


@pytest.mark.slow
def test_differentiable_building_block():
    ns = _run("plot_neural_net_differentiable.py")
    # backward vs finite differences, as printed by the example
    assert np.abs(ns["dparams"] - ns["fd_p"]).max() < 1e-6
    assert np.abs(ns["dx"].ravel() - ns["fd_x"]).max() < 1e-6
    # the Sobolev fit gets the derivative right where the values-only fit does not
    plain, sob, xg = ns["plain"], ns["sob"], ns["xg"]
    cos = np.cos(xg).ravel()
    assert np.abs(sob[1] - cos).max() < 0.05
    assert np.abs(sob[1] - cos).max() < np.abs(plain[1] - cos).max()


@pytest.mark.heavy
def test_pinn_heat_equation():
    ns = _run("plot_pinn_heat_equation.py")
    assert np.abs(ns["u_net"] - ns["u_ex"]).max() < 2e-2


@pytest.mark.heavy
def test_pinn_burgers():
    ns = _run("plot_pinn_burgers.py")
    net, burgers_exact, nu = ns["net"], ns["burgers_exact"], ns["nu"]
    xs = np.linspace(-1, 1, 101)
    for t in (0.25, 0.5, 0.75):
        u_ex = burgers_exact(xs, t, nu)
        u_nn = net.predict_batch_np(np.column_stack([xs, np.full_like(xs, t)])).ravel()
        assert np.linalg.norm(u_nn - u_ex) / np.linalg.norm(u_ex) < 1e-2
