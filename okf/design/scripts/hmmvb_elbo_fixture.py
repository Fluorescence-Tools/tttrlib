"""Same E / H / logZ / BIC / ICL table on the H2MM_C fixture case two_state_2det
(test/data/reference/hmm_h2mm_c_reference.npz), which is the case okf/BUGS.md
cites for the ~1 nat gap. Run from the repo root."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from scipy.special import gammaln
import tttrlib
from hmmvb_elbo_decision import vb_terms, ml_fit, log_evidence_is

d = np.load("test/data/reference/hmm_h2mm_c_reference.npz")
name = "two_state_2det"
g = lambda k: d[f"{name}/{k}"]
off = g("offsets"); T, S = g("times"), g("streams")
times = [T[off[i]:off[i + 1]].tolist() for i in range(len(off) - 1)]
streams = [S[off[i]:off[i + 1]].tolist() for i in range(len(off) - 1)]
n_det = int(g("obs").shape[1])
eng = tttrlib.HMM(); eng.set_bursts(times, streams, n_det)
print(f"{len(times)} bursts, {int(off[-1])} photons, {sum(t[-1]-t[0] for t in times)} ticks")
print(f"{'K':>3s}{'E(engine)':>12s}{'H(header)':>12s}{'E-H':>7s}{'pred':>7s}{'logZ(IS)':>11s}{'ESS':>7s}{'lnK!':>6s}{'ML-loglik':>11s}{'BIC':>10s}{'ICL':>10s}")
for K in (1, 2, 3, 4):
    r = vb_terms(eng, K, n_det, times, streams, 0)
    ll, bic, icl = ml_fit(eng, K, n_det, 0)
    lz, ess = log_evidence_is(eng, K, n_det, r["ap"], r["at"], r["ao"], 3000, 0)
    print(f"{K:3d}{r['E']:12.2f}{r['H']:12.2f}{r['E']-r['H']:7.3f}{r['pred_gap']:7.3f}{lz:11.2f}{ess:7.0f}{float(gammaln(K+1)):6.2f}{ll:11.2f}{bic:10.1f}{icl:10.1f}")
