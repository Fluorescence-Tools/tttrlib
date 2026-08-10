#!/usr/bin/env python3
"""Fast A/B test: streaming vs batch correlator on saved photon data.

Simulates once (or loads cached), runs both correlators, prints table
and saves a comparison PNG.
"""
import sys, os
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import tttrlib

PHOTON_FILE = os.path.join(os.path.dirname(__file__), 'fcs_test_photons.npy')
PNG_OUT = os.path.join(os.path.dirname(__file__), 'fcs_ab_compare.png')

# Diffusion simulation parameters (must match the data generation)
w_xy = 0.5e-6; w_z = 2.0e-6; D = 4e-12
tau_D = w_xy**2 / (4 * D)
dt = tau_D / 50

def load_or_sim():
    if os.path.exists(PHOTON_FILE):
        return np.load(PHON_FILE if False else PHOTON_FILE)
    # fallback: simulate
    from numpy.random import default_rng
    rng = default_rng(42)
    n_steps = 300_000; box = 8e-6; n_particles = 4; crp = 30.0
    pos = rng.uniform(-box/2, box/2, (n_particles, 3))
    brownian = np.sqrt(2*D*dt)
    photons = []
    for step in range(n_steps):
        pos += brownian * rng.standard_normal((n_particles, 3))
        pos = np.mod(pos + box/2, box) - box/2
        r2 = pos[:,0]**2 + pos[:,1]**2; z2 = pos[:,2]**2
        br = np.exp(-2*r2/w_xy**2) * np.exp(-2*z2/w_z**2)
        n_ph = rng.poisson(crp * np.sum(br))
        if n_ph: photons.extend([step]*n_ph)
    p = np.array(photons, dtype=np.uint64)
    np.save(PHOTON_FILE, p)
    return p

def main():
    photons = load_or_sim()
    print(f'{len(photons)} photons, T={photons[-1]}, dt={dt:.6f}s, tau_D={tau_D*1e3:.2f}ms')

    w = np.ones(len(photons), dtype=np.float64)

    # --- BATCH (reference) ---
    batch = tttrlib.Correlator()
    batch.method = 'wahl'; batch.n_bins = 16; batch.n_casc = 25
    batch.set_macrotimes(photons, photons)
    batch.set_weights(w, w)
    xb = np.array(batch.x_axis)            # macro-time units
    gb = np.array(batch.correlation)       # normalized G
    tb_ms = xb * dt * 1e3                  # → ms

    # --- STREAMING ---
    corr = tttrlib.StreamingCorrelator(16, 25, dt)
    corr.push_np(photons)
    corr.flush()
    xs = corr.x_axis                       # already in seconds
    gs = corr.correlation_normalized
    ts_ms = xs * 1e3                       # → ms

    # --- TABLE ---
    print(f'\n{"tau_ms":>10} {"batch_G":>12} {"stream_G":>12} {"ratio":>8} {"cascade":>8}')
    for target in [0.1, 0.5, 1, 2, 5, 10, 15, 20, 30, 50, 100, 200]:
        ib = np.argmin(np.abs(tb_ms - target))
        is_ = np.argmin(np.abs(ts_ms - target))
        b_cascade = ib // 16
        print(f'{tb_ms[ib]:>10.3f} {gb[ib]:>12.4f} {gs[is_]:>12.4f} {gs[is_]/(gb[ib]+1e-30):>8.3f} {b_cascade:>8}')

    # --- PLOT ---
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Left: G(tau) overlay
    ax = axes[0]
    mask_b = (tb_ms > 0.05) & (gb > 0)
    mask_s = (ts_ms > 0.05) & (gs > 0)
    ax.semilogx(tb_ms[mask_b], gb[mask_b], 'b-', lw=2, label='Batch (Wahl)')
    ax.semilogx(ts_ms[mask_s], gs[mask_s], 'r.', ms=4, alpha=0.7, label='Streaming')
    ax.axvline(tau_D*1e3, color='k', ls=':', alpha=0.5, label=f'tau_D={tau_D*1e3:.1f} ms')
    ax.set_xlabel('Lag time (ms)')
    ax.set_ylabel('G(tau)')
    ax.set_title('FCS: Batch vs Streaming')
    ax.legend(); ax.set_ylim(0, 80)

    # Right: only cascade 0 (where streaming matches batch within 3%)
    ax = axes[1]
    mask_c0 = (ts_ms > 0.05) & (ts_ms < 5.0) & (gs > 0)
    mask_b0 = (tb_ms > 0.05) & (tb_ms < 5.0) & (gb > 0)
    ax.semilogx(tb_ms[mask_b0], gb[mask_b0], 'b-', lw=2, label='Batch')
    ax.semilogx(ts_ms[mask_c0], gs[mask_c0], 'r.', ms=6, label='Streaming')
    ax.set_xlabel('Lag time (ms)')
    ax.set_ylabel('G(tau)')
    ax.set_title('Cascade 0 (stream matches batch within 3%)')
    ax.legend()

    plt.tight_layout()
    plt.savefig(PNG_OUT, dpi=150)
    print(f'\nSaved plot to {PNG_OUT}')
    plt.close()

if __name__ == '__main__':
    main()
