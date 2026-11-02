# G64 launch-only diagnostic

This isolated target retains the original pilot's numerical kernel. `shaders/Legacy.hlsl` is the unchanged control; `Packed.hlsl` changes only compile-time group size and per-tile thread guards. `DoubleMath.hlsli` is byte-identical to the pilot. No production application files are edited.

Each independent thread owns one system and all of its globally allocated scratch. There are no cooperative lanes, groupshared arrays, group barriers, floating atomics, or changed accumulation loops. The optional ownership-check PSOs add only integer stage counters in the previously unused status padding word, outside timing runs.

Build with CMake/VS2022 x64; use MSBuild `/m:1` in this environment. CLI:

```
G64LaunchCandidate.exe replay|formation|ownership|hybrid1|hybrid5|dynamics|benchmark INPUT OUTPUT [legacy|1|32|64] [TILE] [DYNAMICS_CORRECTIONS]
```

`replay` and `formation` retain the full 0/1/2/3/5 curve. `hybrid1` and `hybrid5` explicitly select their correction count. `dynamics` records the final argument as `correctionsPerLinearSolve`; default is one. No application-default setting is changed. `benchmark` runs the bounded staged matrix and writes its measured selection; it does not use the CLI launch selection to override that matrix. `ownership` runs all three group sizes.

The old layout remains 64-byte metadata, 88-byte local rows, 8-byte block metadata, 16-byte maps, packed binary64 arrays, and 32-byte status. Constants are four uint32 values: total systems, base system, formation flag, tile system count. Legacy ignores the fourth word. Each stage dispatches `ceil(tileCount/groupSize)` groups; `SV_DispatchThreadID.x` is guarded before adding the base. All tiles share disjoint per-system storage, and one work UAV barrier follows each complete stage. A solve is one command-list submission and one completion wait/readback, with no inter-stage CPU wait.

The optional offline launcher `experiment.py` builds with exit checks, stages native binaries/DXIL, hashes them before execution, and runs bounded tests from an executable-relative isolated directory. Python is not used by the executable/solver. `summary.py` evaluates stored outputs offline. See the root `G64_LAUNCH_OPTIMIZATION.md` for measured results and exact commands.

The host reference links the existing QD 2.3.24 dependency. Its original license/provenance remains in `third_party/qd-2.3.24`; notices are copied into the staged runtime. GPU G64 arithmetic remains binary64, without QD shader arithmetic. G32F64R remains the original documented negative result and is not compiled or investigated here.
