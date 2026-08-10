# `imaging/localization` — Single-Molecule Localization Microscopy

Algorithms for point-spread function (PSF) fitting and single-molecule
localization from photon images.

## Contents

- **`ImageLocalization.h` / `ImageLocalization.cpp`**: 2D Gaussian PSF fitting
  and centroid localization. `fit2DGaussian` fits one, two or three Gaussians
  sharing a width and an ellipticity, against a Poisson maximum-likelihood cost.

## Dependencies

- `core`, `clsm`, `math`
- `autodiff` (vendored, header-only) — forward-mode automatic differentiation

## The gradient, and why the objective looks the way it does

The fit is driven by `i_lbfgs.h`, which by default estimates its gradient with
central differences: 2N objective evaluations per gradient. This module instead
supplies an **exact gradient from one forward-mode AD pass**, seeding
`autodiff::detail::Dual<double, GradVec<N>>` with the N basis vectors so all N
partial derivatives come out of a single evaluation. Measured against a *tuned*
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

`GradVec<N>` (in [`math`](../../math/README.md)) is the derivative carrier. It
replaced `Eigen::Array<double, N, 1>`, which was the last thing in tttrlib that
needed Eigen and made it a hard `REQUIRED` of the whole build for one struct
member. The two are measured head to head in `benchmarks/bench_gradvec.cpp`.

## Testing

`test/python/misc/test_image_localization.py` covers the fit end to end.

The AD machinery itself is guarded separately by `test/cpp/test_ad_gradient.cpp`,
which differentiates the same objective three ways — vectorized dual, autodiff's
own scalar `dual`, and central differences — and requires them to agree. That
test exists because the `NumberTraits` specialization the vectorized path relies
on is **undocumented upstream**: an autodiff bump that changed what `Dual` calls
on its `grad` member would not fail to compile, it would silently produce wrong
derivatives, and the fit would converge to the wrong place.
