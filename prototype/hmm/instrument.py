"""Adversarial data: build a `SimSystem` species graph, let `SimEngine` run it.

Every result up to here was simulated by the same model that was then fitted,
which tests the inference and cannot test the model.  There is no real
measurement to fall back on, so the substitute is a deliberately harder
simulator: instrument imperfections and kinetics the fitted model does **not**
know about.

**This is a builder, not a simulator.**  tttrlib's own `SimEngine` generates the
photons; this module only maps physical parameters onto the species graph it
consumes.  That matters — a second photon simulator could silently disagree with
the engine, and then every degradation number would be measuring the wrong thing.

The species graph is the whole trick
------------------------------------

`SimSpecies` carries **its own** `decay` and **its own** per-channel brightness
`q`, and `k_rad`/`k_nrad` are N×N rate matrices *between species*
(`SimSystem.h:44`).  So a per-channel micro-time distribution — the thing a
single fixed decay cannot express — comes from using more species:

* a **donor-emitting** species with `q = [q, α·q]`: its leaked photons land in
  red and **carry the donor lifetime automatically**, because the donor emitted
  them.  No special case, no correction factor.
* a **sensitized-acceptor** species with `q = [0, γ·q]` and the rise-bearing
  decay `τ_DA ⊛ τ_A`.
* a **directly excited acceptor** with a plain `τ_A`.
* **dark** (blinked) and **bleached** species reached through `k_nrad`.

Per-excitation branching (does the donor emit, or does it transfer?) is encoded
as *fast* interconversion between those species.  Setting ``k[i][j] = p_j · K``
makes the stationary distribution exactly ``p``, so the occupancy — hence the
photon ratio — is whatever the matrix triple says it should be.  ``K`` only has
to beat the photon rate; the engine resolves several transitions per step, so it
does not force a smaller ``dt``.

Verified against the engine: leak ratio tracks ``q[1]/q[0]`` (0.198 vs 0.200),
occupancy tracks ``E`` (0.4005 vs 0.400), and the sensitized channel's mean
micro-time is ``τ_DA + τ_A + IRF`` — the *sum*, which is the rise.
"""
from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from .emission import (Optics, convolved_spectrum, donor_lifetime_spectrum,
                       fret_lifetime_spectrum, lifetime_averages)

__all__ = ["KineticScheme", "Photophysics", "InstrumentConfig", "SimulatedData",
           "build_system", "simulate"]

_FAST = 5.0e3       # interconversion rate (1/ms): must beat the photon rate

# pathways that interconvert per excitation (as opposed to slow photophysics)
_EMITTING = ("donor", "leak", "sensitized", "direct")

# MFD routing convention: green 0 / red 1 parallel, +8 for perpendicular
# (chisurf core/fluorescence/mfd/simulate.py:69)
ROUTING = {(0, 0): 0, (1, 0): 1, (0, 1): 8, (1, 1): 9}


# ---------------------------------------------------------------------------
# what is being simulated
# ---------------------------------------------------------------------------

@dataclass
class KineticScheme:
    """Conformational states and the rates between them.

    ``distances`` are mean donor-acceptor distances (Å) — the physical
    coordinate, not efficiencies.  ``rates[i, j]`` is the i→j rate in 1/ms;
    the diagonal is ignored.  Topology is *data*: a linear chain A↔B↔C is the
    fully connected matrix with ``k13 = k31 = 0``.
    """
    distances: np.ndarray
    rates: np.ndarray

    def __post_init__(self):
        self.distances = np.atleast_1d(np.asarray(self.distances, dtype=float))
        self.rates = np.asarray(self.rates, dtype=float).reshape(
            len(self.distances), len(self.distances))
        np.fill_diagonal(self.rates, 0.0)

    @property
    def n_states(self):
        return len(self.distances)

    @classmethod
    def two_state(cls, distances=(45.0, 62.0), k_forward=1.0, k_backward=1.0):
        return cls(distances, [[0.0, k_forward], [k_backward, 0.0]])

    @classmethod
    def linear(cls, distances=(40.0, 52.0, 65.0), k=1.0):
        """A↔B↔C — the ends do not interconvert directly."""
        n = len(distances)
        r = np.zeros((n, n))
        for i in range(n - 1):
            r[i, i + 1] = r[i + 1, i] = k
        return cls(distances, r)

    @classmethod
    def cyclic(cls, distances=(40.0, 52.0, 65.0), k=1.0):
        """Every state reachable from every other."""
        n = len(distances)
        r = np.full((n, n), float(k))
        np.fill_diagonal(r, 0.0)
        return cls(distances, r)

    @classmethod
    def from_regime(cls, distances, regime="intermediate", burst_ms=2.0):
        """Rates set by *transitions per burst*, which is what decides
        identifiability — not the absolute rate.

        Regimes follow chisurf's `mfd/simulate.py:58`.
        """
        per_burst = {"static": 0.0, "slow": 0.05, "intermediate": 1.5, "fast": 60.0}
        if regime not in per_burst:
            raise ValueError(f"unknown regime {regime!r}; pick from {sorted(per_burst)}")
        return cls.cyclic(distances, k=per_burst[regime] / max(burst_ms, 1e-9))


@dataclass
class Photophysics:
    """Acceptor blinking and bleaching, as extra species.

    Rates in 1/ms.  Bleaching is **absorbing** — it breaks stationarity, so π
    stops being an equilibrium population and dwell-time readings need care.
    Ranges follow burstnet `training/simulate.py:656`.
    """
    blink_off: float = 0.0      # bright -> dark acceptor
    blink_on: float = 0.0       # dark -> bright
    bleach: float = 0.0         # -> permanently dark

    @property
    def enabled(self):
        return (self.blink_off > 0) or (self.bleach > 0)


@dataclass
class InstrumentConfig:
    """The instrument: optics, decays, background, anisotropy, timing."""
    optics: Optics = field(default_factory=Optics)
    donor: object = 4.0               # scalar or (amplitudes, lifetimes)
    r0: float = 52.0
    sigma: float = 6.0                # linker width (Å); 0 = single distance
    brightness: float = 100.0         # photons / molecule / ms

    background: tuple = (0.0, 0.0)    # counts / ms per channel
    background_kind: str = "uniform"  # uniform | scatter | dirt
    background_lifetime: float = 3.0

    # anisotropy — tttrlib's convention (SimSpecies r0/l1/l2/D_rot)
    aniso_r0: float = 0.0
    l1: float = 0.0
    l2: float = 0.0
    d_rot: float = 0.0

    n_channels: int = 2               # 2 = green/red; 4 adds the par/perp pair
    n_microtime_channels: int = 4096
    microtime_resolution: float = 0.008     # ns
    laser_period: float = 32.0              # ns
    irf_mean: float = 1.0
    irf_fwhm: float = 0.15

    dt: float = 0.01                  # ms — the integrator step
    macro_resolution: float = 1e-5    # ms per macro-time tick (10 ns)
    n_photons: int = 200_000
    max_windows: int = 40_000
    box_xy: float = 2.0
    box_z: float = 4.0
    w0: float = 0.3
    z0: float = 2.0
    diffusion: float = 0.0            # 0 = immobile emitters parked in the focus
    n_molecules: int = 1
    """Molecules in the focus.

    **Keep this at 1** unless you are deliberately simulating coincidence.  With
    several emitters lit at once their photons interleave in time, so a
    time-ordered stream is a *mixture* of independent trajectories, not a
    single-molecule trace — the HMM's core assumption.  It shows up as an absurd
    apparent transition rate: four parked molecules gave a state change every 2.6
    photons where the kinetics called for one per burst, and pulled the fitted
    states toward each other.  Real burst analysis avoids this by selecting
    single-molecule bursts; here it is simplest not to create it.
    """

    def irf(self, tttrlib):
        return tttrlib.SimDecay.gaussian_irf(
            self.n_microtime_channels, self.microtime_resolution,
            self.irf_mean, self.irf_fwhm)


@dataclass
class SimulatedData:
    """Photons plus the engine's own ground truth."""
    times: list
    streams: list
    micro: list
    states: list                  # conformational state per photon (-1 = background)
    n_micro_bins: int
    n_streams: int                # 2 (green/red) or 4 (+ parallel/perpendicular)
    truth: dict

    @property
    def n_photons(self):
        return sum(len(t) for t in self.times)


# ---------------------------------------------------------------------------
# the builder
# ---------------------------------------------------------------------------

def _background_pattern(cfg, tttrlib, irf):
    """uniform / scatter (IRF-shaped) / dirt (IRF + a long-lifetime component)."""
    nb, dt = cfg.n_microtime_channels, cfg.microtime_resolution
    if cfg.background_kind == "uniform":
        return tttrlib.SimDecay.from_pattern([1.0] * nb, dt)
    if cfg.background_kind == "scatter":
        return tttrlib.SimDecay.from_pattern(list(irf), dt)
    if cfg.background_kind == "dirt":
        slow = tttrlib.SimDecay.multi_exponential_with_irf(
            [1.0], [cfg.background_lifetime], nb, dt, irf)
        pat = 0.5 * np.asarray(list(irf)) / max(np.sum(list(irf)), 1e-30)
        pat = pat + 0.5 * np.asarray([slow.pdf(i) for i in range(nb)]) \
            if hasattr(slow, "pdf") else pat + 0.5 / nb
        return tttrlib.SimDecay.from_pattern(list(pat), dt)
    raise ValueError(f"unknown background_kind {cfg.background_kind!r}")


def build_system(scheme: KineticScheme, cfg: InstrumentConfig,
                 photophysics: Photophysics = None, tttrlib=None):
    """Map the physics onto a `SimSystem`.

    Returns ``(system, meta)``; ``meta['state_of_species']`` maps each species
    back to its conformational state so the engine's ``emitting_species()`` can
    be reduced to a ground-truth state label.
    """
    if tttrlib is None:
        import tttrlib as _t
        tttrlib = _t
    pp = photophysics or Photophysics()
    o = cfg.optics
    nb, mres = cfg.n_microtime_channels, cfg.microtime_resolution
    irf = cfg.irf(tttrlib)
    q = cfg.brightness

    def decay(amps, taus):
        return tttrlib.SimDecay.multi_exponential_with_irf(
            list(np.atleast_1d(amps)), list(np.atleast_1d(taus)), nb, mres, irf)

    d_amp, d_tau = donor_lifetime_spectrum(cfg.donor)
    tau_x_d0 = lifetime_averages(d_amp, d_tau)[0]
    delta_ex = o.delta / max(o.gamma, 1e-12)     # detected -> photophysical

    species, state_of, kind_of, occupancy = [], [], [], []
    efficiencies = []

    def add(sp_q, sp_decay, state, kind, occ):
        s = tttrlib.SimSpecies()
        s.q = list(sp_q)
        s.decay = sp_decay
        s.D = cfg.diffusion
        s.r0, s.l1, s.l2, s.D_rot = cfg.aniso_r0, cfg.l1, cfg.l2, cfg.d_rot
        species.append(s)
        state_of.append(state)
        kind_of.append(kind)
        occupancy.append(occ)

    # One species per (colour, pathway).  Splitting the donor's green emission
    # from its red leak looks redundant with a single detector pair, but it is
    # what makes polarisation possible: the anisotropy branch overwrites the
    # channel with the parallel/perpendicular split, so **colour has to come from
    # the species identity**, which requires every species to have exactly one.
    GREEN, RED = 0, 1
    colour_of = []

    def add_c(colour, sp_decay, state, kind, occ):
        row = [0.0] * cfg.n_channels
        row[colour] = q
        add(row, sp_decay, state, kind, occ)
        colour_of.append(colour)

    for k, r in enumerate(scheme.distances):
        amp, tau = fret_lifetime_spectrum(r, cfg.donor, cfg.r0, cfg.sigma)
        e = float(np.clip(1.0 - lifetime_averages(amp, tau)[0] / tau_x_d0, 1e-9, 1 - 1e-9))
        efficiencies.append(e)
        w = o.pathway_weights(e)
        d_state = decay(amp, tau)

        add_c(GREEN, d_state, k, "donor", w["donor_green"])
        # the leak keeps the DONOR decay -- that is the whole point of it
        if o.alpha > 0:
            add_c(RED, d_state, k, "leak", w["donor_red"])
        sens_amp, sens_tau = convolved_spectrum(amp, tau, o.tau_a)
        add_c(RED, decay(sens_amp, sens_tau), k, "sensitized", w["sensitized_red"])
        if delta_ex > 0:
            add_c(RED, decay([1.0], [o.tau_a]), k, "direct", w["direct_red"])
        if pp.blink_off > 0 or pp.bleach > 0:
            # dark acceptor: donor unquenched -- the state FRET cannot fake
            add_c(GREEN, decay(d_amp, d_tau), k, "dark", 0.0)

    if pp.bleach > 0:
        add_c(GREEN, decay(d_amp, d_tau), -1, "bleached", 0.0)

    n = len(species)
    k_nrad = np.zeros((n, n))
    for i in range(n):
        si, ki = state_of[i], kind_of[i]
        for j in range(n):
            if i == j:
                continue
            sj, kj = state_of[j], kind_of[j]
            if si == sj and ki in _EMITTING and kj in _EMITTING:
                # fast branching: k[i][j] = p_j * K  =>  stationary distribution = p
                tot = sum(occupancy[m] for m in range(n)
                          if state_of[m] == si and kind_of[m] in _EMITTING)
                k_nrad[i, j] = _FAST * occupancy[j] / max(tot, 1e-12)
            elif si >= 0 and sj >= 0 and si != sj and ki == kj:
                k_nrad[i, j] = scheme.rates[si, sj]      # conformational
            elif si == sj and ki == "donor" and kj == "dark":
                k_nrad[i, j] = pp.blink_off
            elif si == sj and ki == "dark" and kj == "donor":
                k_nrad[i, j] = pp.blink_on
            elif kj == "bleached" and ki != "bleached":
                k_nrad[i, j] = pp.bleach

    system = tttrlib.SimSystem()
    for s in species:
        system.add_species(s)
    system.set_rate_matrices([0.0] * (n * n), list(k_nrad.ravel()))
    system.set_box(cfg.box_xy, cfg.box_z)
    if any(b > 0 for b in cfg.background):
        system.set_background(list(cfg.background))
        system.set_background_decay(_background_pattern(cfg, tttrlib, irf))

    mobile = cfg.diffusion > 0
    for _ in range(cfg.n_molecules):
        system.add_fluorophore(0.0, 0.0, 0.0, 0, mobile)

    meta = {"state_of_species": np.array(state_of),
            "colour_of_species": np.array(colour_of),
            "kind_of_species": kind_of,
            "efficiencies": np.array(efficiencies),
            "distances": scheme.distances.copy(),
            "n_species": n}
    return system, meta


def simulate(scheme: KineticScheme, cfg: InstrumentConfig,
             photophysics: Photophysics = None, seed=1, n_micro_bins=1024,
             burst_ms=2.0, tttrlib=None) -> SimulatedData:
    """Run the engine and chop the stream into bursts.

    Ground truth comes from the engine's own ``emitting_species()`` rather than
    from bookkeeping here, so it cannot share a bug with the builder.
    """
    if tttrlib is None:
        import tttrlib as _t
        tttrlib = _t
    system, meta = build_system(scheme, cfg, photophysics, tttrlib)

    st = tttrlib.SimIntegrator()
    st.dt = cfg.dt
    st.n_channels = cfg.n_channels
    st.n_ph_max = cfg.n_photons
    st.max_windows = cfg.max_windows
    st.n_microtime_channels = cfg.n_microtime_channels
    st.microtime_resolution = cfg.microtime_resolution
    st.laser_period = cfg.laser_period
    st.seed_diffusion = int(seed)
    st.seed_emission = int(seed) + 7919

    exc = tttrlib.SimGrid.gaussian3d(cfg.w0, cfg.z0, cfg.box_xy, cfg.box_z, 0.1, 1.0)
    eng = tttrlib.SimEngine(system, exc, tttrlib.VectorSimGrid(), st)
    eng.run()

    window = np.asarray(eng.macro_window(), dtype=np.int64)
    within = np.asarray(eng.arrival_time(), dtype=float)
    ch = np.asarray(eng.channel(), dtype=np.int64)
    sp = np.asarray(eng.emitting_species(), dtype=np.int64)
    micro = np.asarray(eng.micro_time(), dtype=np.int64)

    keep = ch >= 0
    window, within, ch, sp, micro = (a[keep] for a in (window, within, ch, sp, micro))

    # Macro time on a tick grid *finer* than the integrator step.  Quantising to
    # dt would put several photons on the same tick and erase the very
    # inter-photon gaps the transition rates are inferred from; `within` carries
    # sub-step resolution, so use it.
    t_ms = window.astype(np.float64) * cfg.dt + within
    order = np.argsort(t_ms, kind="stable")
    t_ms, ch, sp, micro = t_ms[order], ch[order], sp[order], micro[order]
    ticks = np.round(t_ms / max(cfg.macro_resolution, 1e-15)).astype(np.int64)

    state_of = meta["state_of_species"]
    colour_of = meta["colour_of_species"]
    idx = np.clip(sp, 0, len(state_of) - 1)
    states = np.where(sp >= 0, state_of[idx], -1)

    # Compose the detection stream.  With anisotropy on, SimEngine overwrites the
    # channel with the parallel/perpendicular split and never reads the species
    # `q` row (SimEngine.h:671-674) -- so colour must come from the species and
    # polarisation from the channel.  Composing them is what recovers all four
    # routing channels (green 0 / red 1 parallel, +8 perpendicular).
    if cfg.aniso_r0 > 0 and cfg.n_channels >= 4:
        colour = np.where(sp >= 0, colour_of[idx], 0)
        pol = np.clip(ch, 0, 1)                      # engine: 0 = par, 1 = perp
        stream = colour + 2 * pol                    # 0 g-par, 1 r-par, 2 g-perp, 3 r-perp
        meta["routing"] = {int(v): ROUTING[(c, p)]
                           for (c, p), v in
                           {(c, p): c + 2 * p for c in (0, 1) for p in (0, 1)}.items()}
        n_streams = 4
    else:
        stream = np.clip(ch, 0, 1)
        meta["routing"] = {0: 0, 1: 1}
        n_streams = 2
    ch = stream

    # coarsen micro-time onto the scoring grid
    bins_per = max(1, cfg.n_microtime_channels // n_micro_bins)
    mb = np.clip(micro // bins_per, 0, n_micro_bins - 1)

    span = max(1, int(round(burst_ms / cfg.macro_resolution)))
    edges = np.arange(ticks.min(), ticks.max() + span, span)
    idx = np.searchsorted(edges, ticks, side="right") - 1

    times, streams, micros, truth_states = [], [], [], []
    for b in np.unique(idx):
        m = idx == b
        if m.sum() < 10:
            continue
        tb = ticks[m]
        times.append((tb - tb[0]).tolist())
        streams.append(ch[m].tolist())
        micros.append(mb[m].tolist())
        truth_states.append(states[m].tolist())

    return SimulatedData(
        times, streams, micros, truth_states, n_micro_bins, n_streams,
        truth={**meta, "scheme": scheme, "config": cfg,
               "photophysics": photophysics or Photophysics()})
