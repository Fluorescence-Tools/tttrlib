"""
s2ISM: joint super-resolution and optical sectioning.

Reference: Zunino et al., "Structured detection for simultaneous super-resolution
and optical sectioning in laser scanning microscopy", Nat. Photonics (2025);
implementation `s2ism/s2ism.py` (amd_update_fft, max_likelihood_reconstruction).

The reference runs on torch, which is not a test dependency, so the oracle here
is a line-by-line numpy transcription of it -- the einsum contraction, the
rfftn/irfftn/ifftshift convolution, the per-plane PSF normalization, the flat
initialization and the float32 division threshold are all reproduced as written.
The C++ port has to agree with it to round-off, which pins down the two details
that are easy to get wrong: the PSF flip is numpy's (about index (N-1)/2, not a
circular flip about 0, which lands one sample off per axis), and the object is
updated multiplicatively without any further normalization because the PSF of
each plane is already normalized over (channel, y, x).
"""

import os
import numpy as np
import pytest

import tttrlib

NX = NY = 24
N_CH = 4
NZ = 3


def _partial_conv(kernel_fft, volume, subscripts, axis_v, axis_out, padding):
    """partial_convolution_rfft with fourier=(1, 0)."""
    vol_fft = np.fft.rfftn(volume, axes=axis_v, s=padding)
    conv = np.einsum(subscripts, kernel_fft, vol_fft)
    conv = np.fft.irfftn(conv, axes=axis_out, s=padding)
    return np.real(np.fft.ifftshift(conv, axes=axis_out))


def _reference_s2ism(dset, psf, max_iter, initialization="flat"):
    """max_likelihood_reconstruction with stop='fixed'; dset (Nx,Ny,Nch), psf (Nz,Nx,Ny,Nch)."""
    nz = psf.shape[0]
    nx, ny, n_ch = dset.shape
    h = psf / psf.sum(axis=tuple(range(1, psf.ndim)), keepdims=True)
    flip_ax = (1, 2)
    ht = np.flip(h, axis=flip_ax)
    padding = [h.shape[d] for d in flip_ax]

    h_fft = np.fft.rfftn(h, axes=flip_ax, s=padding)
    ht_fft = np.fft.rfftn(ht, axes=flip_ax, s=padding)

    obj = np.ones((nz, nx, ny))
    if initialization == "flat":
        obj *= dset.sum() / nz / nx / ny
    else:
        obj *= (dset.sum(-1) / nz)[None]
    eps = np.finfo(np.float32).eps

    for _ in range(max_iter):
        est = _partial_conv(h_fft, obj, "abcd,abc->abcd", (1, 2), (1, 2), padding).sum(0)
        frac = np.where(est < eps, 0.0, dset / np.where(est == 0, 1, est))
        upd = _partial_conv(ht_fft, frac, "abcd,bcd->abcd", (0, 1), (1, 2), padding).sum(-1)
        obj = obj * upd
    return obj


def _dataset(seed=0):
    """A two-point object seen through per-plane, per-element Gaussian PSFs."""
    yy, xx = np.mgrid[0:NX, 0:NY]
    psf = np.zeros((NZ, NX, NY, N_CH))
    for z in range(NZ):
        sigma = 1.4 + 0.8 * abs(z - NZ // 2)          # wider away from focus
        for c in range(N_CH):
            dx, dy = (c % 2 - 0.5) * 2.0, (c // 2 - 0.5) * 2.0
            psf[z, :, :, c] = np.exp(
                -(((xx - (NX / 2 + dx)) ** 2 + (yy - (NY / 2 + dy)) ** 2) / (2 * sigma ** 2))
            )

    obj = np.zeros((NX, NY))
    obj[10, 12] = 1.0
    obj[14, 9] = 0.6
    dset = np.zeros((NX, NY, N_CH))
    for c in range(N_CH):
        spectrum = np.fft.fft2(obj) * np.fft.fft2(np.fft.ifftshift(psf[NZ // 2, :, :, c]))
        dset[:, :, c] = np.real(np.fft.ifft2(spectrum))
    return np.clip(dset, 0, None) * 500.0 + 1.0, psf


def _as_native(dset, psf):
    """The library takes (n_ch, ny, nx) and (nz, n_ch, ny, nx)."""
    return (np.ascontiguousarray(np.moveaxis(dset, -1, 0)),
            np.ascontiguousarray(np.moveaxis(psf, -1, 1)))


@pytest.mark.parametrize("n_iter", [1, 5, 20])
def test_s2ism_matches_the_reference(n_iter):
    """Every iteration must reproduce the reference to round-off."""
    dset, psf = _dataset()
    expected = _reference_s2ism(dset, psf, max_iter=n_iter)
    got = tttrlib.CLSMSuperRes.s2ism_reconstruction(*_as_native(dset, psf), max_iter=n_iter)

    assert got.shape == expected.shape
    scale = max(np.abs(expected).max(), 1e-30)
    assert np.abs(expected - got).max() / scale < 1e-12


def test_s2ism_matches_the_reference_with_sum_initialization():
    dset, psf = _dataset()
    expected = _reference_s2ism(dset, psf, max_iter=8, initialization="sum")
    got = tttrlib.CLSMSuperRes.s2ism_reconstruction(
        *_as_native(dset, psf), max_iter=8, init_from_sum=True
    )
    scale = max(np.abs(expected).max(), 1e-30)
    assert np.abs(expected - got).max() / scale < 1e-12


def test_s2ism_sections_the_object_into_the_focal_plane():
    """
    The point of the axial stack: an object that lives in focus must be
    reconstructed into the focal plane rather than spread over all of them.
    """
    dset, psf = _dataset()
    obj = tttrlib.CLSMSuperRes.s2ism_reconstruction(*_as_native(dset, psf), max_iter=50)
    focal = obj[NZ // 2].sum()
    out_of_focus = obj.sum() - focal
    assert focal > out_of_focus
    assert (obj >= 0).all()


def test_s2ism_auto_stop_obeys_the_threshold():
    """
    amd_stop halts once the focal-plane photon count moves by less than the
    threshold on two consecutive iterations. Driving the threshold to either
    extreme pins the rule down: an unreachable threshold halts at the second
    iteration, a threshold of zero never fires and the run reaches the cap.

    Note the focal count is *not* monotonic in the iteration number -- late
    iterations move flux out to the defocused planes -- which is the whole
    reason the adaptive rule exists.
    """
    dset, psf = _dataset()
    data, h = _as_native(dset, psf)

    halt_at_once = tttrlib.CLSMSuperRes.s2ism_reconstruction(
        data, h, max_iter=200, threshold=1e30, auto_stop=True
    )
    assert np.allclose(
        halt_at_once,
        tttrlib.CLSMSuperRes.s2ism_reconstruction(data, h, max_iter=2),
    )

    never_halts = tttrlib.CLSMSuperRes.s2ism_reconstruction(
        data, h, max_iter=30, threshold=0.0, auto_stop=True
    )
    assert np.allclose(
        never_halts,
        tttrlib.CLSMSuperRes.s2ism_reconstruction(data, h, max_iter=30),
    )


def test_s2ism_rejects_mismatched_psf():
    dset, psf = _dataset()
    data, h = _as_native(dset, psf)
    with pytest.raises(Exception):
        tttrlib.CLSMSuperRes.s2ism_reconstruction(data, h[:, :, :-1, :])
    with pytest.raises(ValueError):
        tttrlib.CLSMSuperRes.s2ism_reconstruction(data[0], h)


# ---------------------------------------------------------------------------
# The real reference: VicidominiLab/s2ISM (torch), run from the junk checkout
# ---------------------------------------------------------------------------

_S2ISM_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..",
                          "chisurf", "junk", "s2ISM", "src")
_S2ISM_RUNNER = r"""
import sys, types, importlib.util, numpy as np, torch
src = sys.argv[1]
# s2ism.psf_estimator pulls brighteyes_ism -> brighteyes_mcs_reader, which the ML
# reconstruction never uses; stub the submodule so s2ism.s2ism imports alone.
spec = importlib.util.spec_from_file_location("s2ism", src + "/s2ism/__init__.py",
                                              submodule_search_locations=[src + "/s2ism"])
pkg = importlib.util.module_from_spec(spec); sys.modules["s2ism"] = pkg
sys.modules["s2ism.psf_estimator"] = types.ModuleType("s2ism.psf_estimator")
spec2 = importlib.util.spec_from_file_location("s2ism.s2ism", src + "/s2ism/s2ism.py")
m = importlib.util.module_from_spec(spec2); sys.modules["s2ism.s2ism"] = m; spec2.loader.exec_module(m)
d = np.load(sys.argv[2]); it = int(sys.argv[3]); init = sys.argv[4]
dset = d["dset"][:, :, None, :]            # (Nx, Ny, Nt=1, Nch)
psf = d["psf"][:, :, :, None, :]           # (Nz, Nx, Ny, Nt=1, Nch)
O = m.max_likelihood_reconstruction(dset, psf, stop="fixed", max_iter=it, rep_to_save="last",
                                    initialization=init, process="cpu")
np.savez(sys.argv[5], obj=np.asarray(O[0]))
"""


def _odd_dataset(NX=25, NY=25, NCH=4, NZ=3):
    """Same construction as _dataset, odd-sized: the reference crops even sizes
    (drops the first row/column and zero-pads them back afterwards)."""
    yy, xx = np.mgrid[0:NX, 0:NY]
    psf = np.zeros((NZ, NX, NY, NCH))
    for z in range(NZ):
        sigma = 1.4 + 0.8 * abs(z - NZ // 2)
        for c in range(NCH):
            dx, dy = (c % 2 - 0.5) * 2.0, (c // 2 - 0.5) * 2.0
            psf[z, :, :, c] = np.exp(-(((xx - (NX // 2 + dx)) ** 2 + (yy - (NY // 2 + dy)) ** 2) / (2 * sigma ** 2)))
    obj = np.zeros((NX, NY))
    obj[10, 12] = 1.0
    obj[14, 9] = 0.6
    dset = np.zeros((NX, NY, NCH))
    for c in range(NCH):
        spectrum = np.fft.fft2(obj) * np.fft.fft2(np.fft.ifftshift(psf[NZ // 2, :, :, c]))
        dset[:, :, c] = np.real(np.fft.ifft2(spectrum))
    return np.clip(dset, 0, None) * 500.0 + 1.0, psf


def _s2ism_available():
    if not os.path.isdir(_S2ISM_SRC):
        return False
    try:
        import torch  # noqa: F401
        return True
    except ImportError:
        return False


@pytest.mark.skipif(not _s2ism_available(), reason="VicidominiLab s2ISM (../chisurf/junk/s2ISM) or torch not available")
@pytest.mark.parametrize("n_updates,init", [(2, "flat"), (6, "flat"), (21, "flat"), (9, "sum")])
def test_s2ism_matches_the_vicidomini_reference(n_updates, init):
    """tttrlib vs the actual VicidominiLab `s2ISM` package (torch), run in a
    subprocess (torch and tttrlib each ship an OpenMP runtime). Two reference
    conventions are pinned, not copied: `max_likelihood_reconstruction(max_iter=n)`
    performs n + 1 updates (its stop test `k == max_iter` runs after the update at
    index k, k from 0), so tttrlib's `max_iter=n+1` is the same computation; and it
    keeps the data in float32 (hence 1e-6, not round-off). Odd sizes, because the
    reference crops even ones."""
    import subprocess, sys, tempfile
    dset, psf = _odd_dataset()
    tmp = tempfile.mkdtemp(prefix="s2ism_ab_")
    fin, fout, frun = [os.path.join(tmp, f) for f in ("in.npz", "out.npz", "run.py")]
    np.savez(fin, dset=dset, psf=psf)
    with open(frun, "w") as fh:
        fh.write(_S2ISM_RUNNER)
    env = dict(os.environ, KMP_DUPLICATE_LIB_OK="TRUE")
    r = subprocess.run([sys.executable, frun, _S2ISM_SRC, fin, str(n_updates - 1), init, fout],
                       capture_output=True, text=True, env=env)
    if r.returncode != 0:
        pytest.skip("s2ISM did not run: " + r.stderr[-600:])
    ref = np.squeeze(np.load(fout)["obj"])
    got = tttrlib.CLSMSuperRes.s2ism_reconstruction(*_as_native(dset, psf), max_iter=n_updates,
                                                    init_from_sum=(init == "sum"))
    assert got.shape == ref.shape
    scale = np.abs(ref).max()
    assert np.abs(got - ref).max() / scale < 1e-6
