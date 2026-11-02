# Fixed-position Stable Cosserat orientation test

Only GuideOrientation is mutable during a solve. Position is bound as a
StructuredBuffer (never RW), velocity is not bound, and rest resources are read
only. There is no position prediction, position solve, velocity update, inertia,
angular velocity, contact, grid, force reconstruction or scalp animation.
Orientation is quasi-static: it minimizes elastic energy with unit quaternion
constraints, rather than integrating angular momentum. Torque still matters.

## References and exact implemented equations

Paper: [Stable Cosserat Rods, SIGGRAPH 2025](https://graphics.cs.utah.edu/research/projects/stable-cosserat-rods/Stable_Cosserat_Rods-SIGGRAPH25.pdf).

Inspected official source revisions:

- CPU `11081ed22ceb3f8824f447b64557745895676d0a`,
  [lambda.cpp](https://github.com/jerry060599/StableCosseratRods/blob/11081ed22ceb3f8824f447b64557745895676d0a/KittenEngine/Cosserat/sim/lambda.cpp),
  `Sim::iterateSegLambda` and `Sim::iterateSegLambdaExact`: v, neighbor b, pole,
  lambda and normalized quaternion update.
- CPU `KittenEngine/KittenEngine/includes/modules/Rotor.h`: xyzw Hamilton
  quaternion convention; `Rotor(1)` is pure +X, NOT quaternion identity.
- GPU `b178c2b15cdb05c2b572f9ef9d326262da533df9`,
  [cosserat.cu](https://github.com/jerry060599/YarnBall/blob/b178c2b15cdb05c2b572f9ef9d326262da533df9/KittenEngine/YarnBall/sim/cosserat.cu),
  `quaternionLambdaItr`: adjacent quaternion targets and normalized Eq. (18).

The implementation follows the paper and MIT CPU formulation. CPU license is
retained in `Tools/HairRest/References/StableCosserat-CPU-LICENSE.txt`. YarnBall
was inspected for comparison; its CUDA kernel/scheduling/material scaling are
not copied. The CPU double-precision validation transcription independently
checks the GPU's first full Gauss-Seidel sweep.

All HLSL quaternions are **xyzw**, Hamilton multiplication, conjugate=(-xyz,w).
Local tangent is **+Z**, not the official code's +X. In the expressions below,
vectors v and e3 are embedded as pure quaternions `(vx,vy,vz,0)` and `(0,0,1,0)`.

For each segment i, and bend b connecting i to i+1:

```
L_i = GuideRestLength[i]
kss_i = baseSS * L_i
kbt_b = 4 * baseBT / L_i
C_ss_i = (x[i+1]-x[i])/L_i - rotate(q_i,e3)
qRel_b = conjugate(q_i) * q_(i+1)
phi_b = dot(qRel_b,q0_b) >= 0 ? +1 : -1
C_bt_b = qRel_b - phi_b*q0_b
E_ss = sum_i 0.5*kss_i*dot(C_ss_i,C_ss_i)
E_bt = sum_b 0.5*kbt_b*dot(C_bt_b,C_bt_b)
```

These are paper Eqs. (1), (3)-(5); full-quaternion rest bend storage is unchanged.
Closest-pole selection happens locally at every update (ties select +1). There
is no phi buffer. Rest sign continuity does not replace runtime pole selection.

The local orientation step is Eqs. (15), (16), (18), (22), (25):

```
v = -2*kss_i/L_i * (x[i+1]-x[i])
b = kbt_left*phi_left*q_left*q0_left
  + kbt_right*phi_right*q_right*conjugate(q0_right)
a = v*b*e3
lambda = |v| + |b|
repeat up to 32 times:
    next = sqrt(|a + lambda*b| + |v|^2)
    if |next-lambda| <= 1e-7*max(lambda,1e-20): lambda=next; break
    lambda=next
q_new = normalize(a + lambda*b)
if dot(q_new,q_old)<0: q_new=-q_new
```

Missing left/right terms are omitted. On the ordinary branch **lambda > |v|**,
Eq. (18)'s denominator `lambda^2-|v|^2` is positive and cancels on normalization.
At the hard boundary **lambda = |v|**, both denominator and numerator can vanish;
normalizing the numerator is not a valid solution there. See the robustness
extension below. Refinement is the paper's scalar fixed-point map,
not quaternion gradient descent, quaternion averaging, XPBD or PBD.

**Important qualification:** the paper's default closed-form variant uses the
approximate lambda bound (22); exact local minimization requires solving its
unit-norm scalar condition. We use its Sec. 3.4 refinement, to floating-point
tolerance/cap, inside each local update. The official exact CPU path instead
caches gamma and performs one refinement per outer iteration. Fixed positions
allow us to refine locally without a persistent lambda/gamma scratch buffer.
This is not a claim of symbolic exactness for an arbitrary large perturbation.
Measured energy is never clamped; significant increases fail validation.

For b=0 (e.g. an unpinned two-vertex strand), only shear/stretch remains. A minimal
rotation aligns the old material tangent to the fixed edge, preserving free
twist. The opposite-tangent ambiguity uses the old material d1 axis. No invalid
quaternion is silently replaced by identity.

## Execution and ownership

`GuideHairRuntime` still owns all physical buffers. `GuideHairOrientationSolver`
borrows them and owns a shader instance, one uint error code and 24-byte diagnostic record per strand, and an
optional debug-axis material. There are no Jacobi copies, coloring or atomics.

One GPU thread owns one entire strand, and sweeps segments serially root to tip
for each requested iteration. The left neighbor has already been updated in
the current sweep; the right neighbor has not. Threads never cross strand ranges.
Workgroups contain 64 threads: 1/10 strands => 1 group, 100 => 2 groups,
2,839 => 45 groups (2,880 dispatched lanes with bounds guards). N requests are
split into chunks of at most 64 sweeps per dispatch for watchdog friendliness;
each chunk preserves the same Gauss-Seidel order and state.

Default selection is one strand, Pin Root Orientation=true, Auto Solve=false.
Pinned segment zero is skipped entirely. Reset/perturb restores its exact rest
value first. Changing participation or pinning in the inspector resets all
orientations, so an inactive strand cannot retain a previous perturbation.
Initialization still writes rest orientations; entering Play alone never solves.

## Temporary stiffness and controls

TEMPORARY baseSS=1 N and baseBT=1e-5 N*m^2, with Eq. (5) scaling above. Their
discrete energy coefficients have energy units. These are bring-up values, not
calibrated hair parameters and not compliance. Physical mass, density and radius
are untouched and are not used in this orientation-only update.

Select the hair object in the existing inspector. Available controls:

- Reset Orientations To Rest.
- Apply Bend / Twist / Smooth / Random Perturbation. Every test starts from rest;
  bend/twist are 20 degrees at the middle segment about material X/Z, smooth is
  a triangular four-segment half-width bend, random defaults to <=5 degrees with
  seed 12345. Only participating non-root segments are perturbed when pinned.
- Run One Orientation Iteration / Run N Orientation Iterations.
- Auto Solve Orientations (off by default; GPU-resident, only in Play mode).
- Per Iteration Diagnostics (explicit step buttons, one strand only).
- Read Orientation Diagnostics (explicit readback, including GPU error status).
- Draw Guide Material Frames: RGB d1/d2/d3 axes at segment midpoints. Default
  display limit one strand, every fourth segment, axis length 4 mm. No readback.

The existing line colors/positions, head, hull debug and camera remain untouched.
Frame drawing is off by default; it is necessary to see orientation-only changes.

## Diagnostics and validation

CPU diagnostics explicitly read GPU orientations/positions/velocities and error
codes. No readback occurs on normal Auto Solve frames. E_ss/E_bt/E_total use the
exact constraint definitions above with double accumulation. `MaxBatchUpdate` is the
sign-invariant angle from the supplied pre-batch snapshot to the current state;
`MaxLastSweepUpdate` measures only the final executed sweep of the last Run(N).
Without a baseline the respective metric is unavailable (NaN in CSV), not zero. Norm error,
pinned-root angle error, fixed-position error, velocity magnitude and invalid
quaternion count are reported. Nonparticipating quaternions must exactly equal
their rest values. Existing validation still checks every immutable resource.

GPU checks range arithmetic before index access, finite positions/lengths,
nonzero finite current/rest quaternions, bend targets and update results. Error
codes: 1 topology, 2 orientation, 3 position/length, 4 bend target, 5 legacy update,
6 unresolved/rejected local solve. Errors stay latched until explicit Reset.
An explicit diagnostics request fails with strand and code; it does not hide
bad values by normalizing zero or NaN. CPU settings reject nonpositive or
nonfinite stiffnesses.

`GuideHairOrientationValidation.ValidateBatch` first verifies 130 idle frames
and unchanged pixels. Then it runs rest 1 + 10 sweeps, followed by single-strand
bend, twist, smooth and deterministic random cases. Every iteration is recorded;
1/2/4/8/16/32 checkpoints and final values are summarized. Debug stopping is
MaxLastSweepUpdate < 2e-6 rad and energy < 0.001 of perturbed energy, with a 2,048-iteration
limit; acceptance requires MaxLastSweepUpdate <1e-5 and energy <0.01 of initial.
The patch additionally requires final-snapshot normalized stationarity and spectral
violation <1e-5 for single-strand convergence. No adaptive
production solve is implemented. It then runs random cases on 10, 100 and all
strands for 128 sweeps. An energy rise >max(1e-12,previousEnergy*1e-4) is a failure;
smaller positive changes are recorded and reported as roundoff, not suppressed.

Historical outputs remain in `Tools/HairRest/Validation/Orientation/`.
Patch outputs go to `Tools/HairRest/Validation/OrientationRobustness/`, including
`Regression/iterations.csv`, summary, Unity log and unchanged-scene/axis images.

## Degenerate-case robustness extension (project-specific)

`Shaders/GuideOrientationLocal.hlsl` factors only the local math. Strand topology,
frozen-pole neighbor collection, serial GS ordering and 64-thread groups remain
in `StableCosseratOrientation.compute`. This fallback is a project-specific
numerical solution of the SAME objective, not an official reference-code path.

For frozen runtime poles and pure v/e3 quaternions, define

```
A(q) = v*q*e3
E_branch(q) = -0.5*dot(q,A(q)) - dot(b,q) + constant
A(q)+b = lambda*q,  |q|=1
A^T=A, A^2=alpha^2*I, alpha=|v|
```

This follows the existing SS/BT constraints, not an alternative strain model.
The editor-only double reference verifies objective differences and directional
derivatives against the original constraint energies independently.

Before local arithmetic, both v and b are divided by their largest absolute
component `common`. The minimizer is unchanged; lambda, raw residual and spectral
violation in these scaled coordinates must be multiplied by common to recover
original units. Diagnostics instead reconstruct unscaled coefficients from the
final snapshot. Zero common means a constant objective and legitimately keeps q.

Ordinary inputs retain the 32-step Eq. (25) iteration with relative stopping
threshold 1e-7. A candidate is rejected before normalization if nonfinite,
`|a+lambda*b| <= 1e-5*(|a|+|lambda|*|b|)`, or
`lambda-alpha <= 1e-5*max(alpha,|b|)`. After normalization it must pass the
stationarity, spectral and energy checks below. `QuatValid` alone is not a
meaningful test of an unnormalized stiffness-dependent numerator.

Fallback uses the two orthogonal eigenspaces (no general GPU eigensolver):

```
P_plus(z)  = (z+A(z)/alpha)/2
P_minus(z) = (z-A(z)/alpha)/2
bp=P_plus(b); bm=P_minus(b)
delta=lambda-alpha > 0
|bp|^2/delta^2 + |bm|^2/(delta+2*alpha)^2 = 1
q=bp/delta + bm/(delta+2*alpha)
```

Retain delta directly; never recover it by subtracting two nearly equal numbers.
The bracket is `[|bp|,|b|]`, with monotone safeguarded bisection. When hi>4*lo,
use a geometric midpoint to handle extreme ratios. Limit 96 iterations; stop at
`abs(normSquared-1)<=2e-7` or relative bracket width <=2e-7. Endpoint/roundoff
results still undergo independent acceptance checks. For bp=0 and |bm|>2alpha,
the nonsingular solution uses delta=|bm|-2alpha.

At the hard boundary bp=0, |bm|<=2alpha:

```
qm=bm/(2*alpha)
q=qm+sqrt(1-|qm|^2)*n_plus
```

Choose n_plus from the normalized plus projection of q_old. If its squared
length <=1e-12, choose the first longest projected xyzw standard basis vector.
This deterministic symmetry choice is not a unique physical rotation reference.
Float projector cancellation is considered numerically zero only when
`|bp| <= 4*float_epsilon*|b|` (4.7683716e-7 relative, never an absolute stiffness
threshold). This includes unresolved roundoff-scale near-hard cases; their
discarded plus contribution must still pass residual/energy checks. Resolvable
near-hard cases use the regular secular solution. For alpha=0, normalize b if
nonzero; if both vanish keep q_old. Actual geometry still rejects zero rest length.
The existing b=0 SS-only minimal rotation is tried first and independently checked.

All candidates require finite unit q (squared norm error <=2e-6), normalized
stationarity <=4e-6, normalized spectral violation <=4e-6, and branch energy
increase <=2e-6*(alpha+|b|). The production kernel also re-evaluates the original
closest-pole SS/BT constraint energy, scaling coefficients by max(kss,kl,kr):
allowed normalized increase <=6e-6. This check uses each neighbor target with its
closest sign, not stale frozen poles. Hemisphere alignment is performed AFTER
branch certification; a sign flip cannot be checked against unchanged branch b.
Unacceptable results preserve the last valid orientation and latch error 6;
they never count as convergence. Prior updates within a sweep are not rolled back.

### Final-snapshot stationarity and minimum branch

Explicit readback diagnostics rebuild v and b with current closest poles from the
FINAL snapshot (including neighbors updated later in the sweep). For each free,
participating segment:

```
s=A(q)+b
lambdaHat=dot(q,s)
residual=s-lambdaHat*q
spectralViolation=max(0,alpha-lambdaHat)
normalized residual=|residual|/(alpha+|b|)
normalized spectral violation=spectralViolation/(alpha+|b|)
```

Report zero for genuinely zero coefficients. Pinned roots and inactive strands
are excluded: a pin can support a reaction. This residual is a 4D constrained
quaternion gradient, NOT a claimed 3D torque. A zero residual is not enough:
the exact 180-degree starting fixture is stationary but violates the minimum
branch certificate lambdaHat>=alpha. The normalized spectral test is necessary
alongside energy and residual. Completed strands need final-state checks even
if each individual local update passed its own solve tolerance.

Near-pole ties are counted per free-segment incident term when
`abs(dot(qRel,q0))<=1e-6`. The pole-minimized energy is nonsmooth there; a selected
branch residual is not a unique smooth gradient. No smooth convergence claim is
made from that quantity alone at a tie.

### Measurements, counters and independent reference

GPU stores a maximum sign-invariant normalized-quaternion chord in each strand's
final sweep, resetting it at every sweep. Explicit diagnostics convert the chord
to radians with `4*asin(chord/2)`; no extra sweep or full orientation copy is needed.
Batch angle uses an explicit CPU pre-batch snapshot. Normal Run(N) has no GetData.
Per-strand counters record ordinary updates, fallback attempts, hard cases,
rejected ordinary candidates and unresolved failures. Counters reset at the first
dispatch of Run(N), accumulate across all 64-sweep chunks, and errors remain latched
across later chunks/calls until Reset. Hard counts are a subset of fallback counts.

`Editor/OrientationLocalReference.cs` is a separate dense symmetric 4x4 double
Jacobi eigendecomposition with a 200-step safeguarded secular solve and a singular
top-eigenspace branch. It is validation-only, never a per-frame CPU fallback.
Reference comparisons use identical frozen b, objective values, norm, residual and
spectral certificate, not quaternion angles at nonunique minima. Existing ordinary
CPU lambda.cpp transcription remains as an additional first-sweep comparison.
Finite differences of normalized tangent perturbations use h=1e-3,1e-4,1e-5,1e-6
away from pole ties and compare actual energy slopes to -dot(residual,d).

`OrientationRobustnessValidation.CaptureLegacyBatch` preserves pre-patch GPU
arithmetic in an editor-only kernel. `ValidateMathBatch` tests double algebra;
`ValidateLocalBatch` runs GPU isolated local and production-kernel fixtures.
The negative-source-Z fixture imports exactly (0,0,0), (0,0,.01), (0,0,.02).
The production scene's groom, immutable buffers and appearance are never altered
to construct these tests.

Patch results (Unity 6000.4.9f1) and complete file inventory are recorded in
[`OrientationRobustness/REPORT.md`](../../Tools/HairRest/Validation/OrientationRobustness/REPORT.md).
The legacy GPU fixture reached lambda=0.02 after 8 refinements with numerator
norm 3.243331e-12 and rejected the update. The patched production first local
update lowers energy 0.02 -> 0.00379999994424 (reference 0.0038).
375 isolated GPU local cases passed: 332 ordinary, 43 fallback (19 hard), no
unresolved valid cases; maximum normalized objective error 2.25024e-9,
stationarity 1.39033e-6, spectral violation 9.07839e-10, norm error 1.21749e-7.
CPU-only finite-difference normalized error was 6.73129e-7 across 432 samples.
Groom regressions recorded 11,384,100 ordinary updates and zero fallback/failures.
For all 2,839 strands after 128 sweeps, batch angle=0.09442556 rad while last-sweep
angle=6.541658e-5 rad; final normalized residual=7.62569e-6 and spectral violation=0.
These scale tests do not claim every strand has reached the single-strand
last-update stopping threshold. Default scene images match the historical baseline
byte-for-byte; rest data/position/velocity/root/inactive and head/camera tests pass.

## Recorded bring-up results

The following table is the historical baseline, not rerun patch results. Its
128-sweep update values were whole-batch measurements and must not be relabeled
as final-sweep updates. See the separate patch directory for current results.

Unity 6000.4.9f1: rest no-op angle change is 2.672e-7 rad after one sweep and
2.730e-7 after ten additional sweeps. Rest energy is approximately 4e-15 J.
Across the recorded trace, one positive energy increment occurs in this rest
noise floor (5.97e-16 J); perturbation traces do not show a significant increase.

| Test | Initial energy | Final energy | Sweeps |
| --- | ---: | ---: | ---: |
| Bend, 1 strand | 6.46788e-4 | 1.54728e-12 | 53 |
| Twist, 1 strand | 1.46511e-4 | 1.16411e-11 | 1031 |
| Smooth, 1 strand | 1.41752e-3 | 3.54143e-12 | 96 |
| Random, 1 strand | 2.28648e-4 | 1.11619e-11 | 813 |
| Random, 10 strands | 2.82919e-3 | 1.21583e-8 | 128 |
| Random, 100 strands | 2.91856e-2 | 8.10754e-8 | 128 |
| Random, 2839 strands | 8.34460e-1 | 2.35920e-6 | 128 |

Single-strand final updates are below 2e-6 rad. Scale tests measure the change
across their entire 128-sweep block; they establish safe energy decrease, not
the single-strand stopping criterion for every strand. Maximum quaternion norm
error is 1.78814e-7. Pinned-root error, position difference, velocity and invalid
quaternion counts are zero. Maximum first-sweep CPU/GPU angle discrepancy is
2.361e-7 rad. Invalid quaternion/topology cases are detected in isolated fixtures,
including zero/NaN/Inf, range overflow, and a two-vertex unpinned SS-only solve.
The default scene image is also identical to the pre-implementation capture.
