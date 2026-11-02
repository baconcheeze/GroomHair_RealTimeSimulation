# Orientation robustness patch validation

Executed on Unity 6000.4.9f1, Windows GPU compute, in the disposable Temp/HairRestValidation project. Original scene not saved or rebuilt. Historical Tools/HairRest/Validation/Orientation output is preserved.

## Files

Modified: Assets/HairRest/Runtime/GuideHairOrientationSolver.cs; Resources/StableCosseratOrientation.compute; Shaders/GuideQuaternion.hlsl; Editor/GuideHairOrientationValidation.cs; ORIENTATION_SOLVER.md.

Added under Assets/HairRest: Shaders/GuideOrientationLocal.hlsl; Editor/OrientationLocalValidation.compute; Editor/OrientationLocalReference.cs; Editor/OrientationRobustnessValidation.cs; Editor/OrientationRobustnessMathValidation.cs; Editor/OrientationRobustnessGpuValidation.cs, and their Unity metadata. GuideHairRuntime and physical resource ownership are unchanged.

## Pre-patch GPU reproduction

Unity x0=(0.000000000, 0.000000000, 0.000000000)
Unity x1=(0.000000000, 0.000000000, 0.010000000)
Unity x2=(0.000000000, 0.000000000, 0.020000000)
qRest0=(0.000000000, 0.000000000, 0.000000000, 1.000000000)
qRest1=(0.000000000, 0.000000000, 0.000000000, 1.000000000)
First local frozen neighbor: v=(0.000000000, 0.000000000, -0.020000000), b=(0.004000000, 0.000000000, 0.000000000, 0.000000000), a=(-0.000080000, 0.000000000, 0.000000000, 0.000000000)
initialLambda=0.024, refinements=8, finalLambda=0.02, gap=0, numeratorNorm=3.243331E-12, valid=0
Constraint energy: old=0.02, example minimum=0.0037999999546708637, legacy candidate=undefined
Actual original strand sweep: Orientation GPU safety failure strand=0, code=5 (1 topology, 2 quaternion, 3 length/position, 4 bend quaternion, 5 update).

## Method

Ordinary 32-step lambda refinement retained, with common coefficient scaling and scale-aware acceptance. Rare same-energy fallback uses two eigenspaces of A: safeguarded secular solve for delta > 0 or deterministic hard-boundary completion at delta=0. This is project-specific, not copied from the official solver. Float projector cancellation below four float eps times |b| is treated as numerically unresolved, with mandatory residual/energy/spectral checks. Delta is retained directly; no inertia, velocities or position update added.

## Independent CPU-only math/reference

PASS CPU-only double: 128 problems, derivative samples=432, max normalized FD error=6.731289220055244E-07, objective/constraint energy difference=1.8648277366750676E-17, symmetry/A^2 error=3.4694469519536142E-18, projector error=1.1015494072452725E-16, reference residual=1.3170532592470794E-15, spectral violation=0, hard energy=0.0038000000000000004, initial hard spectral violation=0.036000000000000004

Double dense symmetric Jacobi eigendecomposition + secular solve is independent of both shader fixed-point and projector algorithms. Directional differences test h=1e-3,1e-4,1e-5,1e-6 away from closest-pole ties.

## Isolated GPU validation

PASS GPU local cases=375; ordinary=332, fallback=43, hard=19, unresolved=0; max normalized objective error=2.25024389348743E-09; residual=1.3903338370240389E-06; spectral=9.0783926983325124E-10; norm=1.2174880725801529E-07; physical energy increase=0
Production isolated first local energy: 0.0037999999442400513; independent minimum=0.0038; initial diagnostics: E_ss=0.039999999105930328, E_bt=0, E_total=0.039999999105930328; MaxBatchUpdate=unavailable rad; MaxLastSweepUpdate=unavailable rad; stationarity=0 (normalized=0); spectral violation=0.035999998450279236 (normalized=1.4999999514936153); near pole ties=0; regular/fallback/hard/rejected/failed=0/0/0/0/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0; after sweep: E_ss=0.000201654342094246, E_bt=1.6576857018280004E-05, E_total=0.000218231199112526; MaxBatchUpdate=3.12340236 rad; MaxLastSweepUpdate=3.123402 rad; stationarity=0.00434373551979661 (normalized=0.18098898192995536); spectral violation=0 (normalized=0); near pole ties=0; regular/fallback/hard/rejected/failed=1/1/1/1/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0
128 batch: E_ss=0, E_bt=0, E_total=0; MaxBatchUpdate=3.14159274 rad; MaxLastSweepUpdate=0 rad; stationarity=0 (normalized=0); spectral violation=0 (normalized=0); near pole ties=0; regular/fallback/hard/rejected/failed=255/1/1/1/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0
Independent captured final sweep: E_ss=0, E_bt=0, E_total=0; MaxBatchUpdate=0 rad; MaxLastSweepUpdate=0 rad; stationarity=0 (normalized=0); spectral violation=0 (normalized=0); near pole ties=0; regular/fallback/hard/rejected/failed=2/0/0/0/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0
Nonconverged Run(2): E_ss=1.3785716588784313E-08, E_bt=1.1299767887389367E-09, E_total=1.491569337752325E-08; MaxBatchUpdate=3.1414423 rad; MaxLastSweepUpdate=0.198681191 rad; stationarity=3.6079527490073815E-05 (normalized=0.0015033136615205467); spectral violation=0 (normalized=0); near pole ties=0; regular/fallback/hard/rejected/failed=3/1/1/1/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0
Independent sweep 2 capture: E_ss=1.3785716588784313E-08, E_bt=1.1299767887389367E-09, E_total=1.491569337752325E-08; MaxBatchUpdate=0.198681191 rad; MaxLastSweepUpdate=0.198681191 rad; stationarity=3.6079527490073815E-05 (normalized=0.0015033136615205467); spectral violation=0 (normalized=0); near pole ties=0; regular/fallback/hard/rejected/failed=2/0/0/0/0; norm=0; root=0 rad; position=0 m; velocity=0 m/s; invalid=0
Expected isolated coefficient failure: Orientation GPU safety failure strand=0, code=6 (1 topology, 2 quaternion, 3 length/position, 4 bend quaternion, 5 update, 6 unresolved local solve). Run counters regular/fallback/hard/rejected/failed=1/0/0/0/1. Failure is latched until Reset.
PASS sign combinations energy difference=0; common-scale energy difference=2.4675689009462376E-11; pole-tie count; failure latch/reset.

Near cases include both signs of offsets 1e-2,1e-4,1e-6,1e-8 with off-axis components; common material scales 1e-6,1,1e6; 16 controlled representation-sign combinations; 300 deterministic SS-dominant/BT-dominant/mixed cases; b=0, alpha=0, both zero, and minus-only above the hard boundary. See local-cases.csv for per-case raw and normalized errors. No quaternion-angle equality is imposed for nonunique minima.

## Existing groom GPU regression

| Test | Sweeps | Initial energy | Final energy | Batch angle (rad) | Last sweep angle (rad) | Final normalized residual |
|---|---:|---:|---:|---:|---:|---:|
| Bend, 1 strand | 53 | 0.00064678798 | 1.5544561e-12 | 1.928979e-06 | 1.9341685e-06 | 2.3280336e-07 |
| Twist, 1 strand | 1030 | 0.00014651093 | 1.1715223e-11 | 1.9458728e-06 | 1.946071e-06 | 2.9488022e-07 |
| Smooth, 1 strand | 95 | 0.0014175188 | 3.6024403e-12 | 1.9982706e-06 | 1.9983118e-06 | 2.217342e-07 |
| Random, 1 strand | 809 | 0.00022864782 | 1.1423247e-11 | 1.9611605e-06 | 1.9732647e-06 | 2.6086659e-07 |
| Random, 10 strands | 128 | 0.0028291916 | 1.2158817e-08 | 0.08665764 | 3.6906877e-05 | 5.2059256e-06 |
| Random, 100 strands | 128 | 0.029185603 | 8.1075256e-08 | 0.09025438 | 4.782451e-05 | 5.2059256e-06 |
| Random, 2839 strands | 128 | 0.8344599 | 2.3592184e-06 | 0.09442556 | 6.541658e-05 | 7.6256935e-06 |

Total recorded groom update counters (rest + perturbations + scale): {'regular': 11384100, 'fallback': 0, 'hard': 0, 'rejected': 0, 'failed': 0}. No fallback used in ordinary small-perturbation tests.
Maximum quaternion normalization error across the groom trace: 1.78813934e-07. Fixed position difference, velocity, root orientation error and invalid quaternion count are zero. Final normalized spectral violation is zero in the converged single and all scale tests.
Rest no-op: one sweep batch angle 1.768054e-7 rad; ten further sweeps batch angle 1.09240048e-7 rad. Baseline tolerances retained. Scale tests establish energy reduction and invariant preservation, not that every strand reaches the single-strand last-update stopping tolerance after 128 sweeps.

Passed: immutable buffers; inactive orientations; zero/NaN/Inf quaternion and invalid vertex/segment/bend ranges; unpinned two-vertex SS-only case; deterministic random perturbation; 130+ idle frames; line/head pixel equality; guide/render visibility modes; original Jolt head/hull modes; camera orbit/pan/zoom clamps/focus/reset math.

The runtime inspector reports both angle names via diagnostics ToString. Missing baselines are unavailable/NaN. Diagnostics read back only when explicitly requested. Final-state stationarity excludes pins/inactive segments and flags near-pole ties; it is a 4D residual, not a claimed physical 3D torque.

## Appearance and asset invariance

- `Assets/HairRest/Scenes/HairRestPreview.unity` SHA256 `7452e01ab3c9ff7118559879562859bf68e06a78ba61eeb4c92e4f1dc7b4c2f6`
- `Assets/HairRest/FemaleHead/FemaleHairRest.bytes` SHA256 `8d1c6197fd3cb2664d84b0914d61d323115c32d40ffef3a08ecf97815a343bfa`
- `Tools/HairRest/Validation/Orientation/before.png` SHA256 `0c78ebbbcf0ff729187f8057dc47c37336eadf68d6bb796c7a25a3d9e35d9af0`
- `Tools\HairRest\Validation\OrientationRobustness\Regression\before.png` SHA256 `0c78ebbbcf0ff729187f8057dc47c37336eadf68d6bb796c7a25a3d9e35d9af0`
- `Tools\HairRest\Validation\OrientationRobustness\Regression\after-tests.png` SHA256 `0c78ebbbcf0ff729187f8057dc47c37336eadf68d6bb796c7a25a3d9e35d9af0`

Scene/groom hashes unchanged from the established baseline; pre/post images byte-identical to historical baseline. Head, camera, colors, visibility controls and defaults were not modified. No position solver, integration, collisions, grid or render-hair reconstruction added.

The logged error-6 overflow test is deliberate and isolated, followed by latch/reset assertions. No unresolved failures occurred for valid tested local problems or groom regressions. Existing Unity obsolete-API warnings remain; no shader/C# compilation errors were reported.
