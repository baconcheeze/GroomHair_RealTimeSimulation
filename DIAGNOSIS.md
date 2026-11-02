# HairLabDX12 첫 수렴 실패 진단

**확인한 실패 유형은 제한된 window 반복의 x-q 결합 완화 부족이다. 특정 로컬 코드 버그나 보장된 최소 수정은 아직 고립하지 못했다.** 첫 substep의 GPU와 동일 순서 double 실행은 거의 같은 잘못된 위치에 도달한다. 위치만 정확히 풀어도 주요 궤적 오차가 남는다. production solver와 기본값·연구 후보는 변경하지 않았다.

원본 자료: [진단 디렉터리](artifacts/diagnosis/20261030T000839Z/), [matched 결과](artifacts/diagnosis/20261030T000839Z/matched/results.json), [입력 전체 JSON](artifacts/diagnosis/20261030T000839Z/fixture.json), [provenance](artifacts/diagnosis/20261030T000839Z/provenance.json).

## 1. 12회 실험에서 이미 알았던 것

REPORT.md와 완료된 EXPERIMENTS.md를 먼저 읽었다. 기존 결과를 다시 생성하지 않았다.

| 회차 | 실제 시험 및 판정 |
|---|---|
| 1–2 | Full/Inertial/raw Adaptive 비교, 독립 double reference 자격 확인. Full의 큰 오차 확인. Inertial의 작은 strain만으로 성공 판정하지 않음 |
| 3–4 | boundary-aware history 시작값, Full/Inertial/history 에너지 guard. 큰 오차 또는 Q failure로 거부 |
| 5 | history 후보를 뺀 Full/Inertial guard. 유한 상태 검사는 통과했지만 궤적 기준 미달; 연구 옵션으로만 보존 |
| 6 | SOR: isolated 수학은 맞지만 coupled 결과 악화, 되돌림 |
| 7 | 역방향/교대 창: cache 일치, 정확도 개선 부족. 기본 forward 유지. checkpoint 번호 문제만 수정 |
| 8 | c=x-y 보정 좌표: 정확도 개선 없음, 되돌림. 이것이 모든 정밀도 문제가 없다는 증명은 아님 |
| 9–10 | scalar extrapolation / Anderson: frozen F를 줄여도 동역학 정확도 미달. 런타임 변경 되돌림 |
| 11–12 | 창 내부 4회 재방문도 개선 부족. 최종 회귀는 통과했지만 5% strain/0.1 mm RMS 기준 실패 |

미해결이었던 항목은 **같은 초기 x/q와 같은 y에서의 구현 오차, 위치 반복 부족, 결합 오차의 분리**였다. 기존 160-prefix cache 시험과 375-local quaternion 시험은 재사용했으며 전부 다시 돌리지 않았다.

## 2. 중력·비교 provenance

문서만 바뀐 것이 아니다. 초기 동역학 실행과 중력 정정 후 실행의 입력 벡터가 실제로 다르다. 그러나 **12회 자율 실험의 baseline 시점에는 이미 정정 코드가 들어 있었다.** 현재 DYNAMICS.md도 예전 identity 해석이 잘못됐고 초기 결과는 역사적 기록이라고 명시한다. 문서를 덮어써 모순을 없애지 않았다.

| 결과군 | 실제 gravitySolver, m/s² | 해석 |
|---|---|---|
| 초기 `artifacts/dynamics/Release/step-16.json` | `(0,-9.81000042,0)` | 8 sweeps. 이전 identity 해석의 결과. 당시 전체 변환 행렬/정확한 실행 바이너리는 보존되지 않아 복원 불가 |
| `.../gravity-frame/step-16.json` | `(-9.69966507,0.06558698,-1.46571171)` | 8 sweeps. world 입력을 neck-local로 변환한 결과. 위 행과 solver 개선 비교하면 안 됨 |
| 12회 baseline/control 및 guarded groom 비교 | 같은 변환 벡터 | 기록된 벡터 및 snapshot의 변환 코드가 일치. 중력 정정은 12회 실험 이전 변경 |
| 이번 A/B/C matched 비교 | `(-9.699665069580078,0.06558697670698166,-1.4657117128372192)` | fixture의 동일 벡터·행렬·target 사용 |

데이터는 계속 **FemaleHairRest, meters, LH neck-local**이다. 렌더 model identity와 물리 world identity는 다른 말이다. row-vector 규약의 `g_solver = g_world * worldToLocalRotation`이며, 이번 실제 역회전의 3×3 부분은 다음과 같다. translation과 camera는 사용하지 않는다.

```text
[-0.00655502081  -0.99997758865  -0.00136741996]
[ 0.98875272274  -0.00668572634   0.14940994978]
[-0.14941574633  -0.00037264824   0.98877435923]
```

[provenance.json](artifacts/diagnosis/20261030T000839Z/provenance.json)은 기존 비교 결과 99개에 대해 실제 기록/소스에서 유추한 값/미기록 값을 구분한다. h, sweep/checkpoint, initializer, seed/kick, pins, 물성, dataset, source/shader hashes와 metric 정의를 연결했다. 일부 과거 frozen 보고서는 h·중력·물성·rest가 자체 저장되지 않았고, cycle 1 benchmark source 및 회차별 실행 DXIL도 모두 보존되지는 않았다. 없는 해시는 현재 해시로 대체하지 않았다. 이번 fixture는 이 누락을 보완해 완전 저장했다.

metric도 섞지 않았다. cycle 1의 strain **0.366411은 마지막 시점 값**, 최종 Full control의 **0.384246은 전체 600-step 최대값**이다. SS vector error와 axial strain도 서로 다르다. 기존 60-step RMS는 모든 step/vertex의 평균이고, 아래 최초 실패 표시는 한 step/한 strand의 RMS다. 0.1/1 mm 문턱은 완화하지 않았다.

## 3. 초기 실패 fixture

- guide index **1023**, source strand ID **9852**, 32 vertices / 31 segments / 30 bends.
- 기존 held-out 4가닥 batch의 네 번째 가닥. **substep 1**, seed **912773**의 일회성 smooth velocity kick (`seed+17=912790`) 사용.
- 기존 저장 궤적에서 첫 RMS 0.1 mm 초과: **0.116713 mm**, 최대 **0.176534 mm**. 분리한 한 가닥 GPU 결과는 그 기존 batch의 위치와 **bit 일치**했다.
- pre-x/pre-q는 rest와 정확히 같고 acceleration history는 전부 0이다. 실제 pre-v 배열을 저장했으므로 재생은 RNG/batch 선택에 의존하지 않는다.
- h=`0.008333333767950535`, 32 forward sweeps, SS=`1`, BT=`9.999999747378752e-6`, root x/q pin 모두 true. 질량/길이/반지름/rest q·relative q·경계 target은 원래 값을 저장했다.
- initializer=`guarded(3)`이며 이 가닥은 Inertial 시작값을 선택했다. **y는 여전히 x+h*v+h²*g**다. float predictor는 저장 y와 bit 일치했고, double 계산과의 최대 차이는 10.50 nm였다.

저장 파일 [fixture.hfd](artifacts/diagnosis/20261030T000839Z/frozen/fixture.hfd)의 SHA256:
`2db58b0c60ae96900288dbd837da3aeaaed061d354429e1516d03a345528e6b7`.
pre-x/v/q, acceleration history, y, initial x/q, 전체 rest/physical arrays, h/coefficients/gravity/transform/root targets를 포함한다. [fixture.json](artifacts/diagnosis/20261030T000839Z/fixture.json)에 각 section의 offset/stride/hash와 실제 수치를 풀어 썼다.

이는 kick 사례의 최초 실패다. **rest-gravity에서도** 기존 자료만으로 같은 가닥 step 1의 RMS 오차 하한 **0.114641 mm**를 얻는다: reference RMS 이동 0.114827 mm에서 기록된 GPU batch 최대 이동 0.0001863 mm를 뺀 triangle-inequality 하한이다. 별도 5초 재실행은 하지 않았다. 전체 groom 장기 발산의 모든 최초 분기를 추적한 것은 아니다.

## 4. SAME frozen 문제 결과

모든 비교는 저장한 **동일 initial x/q, 동일 y, 동일 float 입력 상수와 경계**에서 수행했다. double 경로는 그 입력을 double로 승격해 계산한다. A의 Q는 기존 독립 double local oracle이므로 GPU helper의 연산별 double 번역은 아니다. A의 작은 차이에는 정밀도와 local 알고리즘 차이가 모두 포함될 수 있다.

| 비교 | 위치 차이 RMS / 최대 | 목적함수 또는 gap |
|---|---:|---:|
| A: GPU 32 vs 동일 P-Q-Q-P 순서 double 32 | **0.07337 / 0.15884 µm** | F_GPU=6.24698362e-9 J, F_double=6.24690501e-9 J |
| GPU cached vs GPU immediate-array 32 | **0 / 0** | 위치·q 일치 |
| B: q_initial 고정, GPU 위치 32 vs 삼대각 oracle | 22.868 / 32.099 µm | conditional gap=2.45601e-9 J |
| B: 실제 GPU32의 q 고정, 현재 x vs 삼대각 oracle | 11.166 / 16.049 µm | conditional gap=6.32992e-10 J |
| B: 그 q 고정, 추가 4096 위치 sweeps GPU / double RMS | 2.131 / 0.000281 µm | GPU gap=2.25286e-11 J, double gap=3.70e-19 J |
| C: GPU32 vs qualified coupled reference | **116.713 / 176.531 µm** | F_ref=5.08907861e-9 J, gap=1.15790501e-9 J |
| C: 동일 순서 double32 vs coupled reference | 116.704 / 176.553 µm | double로도 첫 실패가 남음 |

B는 q를 고정한 **조건부** 최소해이며 coupled 전역 oracle이 아니다. 실제 GPU32 q에서 위치를 정확히 풀면 전체 objective gap의 약 **54.67%**는 줄지만, coupled reference와의 RMS 차이는 여전히 **115.280 µm**다. 이때 q stationarity는 약 2.82e-6에서 3.16e-5로 커진다. 즉 위치를 다 풀면 q도 다시 풀어야 하는 결합 문제가 남는다. 4096 위치 sweep은 진단용이며 runtime 예산으로 제안하지 않는다.

### C의 자격과 한계

기존 double manifold Gauss-Newton reference를 그대로 사용했다. 독립 Jacobian finite-difference 검증은 12회 기록의 약 3e-13 오차 증거를 재사용했다. 이번에는 initial과 GPU32 두 시작점에서 각각 풀고, tolerance를 더 엄격히 정제했다.

- 추가 정제 조건: position correction ≤1e-14 m, normalized q stationarity ≤1e-13. 실제 추가 iteration 1회에서 최대 위치 변화 **0.361 nm**.
- 최종 position gradient/localDiagonal ≤1.71e-17 m, q stationarity ≤1.34e-14, q spectral violation 0.
- 두 시작점의 최종 위치 일치: RMS **7.35e-14 m**, 최대 **1.42e-13 m**.
- 기존 저장 reference와 새 matched reference의 위치 차이도 최대 약 **8.08 nm**였다.

이로써 **이 작은 substep의 더 정확한 국소 비교해**로는 충분히 구별된다. 반복 횟수만으로 자격을 주지 않았다. 비선형 전역 최적해, 전체 coupled Hessian의 양의 정부호 증명, 5초 전체 groom reference라고 부르지 않는다.

## 5. 분리한 결론과 다음 한 가지

| 분류 | 이번 증거가 지지하는 결론 |
|---|---|
| 구현/정밀도 | cache 오류 증거 없음. float 장기 정체 약 2 µm는 보이지만 116.7 µm 최초 오차의 주 설명은 아님. double 같은 순서도 실패 |
| 위치 완화 | 32회 위치 완화가 충분하지 않음은 조건부 oracle로 확인. 하지만 이것만 해결해도 주요 위치 오차가 남음 |
| x-q 결합 | 주요 잔여 오차. q 고정 위치 최적화 이후에도 RMS 115.3 µm와 더 큰 q 잔차가 남음 |
| history/prediction | 첫 step·zero acceleration history라 누적 history가 원인일 수 없음. y bit 일치, velocity 재구성 오차 약 1.56e-10 m/s |
| 모델/경계 | 같은 원래 물성·pin에서 더 낮은 F/작은 잔차의 해가 존재. 경계나 물성을 바꿔야 한다는 근거 없음. 모델의 장기 정확성 자체를 증명한 것은 아님 |
| reference 불충분 | 이번 국소 차이를 설명할 정도의 reference 불확실성은 관측 안 됨. 다른 strand/후기 상태로 일반화는 미검증 |

**작게 움직이는 것을 성공으로 보지 않는다.** 첫 step tip 이동은 GPU **0.03341 mm**, coupled reference **0.18359 mm**다. 현재 후보는 이 사례에서 초기 추정 부근에 머물며 응답을 과소평가한다.

다음 단일 수정 후보는 **같은 F의 창 내부 x-q 공동 보정에 대한 전처리**다. 위치 반복만 늘리거나 float를 일괄 double로 바꾸는 수리를 첫 선택으로 정당화할 증거는 부족하다. 다만 구체적인 로컬 코드 수준의 root cause / 보장된 최소 수리는 **아직 고립되지 않았다**. 이 후보를 구현·튜닝하지 않았으며 여기서 진단을 끝낸다.

## 재현 및 보존

```powershell
cmake -S diagnostics -B build/diagnosis -G "Visual Studio 17 2022" -A x64
cmake --build build/diagnosis --config Release --target FirstFailure
& .\build\diagnosis\bin\Release\FirstFailure.exe --fixture artifacts/diagnosis/20261030T000839Z/frozen/fixture.hfd --out artifacts/diagnosis/20261030T000839Z/repro
python diagnostics/audit.py
```

[reproducer](artifacts/diagnosis/20261030T000839Z/reproducer/)는 exe/shaders/fixture만 포함하며 **assets 없는 다른 cwd**에서 결과 JSON이 정확히 같은 것을 확인했다. 형식/셰이더·소스·바이너리 해시는 [diagnostic-source-binary-hashes.json](artifacts/diagnosis/20261030T000839Z/diagnostic-source-binary-hashes.json)과 [provenance.json](artifacts/diagnosis/20261030T000839Z/provenance.json)에 있다. GPU Window/Begin/End DXIL은 보존된 연구 후보와 byte 일치한다.

[production-preserved.json](artifacts/diagnosis/20261030T000839Z/production-preserved.json): 생산 소스·셰이더·assets·CMake·Debug/Release 실행 파일 등 88개 해시와 기존 연구 패키지가 그대로다. 추가 파일은 `diagnostics/`와 본 문서 및 새 진단 artifacts뿐이다. 기존 12회 결과/expected outputs, Unity, 물성, 중력, 감쇠, pin, runtime schedule은 수정하지 않았다. 새 broad campaign이나 전체 5초 suite는 실행하지 않았다.
