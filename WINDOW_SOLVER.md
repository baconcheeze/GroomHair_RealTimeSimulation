# 고정 타깃 coupled sliding-window solver

이 단계는 하나의 고정된 최소화 문제를 반복 완화한다. `RunSweeps(N)`은 시간을
`N*h` 진행하지 않는다. velocity는 0이며 커널에 바인딩하지 않는다. Unity 트리는
이번 작업에서 읽거나 수정하지 않았다. 기존 native asset/import/좌표 변환과
`ORIENTATION_MIGRATION.md`의 quaternion 규약을 유지한다.

## 구현 위치와 변경 범위

- `src/WindowMath.*`: double 위치 수식, 독립 tridiagonal oracle, 즉시 배열 쓰기
  schedule, 기존 독립 quaternion oracle 연결, 최종 상태 에너지/잔차.
- `src/WindowSolver.*`: GuideGpu를 빌리는 compute 소유자, 준비/리셋/업로드,
  파이프라인, 타깃, sticky fault, GPU 진단 snapshot, 명시적 readback.
- `src/WindowSession.*`: 세 실행 모드와 UI/Auto Relax 큐.
- `src/WindowTests.*`, `src/WindowCpuTestMain.cpp`: CPU/GPU oracle와 groom 회귀.
- `shaders/window/PositionLocal.hlsl`: 런타임과 isolated test가 공유하는 P helper.
- `shaders/window/WindowSolve.hlsl`: coupled cache, uncached 검증 경로, GPU perturb.
- `shaders/window/PositionValidation.hlsl`, `cmake/Window.cmake`: helper GPU 시험과
  기존 설정 그대로의 build-time DXC.
- `scripts/validate-window.py`: 두 configuration별 bounded 테스트/PNG/격리 실행.
- 기존 `Main.cpp`, `AppOptions.*`, `StaticPreview.*`, `GuideGpu.*`,
  `OrientationSession.h`, `CMakeLists.txt`에 공유 자원/모드/CLI 연결을 추가했다.
  `validate-orientation.py --offline`은 native 복사본만 검사한다.

기존 `GuideOrientationLocal.hlsl`, `GuideQuaternion.hlsl`은 보존된 reference와
바이트 단위로 동일하다. ordinary/fallback/hard-case 수치 및 physical-energy
acceptance(6e-6)를 변경하지 않았다. 기존 raster/compiler 설정과 ImGui도 유지한다.

## 고정 목적함수와 P의 유도

`F = E_inertia + E_ss + E_bt`이며

```
E_inertia = sum_v .5 * mass[v]/h² * |x[v]-y[v]|²
E_ss      = sum_s .5 * baseSS*L0[s] * |(x[s+1]-x[s])/L0[s]-Rotate(q[s],+Z)|²
kbt[b]    = 4*baseBT/L0[left segment]
E_bt      = sum_b .5*kbt[b]*|inverse(qLeft)*qRight - closestPole(qRestRelative)|²
```

로컬 위치의 `a=mass/h²`, `c=baseSS/L0`, `t=L0*Rotate(q,+Z)`를 놓으면

```
g = a*(x-y) + cl*(x-left-tl) + cr*(x-right+tr)
D = a + cl + cr
xNew = x - g/D
     = (a*y + cl*(left+tl) + cr*(right-tr))/D
```

이다. Hessian은 `D*I`이고, 양의 physical mass와 h에서 유일한 로컬 최소점이다.
없는 endpoint 항은 c=0으로 뺀다. root pin도 mass를 0으로 만들지 않는다.
현재 edge 정규화, length projection, damping, correction clamp는 없다.
Q는 현재 endpoints와 두 BT 이웃에서 coefficient/pole을 다시 구성하여 기존
robust local helper를 딱 한 번 호출한다. collapse된 current edge도 정규화하지
않으므로 SS의 방향 coefficient가 0인 기존 algebraic 경로를 유지한다.

이 목적함수는 Stable Cosserat의 SS/BT 모델에 기반하지만 아래 스케줄은 프로젝트의
명시적 Jolt-inspired 순서다. Jolt나 논문의 기본 전역 패스 순서와 같다고 주장하지 않는다.

## 정확한 schedule과 cache/halo

```
P(0)
if S == 1:
    P(1); Q(0); P(1)
else:
    for i = 0 .. S-2:
        P(i+1); Q(i+1); Q(i); P(i+1)
    P(S); Q(S-1); P(S)
```

pin P/Q는 상태를 쓰지 않는 no-op이다. unpinned P(0)는 prologue에서 실행된다.
각 sweep은 직전 sweep의 완전히 commit된 결과에서 root부터 다시 시작한다.

| cache slot | window i에서의 의미 |
|---|---|
| x0,x1,x2 | x[i], x[i+1], x[i+2] |
| q0,q1 | q[i], q[i+1] |
| left,right | q[i-1], q[i+2] (존재할 때의 halo) |

advance 시 outgoing x0/q0를 저장하고, 갱신된 q0를 다음 left halo로 유지한다.
x1/x2와 q1/right를 앞으로 넘기고 새 x[i+3],q[i+3]만 읽는다. 마지막 window의
tail 완료 후 남은 3개 위치와 2개 q를 저장한다. 실패/diagnostic prefix stop에도
유효한 cache를 flush한다. retired 값의 에너지 항이나 BT 연결을 버리지 않는다.
S=1은 별도 작은 cache로 처리한다. full-strand thread-local 배열은 없다.
실제 레지스터 수/spill은 측정하지 않았으며 register residency를 보장하지 않는다.

`ReferenceFlags=1`은 동일한 P/Q helper를 즉시 UAV 배열에 쓰는 uncached oracle,
`ReferenceFlags=2`는 fixed-q 검증 경로다. `OperationLimit`은 window 전이의
prefix 검증용이다. 일반 실행은 이 세 값에 모두 0을 사용한다.

## 준비와 경계의 수명

`PrepareProblem(y,h,settings)`만 InertialTarget을 업로드한다. rest-target 또는
결정적 smooth-displaced rest target을 만들며 pinned root y는 rest root와 같다.
pin의 고정 경계는 기존 rest root 위치/방향이다. x/q는 별도의 초기 추정이며
Prepare는 이를 rest로 덮어쓰지 않는다. Perturb는 x/q만 수정한다.

Prepare 당시 h, baseSS, baseBT, 참여 가닥, pin 두 개를 서명으로 저장한다.
바뀐 서명은 stale로 invalidate하며 Run을 거부한다. 관측된 stale 서명은 값을
되돌려도 명시적 Prepare가 필요하다. immutable mass/rest는 변경 API가 없다.
반복 Run/Auto는 같은 y와 서명을 사용한다. 새 UI 모드/참여/pin 전환은 충돌하는
queued Auto를 취소하고 reset한다. Auto는 기본 off다.

Reset은 shared guide current x/q를 rest로 복원하고 velocity/status를 0으로
초기화하며 문제를 unprepared로 만든다. 다음 실행에는 다시 Prepare한다.
NaN/overflow/local unresolved failure는 strand/sweep/window/P-or-Q/index와 함께
sticky fault로 남고 해당 strand의 안전하지 않은 작업을 중단한다. 후속 chunk나
Run은 이를 성공/수렴으로 바꾸지 않는다. Prepare/Reset만 오류를 지운다.

## 자원, binding, 동기화

`StaticPreview`가 처음 생성하는 **기존 GuideCurrentPosition**에 UAV flag를 추가했다.
GuideGpu가 같은 ComPtr를 빌리며 renderer와 solver는 동일 GPU address를 사용한다.
두 번째 simulation 위치 버퍼나 프레임별 render 복사는 없다. immutable
GuideRestPosition은 별도 자원이다. render hair는 계속 static이다.

모든 배열은 default heap이다. float3=12, xyzw quaternion=16, range=24 bytes.
물리 quaternion은 Hamilton product, local +Z이며 renderer row-vector와 별개다.

| register | 자원 / element |
|---|---|
| t0 | StrandRange / 6 uint32 |
| t1,t2,t3 | RestLength / float, RestRelative / float4, RestOrientation / float4 |
| t4,t5,t6 | Mass / float, InertialTarget / float3, RestPosition / float3 |
| u0,u1 | authoritative CurrentPosition / float3, CurrentOrientation / float4 |
| u2,u3 | Fault / 32 bytes, Stats / 56 bytes |
| u4,u5 | Run 시작 BatchX / float3, BatchQ / float4 |
| u6,u7 | sweep 시작 SweepX / float3, SweepQ / float4 |

root parameter 0은 16 DWORD/64-byte b0, 1..7은 t0..6, 8..15는 u0..7이다.

| b0 offset | C++/HLSL型と内容 |
|---|---|
| 0,4,8,12 | uint strands,vertices,segments,bends |
| 16,20,24,28 | uint participating,iterations,pinX,pinQ |
| 32,36,40,44 | float baseSS,baseBT,invH2; uint firstChunk |
| 48,52,56,60 | uint baseSweep,operationLimit,referenceFlags,perturbMode |

boolean upload는 uint 0/1이고 padding 없이 static_assert로 크기/offset을 검사한다.
zero-bend는 기존 GuideGpu의 최소 16-byte unused resource와 guarded indexing을 쓴다.

일반 실행은 한 strand/한 thread, 64 threads/group, 최대 **8 sweeps/dispatch**다.
단일 direct queue에서 두 mutable buffer를 tracked read state→UAV로 전환한다.
compute에서는 current SRV를 동시에 바인딩하지 않는다. 의존 chunk 사이 양쪽
mutable 자원과 status/snapshot에 UAV barrier를 넣고 렌더 전에 NON_PIXEL_SHADER_RESOURCE로
돌린다. compute root/pipeline 이후 graphics/ImGui가 자기 binding을 복구한다.
window/sweep 사이 CPU fence wait는 없다. explicit diagnostics/capture/reset upload
수명 처리만 기존 queue/fence 경로를 이용한다. root constants는 command list에 복사되고
upload staging은 frame-slot fence 또는 synchronous submission 완료까지 보존된다.

PreparedX/Q는 Prepare 순간의 진단 전용 GPU snapshot이다. inactive 상태가 처음부터
non-rest일 때도 보존 여부를 검사한다. 이는 추가 simulation state가 아니다.

## 진단의 의미

에너지는 최종 commit 상태에서 double로 재계산한다. monotonicity 대상은 TotalObjective며
SS/BT 개별 항의 증가는 실패가 아니다. free position의 gradient 및 gradient/localDiagonal(m),
free q의 final-neighbor stationarity/spectral/norm/pole tie를 보고한다. pinned DOF는
free convergence에서 제외한다.

MaxBatchPosition/OrientationUpdate는 요청 Run 시작과 종료 상태의 최대 **순변화**,
MaxLastSweepPosition/OrientationUpdate는 마지막 실제 sweep 전후의 최대 **순변화**다.
재방문 P/Q의 최대 개별 correction과 다르다. GPU baseline snapshot으로 측정하고
측정용 extra sweep은 없다. quaternion은 sign-equivalent chord를 각도(rad)로 변환한다.
아직 Run하지 않았거나 perturb 직후에는 update metric을 null로 보고한다.
counter는 해당 Run 전체 chunk를 누적하고 sticky failure는 요청 경계를 넘어 유지한다.
명시적 Read Diagnostics/CLI test 외 정상 프레임 CPU readback은 없다.

## 재현 명령

프로젝트 루트 PowerShell:

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
ctest --test-dir build/vs2022 -C Debug -L CPU --output-on-failure
ctest --test-dir build/vs2022 -C Release -L CPU --output-on-failure
python scripts/validate-window.py --config Debug
python scripts/validate-window.py --config Release
python scripts/validate-orientation.py --config Debug --offline
python scripts/validate-orientation.py --config Release --offline
```

전체 GPU window 검증은 explicit script에만 두어 기본 CTest를 제한했다. 기존
`HAIR_ENABLE_GPU_TESTS=ON`일 때 추가되는 orientation CTest도 두 configuration에서 통과했다.

```powershell
& .\build\vs2022\bin\Debug\HairLabDX12.exe --solver-mode window --scene hair --draw guides
& .\build\vs2022\bin\Release\HairLabDX12.exe --scene hair --view front --width 1024 --height 1024 --draw guides --participating all --window-target rest --window-perturb joint --window-sweeps 128 --frames 130 --validate-rest --report artifacts/window/manual.json --capture artifacts/window/manual.png --log artifacts/window/manual.log
& .\build\vs2022\bin\Debug\HairLabDX12.exe --test position-local --frames 130 --report artifacts/window/Debug/position-local.json
& .\build\vs2022\bin\Debug\HairLabDX12.exe --test window-solver --frames 130 --report artifacts/window/Debug/window-solver.json
& .\build\vs2022\bin\Debug\HairLabDX12.exe --test window-groom --frames 130 --report artifacts/window/Debug/window-groom.json
```

대화형 실행은 Coupled mode → Prepare Rest/Displaced → Perturb Both → Run N.
기본 참여 1가닥은 화면에서 작게 보인다. 움직임을 보려면 All을 선택하고 Prepare 후
Draw Render Hair를 끄고 Draw Guide Hair를 켠다. F/R/RMB/MMB/wheel은 기존 동작을 유지한다.
Orientation Only의 Bend는 선 endpoint가 아니라 material frame만 바꾼다.

## 실행한 검증과 결과 해석

환경: VS2022 Community MSVC 19.44.35229.0, Windows SDK 10.0.26100.0,
NVIDIA GeForce RTX 5070 Ti hardware. DXC 1.9.2602.17의 기존 cs_6_0,
`-HV 2021 -Gis`, Debug `-Od -Zi -Qembed_debug`, Release `-O3`를 그대로 쓴다.
`bin/<config>/shaders/window/compiler.txt`에 compiler 경로/version/hash/flags와
새 HLSL hash를 저장한다. include는 build dependency다. runtime에는 DXC가 필요 없다.

Guide 실측 2,839 strands / 90,848 vertices / 88,009 segments / 85,170 bends.
기존 physical mass 1.0366379e-8..7.5421852e-8 kg을 유지한다.

Debug/Release 공통 결과:

| 검증 | 최대 오차/결과 |
|---|---|
| double local 256, finite differences | 3.44272e-10 |
| double local 최소점 gradient | 3.89687e-15 |
| numerator/residual 동치 | 2.35514e-16 |
| moderate tridiagonal oracle | 1.11886e-16 m |
| GPU local 256 vs double | 1.79123e-7 m; objective growth 0 |
| 실제 source ID14, 32 vertices fixed-q double | 1.25213e-13 m |
| 같은 실제 scale fixed-q GPU 8,192 sweeps | 1.95683e-6 m; residual correction 7.49645e-9 m |
| 실제 scale infinity condition bound | 2063.1093 |
| cache/uncached prefix 160 + 17 sweeps/chunk8 vs1 | position 0, quaternion rotation 0 |
| 독립 double schedule | 1.20156e-8 m / 5.89584e-7 rad |
| true 마지막 sweep metric 재생 오차 | 9.69629e-13 m / 2.10743e-9 rad |

8192 fixed-q sweeps는 oracle 시험이며 프레임 예산이 아니다. float 정밀도와 실제
조건수의 영향이 있어 double/float의 독립 재계산을 bitwise 같다고 요구하지 않는다.
tridiagonal oracle은 fixed-q만의 해이며 coupled nonlinear global oracle가 아니다.

mixed lengths 2/3/4/5/32, pin 4조합, inactive non-rest 상태, window prefix/overlap/tail,
stale/unprepared 거부, collapsed current edge/180도, invalid 입력, 실패 sticky를 시험했다.
가닥 1/10/100/All 각각 rest/position/orientation/joint/displaced target를
1,2,4,8,10,16,32,128 sweeps에서 검사했다. raw JSON/CSV는 `artifacts/window/<config>/`.

All strands의 128 sweeps 목적함수:

| 초기 조건 | 초기 F → 최종 F | 최종 max position correction (m) | q stationarity |
|---|---|---|---|
| position | .459861512 → .0420271327 | 4.12508e-6 | 2.79445e-4 |
| orientation | 19.4397038 → .000608027681 | 6.25493e-7 | 3.84243e-5 |
| joint | 19.3499332 → .0390012199 | 4.22997e-6 | 2.94704e-4 |
| displaced target | .00232126881 → .00231803624 | 5.91988e-8 | 3.95084e-6 |

128 sweeps에서 수렴 완료나 전역 최소점을 주장하지 않는다. rest F는 전체 약 1e-11
float floor다. monotonicity 검사는 raw energy를 clamp하지 않고
`max(1e-11, previousF*1e-6)`의 수치 허용 범위를 쓴다. rest no-op도 float tolerance로
검사하며 idle/reset의 bitwise invariant와 구별한다.
실측 최대 checkpoint 증가(초기→1 sweep 포함)는 8.38728e-13이었다.
rest test의 최대 위치 drift는 4.37722e-8 m, 회전 drift는 5.57848e-6 rad였다.
joint All의 마지막 sweep 순변화는 1.03616e-5 m / 3.74454e-4 rad였다.
groom JSON의 최종 batch 값은 마지막 요청인 32→128의 96 sweeps 기준이다.
`relaxed-128.json`은 한 번의 Run(128) 기준이므로 batch 시작 상태가 다르다.

groom 회귀 전체 attempted/executed P=118,944,000/117,056,000,
Q=115,168,000/113,280,000. ordinary=113,280,000, fallback/hard/failure=0.
기존 별도 robust suite는 실제 375 cases, ordinary332/fallback43/hard19/unresolved0,
hard energy .02→.0038, normalized objective error2.25024e-9로 유지됐다.

PNG: rest, joint-perturbed, relaxed-1/8/32/128, reset, idle-before/after, queued-auto,
isolated. 1024², ImGui 없이 저장한다. rest/reset 및 normal idle1/130은 바이트 동일하다.
이전 static front capture와도 같았다. guide가 움직여도 render hair와 head는 유지됐다.
Auto130은 ProblemGeneration=1, 같은 y/h에서 130 sweeps였다. 격리 exe/assets/shaders만
다른 cwd에서 실행했다. explicit readback에서 rest/y/pins/inactive/velocity 변경은 0.
Debug D3D12 debug audit 오류는 없었다. Release에는 debug layer가 활성화되지 않는다.

이미지 파일 작성과 byte 비교는 자동 검증이다. 직접 확인한 파일 목록은
`artifacts/window/visual-review.json`에 별도로 기록한다. mouse/keyboard/UI/resize/
minimize 동작은 이번 작업에서 직접 조작하지 않았다. 수동으로 모드 전환, Prepare→
Perturb→Run→Reset, root pin 두 개, ImGui capture, 카메라, resize/restore를 확인할 것.
GPU timestamp, 레지스터/spill, 다른 GPU/WARP에 대한 검증은 하지 않았다.

물리 시간, gravity/integration, velocity reconstruction, angular state, collision/OGC,
animated roots, render-hair reconstruction는 추가하지 않았다.

## 코드 재검토에서 수정한 실행 제어 문제

- Bootstrap와 solver 옵션 조합이 solver를 실행하지 않고 성공할 수 있었다.
  이제 hair가 필요한 옵션을 다른 scene에서 거부하며, 명시적 실행 모드와 충돌하는
  옵션 및 `--test`와 일반 solver 실행 옵션의 혼합도 거부한다. h/pin 옵션만 있을
  때도 window 모드를 명시적으로 선택하는 것과 동일하게 해석한다.
- 모드 전환 후 reset을 프레임 기록까지 미루어 새 패널의 immediate diagnostic이
  이전 coupled 상태를 먼저 읽을 수 있었다. `WindowSession::selectMode`가 Auto/대기
  명령/이전 진단 문자열을 취소하고 동기 reset을 완료한 뒤 새 패널을 연다.
  평상시 sweep마다 CPU wait를 추가한 것은 아니다.
- D3D12 debug audit을 최종 diagnostic readback 뒤로 옮겨 마지막 GPU 명령도 검사한다.

수식/HLSL/계수/허용 오차는 이번 재검토에서 변경하지 않았다. 새 CLI 회귀는 잘못된
12조합과 정상 5조합을 검사한다. 기존 `--test window-solver`는 이제 production
모드 전환 메서드로 coupled→orientation→static을 실행하고 첫 진단 전에 정확한
reset/Auto 취소를 확인한다. 실제 mouse/UI 조작 시험과 구별된다.

```powershell
python scripts/validate-window-cli.py --config Debug
python scripts/validate-window-cli.py --config Release
```

결과는 `artifacts/window/<config>/review/cli.json` 및
`window-solver.mode-transitions.json`에 남는다. 검증 스크립트의 `--build-dir`로
기본 `build/vs2022` 이외의 CMake binary directory도 지정할 수 있다.
