# HairLabDX12 동역학 수렴·안정성 실험 보고서

**분류: improved-but-incomplete — 개선은 확인했지만 완료 기준은 미달.**

가설/검증 12회를 마치고 지정된 한도에서 종료했다. 전체 가닥의 유한 상태 검사와 기존 회귀는 통과했지만, 5초 중력 테스트의 최대 축 변형률은 **28.18%**로 5% 기준을 넘었다. held-out 0.5초 궤적도 **RMS 0.296 mm / 최대 1.297 mm**로 0.1/1 mm 기준에 미달했다. kick 궤적 오차는 더 크다. **600-step 캡처에서 가닥이 크게 방사상으로 벌어지는 실패도 직접 확인했다.** 오류 코드가 없다는 사실을 물리적 안정성이나 수렴 성공으로 해석하지 않는다.

기본 앱은 원래대로 정적 미리보기이며 동역학은 자동 실행하지 않는다. 동역학 기본 초기값도 기존 FullPrediction을 유지했다. 가장 나았던 후보는 명시적인 `guarded` 연구 옵션과 실행 가능한 별도 패키지로 보존했다. 생산용으로 검증된 동역학이라고 주장하지 않는다.

## 보존한 기준과 환경

- 작업 위치: `C:\Users\caw20\MyProject\HairLabDX12`만 사용했다. Unity 디렉터리는 읽거나 수정하지 않았다. staged assets 변경은 **0개**다.
- AGENTS.md는 루트 및 src/shaders/scripts/cmake 하위에서 발견되지 않았다. Git 저장소가 없어 최초 파일 해시, 전체 소스 복사본, 실행 패키지로 기준선을 보존했다.
- 기준선: [baseline](artifacts/autonomous/20261029T173940Z/baseline/), [파일 해시](artifacts/autonomous/20261029T173940Z/baseline/hashes.json). 최초 코드에는 adaptive initialization이 없었다.
- 실제 가이드: **2,839 strands / 90,848 vertices / 88,009 segments / 85,170 bends**. 전체 테스트에서 가닥을 생략하지 않았다.
- VS 2022 Community, MSVC **19.44.35229**, Windows SDK **10.0.26100.0**, CMake **4.3.1**, NVIDIA **GeForce RTX 5070 Ti** 하드웨어를 사용했다.
- 기존 SDK DXC `10.0.26100.0/x64/dxc.exe`, **1.9.2602.17**, `cs_6_0`, `-HV 2021 -Gis`; Debug `-Od -Zi -Qembed_debug`, Release `-O3`. 컴파일러/프로필/플래그를 변경하지 않았다. 실행 파일 옆 `shaders/*/compiler.txt`에 경로·버전·해시가 있다.
- 물성 SS=1, BT=1e-5, 밀도 1300 kg/m³, 반지름 0.00004 m, 질량 분배, rest 데이터, pin과 시간 간격을 보존했다. 실제 최소/최대 질량은 약 1.037e-8 / 7.542e-8 kg이다.
- 중력은 기존 고정 neck 회전으로 변환한 world `(0,-9.81,0)`이며 solver 입력은 `(-9.69966507,0.06558698,-1.46571171)` m/s²다. 카메라·좌표 변환을 바꾸지 않았다.

## 남긴 구현

`--sim-initializer guarded`는 각 가닥에서 현재 q를 고정하고 **같은 inertia+SS 에너지**를 비교하여 FullPrediction과 InertialOnly 중 초기 x만 선택한다. BT는 이 비교에서 상수다. 다음 식과 최종 속도 계산은 모든 초기화에서 같다.

```text
y = x_n + h*v_n + h*h*g
q initial = q_n
F = inertia(x,y) + SS(x,q) + BT(q)
v_new = (x_final - x_n)/h
```

감쇠, 속도/길이 clamp, 추가 pin, 질량·강성 조정, 중력 약화, 시간 축소는 없다. InertialOnly 단독은 자유낙하를 실패하므로 앱 옵션으로 제공하지 않는다. raw AdaptiveAcceleration도 실패하여 benchmark 전용으로만 남겼다.

가속도 추정 이력은 packed float3 GPU 버퍼이며 End에서 `(vNew-vOld)/h`를 기록하고 reset에서 0으로 만든다. guarded 후보는 이 이력을 사용하지 않는다. 기존 authoritative CurrentPosition/Velocity와 독립적인 rest 소유권은 그대로다. 별도의 simulation/render 위치 상태를 추가하지 않았다.

Dynamics 바인딩은 t0 ranges, t1 rest x, t2 rest q, t3 mass, t4 rest length; u0 x, u1 q, u2 velocity, u3 previous, u4 target, u5 faults, u6 stats, u7 status, u8 acceleration이다. 64-byte 상수의 initializer는 byte36, BaseSS는 byte60이다. Begin → target SRV/Window chunks → End의 단일 큐 전이와 UAV barrier를 확장했다. 일반 실행에 geometry readback이나 전체 target CPU upload를 넣지 않았다.

원래 P/Q와 forward cached/uncached 순회 본문, robust quaternion helper는 유지했다. 선택 가능한 역방향 순회는 benchmark 비교용으로 남겼으며 기본값은 forward다. checkpoint benchmark에서 연속 sweep 번호가 초기화되던 문제를 고쳤다. 초기값이 y가 아닐 수 있으므로 진단의 solve-before 에너지도 가정한 y 대신 실제 BatchX snapshot으로 계산하도록 고쳤다.

추가/수정 파일은 [changed-files.json](artifacts/autonomous/20261029T173940Z/changed-files.json), 수학·단위·history·root·cache·동기화 검토는 [AUDIT.md](artifacts/autonomous/20261029T173940Z/AUDIT.md)에 기록했다. 핵심 추가 파일은 `AutonomousBench.*`, `AutonomousReference.*`, `scripts/autonomous_*.py`, `scripts/run-autonomous-best.py`다. 기존 Dynamics/WindowSolver 및 CLI/UI 연결을 제한적으로 수정했다. 렌더러를 재작성하지 않았다.

## 판정 기준

| Gate | 결과 | 근거 |
|---|---|---|
| 1. 기존 Debug/Release 회귀 | 통과 | 각 CTest 7/7, 전체 dynamics/기존 orientation/window/정적 회귀 통과 |
| 2. 전체 가닥 5초 및 controlled kicks: 유한 상태·불변성 | 통과 | rest-start와 두 kick seed 모두 600 substeps 완료, failure/rest/pin/inactive corruption 0 |
| 3. h=1/120, ≤32 sweeps, 최대 축 변형률 ≤5% | **실패** | 전체 rest-start 최대 **28.1813%** |
| 4. qualified same-h reference 대비 RMS≤0.1 mm, 최대≤1 mm | **실패** | held-out 및 kick 궤적 미달 |
| 5. 동일 frozen 문제 objective/residual 개선 | 개선 확인 | 32/512 sweeps에서 두 잔차와 에너지 감소. 충분한 수렴을 뜻하지 않음 |

[GATES.json](artifacts/autonomous/20261029T173940Z/GATES.json)에 판정을 저장했다. `python scripts/autonomous_gates.py`는 미달 gate를 감지하여 **exit 1**을 반환한다. finite-state benchmark의 exit 0을 전체 gate 통과로 포장하지 않는다.

## 기준선과 최종 후보

모두 h=1/120, 32 sweeps/substep, 양쪽 root pin, 동일 질량·강성·중력이다.

| 항목 | 기존 FullPrediction | guarded 후보 |
|---|---:|---:|
| All / 600 steps 최대 절대 축 변형률 | 38.4246% | 28.1813% |
| held-out / 60 steps RMS 위치 오차 | 48.7438 mm | 0.295777 mm |
| held-out / 60 steps 최대 위치 오차 | 186.2668 mm | 1.296580 mm |
| train / 60 steps RMS 위치 오차 | 약 18.94 mm | 0.077056 mm |
| train / 60 steps 최대 위치 오차 | 약 77.73 mm | 0.398785 mm |

이는 상대적인 개선이지 요구된 물리 정확도 달성이 아니다. 특히 작은 변형률만 보고 움직임이 맞다고 결론 내리지 않았다.

| 동일 frozen 문제 | Full | guarded |
|---|---:|---:|
| F, 32 sweeps | 0.003782823 | 0.003428223 |
| 최대 position gradient/localDiagonal, 32 | 17.7394 µm | 17.6370 µm |
| normalized q stationarity, 32 | 3.34621e-4 | 3.30301e-4 |
| F, 512 sweeps | 0.000488628 | 0.000454075 |

같은 저장된 x/v/q/history에서 시작해 **target bytes가 동일함**을 검사했다. cached/immediate 경로는 1/2/4/8/16/32/128/512 checkpoint에서 x/q byte 일치했다. 512 sweep 결과조차 독립 reference 수준으로 수렴했다고 주장하지 않는다.

## 독립 기준해와 held-out 검증

사전에 선언한 train indices `[0,1,5,9]`, held-out `[17,127,511,1023]`, seed `[31871,912773]`를 사용했다. [FIXTURES.json](artifacts/autonomous/20261029T173940Z/FIXTURES.json)은 튜닝 전에 저장했다. held-out은 반복적인 검증에 사용했으므로 완전히 숨겨진 blind test라고 부르지 않는다. 최종 kick 궤적은 두 seed 모두 독립 reference와 비교했다.

검증 전용 double manifold Gauss-Newton은 analytic Jacobian, banded positive-pivot Cholesky, diagonal conditioning, LM/정확한 에너지 line search를 사용한다. 앱에는 링크하지 않는다. 반복 횟수가 아니라 position residual, q stationarity/spectral condition, 더 엄격한 허용오차의 궤적 refinement로 자격을 확인했다. Jacobian finite difference 오차는 약 **3.01e-13 이하**였다. 비선형 전역 최소해 또는 유일한 q라고 주장하지 않는다.

무-kick 전체 궤적 refinement 차이는 train 최대 1.21 µm, held-out 최대 3.61 µm였다. kick reference들도 residual/refinement 자격을 통과했다. 모든 비교는 동일 h와 동일 입력/float-rounded kick 배열을 사용한다. Unity를 새로 실행한 비교가 아니다.

| 0.5초 후보 궤적 | RMS 오차 | 최대 오차 |
|---|---:|---:|
| train, gravity | 0.0771 mm | 0.3988 mm |
| held-out, gravity | 0.2958 mm | 1.2966 mm |
| train, kick seed31871 | 3.7983 mm | 18.3988 mm |
| train, kick seed912773 | 3.6333 mm | 22.5777 mm |
| held-out, kick seed31871 | 3.8059 mm | 17.8863 mm |
| held-out, kick seed912773 | 3.3668 mm | 18.0228 mm |

작은 0.5초 reference에서 gate와의 모순은 발견하지 않았으나, 전체 5초 모델에서 5% gate가 반드시 가능한지 증명하지는 않았다. 전체 2,839가닥 5초에 대한 qualified double reference는 만들지 않았다. h=1/240, 120 steps도 같은 0.5초 구간으로 실행했지만 그 h의 독립 reference는 미검증이다. 따라서 시간 이산화 오차와 불완전 수렴을 완전히 분리했다고 주장하지 않는다.

## 해석·이력·경계 검사

- 기존 position-local/삼대각 oracle 및 coupled derivative 검사가 통과했다. P의 좌우 부호, rest-length 사용, 단위에서 입증된 수학 버그는 발견하지 못했다.
- 2/5/32 vertices의 unpinned uniform translation/free-fall을 Debug/Release에서 검사했다. 32-vertex 자유낙하 최대 위치 오차 약 **9.74e-8 m**, 속도 오차 **1.75e-6 m/s**. gravity predictor의 h² 계수나 최종 속도식을 바꾸지 않았다.
- 기존 one-substep wrapper 대 fixed-target 비교는 prediction/x/q 오차 0이었다. grouping/reset/replay, sticky failure, failed-strand velocity 보존, pin 조합 검사도 유지했다.
- 전체 groom 무중력 120 steps의 위치 drift 최대 **37.55 µm**, 속도 최대 **90.08 µm/s**를 기록했다. clamp로 0을 만들지 않았다.
- 전체 600-step gravity ordinary update **3,270,528,000**, fallback/hard/failure **0/0/0**. 두 kick seed도 unresolved failure 0. 이것은 수렴 인증이 아니다.
- 기존 robust local suite는 Debug/Release 각각 **375 GPU cases**, ordinary/fallback/hard **332/43/19**, unresolved 0. exact-hard energy는 **0.02 → 0.0038**이었다.
- 기존 window regression: prefix 160 cases, cache/immediate x/q 오차 0; double schedule x 오차 약 1.20e-8 m, q 약 5.90e-7 rad. true last-sweep metric 오차는 x 약 9.70e-13 m, q 약 2.11e-9 rad였다.
- 후보 All/16 diagnostics에서 batch/last-sweep x는 각각 약 4.20e-7 / 2.58e-8 m이며, q도 별도 측정했다. 물리 변위·prediction correction·sweep update를 혼용하지 않았다.

## 수렴 예산과 GPU 시간

held-out, 같은 h=1/120, 같은 0.5초 gravity 문제:

| sweeps/substep | RMS 궤적 오차 | 최대 tip 변위 |
|---:|---:|---:|
| 1 | 0.298001 mm | 0.00528 mm |
| 4 | 0.297369 mm | 0.01577 mm |
| 8 | 0.296758 mm | 0.02120 mm |
| 16 | 0.296143 mm | 0.04699 mm |
| 32 | 0.295777 mm | 0.13632 mm |

작은 local correction은 soft-mode 궤적 정확도를 보장하지 않았다. h=1/240에서는 같은 0.5초에 tip 0.64842 mm로 달라졌다. 이것만으로 더 정확하다고 선택하지 않았다.

Release, guarded/32 sweeps, GPU timestamp 측정. 각 단계 **median / P95, ms**:

| 가닥 | Begin | Window | End |
|---:|---:|---:|---:|
| 10 | 0.03755 / 0.04205 | 2.52427 / 3.02909 | 0.01122 / 0.01322 |
| 100 | 0.09211 / 0.09846 | 3.48630 / 3.76243 | 0.03040 / 0.03472 |
| All | 0.11178 / 0.15197 | 3.58867 / 3.74144 | 0.03278 / 0.03482 |

8 warm-up, 24 samples, queue frequency 1e9 ticks/s, fence 완료 후 resolve/read. 진단 geometry readback·capture·렌더·VSync 제외. Full 경로 측정도 `cycle-12/Release/timing-*-0.json`에 보존했다. 서로 다른 초기값은 Q 경로와 상태도 달라지므로 단순한 구현 속도 향상으로 해석하지 않는다. **정확도 gate 실패 상태이므로 실시간 품질 달성을 주장하지 않는다.**

## 채택하지 않은 접근

[EXPERIMENTS.md](artifacts/autonomous/20261029T173940Z/EXPERIMENTS.md)에 12회 가설·결과·결정을 모두 기록했다.

- raw acceleration history: 초기/guard 보강 후에도 strand failure 또는 큰 궤적 오차. 거부.
- SOR ω=1.8: isolated quadratic identity는 맞지만 frozen/coupled 결과 악화. 되돌림.
- forward/backward/alternating: cache 일치 검사는 통과했지만 held-out 정확도 개선 부족. 기본 forward 유지.
- c=x-y 보정 좌표: 작은 float 보정의 누적을 시험했으나 정확도 이득 없음. workspace 제거.
- coupled sweep scalar line search와 depth-one Anderson: 같은 F를 감소시켜도 필요한 동역학 정확도 미달. 추가 런타임 자원/코드 되돌림.
- 창 내부 4회 재방문: 추가 P/Q 비용에 비해 궤적 정확도 이득 없음. 되돌림.

실패 결과와 당시 소스는 각 cycle 디렉터리에 남겼다. 허용오차, expected output, 물리 파라미터를 바꿔 통과시키지 않았다.

## 캡처·회귀·배포

직접 내용을 확인한 PNG:

- `cycle-12/Release/regression/static-idle.png`: 원래 head/render hair 미리보기.
- `cycle-12/final-checks/guarded-60.png`: guide-only 0.5초.
- `cycle-12/final-checks/guarded-kick-60.png`: 일회성 velocity kick 반응.
- `cycle-12/final-checks/guarded-600.png`: **장기 실패 외형 확인**.

reset/rest, 60-step 뒤 130 idle render frames, 다른 cwd의 isolated 실행은 PNG byte 일치했다. 별도 [best-tested-runtime](artifacts/autonomous/20261029T173940Z/best-tested-runtime/) 패키지도 프로젝트 실행과 같은 PNG를 썼다. 패키지·소스 해시는 [runtime-hashes.json](artifacts/autonomous/20261029T173940Z/runtime-hashes.json), [changed-files.json](artifacts/autonomous/20261029T173940Z/changed-files.json), [final-source-hashes.json](artifacts/autonomous/20261029T173940Z/final-source-hashes.json)에 있다.

Debug/Release의 기존 static, guide-state, orientation-local/groom, position-local, window-solver/groom, dynamics-local/groom, bootstrap, isolated/다른 cwd, negative CLI, reset/replay 회귀가 통과했다. Debug 후보의 All/600 kick와 held-out 검증도 완료했다. Debug layer 오류가 검출되지 않았다. 마우스·UI 클릭·resize·pause/resume을 실제 사람 입력으로 조작한 검사는 **수행하지 않았다**. CLI/PNG 성공으로 대신 주장하지 않는다.

PowerShell helper 실행은 기존 시스템 execution policy에 막혔다. 정책이나 보안 설정을 바꾸거나 우회하지 않았다. Python helper와 직접 exe 실행은 실제 확인했다. matplotlib은 없어 플롯용 패키지를 설치하지 않았으며 원본 JSON/표를 보존했다.

## 재현 명령

프로젝트 루트에서:

```powershell
cmake --preset vs2022
cmake --build --preset debug
cmake --build --preset release
ctest --test-dir build/vs2022 -C Debug --output-on-failure
ctest --test-dir build/vs2022 -C Release --output-on-failure

# 제한된 60-step 후보 캡처. 알려진 정확도 한계가 있는 연구 옵션.
python scripts/run-autonomous-best.py --steps 60
# 창을 열되 일시정지 상태로 시작:
python scripts/run-autonomous-best.py --interactive

# UI 없이 명시적으로 재현:
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode dynamics --draw guides --participating all --sim-initializer guarded --sweeps-per-step 32 --sim-steps 60 --frames 1 --capture artifacts/autonomous/repro.png --report artifacts/autonomous/repro.json --log artifacts/autonomous/repro.log

python scripts/autonomous_validate.py --phase stress --config Release
python scripts/autonomous_validate.py --phase references --config Release
python scripts/autonomous_validate.py --phase analytic --config Release
python scripts/autonomous_validate.py --phase timing --config Release
python scripts/autonomous_final_tests.py
python scripts/autonomous_gates.py   # gate 3/4 미달로 exit 1
```

재검사 전 현재 artifacts를 보관하거나 새 run output을 지정한다. 위 검증 명령은 현재 run의 자기 결과를 갱신한다. 기존 역사적 expected outputs를 재생성하는 명령이 아니다.

실행 파일: `build/vs2022/bin/Release/HairLabDX12.exe`. 별도 패키지: `artifacts/autonomous/20261029T173940Z/best-tested-runtime/HairLabDX12.exe`. VS에서는 현재 폴더를 열고 기존 CMake preset과 HairLabDX12 target을 선택한다. 추가 SDK/Unity가 실행 시 필요하지 않다.

## 남은 위험과 재개 지점

수렴/장기 외형 안정성은 해결되지 않았다. 새로운 회차 한도를 받기 전 자동으로 다시 시작하지 않는다. [RESUME.md](artifacts/autonomous/20261029T173940Z/RESUME.md)에 현재 소스·실행 상태·실패 재현과 다음 검토 지점을 남겼다. 다음 연구는 같은 에너지의 soft-mode conditioning을 qualified reference로 확인하는 데 집중해야 하며, 감쇠·clamp·물성 변경으로 이 실패를 숨기면 안 된다.

collision/OGC, velocity field, reconstruction, head animation, renderer rewrite는 추가하지 않았다. rest와 assets는 그대로이고 robust local solver 및 spline-local moving-window x-q 구조를 유지했다.
