# C++ tests for the shared numerics

`modules/math/include/Mat.h` and `QREigen.h` are header-only and sit under every
ported spectroscopy algorithm — MaxEnt, the Kalman burst search, the HMM
surrogate, Gopich–Szabo, BurstML. A defect here surfaces as a plausible-looking
but wrong result several layers up, which is exactly the kind of defect that
survives an end-to-end test. These check the kernels directly.

`GradVec.h` is here for the same reason and one more: it has to satisfy a
contract with a third-party template (`autodiff`'s `Dual`) that upstream does
not document, so nothing but a test stands between an autodiff bump and a
silently wrong gradient. See "the AD guard" below.

## Running

They need nothing but a compiler and an include path:

```bash
c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_mat_linalg.cpp \
    -o /tmp/test_mat_linalg && /tmp/test_mat_linalg
c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_qreigen.cpp \
    -o /tmp/test_qreigen && /tmp/test_qreigen
c++ -std=c++17 -O2 -I modules/math/include -I thirdparty \
    test/cpp/test_ad_gradient.cpp -o /tmp/test_ad_gradient && /tmp/test_ad_gradient
```

`test_ad_gradient` additionally needs `thirdparty/` for the autodiff headers.

Or through CMake:

```bash
cmake -S . -B build -DTTTRLIB_BUILD_CPP_TESTS=ON
cmake --build build --target test_mat_linalg test_qreigen test_ad_gradient
ctest --test-dir build -R 'test_mat_linalg|test_qreigen|test_ad_gradient'
```

Exit status is the number of failed checks.

Build them with `-Xpreprocessor -fopenmp` too when touching `QREigen.h`: the
per-eigenvector loop is parallel above n = 32, and a data race there would not
show up in a serial build.

## What they check, and why that way

Properties, not stored numbers:

- `A x = b` residual, and that the planted solution comes back
- `A^T (A x - b) = 0` — the definition of a least-squares solution
- adding a null-space direction lengthens the returned vector — the definition
  of *minimum-norm*
- `A v = lambda v` and `A = V diag(lambda) V^-1`
- `sum(lambda) = trace(A)`
- rank deficiency is *detected*, on matrices whose singularity is only visible
  relative to their scale

A stored-number test would have passed against the broken eigensolver, because
its eigenvalues were correct and only its eigenvectors were noise. The residual
is what told them apart.

The cases are chosen to break specific things: a rank-1 outer product with
entries ~1e8 (an absolute singularity floor calls it regular), a system scaled
by 1e-15 (an absolute `rcond` zeroes it), a cyclic permutation (a QR iteration
with no exceptional shift never deflates it), and a matrix scaled by
`10^(3(i-j))` (eigenvectors read off the unbalanced matrix are unusable).

## The AD guard (`test_ad_gradient`)

`imaging/localization` differentiates its 2D-Gaussian objective by seeding
`autodiff::detail::Dual<double, GradVec<N>>` with the N basis vectors and
reading all N partials out of one forward pass. Carrying a *vector* in the
derivative slot is not a documented autodiff feature — it works because
`NumberTraits` can be specialized to say what the underlying scalar is — and
`GradVec` implements exactly the operators `Dual` happens to call. Both halves
of that fail quietly: an autodiff upgrade, or a sign or aliasing bug in one
operator, still compiles, still converges, and lands somewhere else.

So the check is differential. The same objective is differentiated three ways —
vectorized dual, autodiff's own scalar `dual` seeded N times, and central
differences — and they must agree. Scalar `dual` is the reference because it is
autodiff's tested path; central differences are the independent one, at a
tolerance loose enough not to be a precision test.

Two findings from writing it, both worth not rediscovering:

- **Exact agreement with the scalar reference is achievable but not free.** The
  first `GradVec` divided by multiplying with a reciprocal, and computed
  `s * g` into a temporary before accumulating it. Each cost a last-place
  difference against the scalar path. Dividing properly and fusing
  `grad += s * g` into one loop made the two paths bitwise identical — the
  fused form contracts to the same FMA the scalar path does.
- **The tolerance is still not zero, on purpose.** Whether the compiler
  contracts a multiply-add depends on the shape it inlines into, so
  `-ffp-contract=off` and clang's default disagree by 1 ulp on one component.
  Asserting bitwise equality would make a legal optimisation a build failure.

## The likelihood floor (`test_decay_likelihood`)

`Wcm` and `wcm_p2s` are what `DecayFit23/24/25/26` minimise. They used to answer
a model bin at or below `1e-12` by skipping it — a line commented "this is only
for stability reasons", which it was not. The term a near-zero bin contributes
to the minimised objective is `-C·log(m)`, large and positive, so dropping it is
a discontinuous *improvement*: the optimiser is paid 828.9 units to push the bin
under the floor, and below it the objective is flat, so nothing brings it back.
They are now continued by the tangent to `log` at the floor.

The test has two jobs and the first matters more:

1. **Prove the change is a no-op above the floor**, bitwise, over ten
   magnitudes. These functions are pinned by cross-language reference tests; a
   one-ulp drift in a fix that is supposed to change nothing is a regression
   wearing a fix's clothes. This is also why `Wcm` keeps its multiply in the
   loop body rather than inside the helper — moving it stops the compiler
   contracting it into the accumulate, and every ordinary evaluation shifts.
2. **Prove the pathology is gone**: continuous, finite below the floor including
   at negative model values, and strictly worse the further down it goes.

One thing the test gets right that an obvious version gets wrong: continuity is
*not* "the gap either side of the floor is small". The slope of `log` at `1e-12`
is `1e12`, so any finite probe shows a finite gap — 6e-05 for a step of 1e-18,
which is correct C1 behaviour. What separates a steep join from a cliff is that
the gap **halves when the probe step halves**. The original's gap was 828.9
regardless of step. That is the assertion.
