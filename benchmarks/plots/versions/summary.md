Wall time and task memory footprint (peak RSS minus the post-import baseline), tttrlib v0.27.0 vs v0.26.2 — same machine, same inputs. Negative Δ is an improvement.

| Task | v0.26.2 time | v0.27.0 time | Δ time | v0.26.2 mem MB | v0.27.0 mem MB | Δ mem |
|------|----------:|----------:|:-----:|----------:|----------:|:-----:|
| TTTR file reading (3.5 M-photon PTU) | 31.05 ms | 29.74 ms | -4% | 138 | 144 | +5% |
| CLSM intensity — fill+structure (512x512 PTU) | 125.19 ms | 46.27 ms | -63% | 107 | 93 | -12% |
| CLSM fill — 2.6 M-pixel FLIM image (40x256x256 HT3) | 1483.78 ms | 279.39 ms | -81% | 515 | 307 | -40% |
| CLSM intensity — virtual fill (512x512 PTU) | n/a | 21.94 ms | new in v0.27.0 | — | 87 | new |
| Burst search (3.5 M photons) | 2.74 ms | 2.77 ms | +1% | 73 | 74 | +0% |
| Correlation / FCS (3.5 M photons) | 437.97 ms | 279.06 ms | -36% | 350 | 325 | -7% |
| Per-pixel reconvolution-MLE map (256x256) | n/a | 900.05 ms | new in v0.27.0 | — | 934 | new |
