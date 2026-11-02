# Fixed-step guide dynamics

## 실행과 범위

일반 실행은 Static Preview이며 자동 simulation은 시작하지 않는다. 새
`Guide Dynamics` 모드도 Running=false, h=1/120 s, 8 sweeps/substep, 참여 1가닥,
position/orientation root pin 모두 on이 기본값이다. 기존 fixed-target mode의
RunSweeps(N)은 여전히 **한 고정 문제를 N회 완화**한다.

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
& .\build\vs2022\bin\Debug\HairLabDX12.exe --solver-mode dynamics --draw guides
```

패널에서 Dynamic participating을 All로 선택한 뒤 **Reset Simulation**을 누른다.
Draw Guide Hair on / Draw Render Hair off로 표시하고 Step One / Step N 또는
Run Dynamics를 사용한다. h, gravity, sweep budget, pin, participation을 바꾸면
정지하고 Reset이 필요하다. velocity control은 m/s 단위의 smooth amplitude를
**한 번만** 더한다. Render hair와 head/hull은 움직이지 않는다.

**Body/self collision is not enabled.** 머리/몸 관통을 투영이나 solver 변경으로
보정하지 않는다. gravity, integration 외의 외력/감쇠/velocity clamp/length projection,
animated roots, rotational inertia/angular velocity, render reconstruction는 없다.

## 정확한 한 substep

```
xOld = x_n
y = x_n + h*v_n + h*h*g
x = y                         // free vertices
q = q_n                       // warm start; rest/tangent 재생성 안 함
F_n = sum .5*mass/h²*|x-y|² + E_ss(x,q) + E_bt(q)
WindowSolve(SweepsPerSubstep)   // y/h fixed during this solve
v_(n+1) = (x_(n+1)-xOld)/h
```

gravity coefficient는 h²이고 .5*h²가 아니다. finalization에서 gravity를 다시
더하지 않으며 목적함수에 gravity potential도 더하지 않는다. 고정 root는 rest
position/orientation을 경계값으로 쓰고, root y도 같은 위치, velocity는 0이다.
vertex 1은 pin하지 않는다. physical root mass도 그대로 보존한다.

solver position은 고정 frame-0 neck-local 좌표다. 렌더러에 별도 model matrix가 없다는
것을 물리 world 기준도 identity라는 뜻으로 해석했던 이전 구현은 잘못이었다.
staged `assets/head/JoltHeadRest.bytes`의 고정 frame-0 neck pose에서 native LH의
hair-local-to-world 회전 R을 얻고, row-vector 규약으로 `g_local = g_world * transpose(R)`을 쓴다.
R의 직교성/단위 길이/양의 determinant를 확인하며 translation은 제거한다.
nominal world (0,-9.81,0)은 이 에셋에서 약 (-9.699665,0.065587,-1.465712)가 된다.
UI/CLI 입력은 world m/s²이고 solver vector도 따로 표시/기록한다. OrbitCamera의
yaw/pitch/view 방향은 이 계산에 쓰지 않으며 Z reflection을 다시 하지 않는다.
world→local→world 왕복, translation 무시, camera 변경 독립성을 CPU/GPU 실행으로 확인한다.
단위 meters/seconds, +Y up, LH, Hamilton xyzw 규약은 유지한다.

## 구현과 소유권

- `src/Dynamics.*`: GPU substep wrapper, status 관측, explicit diagnostics,
  optional GPU timestamp, 순수 CPU fixed clock.
- `src/DynamicsSession.*`: UI, pause/resume, clock baseline, 모드 전환.
- `src/DynamicsTests.cpp`, `DynamicsCpuTestMain.cpp`: 독립 wrapper/clock 시험과 groom budget 시험.
- `shaders/dynamics/Dynamics.hlsl`: BeginSubstep / EndSubstep / KickVelocity.
- `cmake/Dynamics.cmake`, `scripts/validate-dynamics.py`: 기존 DXC flags 재사용 및 재현.
- 기존 `GuideGpu.*`, `WindowSolver.*`, `WindowSession.cpp`, `Main.cpp`, `AppOptions.*`,
  `CMakeLists.txt`에 자원 flag/진단 모드/dispatch/CLI 연결을 추가했다.

GuideGpu의 **기존 authoritative Velocity**만 UAV-capable로 확장한다.
WindowSolver의 기존 InertialTarget도 UAV-capable로 확장한다. CurrentPosition은
이전과 동일한 renderer 소유 allocation을 빌리며 매 프레임 복사할 simulation buffer는 없다.
DynamicsSolver의 새 PreviousPosition은 별도 default-heap 12-byte float3 배열이다.
이는 substep history이며 render 위치나 solver sweep snapshot과 alias하지 않는다.

Begin은 참여 strand별 직렬 vertex loop로 예측 전체의 finite 여부를 먼저 검사하고,
그 다음 old x 저장, y 기록, x=y 초기화를 한다. root q 외의 orientation은 쓰지 않는다.
End는 Window fault를 확인한 뒤 전체 velocity reconstruction을 preflight하고 기록한다.
두 커널 모두 한 thread/strand, 64 threads/group이다. packed range의 vertex는
strand끼리 공유하지 않으므로 root/vertex 쓰기에 race가 없다.

기존 WindowSolver::runDynamic은 같은 pipeline/chunk8/cache/halos/P/Q helper를
호출한다. 고정 문제 Prepare의 CPU target 업로드나 fault reset은 호출하지 않는다.
dynamic substep ID는 기존 requested RunSweeps ordinal과 별개다.
ReferenceFlags bit4는 **대형 진단 snapshot 쓰기만 생략**한다. P/Q 및 schedule은
바꾸지 않았고, 복사해 둔 pre-dynamics shader의 P/Q/CachedSweep/UncachedSweep와
비교한다. robust quaternion 두 helper는 보존 reference와 byte-identical이다.

## binding과 동기화

| dynamics register | 내용 |
|---|---|
| t0,t1,t2 | immutable ranges / RestPosition / RestOrientation |
| u0,u1,u2 | authoritative CurrentPosition / CurrentOrientation / Velocity |
| u3,u4 | PreviousPosition / 기존 InertialTarget |
| u5,u6,u7 | 기존 WindowFault / WindowStats / DynamicsStatus |

b0 root constants 64 bytes: offset0 uint strands/participating/pinX/pinQ,
offset16 float h/gx/gy/gz, offset32 uint substep/padding×3,
offset48 float kickX/kickY/kickZ/padding. bool을 직접 업로드하지 않는다.
float3=12, quaternion=16, range=24, DynamicsStatus=48 bytes이며 크기/offset을 검사한다.

```
Begin: x/q/v/previous/y/status UAV
   UAV barriers (including y, previous, velocity)
   y UAV -> NON_PIXEL_SHADER_RESOURCE
Window: x/q UAV; y/rest/material/ranges SRV
   existing UAV barriers between chunks (max8 sweeps)
End: x/q/v/previous/y/status UAV; velocity written once
   UAV barriers before next Begin
... next substep, without render transitions ...
draw: x/q -> NON_PIXEL_SHADER_RESOURCE as required
```

동일한 target을 solve 중 동시에 SRV/UAV로 접근하지 않는다. single direct queue를 유지한다.
정상 substep에는 CPU geometry readback/upload, GPU resource allocation, fence wait가 없다.
root constants는 command list에 복사되고 reset upload staging은 기존 fence까지 유지한다.
Previous는 모든 window chunk를 넘어 End까지 보존한다.

미리 만든 frame-slot readback 두 개에 작은 status 배열만 복사한다. 해당 slot의
기존 frame fence 완료 뒤에만 Map한다. 관측된 실패는 Run과 대기 manual steps를 멈춘다.
명시적 report/capture/test는 기존 synchronous submission으로 GPU 완료를 기다린다.

## 실패와 고정 시간 누산기

실패는 새 substep에서 지우지 않는다. code/substep/strand/operation/index를 sticky하게
기록한다. prediction 실패 code8, velocity reconstruction 실패 code9이며 Window의
기존 실패 code도 유지한다. 실패한 solve 뒤에는 velocity를 재구성하지 않는다.
후속 substep은 해당 strand를 skip한다. 전체 substep rollback은 하지 않는다.
유효한 prediction/partial solve가 남을 수 있으며, 이를 성공한 완료 step으로 부르지 않는다.
관측 전에 다른 strand와 이미 queued된 작업은 진행할 수 있다. explicit test/report는
실패 exit code를 반환하고, 명시적인 Reset만 오류를 지우는 복구 경로이다.

Running 중 wall elapsed를 누적하여 whole h 단위로 진행한다. 한 rendered frame에서
최대 8 substeps이며 remainder+elapsed를 8h로 제한한다. 버린 시간은 discardedWallTime에
초 단위로 누적한다. 정상적인 fractional remainder는 다음 frame에 넘긴다.
Pause/resume은 baseline과 remainder를 재설정한다. 최소화 중에는 진행하지 않고
Running 상태였던 최소화 기간을 discarded time으로 센다. debugger stall에도 같은 cap을
적용한다. Bootstrap 장면을 선택할 때도 Running을 중단한다.

UI는 queued substeps/time과 fence 뒤 관측한 completed count를 구별한다.
CLI --sim-steps는 wall-clock stepping을 끄며 --frames와 독립적으로 N physical steps만
기록한다. 실패 시 queued count를 성공한 completed count로 보고하지 않는다.

## 진단과 GPU 측정

정상 경로는 대형 sweep snapshot을 쓰지 않으므로 last-sweep/batch update가 null이다.
UI의 Enable expensive sweep diagnostics 또는 --sim-diagnostics를 명시하면 기존 GPU
baseline으로 true net last-sweep/batch update를 측정한다. 측정용 extra sweep은 없다.
초기 q snapshot과 y로 마지막 substep 안에서의 objective before/after를 재계산하여
비증가를 검사한다. 서로 다른 y를 쓰는 F_n과 F_(n+1)을 같은 최적화 문제로 비교하지 않는다.

speed, kinetic/elastic energy, axial strain, SS error, 최종 x/q residual, root error,
tip displacement, ordinary/fallback/hard/failure를 보고한다. physical substep displacement,
prediction으로부터의 correction, solver batch와 last-sweep 순변화를 구별한다.
유한 스윕 solve의 mechanical energy가 매 step 감소한다고 주장하지 않는다.

GPU timestamp는 Begin / Window / End 세 구간과 queue GetTimestampFrequency를 사용한다.
8 substeps warm-up 후 24개의 한 substep sample을 fence 완료 뒤 읽어 median/P95를 낸다.
10 strands, 8 sweeps라는 제한된 microbenchmark이며 큰 진단 snapshot, geometry readback,
CPU recording, VSync 시간은 제외한다. 정상 실행에서는 timestamp를 사용하지 않는다.
머신 전체를 배타 제어하지 않았으며 전체 groom의 실시간 성능을 주장하지 않는다.

## 재현 CLI

```powershell
# 32 physical steps를 끝낸 상태를 저장한다. --frames는 표시 횟수이다.
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode dynamics --draw guides --participating all --sim-steps 32 --sim-h 0.008333333333 --sweeps-per-step 8 --gravity "0,-9.81,0" --frames 1 --sim-diagnostics --capture artifacts/dynamics/Release/step32.png --report artifacts/dynamics/Release/step32.json --log artifacts/dynamics/Release/step32.log
# 중력 없이 m/s amplitude를 한 번만 더한다.
& .\build\vs2022\bin\Debug\HairLabDX12.exe --sim-steps 16 --gravity "0,0,0" --sim-velocity "0.01,0.02,0.005" --draw guides --frames 1 --report artifacts/dynamics/Debug/velocity.json
ctest --test-dir build/vs2022 -C Debug -L CPU --output-on-failure
ctest --test-dir build/vs2022 -C Release -L CPU --output-on-failure
python scripts/validate-dynamics.py --config Debug
python scripts/validate-dynamics.py --config Release
```

--sim-capture-step N은 --sim-steps N의 alias이며 N 종료 뒤 기존 capture 경로로 PNG를 쓴다.
--sim-unpin-position / --sim-unpin-orientation은 독립적이다. --sim-reset-after는 검증용 reset이다.
--test dynamics-local / dynamics-groom은 명시적인 GPU 시험이다. 개발 중에는 스크립트의
--skip-regression으로 과거 suite를 생략할 수 있지만 완료 검증에서는 생략하지 않았다.
모든 새 출력은 artifacts/dynamics/<configuration>에 두고, 기존 artifact는 덮어쓰지 않는다.
격리된 exe/assets/shaders만 다른 cwd에서 실행하며 runtime에 Unity나 DXC가 필요 없다.

## 최초 실행 결과와 중력 좌표계 정정

아래 최초 groom 응답/예산/성능 수치는 world gravity 변환이 빠졌던 실행의 기록이다.
원본 JSON/PNG는 이력으로 보존하지만 현재 올바른 중력 응답의 기준으로 쓰면 안 된다.
수정 후 결과는 `artifacts/dynamics/<configuration>/gravity-frame/`에 별도로 저장한다.
수정 후에도 아래 synthetic local wrapper 오차는 동일하다. synthetic fixture는
독립적인 identity world/local 프레임을 의도적으로 사용한다.

Debug/Release 빌드와 구성별 CPU CTest 4개가 통과했다. 새 local/groom GPU suite 및
기존 guide-state, orientation-local/groom, position-local, window-solver/groom도 통과했다.
기존 robust local suite는 375 cases, ordinary332/fallback43/hard19/failure0을 유지한다.
shader compiler/profile/flags는 기존 Windows SDK DXC, cs_6_0 경로와 같다.

local suite는 zero-g rest, unpinned uniform translation/free-fall, CPU predictor와 기존
fixed-target path의 한 step 등가성, y/previous 보존, Step One/N/grouped submission,
2/3/5 vertices의 pin 4조합, inactive/rest, stale 설정과 sticky/failed-End를 검사한다.
참조식은 v_n=v0+n*h*g, x_n=x0+n*h*v0+.5*n*(n+1)*h²*g이다.
continuous ballistic trajectory와 비교하지 않는다. synthetic fixture는 원점 근처이며 16 steps다.

| 항목 | Debug/Release 오차 |
|---|---|
| 자유 이동 위치 / 속도 | 7.47495e-9 m / 1.13869e-7 m/s |
| 자유 낙하 위치 / 속도 | 1.42206e-8 m / 1.06121e-6 m/s |
| GPU prediction vs CPU prediction | 0 |
| 기존 fixed-target 경로와 한 step x/q | 0 / 0 |
| synthetic zero-g rest x/q/v drift | 0 / 0 / 0 |
| 한 step 목적함수 | 0.00136851 -> 8.90163e-7 |

실제 groom의 10 strands, zero-g 16 steps에서는 위치 drift 1.08965e-7 m,
회전 drift 3.44213e-6 rad, 최대 속도 1.78814e-6 m/s를 기록했다. 작은 속도를 0으로
clamp하지 않았다. zero-g one-shot velocity test의 tip 이동은 0.00307382 m였다.
root x/q와 inactive/rest 변경은 0이었다. y/Previous는 solve 중 byte-identical했다.

가닥 1/10/100/All을 기본 8 sweeps로 16 steps 검사했다. 10 strands는 같은 물리 구간
약 0.133333 s에서 1/4/8/16/32 sweeps, h=1/240은 32 steps로 비교했다.

| sweeps, h=1/120 | 최대 axial strain | q stationarity | 최대 tip 이동(m) |
|---|---|---|---|
| 1 | 1.77512 | .0342458 | .0926614 |
| 4 | .851780 | .00911031 | .0935471 |
| 8 | .578372 | .00485849 | .0956374 |
| 16 | .374813 | .00230749 | .0984728 |
| 32 | .213736 | .00108436 | .105970 |

같은 h에서 8과 32 sweeps의 전체 위치 최대 차이는 0.0237046 m였다.
h=1/240, 32 sweeps에서는 axial strain .0616639, q residual .000384353이었다.
h=1/120/32와 위치 차이 0.0288587 m는 시간 이산화와 미수렴 solve의 혼합 효과다.
이를 순수 시간 적분 오차, 완전 수렴해, global exact solution으로 해석하지 않는다.
8 sweeps는 bring-up 기본값이며 All에서 최대 axial strain이 약 .670459였다.
clamp/damping/mass scaling/material calibration으로 이 결과를 숨기지 않았다.

Release RTX 5070 Ti, 10 strands/8 sweeps의 Begin/Window/End GPU median은
0.009696 / 0.583248 / 0.007040 ms, P95는 0.010112 / 0.593376 / 0.007392 ms였다.
24 samples와 1 GHz timestamp frequency를 사용했다. 실제 raw 수치는 JSON에 보존한다.

PNG checkpoint 0/1/8/16/32, reset/replay/velocity-only를 저장했다. 같은 32 steps를
1 또는 130 rendered frames로 표시한 PNG, reset/rest 및 replay가 byte-identical하다.
최초 Release의 step-0/8/32/reset/velocity-only 이미지를 직접 열어 확인했으나,
오른쪽으로 움직이는 결과를 camera roll의 특성으로만 설명한 것은 잘못이었다.
world와 neck-local의 기준을 맞춰야 하며, 렌더러의 identity model matrix는 이를 대신하지 않는다.
좌표계 정정 후 front checkpoint에서는 중력에 의해 가이드가 아래로 움직이는 것을 확인했다.

각 config JSON/CSV와 summary가 실행 범위를 기록한다. 이미지 파일 작성/byte 비교와
직접 연 visual review는 별도로 기록한다. 실제 mouse/UI/resize/minimize 조작, WARP/다른 GPU,
긴 시간 안정성, 완전 수렴 상태에서의 순수 시간 이산화 오차는 미검증이다.
수동 확인은 mode -> All -> Reset -> Step/Run/Pause, gravity 변경 후 Reset,
독립 root pins, camera/resize/restore 순으로 수행한다.

중력 좌표계 수정만 재현하려면 기존 artifact를 보존하면서 다음을 실행한다.

```powershell
python scripts/validate-dynamics.py --config Debug --skip-regression --output-subdir gravity-frame
python scripts/validate-dynamics.py --config Release --skip-regression --output-subdir gravity-frame
```

이 실행은 새 dynamics suite, 방향/카메라 독립성, 정적 프리뷰, reset/replay, 격리 실행을
검증하며 과거 orientation/window GPU 전체 suite를 다시 실행한 것으로 표기하지 않는다.
초기 속도 `--sim-velocity`/one-shot velocity amplitude는 기존 hair-local m/s 정의를 유지한다.


## Autonomous convergence experiment (2026-10-30)

The default remains `FullPrediction`. The optional `--sim-initializer guarded`
changes only the starting iterate, choosing FullPrediction or InertialOnly per
strand by the SAME inertia + SS objective at the unchanged current q (BT is
constant in this comparison). `y=x+h*v+h*h*g`, masses, SS/BT, pins, h, root state
and `v=(xFinal-xOld)/h` are unchanged. This is an **incomplete research candidate**:
held-out 0.5-second accuracy and the all-strand 5% strain gate are not met.
Opening the app still does not run dynamics. UI changes require Reset Simulation.

`--sim-initializer full` retains the original initialization. Raw history reuse
and InertialOnly are benchmark-only rejected experiments, not app options.
The extra packed 12-byte acceleration history is written once at EndSubstep,
reset to zero, and never modifies velocity or gravity. It is only consumed by
the explicit benchmark mode 2. No adaptive initializer existed in the baseline.
Dynamics root bindings are t0 ranges/t1 rest x/t2 rest q/t3 mass/t4 rest length,
u0 x/u1 q/u2 v/u3 previous/u4 y/u5 faults/u6 stats/u7 status/u8 acceleration.
The 64-byte parameter block uses uint initializer at byte36 and float BaseSS at
byte60; 16 root DWORDs. All new UAVs use the same queue barriers/fence lifetime.

Full details, rejected experiments, references and reproducible finalist outputs:
`artifacts/autonomous/20261029T173940Z/REPORT.md`. The original forward schedule
and robust P/Q math remain; optional benchmark schedules are ordering experiments.
The independent double reference is linked only into AutonomousBench, never the app.

```powershell
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode dynamics --draw guides --participating all --sim-initializer guarded --sweeps-per-step 32
# In UI: Reset Simulation, then Step One / Step N / Run Dynamics.
# Each step advances h; N steps advance N*h, never h/N.
python scripts/autonomous_validate.py --phase analytic --config Release
python scripts/autonomous_validate.py --phase stress --config Release
python scripts/autonomous_validate.py --phase references --config Release
python scripts/autonomous_validate.py --phase timing --config Release
```

The validation driver reports process/finite-state success separately from
engineering gates: a zero benchmark exit alone does NOT certify trajectory
accuracy or the 5% strain limit. See the gate report. Expensive phases are
explicit and bounded; they do not run at normal startup or default CTest.
