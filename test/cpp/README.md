# C++ tests for the shared numerics

`modules/math/include/Mat.h` and `QREigen.h` are header-only and sit under every
ported spectroscopy algorithm — MaxEnt, the Kalman burst search, the HMM
surrogate, Gopich–Szabo, BurstML. A defect here surfaces as a plausible-looking
but wrong result several layers up, which is exactly the kind of defect that
survives an end-to-end test. These check the kernels directly.

`Dual.h` and `GradVec.h` are here for the same reason and one more: a wrong
derivative does not crash and does not fail to compile — the fit converges, to
the wrong place. Nothing but a test notices. See "the AD checks" below.

## Running

They need nothing but a compiler and an include path:

```bash
c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_mat_linalg.cpp \
    -o /tmp/test_mat_linalg && /tmp/test_mat_linalg
c++ -std=c++17 -O2 -I modules/math/include test/cpp/test_qreigen.cpp \
    -o /tmp/test_qreigen && /tmp/test_qreigen
c++ -std=c++17 -O2 -I modules/math/include \
    test/cpp/test_ad_gradient.cpp -o /tmp/test_ad_gradient && /tmp/test_ad_gradient
c++ -std=c++17 -O2 -I modules/math/include \
    test/cpp/test_mlp_core.cpp -o /tmp/test_mlp_core && /tmp/test_mlp_core
```

Or through CMake:

```bash
cmake -S . -B build -DTTTRLIB_BUILD_CPP_TESTS=ON
cmake --build build --target test_mat_linalg test_qreigen test_ad_gradient test_mlp_core
ctest --test-dir build -R 'test_mat_linalg|test_qreigen|test_ad_gradient|test_mlp_core'
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

## The AD checks (`test_ad_gradient`)

`imaging/localization` differentiates its 2D-Gaussian objective by seeding
`tttrlib::Dual<GradVec<N>>` with the N basis vectors and reading all N partials
out of one forward pass. `Dual.h` is the dual number and `GradVec.h` implements
exactly the operators it calls; both fail quietly — a sign, an aliasing bug, a
missing term in the product rule still compiles, still converges, and lands
somewhere else.

This used to be a guard on someone else's contract: the derivative slot held a
vector, which `autodiff` does not document as possible, so an upstream bump
could silently change the answer. `Dual.h` replaced autodiff and the test
changed with it — it now checks an implementation. First every operator of
`Dual` and `GradVec` against a derivative written by hand, then the objective
differentiated four ways — vectorized dual, scalar dual, a long-double dual, and
central differences — which must agree. The scalar dual shares `Dual`'s
formulas, so it isolates the carrier; the long-double dual shares them at higher
precision, so it says how much of the disagreement is rounding; central
differences share nothing, so they are what catches a wrong formula.

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

## The differentiable MLP core (`test_mlp_core`)

`MlpCore.h` is the forward and reverse pass of the dense network behind
`NeuralNet`, plus the same two augmented with a directional Taylor expansion of
the input to second order — so a loss on `dy/dx` and `d²y/dx²` (a physics
residual) can still be differentiated with respect to the weights by
backpropagation. It is header-only and std-only because imp.bff carries a
verbatim copy. The failure mode is the same as `Dual`'s: a wrong `f''` or a
dropped term in the Taylor adjoint trains fine, to the wrong minimum.

Every derivative is therefore checked two independent ways. The activation
derivatives `f'`, `f''`, `f'''` against central differences of `f`. The input
derivatives against the forward-mode `Dual` pass through the dot-product
identity `<w, J v> == <J^T w, v>` — forward mode and reverse mode share no code
beyond the activation value, so a transposition or an off-by-one layer in the
reverse sweep cannot cancel. The Taylor companions of orders 1 and 2 against
first and second central differences along the same direction, and a
`Dual<GradVec<3>>` pass against three order-1 passes for the full Jacobian.
Finally the adjoint of the augmented pass — `dL/dparams`, `dL/dx`, `dL/dv` for a
loss that uses all three outputs — against central differences of that loss,
parameter by parameter, for every smooth activation and for ReLU. The
`PortableGemm` policy is checked against naive triple loops; the Mat.h policy
the library itself uses is validated by the Python suite (`test_neural_net.py`)
through the same entry points.

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

## The A/B against numpy, scipy and the canonical generators (`ab_numerics_harness`)

`ab_numerics_harness.cpp` is not a test on its own. It is the C++ half of
`test/python/misc/test_math_ab_numerics.py`, which compiles it (skipping when no
compiler is on the PATH), feeds each subcommand its inputs on stdin and compares
the printed result against the *real* reference library on the Python side —
`numpy.linalg`, `scipy.optimize`, scikit-learn, and from-the-paper Python
implementations of Philox4x32-10, pcg32 and SplitMix64 — rather than against a
recorded number. Kernels covered: `NelderMead.h`, `i_lbfgs.h` (including the
soft-bound semantics and the `fgrad1/2/4` stencils), `Mat.h` (`mat_solve`,
`mat_lstsq_minnorm`, `mat_inverse_inplace`, `mat_power`, GEMM NN/NT/TN,
reductions), `QREigen.h` (`qr_eigendecompose`, `zmatmul`, `zmatvec`, `zinv`),
`Random.h` (Philox stream, `seek`, `deterministic` per engine, `normal`),
`SimPcgRandom.h`, and `Sampling.h`. Two known gaps are pinned there as
`expectedFailure` so a fix flips a test: the `pcg` engine of
`Random::deterministic` does not implement PCG's XSH-RR output function
(19 live bits before the rotation, not 32 — visible as biased output bits), and
`mt19937` is not implemented (falls through to Philox, as the header says).

```bash
c++ -std=c++17 -O2 -I modules/math/include -I modules/util/include \
    test/cpp/ab_numerics_harness.cpp -o /tmp/ab_numerics_harness
python -m pytest test/python/misc/test_math_ab_numerics.py -q
```

## The simulator's samplers against their canonical references (`ab_simulation_harness`)

`ab_simulation_harness.cpp` is the same idea for `modules/simulation`: it is the
C++ half of `test/python/simulation/test_ab_simulation_reference.py`, which
compiles it together with `modules/simulation/src/SimRandom.cpp` and compares
each subcommand's output with a from-the-paper Python implementation or with
scipy — `SimXoshiroRandom` against Blackman & Vigna's xoshiro256++ (bit-exact,
seed hash included), `SimCounterRandom` against Philox4x32-10, `SimRandom`'s
`init_by_array` against the mt19937ar.c test vector, `sim_randn` against
Marsaglia & Tsang's `zigset`/`RNOR` on the same MT stream (tables and outputs
bit-exact), the boundary-flux samplers of `SimInjection.h` (`qnorm` vs
`scipy.stats.norm.sf`, `random_erfc` / `random_entry_depth` KS against their
integrated densities), and `SimDecay::sample_ns` (chi-square against the pdf).

```bash
c++ -std=c++17 -O2 -I modules/simulation/include -I modules/math/include \
    -I modules/util/include modules/simulation/src/SimRandom.cpp \
    test/cpp/ab_simulation_harness.cpp -o /tmp/ab_simulation_harness
python -m pytest test/python/simulation/test_ab_simulation_reference.py -q
```
