"""Physically parameterised emission — where the physics actually enters.

Classic H2MM gives each state a free row of ``P(stream | state)``.  That row is
an operational cluster label: it says a state emits more acceptor photons, and
nothing about why.  Crucially, a state that is dark on the acceptor and a state
at genuine FRET can produce the *same* intensity ratio, so the free model cannot
tell them apart at all -- not poorly, but in principle.

The lifetime-resolved model gives each state a distribution over the **product
alphabet** ``symbol = stream * n_micro_bins + micro_bin``::

    obs[k, s*B + b] = P(stream s | k) * f_{k,s}(b)

and then *generates* that whole row from **one physical parameter per state** —
the mean donor-acceptor distance.

**That coupling is the entire point.**  One number moves the photon ratio and
the micro-time distribution together, in the specific way FRET moves them.
Photophysics does not: a dark acceptor moves the ratio while leaving the donor
unquenched.  So the two become distinguishable, and the free categorical model
provably cannot do it — the negative control the tests assert.

It also collapses the parameter count: a FRET state costs **one** emission
degree of freedom instead of ``p - 1``.  The micro-time bins are not free
quantities — they are decay curves sampled on a grid, built once per parameter
update and read to *score* photons.  Binning resolution is therefore an accuracy
knob, not a statistical cost.

Two idealisations are worth naming, because assuming them silently biases the
very axis this model exploits, and :class:`FretDistanceEmission` drops both:

* **a donor is multi-exponential** even with no acceptor, so a state has no
  single lifetime.  ``tau_x = <a tau>/<a>`` drives the photon ratio; a
  mono-exponential read of the decay returns ``tau_f = <a tau^2>/<a tau>``;
* **a state is a distribution over distances**, not one distance.  Averaging
  over it means ``E != 1 - tau/tau_D0`` and the mean distance for a given ``E``
  is **not** ``R0 (1/E - 1)^(1/6)``.  That gap is the curvature of the static
  FRET line.

:class:`FretEmission` keeps the idealisation (mono-exponential donor, single
distance) as the degenerate case and as the control the sweep measures against.

The decay patterns mirror ``include/SimDecay.h`` (multi-exponential, arbitrary
IRF, convolution) so the port shares one representation between simulating
photons and scoring them.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .constraints import HmmConstraints, row_normalize
from .core import PhotonData, e_step, matrix_powers
from .fit import HmmModel

__all__ = [
    "multi_exponential", "gaussian_irf", "convolve_pattern",
    "DecayState", "build_emission", "Optics",
    "FretEmission", "FretDistanceEmission", "LifetimeEmission",
    "donor_lifetime_spectrum", "gaussian_distance_distribution",
    "fret_lifetime_spectrum", "lifetime_averages", "convolved_spectrum",
    "Anisotropy",
    "fit_emission", "simulate_lifetime",
]

_EPS = 1e-9


# ---------------------------------------------------------------------------
# decay patterns  (mirrors include/SimDecay.h)
# ---------------------------------------------------------------------------

def multi_exponential(lifetimes, amplitudes, n_bins, dt, t0=0.0):
    """Unnormalised ``sum_c a_c exp(-t / tau_c)``, **integrated over each bin**.

    A micro-time bin is an interval, not a sample point, so the probability of
    landing in bin ``b`` is the integral over it — not the density at its edge.
    The distinction is not cosmetic here: a quenched donor at high FRET has
    ``tau ~ 1 ns`` against bins of ~0.5 ns, so ``tau`` spans only a couple of
    bins and edge-sampling biases fitted distances by several Å.

    ``∫ a·exp(-t/τ) dt`` over ``[t_b, t_b+dt]`` is ``a·τ·(e^{-t_b/τ} −
    e^{-t_{b+1}/τ})``.  The ``τ`` factor matters for multi-component spectra: it
    reweights components against each other, which edge-sampling gets wrong.
    Negative amplitudes (the sensitized rise) carry through linearly.
    """
    edges = t0 + dt * np.arange(n_bins + 1)
    lo = np.maximum(edges[:-1], 0.0)
    hi = np.maximum(edges[1:], 0.0)
    tau = np.atleast_1d(np.asarray(lifetimes, dtype=np.float64))
    amp = np.atleast_1d(np.asarray(amplitudes, dtype=np.float64))
    keep = tau > 0
    if not keep.any():
        return np.zeros(n_bins)
    # Vectorised over components: a distance distribution crossed with a
    # multi-exponential donor runs to >100 components, and this sits inside a
    # golden-section search, so a Python loop here dominates the whole fit.
    tk = tau[keep][:, None]
    out = ((amp[keep][:, None] * tk)
           * (np.exp(-lo[None, :] / tk) - np.exp(-hi[None, :] / tk))).sum(axis=0)
    return np.maximum(out, 0.0)


def gaussian_irf(n_bins, dt, mean=0.0, fwhm=0.1):
    """One convenient IRF shape; any non-negative array works as an IRF."""
    if fwhm <= 0:
        irf = np.zeros(n_bins)
        irf[0] = 1.0
        return irf
    sigma = fwhm * 0.4246609 / dt
    k = np.arange(n_bins)
    return np.exp(-0.5 * ((k - mean / dt) / sigma) ** 2)


def convolve_pattern(sig, irf):
    """Linear convolution truncated back to ``len(sig)``."""
    return np.convolve(sig, irf)[:len(sig)]


def _normalize(p):
    p = np.maximum(np.asarray(p, dtype=np.float64), 0.0)
    s = p.sum()
    return p / s if s > 0 else np.full_like(p, 1.0 / len(p))


# ---------------------------------------------------------------------------
# lifetime spectra and distance distributions
#
# A real donor is **multi-exponential** even with no acceptor, and a FRET
# "state" is not one distance but a *distribution* over distances (linker and
# conformational flexibility).  Both break the tidy relations the simple model
# assumes:
#
#   * the donor decay of a single state is not mono-exponential, so "the"
#     lifetime is ambiguous -- the species average tau_x = <a tau>/<a> drives the
#     steady-state intensity, while a mono-exponential fit of the decay returns
#     the fluorescence average tau_f = <a tau^2>/<a tau>;
#   * E is no longer 1 - tau/tau_D0, and the mean distance giving a target E is
#     **not** R0 (1/E - 1)^(1/6) -- averaging over the distribution shifts it.
#
# Reimplemented from chisurf's `core/fluorescence/fret/lines.py` (chisurf is not
# a dependency), keeping its conventions so results are comparable.
# ---------------------------------------------------------------------------

def donor_lifetime_spectrum(donor):
    """Normalise a donor specification into ``(amplitudes, lifetimes)``.

    Accepts a scalar lifetime or a pair of sequences, so a mono-exponential
    donor stays a one-liner while a real one can carry its full spectrum.
    """
    if np.isscalar(donor):
        return np.array([1.0]), np.array([float(donor)])
    amp, tau = donor
    amp = np.atleast_1d(np.asarray(amp, dtype=np.float64))
    tau = np.atleast_1d(np.asarray(tau, dtype=np.float64))
    if amp.shape != tau.shape:
        raise ValueError("donor amplitudes and lifetimes must have equal length")
    total = amp.sum()
    if total <= 0:
        raise ValueError("donor amplitudes must sum to a positive number")
    return amp / total, tau


def gaussian_distance_distribution(mean, sigma, n_points=81, n_sigma=3.5, r_min=1.0):
    """Discretised Gaussian donor-acceptor distance distribution.

    ``sigma = 0`` collapses to a single distance, which is the idealisation the
    simple model makes.  ``r_min`` keeps ``(R0/R)^6`` finite.
    """
    mean = float(mean)
    if sigma <= 0:
        return np.array([1.0]), np.array([mean])
    offsets = np.linspace(-n_sigma * sigma, n_sigma * sigma, int(n_points))
    r = np.clip(mean + offsets, r_min, None)
    w = np.exp(-0.5 * ((r - mean) / sigma) ** 2)
    return w / w.sum(), r


def fret_lifetime_spectrum(mean_distance, donor=4.0, r0=52.0, sigma=6.0,
                           n_distance_samples=81):
    """Donor lifetime spectrum of one FRET state: distance distribution × donor decay.

    Each donor component ``i`` at each distance ``R_j`` is quenched to
    ``tau_i / (1 + (R0/R_j)^6)`` with weight ``a_i · w_j``.  The result is the
    multi-exponential decay a donor *actually* shows when its acceptor sits at
    ``mean_distance`` with linker width ``sigma`` — which is why one state does
    not have one lifetime.
    """
    donor_x, donor_tau = donor_lifetime_spectrum(donor)
    w, r = gaussian_distance_distribution(mean_distance, sigma,
                                          n_points=n_distance_samples)
    quench = 1.0 / (1.0 + (float(r0) / r) ** 6)          # (n_r,)
    tau = quench[:, None] * donor_tau[None, :]           # (n_r, n_donor)
    amp = w[:, None] * donor_x[None, :]
    amp, tau = amp.ravel(), tau.ravel()
    return amp / amp.sum(), tau


def convolved_spectrum(amplitudes, lifetimes, tau_a):
    """Lifetime spectrum of ``donor ⊛ acceptor`` — the sensitized-acceptor decay.

    ``Exp(τ_i) ⊛ Exp(τ_A)`` is exactly the two-component spectrum
    ``[c, −c]`` on ``[τ_A, τ_i]`` with ``c = 1/(τ_A − τ_i)``; the negative
    amplitude *is* the rise.

    Doing this analytically rather than convolving sampled patterns matters:
    a numerical convolution on a coarse micro-time grid shifts the result by
    ~2 bins, which was enough to bias fitted distances by several Å.  It also
    means the scorer and the simulator use the same closed form.
    """
    out_a, out_t = [], []
    for a_i, t_i in zip(np.atleast_1d(amplitudes), np.atleast_1d(lifetimes)):
        t_i = float(t_i)
        if abs(tau_a - t_i) < 1e-6:      # degenerate; nudge far below resolution
            t_i = tau_a - 1e-6
        c = 1.0 / (tau_a - t_i)
        out_a += [float(a_i) * c, -float(a_i) * c]
        out_t += [tau_a, t_i]
    return np.array(out_a), np.array(out_t)


def lifetime_averages(amplitudes, lifetimes):
    """``(tau_x, tau_f)`` — species-averaged and fluorescence-averaged lifetime.

    ``tau_x = Σaτ/Σa`` sets the steady-state intensity, hence the FRET
    efficiency seen in the photon ratio.  ``tau_f = Σaτ²/Σaτ`` is what a
    mono-exponential fit of the decay returns.  They differ whenever the decay
    is not mono-exponential, which is the entire point.
    """
    a = np.asarray(amplitudes, dtype=np.float64)
    t = np.asarray(lifetimes, dtype=np.float64)
    s0, s1, s2 = a.sum(), (a * t).sum(), (a * t * t).sum()
    return (s1 / s0 if s0 else 0.0), (s2 / s1 if s1 else 0.0)


# ---------------------------------------------------------------------------
# the state
# ---------------------------------------------------------------------------

@dataclass
class DecayState:
    """One state's emission: a stream split plus a micro-time density per stream.

    ``physical_parameters`` is provenance the analyser never reads -- it is what
    lets a fitted state report *why* it is where it is (``E``, ``tau_DA``, and,
    once an external model supplies them, a distance and an ``R0``).
    """
    stream_probability: np.ndarray          # (n_streams,)
    decay: np.ndarray                       # (n_streams, n_bins), rows sum to 1
    physical_parameters: dict = field(default_factory=dict)
    physical_model: str = ""

    def row(self):
        """Flatten to the product alphabet: ``obs[s*B + b]``."""
        return (self.stream_probability[:, None] * self.decay).ravel()


def build_emission(states):
    """Stack ``DecayState``s into the ``(n_states, n_streams*n_bins)`` table."""
    return np.vstack([s.row() for s in states])


# ---------------------------------------------------------------------------
# the FRET parameterisation
# ---------------------------------------------------------------------------

@dataclass
class Optics:
    """The instrument, as a matrix triple.  Scalars are a read-out, not the model.

    The representation is ``excitation · transfer · emission`` (chisurf's
    convention, `pda3c/physics.py`).  Hellenkamp's ``α/γ/δ`` are *derived* for
    display: they are a lossy two-colour projection that cannot express
    three-colour cascades, non-triangular mixing or polarization, and the field
    carries two incompatible definitions of ``α``.  Keeping the matrix primary
    means no convention has to be picked.

    ``alpha`` — donor leakage into the red channel, relative to donor green.
    ``delta`` — direct acceptor excitation, as a **detected** red contribution
    (the quoted convention), i.e. γ is already folded in.
    ``gamma`` — acceptor/donor detection efficiency × quantum yield.

    That last point is the convention trap in miniature: an excitation-matrix
    entry is a *photophysical* quantity that the emission matrix then multiplies
    by γ, whereas the δ people quote is already a detected signal.  Feeding a
    quoted δ straight into the excitation matrix double-counts γ.  Here it is
    divided out on entry, so composition stays physical and the quoted scalar
    keeps its usual meaning — which is precisely why the matrix, not the scalar,
    is the model.
    """
    alpha: float = 0.0
    delta: float = 0.0
    gamma: float = 1.0
    tau_d0: float = 4.0
    tau_a: float = 3.0

    def excitation_matrix(self):
        """``(n_lasers, n_dyes)``.  Rows sum to 1 — direct excitation *partitions*."""
        d_ex = self.delta / max(self.gamma, _EPS)   # detected -> photophysical
        d = d_ex / (1.0 + d_ex)
        return np.array([[1.0 - d, d]])

    def transfer_matrix(self, e):
        """``(n_dyes, n_dyes)``: P(excitation on dye i is emitted by dye j)."""
        return np.array([[1.0 - e, e], [0.0, 1.0]])

    def emission_matrix(self):
        """``(n_dyes, n_channels)``: relative detected brightness."""
        return np.array([[1.0, self.alpha], [0.0, self.gamma]])

    def pathway_weights(self, e):
        """Relative rate of each emission pathway, composed from the triple.

        Four pathways, and they matter because **each carries its own
        micro-time distribution**:

        ``donor_green``    donor emits in green            — donor decay τ_DA
        ``donor_red``      donor *leaks* into red          — still τ_DA
        ``sensitized_red`` acceptor emits after transfer   — τ_DA ⊛ τ_A (rises)
        ``direct_red``     directly excited acceptor       — plain τ_A
        """
        ex = self.excitation_matrix()[0]
        tr = self.transfer_matrix(e)
        em = self.emission_matrix()
        w_dd = ex[0] * tr[0, 0]      # donor excited, stays on donor
        w_da = ex[0] * tr[0, 1]      # donor excited, transferred to acceptor
        w_aa = ex[1] * tr[1, 1]      # acceptor excited directly
        return {"donor_green": w_dd * em[0, 0],
                "donor_red": w_dd * em[0, 1],
                "sensitized_red": w_da * em[1, 1],
                "direct_red": w_aa * em[1, 1]}

    def hellenkamp(self):
        """Scalars for display and comparison with published values only."""
        return {"alpha": self.alpha, "delta": self.delta, "gamma": self.gamma,
                "convention": "Hellenkamp"}


@dataclass
class Anisotropy:
    """Polarisation split, in tttrlib's convention.

    Detection becomes four streams — green/red × parallel/perpendicular, routed
    as 0/1 parallel and 8/9 perpendicular (the MFD convention). The split is
    **micro-time dependent**, because the emission dipole rotates during the
    excited state:

        r(t) = r_inf + (r0 - r_inf)·exp(-t/rho)
        P(par | t) ∝ 1 + 2r(t)          P(perp | t) ∝ G·(1 - r(t))

    so polarisation and micro-time are *not* independent, and a fixed par:perp
    ratio would be wrong. The `DecayState` layout already allows this — every
    stream carries its own decay — but the parameterisation has to generate the
    joint rather than a product.

    ``r0 = 0`` collapses to an unpolarised 50/50 split, which is the two-stream
    model with each colour duplicated.
    """
    r0: float = 0.0
    rho: float = 1.0            # rotational correlation time (ns)
    r_inf: float = 0.0
    g_factor: float = 1.0

    def parallel_fraction(self, t):
        """``P(parallel | micro-time)`` on a time grid."""
        r = self.r_inf + (self.r0 - self.r_inf) * np.exp(-np.asarray(t) / max(self.rho, _EPS))
        par = 1.0 + 2.0 * r
        perp = self.g_factor * (1.0 - r)
        return par / np.maximum(par + perp, _EPS)


class _DecayBase:
    """Shared micro-time machinery: build a normalised decay on the bin axis.

    Every parameterisation follows the same protocol, so ``fit_emission`` does
    not care which one it is given:

    ``p``            size of the product alphabet it produces
    ``n_free()``     emission degrees of freedom, for model selection
    ``to_obs(par)``  ``(n_states, n_params) -> (n_states, p)``
    ``m_step(...)``  expected counts -> new parameters

    Shapes are built **without** background, mixed, and only then given their
    background component — otherwise a channel built from three pathways would
    receive three doses of background.
    """

    def _raw(self, tau):
        return multi_exponential(tau, 1.0, self.n_bins, self.dt)

    def _shape(self, pattern):
        """IRF-convolve and normalise; no background."""
        pat = pattern
        if self.irf is not None:
            pat = convolve_pattern(pat, self.irf)
        return _normalize(pat)

    def _decay_shape(self, tau):
        return self._shape(self._raw(tau))

    def _sensitized_shape(self, tau_d, tau_a):
        """Micro-time of a FRET-sensitized acceptor photon.

        The acceptor cannot emit before the transfer happens, so the delay is
        the **sum** of the donor's quenched excited-state lifetime and the
        acceptor's own — the convolution of the two exponentials.  It rises from
        zero instead of starting at its maximum, which is the whole difference
        from a plain ``Exp(τ_A)``.  Convolving numerically also handles the
        τ_A == τ_DA degeneracy that the closed form does not.
        """
        amp, tau = convolved_spectrum([1.0], [tau_d], tau_a)
        return self._shape(multi_exponential(tau, amp, self.n_bins, self.dt))

    def _background_fraction(self, channel):
        bg = getattr(self, "background", 0.0)
        if np.isscalar(bg):
            return float(bg)
        return float(bg[channel]) if channel < len(bg) else 0.0

    def _background_shape(self):
        pat = getattr(self, "background_pattern", None)
        if pat is None:
            return np.full(self.n_bins, 1.0 / self.n_bins)
        return _normalize(pat)

    def _add_background(self, density, channel):
        bg = self._background_fraction(channel)
        if bg <= 0.0:
            return density
        return (1.0 - bg) * density + bg * self._background_shape()

    def _decay(self, tau, channel=0):
        return self._add_background(self._decay_shape(tau), channel)


class LifetimeEmission(_DecayBase):
    """Per state: a donor lifetime and an acceptor fraction — two free numbers.

    More general than ``FretEmission``, which ties those two together through
    ``E``.  This one can represent a state that FRET cannot: a dark or blinking
    acceptor, where the intensity ratio shifts while the donor stays
    *unquenched*.  That is exactly the state the free categorical model confuses
    with genuine FRET, so this is the parameterisation the separability test
    needs.

    The M-step factorises, which is what makes it cheap and robust:

    * the stream split has the usual **closed form** — sum the expected counts
      over micro-time bins and normalise, exactly the categorical M-step;
    * only the lifetime needs a search, and it is one scalar per state.

    So a two-state model carries 4 emission parameters against the free
    categorical model's ``2 * (2*B - 1)`` — 126 at ``B = 32``.
    """

    DONOR, ACCEPTOR = 0, 1

    def __init__(self, n_states, tau_a, n_bins=32, dt=0.25, irf=None,
                 background=0.0, tau_bounds=(0.05, 10.0), alpha=0.0,
                 optics=None, background_pattern=None):
        self.n_states = int(n_states)
        self.tau_a = float(tau_a)
        self.n_bins = int(n_bins)
        self.dt = float(dt)
        self.irf = None if irf is None else np.asarray(irf, dtype=np.float64)
        self.background = background
        self.background_pattern = background_pattern
        self.tau_bounds = (float(tau_bounds[0]), float(tau_bounds[1]))
        self.n_streams = 2
        self.optics = optics if optics is not None else Optics(alpha=alpha, tau_a=tau_a)

    @property
    def p(self):
        return self.n_streams * self.n_bins

    def n_free(self):
        return 2 * self.n_states

    def state(self, params):
        tau_d, p_acc = float(params[0]), float(np.clip(params[1], _EPS, 1 - _EPS))
        tau_d = float(np.clip(tau_d, *self.tau_bounds))

        f_donor = self._decay_shape(tau_d)
        f_acc = self._decay_shape(self.optics.tau_a)

        # Leaked donor photons are always alpha x the donor's green signal, and
        # they carry the *donor* lifetime -- so the red channel is a mixture even
        # here, where the stream split itself is free.
        w_green, w_red = 1.0 - p_acc, p_acc
        w_leak = min(self.optics.alpha * w_green, w_red)
        red = (_normalize(w_leak * f_donor + max(w_red - w_leak, 0.0) * f_acc)
               if w_red > 0.0 else f_acc)

        return DecayState(
            stream_probability=np.array([w_green, w_red]),
            decay=np.vstack([self._add_background(f_donor, self.DONOR),
                             self._add_background(red, self.ACCEPTOR)]),
            physical_parameters={"tau_D": tau_d, "p_acceptor": p_acc,
                                 "leak_fraction": (w_leak / w_red) if w_red > 0 else 0.0},
            physical_model="lifetime + stream split (no FRET coupling imposed)")

    def to_obs(self, params):
        params = np.atleast_2d(params)
        return build_emission([self.state(row) for row in params])

    def m_step(self, gamma_obs, params, log_prior=None):
        gamma_obs = np.asarray(gamma_obs, dtype=np.float64)
        out = np.array(np.atleast_2d(params), dtype=np.float64, copy=True)
        B = self.n_bins
        for k in range(self.n_states):
            counts = gamma_obs[k].reshape(self.n_streams, B)

            # stream split: closed form, identical to the categorical M-step
            tot = counts.sum(axis=1)
            if tot.sum() > 0:
                out[k, 1] = tot[self.ACCEPTOR] / tot.sum()

            # donor lifetime: one scalar search against the donor micro-times
            donor = counts[self.DONOR]

            def objective(tau, donor=donor, k=k):
                f = self._decay(float(np.clip(tau, *self.tau_bounds)))
                q = float(np.dot(donor, np.log(np.maximum(f, np.finfo(float).tiny))))
                if log_prior is not None:
                    q += float(log_prior(k, tau))
                return q

            out[k, 0] = _golden_max(objective, *self.tau_bounds)
        return out


class FretDistanceEmission(_DecayBase):
    """FRET states as **distance distributions** over a multi-exponential donor.

    The physically complete version of :class:`FretEmission`, and the one to
    reach for by default.  A state is a mean donor-acceptor distance ``<R>``;
    the linker width ``sigma``, the Förster radius ``R0`` and the donor's own
    (multi-exponential) lifetime spectrum are shared nuisances.

    Two things follow that the single-``E``, mono-exponential model gets wrong:

    * **the donor decay of one state is multi-exponential**, because every
      distance in the distribution quenches every donor component differently.
      There is no single "the lifetime" of a state;
    * **E is not 1 − τ/τ_D0.**  The photon ratio follows the *species-averaged*
      lifetime, ``E = 1 − τ_x(DA)/τ_x(D0)``, while a mono-exponential read of the
      decay returns ``τ_f``.  The gap between them is the curvature of the static
      FRET line, and assuming the naive relation bakes in a bias exactly along
      the axis this model exists to exploit.

    Still **one free parameter per state** — the mean distance — so the fit stays
    as findable as before.  Set ``sigma=0`` with a scalar donor to recover the
    idealisation.
    """

    DONOR, ACCEPTOR = 0, 1

    def __init__(self, n_states, donor=4.0, tau_a=3.0, r0=52.0, sigma=6.0,
                 n_bins=1024, dt=0.032, irf=None, background=0.0, gamma=1.0,
                 alpha=0.0, delta=0.0, optics=None, background_pattern=None,
                 distance_bounds=(15.0, 150.0), n_distance_samples=15,
                 anisotropy=None):
        self.n_states = int(n_states)
        self.donor = donor
        self.r0 = float(r0)
        self.sigma = float(sigma)
        self.n_bins = int(n_bins)
        self.dt = float(dt)
        self.irf = None if irf is None else np.asarray(irf, dtype=np.float64)
        self.background = background
        self.background_pattern = background_pattern
        self.anisotropy = anisotropy
        # two colours, doubled into parallel/perpendicular when polarisation is modelled
        self.n_streams = 2 if anisotropy is None else 4
        self.distance_bounds = (float(distance_bounds[0]), float(distance_bounds[1]))
        self.n_distance_samples = int(n_distance_samples)
        self.optics = optics if optics is not None else Optics(
            alpha=alpha, delta=delta, gamma=gamma, tau_a=tau_a)
        # donor-only species average sets the reference the efficiency is against
        d_x, d_tau = donor_lifetime_spectrum(donor)
        self.tau_x_d0 = lifetime_averages(d_x, d_tau)[0]

    @property
    def p(self):
        return self.n_streams * self.n_bins

    def n_free(self):
        return self.n_states

    def spectrum(self, mean_distance):
        return fret_lifetime_spectrum(mean_distance, self.donor, self.r0,
                                      self.sigma, self.n_distance_samples)

    def efficiency(self, mean_distance):
        """``E`` as the photon ratio sees it: ``1 − τ_x(DA)/τ_x(D0)``."""
        amp, tau = self.spectrum(mean_distance)
        return 1.0 - lifetime_averages(amp, tau)[0] / self.tau_x_d0

    def state(self, params):
        r = float(np.clip(np.atleast_1d(params)[0], *self.distance_bounds))
        o = self.optics
        amp, tau = self.spectrum(r)
        e = float(np.clip(1.0 - lifetime_averages(amp, tau)[0] / self.tau_x_d0,
                          _EPS, 1.0 - _EPS))
        w = o.pathway_weights(e)

        f_donor = self._shape(multi_exponential(tau, amp, self.n_bins, self.dt))
        s_amp, s_tau = convolved_spectrum(amp, tau, o.tau_a)
        f_sens = self._shape(multi_exponential(s_tau, s_amp, self.n_bins, self.dt))
        f_direct = self._decay_shape(o.tau_a)

        w_green = w["donor_green"]
        w_red = w["donor_red"] + w["sensitized_red"] + w["direct_red"]
        red = (w["donor_red"] * f_donor + w["sensitized_red"] * f_sens
               + w["direct_red"] * f_direct)
        red = _normalize(red) if w_red > 0.0 else f_direct

        tau_x, tau_f = lifetime_averages(amp, tau)
        total = w_green + w_red
        green_d = self._add_background(f_donor, self.DONOR)
        red_d = self._add_background(red, self.ACCEPTOR)
        probs = np.array([w_green / total, w_red / total])
        decays = np.vstack([green_d, red_d])
        if self.anisotropy is not None:
            probs, decays = self._polarise(probs, decays)
        return DecayState(
            stream_probability=probs,
            decay=decays,
            physical_parameters={"distance": r, "E": e, "R0": self.r0,
                                 "sigma": self.sigma, "tau_x": tau_x,
                                 "tau_f": tau_f, "n_components": len(tau),
                                 **o.hellenkamp()},
            physical_model="FRET, distance distribution x multi-exponential donor")

    def _polarise(self, probs, decays):
        """Split each colour into parallel/perpendicular, per micro-time bin.

        The joint over (stream, bin) is renormalised into a stream probability
        and a per-stream density, which is the layout the E-step consumes.
        Stream order is ``g-par, r-par, g-perp, r-perp`` to match the routing.
        """
        t = self.dt * np.arange(self.n_bins)
        w_par = self.anisotropy.parallel_fraction(t)
        joint = np.vstack([probs[0] * decays[0] * w_par,
                           probs[1] * decays[1] * w_par,
                           probs[0] * decays[0] * (1.0 - w_par),
                           probs[1] * decays[1] * (1.0 - w_par)])
        out_p = joint.sum(axis=1)
        out_d = joint / np.maximum(out_p[:, None], _EPS)
        return out_p / out_p.sum(), out_d

    def to_obs(self, params):
        params = np.atleast_2d(np.asarray(params, dtype=np.float64)
                               .reshape(self.n_states, -1))
        return build_emission([self.state(row) for row in params])

    def m_step(self, gamma_obs, params, log_prior=None):
        """One bounded scalar search per state, over the mean distance.

        Distance is the natural coordinate for a prior: a structure or a
        calibration constrains ``R``, not ``E``, and ``DecayFitPrior`` kinds like
        Normal or LogNormal apply directly to it.
        """
        gamma_obs = np.asarray(gamma_obs, dtype=np.float64)
        out = np.array(np.atleast_2d(np.asarray(params, dtype=np.float64)
                                     .reshape(self.n_states, -1)), copy=True)
        for k in range(self.n_states):
            counts = gamma_obs[k]

            def objective(r, counts=counts, k=k):
                row = self.state([r]).row()
                q = float(np.dot(counts, np.log(np.maximum(row, np.finfo(float).tiny))))
                if log_prior is not None:
                    q += float(log_prior(k, r))
                return q

            out[k, 0] = _golden_max(objective, *self.distance_bounds)
        return out


class FretEmission(_DecayBase):
    """Two-stream FRET: one efficiency per state generates the whole row.

    .. note::
       This is the **idealisation**: a mono-exponential donor and a single
       donor-acceptor distance, so ``τ_DA = τ_D0(1−E)`` holds exactly.  Real
       donors are multi-exponential and real states are distributed in distance,
       which breaks that relation — use :class:`FretDistanceEmission` for the
       physically complete model.  This class is retained as the degenerate case
       and as the control the misspecification sweep measures against.

    Parameters
    ----------
    tau_d0 : float
        Donor lifetime with no acceptor, in the same units as ``dt``.
    tau_a : float
        Acceptor lifetime (treated as a fixed nuisance).
    n_bins, dt : int, float
        Micro-time axis.  32-64 bins is plenty to separate lifetimes.
    irf : array, optional
        Instrument response.  ``None`` means a delta IRF.
    background : float
        Fraction of photons that are uncorrelated, flat in micro-time.
    gamma : float
        Detection-correction factor relating the measured ratio to ``E``.
    """

    DONOR, ACCEPTOR = 0, 1

    def __init__(self, n_states, tau_d0, tau_a, n_bins=32, dt=0.25,
                 irf=None, background=0.0, gamma=1.0, alpha=0.0, delta=0.0,
                 optics=None, background_pattern=None):
        self.n_states = int(n_states)
        self.tau_d0 = float(tau_d0)
        self.tau_a = float(tau_a)
        self.n_bins = int(n_bins)
        self.dt = float(dt)
        self.irf = None if irf is None else np.asarray(irf, dtype=np.float64)
        self.background = background
        self.background_pattern = background_pattern
        self.n_streams = 2
        self.optics = optics if optics is not None else Optics(
            alpha=alpha, delta=delta, gamma=gamma, tau_d0=tau_d0, tau_a=tau_a)
        self.gamma = self.optics.gamma

    @property
    def p(self):
        return self.n_streams * self.n_bins

    def n_free(self):
        """One efficiency per state -- not ``p - 1`` per state.

        This is what keeps model selection honest once the alphabet is the
        product one: a three-state lifetime-resolved model has 3 emission
        parameters, where a free categorical model over 64 bins would have 189.
        """
        return self.n_states

    # -- the physics ------------------------------------------------------

    def state(self, params):
        """The ``DecayState`` implied by a FRET efficiency.

        Accepts a bare float or a length-1 parameter row.
        """
        e = float(np.clip(np.atleast_1d(params)[0], _EPS, 1.0 - _EPS))
        o = self.optics
        tau_da = o.tau_d0 * (1.0 - e)
        w = o.pathway_weights(e)

        # Each pathway has its own micro-time law, so the red channel is a
        # *mixture* -- and the leaked-donor term carries the DONOR lifetime,
        # which is why the red channel also carries information about E.
        f_donor = self._decay_shape(tau_da)
        f_sens = self._sensitized_shape(tau_da, o.tau_a)
        f_direct = self._decay_shape(o.tau_a)

        w_green = w["donor_green"]
        w_red = w["donor_red"] + w["sensitized_red"] + w["direct_red"]
        red = (w["donor_red"] * f_donor
               + w["sensitized_red"] * f_sens
               + w["direct_red"] * f_direct)
        red = _normalize(red) if w_red > 0.0 else f_direct

        total = w_green + w_red
        return DecayState(
            stream_probability=np.array([w_green / total, w_red / total]),
            decay=np.vstack([self._add_background(f_donor, self.DONOR),
                             self._add_background(red, self.ACCEPTOR)]),
            physical_parameters={"E": e, "tau_DA": tau_da, "tau_D0": o.tau_d0,
                                 "tau_A": o.tau_a, **o.hellenkamp()},
            physical_model="FRET via matrix triple (excitation x transfer x emission)")

    def to_obs(self, params):
        params = np.atleast_2d(np.asarray(params, dtype=np.float64).reshape(self.n_states, -1))
        return build_emission([self.state(row) for row in params])

    # -- the M-step -------------------------------------------------------

    def m_step(self, gamma_obs, params, log_prior=None):
        """Maximise ``Q(E) + log p(E)`` per state, by bounded 1-D search.

        The whole row depends on exactly one number, so this is a set of
        independent scalar maximisations -- cheap, and the natural home for
        non-conjugate priors (Normal on a distance, LogNormal on a lifetime),
        which have no exact form on simplex entries.

        Note the search cannot be factorised the way ``LifetimeEmission`` does:
        ``E`` sets the stream split *and* the donor lifetime, so both parts of
        the data pull on the same parameter.  That coupling is the model's
        content, not an inconvenience.
        """
        gamma_obs = np.asarray(gamma_obs, dtype=np.float64)
        out = np.array(np.atleast_2d(np.asarray(params, dtype=np.float64)
                                     .reshape(self.n_states, -1)), copy=True)
        for k in range(self.n_states):
            counts = gamma_obs[k]

            def objective(e, counts=counts, k=k):
                row = self.state(e).row()
                q = float(np.dot(counts, np.log(np.maximum(row, np.finfo(float).tiny))))
                if log_prior is not None:
                    q += float(log_prior(k, e))
                return q

            out[k, 0] = _golden_max(objective, _EPS, 1.0 - _EPS)
        return out


def _golden_max(f, lo, hi, tol=1e-7, max_iter=200):
    """Golden-section maximisation of a unimodal ``f`` on ``[lo, hi]``.

    Written out rather than imported: SciPy is unavailable, and the C++ port
    needs this anyway.
    """
    invphi = (np.sqrt(5.0) - 1.0) / 2.0
    a, b = lo, hi
    c = b - invphi * (b - a)
    d = a + invphi * (b - a)
    fc, fd = f(c), f(d)
    for _ in range(max_iter):
        if b - a < tol:
            break
        if fc > fd:
            b, d, fd = d, c, fc
            c = b - invphi * (b - a)
            fc = f(c)
        else:
            a, c, fc = c, d, fd
            d = a + invphi * (b - a)
            fd = f(d)
    return 0.5 * (a + b)


# ---------------------------------------------------------------------------
# fitting
# ---------------------------------------------------------------------------

def fit_emission(data: PhotonData, emission, params, trans, prior,
                 constraints: HmmConstraints = None, log_prior=None,
                 max_iter=200, tol=1e-8, min_trans=1e-12, track=False):
    """EM where the emission M-step is the physical one.

    ``pi`` and ``A`` keep their exact Dirichlet M-steps; only ``B`` changes, and
    it changes from "normalise the expected counts" to "find the physical
    parameters that best explain them".  Works with any object following the
    ``_DecayBase`` protocol.  Returns ``(model, params)``.
    """
    n = emission.n_states
    if data.p != emission.p:
        raise ValueError(f"data alphabet is {data.p}, emission needs {emission.p}")
    if constraints is None:
        constraints = HmmConstraints.flat(n, data.p)

    e = np.atleast_2d(np.asarray(params, dtype=np.float64).reshape(n, -1))
    prior = row_normalize(np.asarray(prior, float).reshape(1, -1)).ravel()
    trans = row_normalize(np.asarray(trans, float))
    obs = emission.to_obs(e)

    history = []
    prev = -np.inf
    converged = False
    it = 0
    loglik = -np.inf

    for it in range(1, max_iter + 1):
        apow = matrix_powers(trans, data.max_gap)
        prior_acc, gamma_obs, xi, loglik = e_step(data, prior, trans, obs, apow)
        objective = loglik + constraints.log_prior(prior, trans, obs)
        if track:
            history.append(objective)

        prior = constraints.apply(prior_acc, "prior")
        trans = constraints.apply(xi, "trans")
        e = emission.m_step(gamma_obs, e, log_prior)
        obs = emission.to_obs(e)

        if min_trans > 0.0:
            off = ~np.eye(n, dtype=bool)
            bump = off & (trans < min_trans)
            if bump.any():
                trans[bump] = min_trans
                trans = row_normalize(trans)

        if it > 1 and objective - prev < tol:
            converged = True
            break
        prev = objective

    model = HmmModel(prior, trans, obs, float(loglik),
                     float(loglik + constraints.log_prior(prior, trans, obs)),
                     it, converged, history)
    return model, e


# ---------------------------------------------------------------------------
# simulation
# ---------------------------------------------------------------------------

def simulate_lifetime(prior, A, states, times, seed=0):
    """Photon streams *and* micro-time bins from a set of ``DecayState``s.

    Returns ``(streams, micro, paths)``; ``paths`` is the true state per photon,
    which is what makes the separability test scorable.
    """
    rng = np.random.default_rng(seed)
    prior = np.asarray(prior, float)
    A = np.asarray(A, float)
    n = len(prior)
    n_streams = len(states[0].stream_probability)
    n_bins = states[0].decay.shape[1]

    streams, micro, paths = [], [], []
    for t in times:
        t = np.asarray(t, dtype=np.int64)
        s = np.empty(len(t), dtype=np.int64)
        b = np.empty(len(t), dtype=np.int64)
        z = np.empty(len(t), dtype=np.int64)
        state = rng.choice(n, p=prior)
        for k in range(len(t)):
            if k > 0:
                dt = int(t[k] - t[k - 1])
                if dt > 0:
                    state = rng.choice(n, p=np.linalg.matrix_power(A, dt)[state])
            z[k] = state
            s[k] = rng.choice(n_streams, p=states[state].stream_probability)
            b[k] = rng.choice(n_bins, p=states[state].decay[s[k]])
        streams.append(s.tolist())
        micro.append(b.tolist())
        paths.append(z.tolist())
    return streams, micro, paths
