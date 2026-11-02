# Guide state / robust orientation-only 이관

## 범위와 소스

Unity 루트 `C:/Users/caw20/MyProject/UnityProject/RealTimeHairSimulation`은
읽기 전용으로 사용했습니다. Unity Editor나 소스 exporter는 실행하지 않았습니다.
현재 patched HLSL/검증 코드를 읽고 `references/orientation/`에 보존했습니다.
전체 원본 경로와 raw SHA-256은 `references/orientation/manifest.json`에 있습니다.
기존 `assets/`와 importer는 변경하지 않았습니다. 물리 버퍼는 해당 에셋에서 실제
counts를 읽으며 다음 수치는 이 데이터의 검증 결과이지 런타임 상수가 아닙니다.

| 항목 | 실제 개수 |
| --- | ---: |
| Guide strand / vertex / segment / bend | 2,839 / 90,848 / 88,009 / 85,170 |
| Render strand / vertex / segment | 28,381 / 908,192 / 879,811 |

선택 여성 머리, 원본 Jolt scalp 선택, 22개 hull debug, 카메라, 캡처, bootstrap을
유지합니다. 원본 hull을 여성 머리의 검증된 collision fit으로 간주하지 않습니다.

| 핵심 원본 (Assets/HairRest 기준) | SHA-256 |
| --- | --- |
| Runtime/GuideHairResources.cs | `5dbf2d56a6ab95bee9f6116c8678c0df48fb5e853422fa1767b1c2205921410f` |
| Runtime/GuideHairFrames.cs | `21478d17406fad166f319b5e77595efdcd2b71b1d6ebb36970386d98cce8c721` |
| Runtime/GuideHairOrientationSolver.cs | `f366d52703a87f1d068fa1083d9fbc27746435c429b9c3a84e6d2b10b028f5f3` |
| Shaders/GuideQuaternion.hlsl | `36b066253334b0e11b35c92759a5628a1b4d8b90b038b6160bfcba618854f740` |
| Shaders/GuideOrientationLocal.hlsl | `7e783ce456259f2e43bd754f055a1df2bd91b76eabe91558ec1b291d8687a3c3` |
| Resources/StableCosseratOrientation.compute | `1790e7b88f7cf99278d3b2321d628245e8240299ff5d05e37794fb9a2f39afb8` |

GuideQuaternion/GuideOrientationLocal은 native shader 폴더에도 **바이트 동일**하게
복사했습니다. `GuideSolve.hlsl`은 kernel pragma 제거, include 경로, 명시 register,
48-byte cbuffer만 변경했습니다. local 함수와 production sweep의 수학·threshold·
failure 처리에는 변경이 없습니다. legacy-only normalize numerator 구현으로 대체하지
않았습니다. local validation wrapper에는 원본 legacy 비교 함수도 남지만 실제 빌드는
RobustLocal 진입점만 사용합니다.

MIT CPU 참고 코드의 Copyright 2025 Jerry Hsu 고지는
`references/orientation/StableCosserat-CPU-LICENSE.txt`에 보존하고 실행 파일 옆
`shaders/orientation/`에도 복사합니다. 기존 모델·ImGui notices는 그대로 유지합니다.

## 함수와 소유권 매핑

| Unity | Native |
| --- | --- |
| GuideHairTopology / GuideStrandRange | GuideCpuState.ranges / GuideRange, guidegpu::State.ranges |
| GuideHairRestState | GuideCpuState + guidegpu::State의 immutable buffers |
| GuideHairDynamicState | 공유 GuideCurrentPosition, CurrentOrientation, zero Velocity |
| GuideHairFrames.First/Transport/BendTwist | GuideState.cpp의 first/transport/relative quaternion 초기화 |
| GuideHairOrientationSolver.Run/Reset | OrientationSolver::run/resetStatus, State::reset |
| Perturb / UI / DrawFrames | OrientationSession + GuideCpuState::perturb |
| Measure / final stationarity | OrientationDiagnostics.cpp |
| OrientationLocalReference | QuaternionMath.cpp의 double dense Jacobi reference |
| RobustnessMath/GpuValidation | OrientationMathTests / OrientationFixtures / OrientationTests |

State가 physical 자원을 소유합니다. 기존 StaticPreview의 GuideRestPosition과
GuideCurrentPosition, segment connectivity를 빌려 쓰며 두 번째 groom 위치 상태는
만들지 않습니다. Solver는 pipeline과 오류/stat scratch만 소유합니다. 별도 위치
자원이 있는 테스트 fixture는 실제 groom과 완전히 격리되어 있습니다.

source의 임시 baseSS=1 N, baseBT=1e-5 N*m²를 유지합니다. segment kss=baseSS*L0,
bend kbt=4*baseBT/왼쪽 segment L0이며 compliance나 시간 적분 계수가 아닙니다.

## 프레임·quaternion·질량 초기화

미터, +Y up, 이미 변환된 native scene 위치를 그대로 사용합니다. 여기서는 Z 반사를
추가하지 않습니다. renderer의 row-vector/row_major 규약과 physical quaternion
algebra는 별개입니다. quaternion은 packed xyzw, Hamilton a*b, local +Z tangent입니다.
XMQuaternionMultiply(a,b)가 b*a를 반환하므로 physical math에는 사용하지 않습니다.

첫 frame은 abs(t.x)<0.8이면 +X, 아니면 +Y를 reference로 정하고 tangent에 투영합니다.
다음 frame은 최소 회전으로 transport하고 반대 tangent에서는 이전 d1 축을 사용합니다.
새 tangent에 d1을 재투영하고 q를 normalize한 뒤 이전 frame과 sign continuity를 맞춥니다.
물리 basis d1/d2/d3로 직접 quaternion을 구성하며 실제 Rotate(q,+Z)와 tangent를 검사합니다.
rest bend는 inverse(qLeft)*qRight의 **full relative quaternion**이고 log curvature가 아닙니다.

N>=2만 허용하며 strand별 vertex/segment/bend 범위를 checked 64-bit 계산 후 uint32로
저장합니다. source와 동일하게 radius=0.00004 m, density=1300 kg/m³,
length epsilon=1e-7 m입니다. 각 segment 질량의 절반씩 두 끝점에 더합니다.
root 질량도 양수입니다. inertia/omega나 intrinsic SS 버퍼는 없습니다.

초기화 측정:

| 항목 | 결과 |
| --- | ---: |
| rest length min / max / mean (m) | 0.00317280856 / 0.01170679275 / 0.00728450374 |
| 최대 quaternion norm 오차 | 1.19209e-7 |
| 최대 tangent 각 오차 (rad) | 5.16406e-7 |
| 최대 rest SS | 6.48158e-7 |
| 최대 relative reconstruction 오차 (rad) | 3.67428e-7 |
| vertex mass min / max (kg) | 1.03664e-8 / 7.54219e-8 |
| guide 총 질량 (kg) | 0.00418928832 |

기존 source current.txt의 counts/length와 일치합니다. 독립적으로 재계산한 C#/C++
frame은 bitwise 동일성을 요구하지 않습니다. source의 tangent<2e-5, norm<2e-6,
SS<2e-5, bend reconstruction<1e-5 tolerance를 그대로 통과했습니다.
같은 CPU 배열을 업로드한 결과와 immutable readback에는 memcmp를 사용합니다.

## GPU layout / registers

Float3=12바이트(x/y/z offset 0/4/8), quaternion=16(0/4/8/12), GuideRange=24
(firstVertex/vertexCount/firstSegment/segmentCount/firstBend/bendCount offset 0/4/8/12/16/20)를
static_assert합니다. mass/length/radius는 float32, velocity는 float3 zero입니다.

| register | 자원 | stride / count |
| --- | --- | --- |
| t0 | ranges | 24 / 2,839 |
| t1 | 기존 GuideCurrentPosition (read-only) | 12 / 90,848 |
| t2 | RestLength | 4 / 88,009 |
| t3 | RestBendTwist | 16 / 85,170 |
| t4 | RestOrientation | 16 / 88,009 |
| u0 | CurrentOrientation (독립 UAV allocation) | 16 / 88,009 |
| u1 | sticky error code | 4 / strand |
| u2 | regular/fallback/hard/rejected/failures + lastSweep | 24 / strand |

Mass(90,848), Radius(88,009), Velocity(90,848)는 state가 소유하지만 solver에 bind하지
않습니다. 기존 rest positions도 immutable이며 shader에는 current position만 read-only로
bind합니다. 모든 물리 배열은 default heap입니다. 0-bend fixture는 16바이트의 유효한
unused resource를 만들고 shader의 guarded indexing을 사용합니다.

b0는 12 root DWORD / 48바이트입니다:

| offset | 타입/필드 |
| --- | --- |
| 0 / 4 / 8 / 12 | uint Strands / Vertices / Segments / Bends |
| 16 / 20 / 24 / 28 | uint Participating / Iterations / PinRoot / FirstChunk |
| 32 / 36 | float BaseSS / BaseBT |
| 40 / 44 | uint padding |

C++ bool을 업로드하지 않습니다. Root parameter 0은 상수, 1..5는 t0..t4 SRV,
6..8은 u0..u2 UAV입니다. root descriptors를 사용하므로 별도 shader-visible heap을
할당하지 않습니다. root constants는 dispatch마다 command stream에 복사되어
frames-in-flight 동안 재사용되는 cbuffer 덮어쓰기가 없습니다.

Material-frame VS는 t0 position, t1 current orientation, t2 기존 SegmentStarts를
GPU에서 직접 읽습니다. b0의 view-projection은 64바이트, stride uint는 offset64,
axisLength float는68, padding은72/76입니다. midpoint에 RGB d1/d2/d3를 표시하며
기본 1가닥/stride4/4mm/off입니다. source와 같은 depth-independent **debug axis overlay**이며
hair/head의 정상 depth test와 back-face culling에는 영향을 주지 않습니다.

## 컴파일·동기화·기본 동작

설치된 SDK x64 DXC:
`C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe`.
실제 버전은 `1.9.2602.17 (21d28f727)`입니다. CMake가 발견 경로/버전을 출력합니다.
컴파일러 SHA-256 `073a5d88b2643622905677ef463d1ca3d4dcc7ae4a8295431d36377a57e599d8`,
dxcompiler.dll SHA-256 `495033935c058fc6d318ac257afdfa910795bf41b7e5ba905a0e7191dc8bd4fa`.

- profile cs_6_0, -HV 2021, -Gis
- Debug: -Od -Zi -Qembed_debug
- Release: -O3
- entry SolveOrientation / RobustLocal
- includes는 build dependency이며 compiler.txt에 source hashes도 기록
- runtime은 executable-relative GuideSolve.dxil/LocalValidation.dxil을 로드
- build/normal runtime에 Unity 불필요, runtime에는 DXC도 불필요
- SM6.0 지원을 검사하며 미지원 시 명확히 실패; CPU solver로 대체하지 않음

기존 raster path는 D3DCompileFromFile vs_5_0/ps_5_0 그대로입니다. 새 axis shader도
같은 raster 경로를 사용합니다. reduced precision/wave/experimental feature는 없습니다.

한 direct queue, 한 thread가 한 strand를 독점하며 numthreads=64입니다. root-to-tip
serial GS, prefix guards, pin skip을 그대로 사용합니다. Run(N)은 최대64 sweeps씩
dispatch하고 모든 의존 UAV에 barrier를 넣습니다. error는 Reset까지 sticky이고 stats는
Run의 첫 chunk에서 초기화하여 이후 chunk에 누적합니다. lastSweep는 실제 매 sweep에서
리셋하므로 마지막 실제 sweep만 나타냅니다.

CurrentOrientation의 실제 state를 추적해 compute UAV ↔ frame VS SRV, upload COPY_DEST,
readback COPY_SOURCE 전환을 수행합니다. readback 후 원래 state로 복원합니다.
startup/명시적 진단만 fence를 기다립니다. 일반 dispatch 사이에는 CPU wait/GetData가
없습니다. frame별 reset/perturb upload는 해당 back-buffer fence가 끝날 때까지 보존합니다.
compute 후 기존 graphics root/PSO를 설정하고 마지막 ImGui heap/상태도 다시 설정합니다.

기본 참여=1, root pin=true, sweep32, Auto Solve=false, frame display=false입니다.
앱을 여는 것만으로는 dispatch하지 않습니다. reset/참여/핀 변경은 source처럼 상태를
rest로 돌립니다. perturb는 20° material X/Z, smooth 4-segment half-width, random 최대5°,
seed12345입니다. .NET seeded System.Random의 subtractive generator를 직접 이식했으며
알려진 시퀀스와 source suite의 double finite-difference 결과로 검사합니다.

2026-10-30 UI 가시성 수정: 교란 버튼은 frame overlay를 켜고 stride=1, 축 길이 최소8mm로
설정하며 Auto Solve를 일시 정지합니다. 기본 stride4가 이 groom의 bend 대상 segment15를
건너뛰던 문제를 해결합니다. 적용 상태를 패널에 표시하며 perturbation 수학과 CLI의
명시적 표시 설정은 변경하지 않습니다.

## 검증 및 source 비교의 한계

source의 ordinary refinement, safeguarded secular fallback, deterministic hard case,
near-hard classification, branch/physical energy acceptance, hemisphere 순서, sticky failures를
그대로 사용합니다. 독립 double reference는 source OrientationLocalReference.cs의 dense
symmetric 4x4 Jacobi + 200-step secular solve입니다. GPU projector/fixed-point 알고리즘의
복사본이 아니며 normal frame의 CPU fallback으로 사용하지 않습니다.

source에는 fixture 생성 코드와 historical CSV가 있지만 Unity의 raw orientation/fixture
입력 binary snapshot은 없습니다. source C# generator를 이식하여 native 입력을 persist하고
historical 375개 initial/reference energies를 비교했습니다(최대 상대차 약 3.74e-16 /
1.21e-16). Debug/Release native 입력은 바이트 동일합니다:
`47566e03514d5ab7dfc9debaeac01c2add264543efc138b374f5b6b138886a5d`.
새 Unity 실행이나 bit-identical C# groom frame equivalence를 주장하지 않습니다.

CPU 테스트: 서로 다른 축의 noncommuting 합성, composed vector rotation,
inverse/relative/sign equivalence, 실제 +Z basis mapping, U-turn, variable lengths,
endpoint mass, invalid geometry 및 독립 double 128문제 / FD 432샘플을 실행했습니다.
FD 최대 normalized error=6.73128922e-7, source와 같은 threshold3e-6을 통과합니다.

Debug와 optimized Release 각각 실행한 로컬 GPU 결과:

| 항목 | Debug / Release |
| --- | ---: |
| 실제 valid local cases | 375 / 375 |
| ordinary / fallback / hard / unresolved | 332 / 43 / 19 / 0 (각 구성) |
| hard energy | 0.02 → 0.0038 |
| 최대 normalized objective error | 2.25024e-9 |
| 최대 normalized stationarity | 2.34137e-6 |
| 최대 normalized spectral violation | 9.07839e-10 |
| 최대 quaternion norm error | 1.31528e-7 |
| independent last-sweep replay error (rad) | 7.40781e-10 |

exact180, 양쪽 near-hard, 16 sign 조합, common scaling 1e-6/1/1e6,
deterministic random300, SS-only/alpha-zero/both-zero/minus-only algebra를 포함합니다.
invalid local input5개, production code1/2/3/4/6, nonfinite coefficient sticky failure와
chunk counter, reset 회복, two-vertex/zero-bend unpinned solve도 별도로 통과했습니다.
expected negative fixtures를 valid-case 미해결 실패와 혼동하지 않습니다.
source보다 tolerance를 느슨하게 바꾸지 않았습니다. DXC IEEE/최적화와 C#/C++ float
연산 차이로 source historical residual/norm의 마지막 자릿수는 다르지만 같은 기준을
통과하고 유효 local branch counters도 이번 실행에서는 같았습니다.

Groom은 rest no-op 및 Bend/Twist/Smooth/Random 각각 1/10/100/All을 실행합니다.
single은 powers-of-two checkpoint로 최대2048 sweeps, multi는128입니다. 각 checkpoint의
에너지/최종 이웃 residual/counters를 기록하고 multi는 단일 Run(128)을 reset replay해
전체 batch angle도 별도로 기록합니다. checkpoint schedule이 source의 single-strand
매 sweep readback과 다르므로 source의 정확한 종료 iteration을 재현했다고 하지 않습니다.

대표 random 결과(Debug/Release 동일 측정값):

| 참여 | 초기 에너지 | 최종 에너지 | 마지막 sweep (rad) | normalized residual |
| --- | ---: | ---: | ---: | ---: |
| 1 (1024 sweeps) | 2.28648e-4 | 3.16484e-12 | 1.10550e-6 | 1.48012e-7 |
| 10 (128) | 2.82919e-3 | 1.21583e-8 | 3.68806e-5 | 5.20732e-6 |
| 100 (128) | 2.91856e-2 | 8.10692e-8 | 4.77011e-5 | 5.20732e-6 |
| 2,839 (128) | 0.834460 | 2.35921e-6 | 6.53310e-5 | 7.65776e-6 |

All random의 전체 batch angle=0.0944242 rad이며 마지막 sweep과 다릅니다.
128-sweep multi 테스트는 에너지 감소·안전을 검증하며 모든 가닥이 single의 마지막
업데이트 종료 기준에 도달했다는 뜻은 아닙니다.
16조합+full-batch replay groom 회귀에서 actual ordinary=90,691,200,
fallback/hard/rejected/failed=0입니다. source suite보다 실행 조합/스케줄이 많으므로
source 총 update counter와 동일해야 한다고 강제하지 않습니다.
독립 dense reference 첫 GS sweep과 GPU 최대 각 차이는2.73094e-7 rad입니다.
에너지를 clamp하지 않으며 pinned/inactive 변수는 free convergence 진단에서 제외합니다.
최종 neighbor와 closest poles로 residual을 다시 구성하고 near-pole ties를 보고합니다.
이는 4D constrained quaternion gradient이고 unique smooth 3D torque라는 주장이 아닙니다.
LastSweep는 저장 chord를4*asin(chord/2)로 변환합니다. baseline 없는 Batch와 Run 전
LastSweep는 JSON null입니다. 진단 버튼이 추가 auto sweep을 실행하지 않습니다.

## 재현 명령과 출력

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
ctest --test-dir build/vs2022 -C Debug -L CPU --output-on-failure
ctest --test-dir build/vs2022 -C Release -L CPU --output-on-failure

& .\build\vs2022\bin\Debug\HairLabDX12.exe --test guide-state --frames 130 --report artifacts/guide-state.json --log artifacts/guide-state.log
& .\build\vs2022\bin\Debug\HairLabDX12.exe --test orientation-local --frames 130 --report artifacts/local.json --log artifacts/local.log
& .\build\vs2022\bin\Release\HairLabDX12.exe --test orientation-groom --frames 130 --report artifacts/groom.json --log artifacts/groom.log

python scripts/validate-orientation.py --config Debug
python scripts/validate-orientation.py --config Release
```

GPU CTest는 명시적 opt-in이며 CPU 테스트와 분리합니다:

```powershell
cmake --preset vs2022 -DHAIR_ENABLE_GPU_TESTS=ON
ctest --test-dir build/vs2022 -C Debug -L HardwareGPU --output-on-failure
ctest --test-dir build/vs2022 -C Release -L HardwareGPU --output-on-failure
```

이번 세션에서 두 구성 모두 CPU2 + HardwareGPU3 CTest가 통과했습니다.
추가 스크립트는 130 idle frames, GPU immutable readback, frame1/130 PNG 일치,
orientation perturb 중 hair lines 불변, frame overlay 변화, reset 후 원본 PNG 복원,
queued Auto Solve130프레임과 SRV/UAV 교대, 다른 cwd의 isolated runtime을 검사합니다.
기존 validate-preview.py도 두 구성에서 bootstrap/head/hull/capture 회귀를 통과했습니다.
Debug layer 오류가 없습니다. 정상 실행에 per-frame readback은 없습니다.

캡처 단독 명령:

```powershell
& .\build\vs2022\bin\Debug\HairLabDX12.exe --scene hair --view front --frames 130 --width 1024 --height 1024 --orientation-perturb bend --material-frames --frame-stride 1 --capture artifacts/frames-perturbed.png --log artifacts/capture.log
& .\build\vs2022\bin\Debug\HairLabDX12.exe --scene hair --view front --frames 130 --width 1024 --height 1024 --orientation-perturb bend --orientation-sweeps 512 --material-frames --frame-stride 1 --capture artifacts/frames-relaxed.png --log artifacts/capture.log
& .\build\vs2022\bin\Debug\HairLabDX12.exe --scene hair --view front --frames 130 --width 1024 --height 1024 --orientation-perturb bend --orientation-sweeps 512 --orientation-reset-after --capture artifacts/after-reset.png --log artifacts/capture.log
```

`--participating 1|10|100|all`, `--unpin-root`도 지원합니다. `--orientation-auto`는 명시적
bounded --frames와 함께 사용하며 normal frame queue에서 source32 sweeps/frame을 실행합니다.
`--validate-rest`를 함께 주면 종료 시 physical immutable/zero velocity와 orientation 진단도
readback하여 로그에 남깁니다. 기존 --frames/--log/--warp/--capture/--validate-rest는 유지합니다.

출력은 `artifacts/orientation/Debug/`, `artifacts/orientation/Release/`의
guide-state.json, orientation-local.json/csv/inputs.bin, orientation-groom.json/csv,
summary.json, baseline/idle/perturbed-lines/frames-perturbed/frames-relaxed/after-reset/
queued-auto PNG와 각 로그입니다. 테스트/asset/capture 실패는 nonzero입니다.
compiled source/DXIL 해시는 summary.json, compiler 경로/version/flags는 실행 파일 옆
shaders/orientation/compiler.txt와 build/vs2022/orientation-compiler-*.txt에 있습니다.

생성된 perturbed/relaxed/reset PNG를 직접 열어 확인했습니다. mouse/UI/resize 조작은
실행 검증하지 않았으며 CLI/PNG 성공을 상호작용 검증으로 간주하지 않습니다.
수동으로 참여/핀 변경 reset, 버튼, Auto Solve, frame limit/stride, ImGui 입력 차단,
카메라, resize/minimize/restore를 확인하세요. WARP 및 SM6 미지원 기기도 미실행입니다.

## 엄격한 중단 지점

Unity 원본은 수정하지 않았고 native build/runtime은 그 폴더에 의존하지 않습니다.
기존 rest/current positions는 모든 검증에서 바이트 동일, velocity는 정확히 zero,
rest/mass/radius/ranges는 immutable이며 pinned root/inactive orientations도 유지됐습니다.
current orientation과 solver status만 변합니다. position solve, sliding window x–q,
gravity/integration, velocity reconstruction, inertia/omega, collision/OGC, velocity fields,
render-hair reconstruction, scalp animation, production shading은 구현하지 않았습니다.
