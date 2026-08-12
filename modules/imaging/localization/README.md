# `imaging/localization` — Single-Molecule Localization Microscopy

Algorithms for point-spread function (PSF) fitting and single-molecule
localization from photon images.

## Contents

- **`ImageLocalization.h` / `ImageLocalization.cpp`**: 2D Gaussian PSF fitting
  and centroid localization. `fit2DGaussian` fits one, two or three Gaussians
  sharing a width and an ellipticity, against a Poisson maximum-likelihood cost.

## Dependencies

- `core`, `clsm`, `math`

No third-party dependency. The forward-mode AD below used to come from a
vendored copy of `autodiff`; it is now `Dual.h` in
[`math`](../../math/README.md).

## The gradient, and why the objective looks the way it does

The fit is driven by `i_lbfgs.h`, which by default estimates its gradient with
central differences: 2N objective evaluations per gradient. This module instead
supplies an **exact gradient from one forward-mode AD pass**, seeding
`tttrlib::Dual<GradVec<N>>` with the N basis vectors so all N partial
derivatives come out of a single evaluation. Measured against a *tuned*
central difference (not the untuned one it replaced), that is **3.95×–5.44×**
faster per gradient at this objective's real free-parameter counts.

Those counts are 6 / 9 / 12, for one / two / three Gaussians — **not 18**.
Entries 12..17 of the public `vars` array are flags and outputs, and `i_lbfgs`
minimises in the reduced space of free parameters only. An earlier benchmark
that quoted N=18 was measuring a decay objective and did not apply here.

Two consequences that are easy to undo by accident:

**The objective is a pure function, and it has to stay one.** The original
`target2DGaussian` enforced bounds by *teleporting* an out-of-range parameter to
the middle of its range and writing the result back into the caller's array.
That is not differentiable, and it was already corrupting the finite-difference
gradient it was written for: at `x0 = 15.0` with `xlen = 15`, `f(x−h)` was
evaluated at 15.0 and `f(x+h)` at **7.5**, so the difference quotient divided a
step of 7.5 by 3e-05. That component of the gradient was meaningless. The
current `gauss_cost` neither clamps nor writes back.

**Bounds are enforced by reparameterisation, not by clamping.** Positions go
through a logistic (`x = len·sigmoid(u)`) and strictly positive quantities
through an exponential (`sigma = exp(u)`), so every point of the search space is
interior and the objective is differentiable everywhere. Clamping instead would
propagate an exactly zero derivative at the bound under AD, where L-BFGS can
stall — the failure mode that made the conversion order matter. The public
`vars` vector keeps its original constrained meaning; the transform is applied
only around the optimiser.

`Dual<G>` and `GradVec<N>` both live in [`math`](../../math/README.md): the dual
number and the vector it carries in its derivative slot. Each replaced a
third-party header — `autodiff` and `Eigen::Array<double, N, 1>` — that the
whole build had to find in order to compile one file. `Dual` is measured against
autodiff in the log entry for the removal, and `GradVec` against Eigen in
`benchmarks/bench_gradvec.cpp`, which still builds the Eigen column when Eigen
is present.

## Testing

`test/python/misc/test_image_localization.py` covers the fit end to end.

The AD machinery itself is checked separately by `test/cpp/test_ad_gradient.cpp`,
which differentiates the same objective four ways — vectorized dual, scalar
dual, a long-double dual, and central differences — and requires them to agree,
on top of unit checks of every `Dual` and `GradVec` operator. The layering is
the point: a wrong sign, an aliasing bug in `*=` or a missing term in the
product rule does not fail to compile and does not crash. The fit still
converges, to the wrong place.
