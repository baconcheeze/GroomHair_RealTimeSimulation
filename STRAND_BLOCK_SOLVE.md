# Full-strand 구조화 선형 백엔드 실험

## 판정

**numerical/backend mismatch — 독립 QR 검증까지 포함한 전체 승인 보류.** 기존 참조와 블록 백엔드 사이의 조립·선형 보정·비선형 결과는 일치한다. 그러나 자유 루트 방향을 가진 1세그먼트 두 사례에서 두 정상방정식 백엔드 모두 QR과의 사전 각도 허용오차를 넘었다. 기준을 완화하거나 정규화를 높여 통과시키지 않았다. 따라서 GPU 포팅을 권고하지 않고 이 수치 차이의 독립 규명을 다음 한 단계로 남긴다. 실패한 joint-window 후보는 그대로 NO-GO다.

## 입력과 보존

DIAGNOSIS.md와 JOINT_WINDOW_PROTOTYPE.md를 먼저 읽고 기존 12개 JWP 파일 전부를 복사했다. 주 입력은 source 9852 / guide 1023, 첫 substep, 32 vertices다. 저장된 x/v/q/history/y, h, 질량, rest, 계수, pins를 그대로 사용했으며 kick을 재생성하지 않았다. h≈0.00833333376795, SS=1, BT≈9.999999747e-6, 양 루트 pin, solver gravity=(-9.69966507,0.06558698,-1.46571171)다. 이 실험에서 중력이나 예측을 다시 계산하지 않는다.

추가 입력은 기존 heldout-slot0/2, N=2/5의 네 pin 조합 각각, tie-jacobian-only다. held-out은 이번 실험에 대한 고정 비교 집합이지 역사적으로 보지 않은 blind 자료가 아니다. tie 입력은 이전에는 Jacobian 검사 용도였으므로 역사적 qualified optimum으로 취급하지 않는다.

생산 코드/셰이더/자산/설정/기존 문서 등 90개 해시를 재확인했고 변경은 0개다. Unity 접근, HLSL 포팅, GPU 실행, 5초 groom 재실행은 하지 않았다. 이전 산출물은 보존했다.

## 같은 비선형 방법

기존 AutonomousReference.cpp의 residual/Jacobian 및 minimize 본문을 기계적으로 추출한 adapter를 사용했다. 기존 함수를 직접 실행한 결과와 adapter도 전 입력에서 재현 검증했다. Hamilton xyzw, spatial/left physical-angle radian increment, closest-pole residual, SS/BT 가중치, pin 제거, 자유 q 정규화는 그대로다. 전체 물리 목적함수만으로 trial을 승인한다.

D_j=sqrt(max(H_jj,1e-30)); scaled system은 (D^-1 H D^-1 + lambda I)u=-D^-1 g, delta=D^-1 u다. lambda=1e-8, accepted 후 max(1e-12,0.2 lambda), rejected 후 min(1e12,10 lambda), retry/line 각16, alpha=2^-k를 그대로 유지했다. primary 기존 세 tolerance phase (1e-11m,1e-9), (1e-12m,1e-10), (1e-14m,1e-13), 다른 입력 두 phase (1e-11m,1e-9),(1e-13m,1e-11), phase cap1500이다. cap까지 돌았다는 이유로 qualified로 판정하지 않는다.

## 관측된 chain 구조와 구현

z_i=[dx_i,dtheta_i], 끝 z_S=[dx_S]. inertia는 한 station, SS는 이웃 x와 자신의 q, BT는 인접 q에만 걸린다. 실제 Jacobian row의 station span 최대1, 직접 관측한 off-band H=0, dense 재구성 및 scaled RHS 차이=0이었다. 핀 DOF를 제거하며 primary 블록은 [0, 6×30, 3], 총183 DOF다. root pin 조합은 0/3/6 블록으로, terminal은3으로 처리한다.

기존 구현은 dense H/L 저장을 사용하지만 factor 계산은 이미 scalar band12 Cholesky다. 그러므로 일반 dense O(n^3) 계산을 처음 선형화했다는 주장은 틀리다. 새 구현은 block Cholesky: C_i=E_i L_(i-1)^(-T)를 삼각 풀이로 구하고 D_i-C_i C_i^T를 factor한다. forward/back substitution하며 explicit inverse는 없다. 직접 band 조립은 dense H/L을 할당하지 않는다. 검증 전용 경로만 dense 추출·QR을 사용한다. factor 실패는 명시적으로 반환한다. lambda=0, 자유 twist null-mode 테스트는 두 백엔드 모두 예상대로 실패했고 pin/spring을 추가하지 않았다.

## 선형 일치 및 독립 QR 한계

원래 초기 상태, 보존 sequential32, 기존 참조 trajectory의 초기/1/2/4/8/16/종료 시점 중 실제 방문한 상태를 같은 pole/scaling/lambda로 검사했다. 추출 band와 직접 band의 보정은 bitwise 같았다.

```json
{
  "linearComparisons": 83,
  "strictPass": 74,
  "offBandMax": 0,
  "matrixDifference": 0,
  "rhsDifference": 0,
  "maxReferenceBackward": 1.2273515121673079e-16,
  "maxBlockBackward": 1.2273515121673079e-16,
  "maxQRKKTBackward": 2.9760854388426817e-15,
  "maxBlockPositionDifference": 1.2381285001595533e-17,
  "maxBlockAngleDifference": 2.9054417370672405e-16,
  "maxBlockScaledRelative": 1.5325711684830281e-15,
  "maxQRPositionDifference": 1.94645003942156e-13,
  "maxQRAngleDifference": 1.0402911781111874e-08,
  "maxNonlinearPositionDifference": 6.397344083151129e-17,
  "alphaDifferences": 0,
  "strictOverallPass": false,
  "classification": "numerical/backend mismatch: independent QR check unresolved; reference-versus-block equivalence reproduced"
}
```

QR은 동일 augmented LS [J D^-1; sqrt(lambda)I]의 double Householder 검증이다. synthetic-2-pins0/1에서 자유 twist 방향의 작은 수치 차이가 최대 약1.04e-8 rad로 각도 절대 기준1e-9를 넘었다. 기존↔블록 차이는 거의 roundoff이며 두 정상방정식 풀이의 backward residual은 작다. QR도 augmented KKT residual은 작으나 이것만으로 ill-conditioned 방향의 forward 정확도를 보증하지 않는다. 현 자료만으로 QR 또는 정상방정식 중 어느 쪽의 마지막 자리까지 정확한지 확정하지 않는다.

첫 verification에서는 near-zero J^T r에 대한 RHS-relative QR residual이 무의미하게 커지는 현상을 확인했다. final에서는 LS 입력 크기로 정규화한 KKT residual을 추가하고 원래 normal residual도 남겼다. 절대/상대 물리 step 기준은 유지했다. final executable exit=1이며 2개 synthetic 사례의 엄격한 실패가 그대로 남아 있다. verification/과 final/ 모두 보존했다.

## 비선형 결과와 비용

|입력|자유 DOF|accepted 기존/블록|rejected 기존/블록|최종 RMS 차이(m)|기존/블록 CPU median ms|기존/블록 P95 ms|
|---|---:|---:|---:|---:|---:|---:|
|heldout-slot0|183|7/7|0/0|1.32e-17|0.7502/0.6876|1.1116/1.0325|
|heldout-slot2|183|6/6|0/0|2.95e-17|0.6199/0.5468|1.0394/0.7127|
|primary|183|3/3|0/0|1.02e-17|0.5563/0.4256|0.8304/0.6655|
|synthetic-2-pins0|9|4/4|0/0|3.75e-20|0.0076/0.0121|0.0109/0.0158|
|synthetic-2-pins1|6|4/4|0/0|8.91e-19|0.0093/0.0154|0.0118/0.0258|
|synthetic-2-pins2|6|2/2|0/0|0|0.0044/0.0084|0.0052/0.0102|
|synthetic-2-pins3|3|2/2|0/0|0|0.0029/0.0072|0.0031/0.0077|
|synthetic-5-pins0|27|4/4|0/0|2.69e-19|0.0450/0.0581|0.0589/0.0771|
|synthetic-5-pins1|24|4/4|0/0|1.92e-20|0.0454/0.0570|0.0743/0.1125|
|synthetic-5-pins2|24|3/3|0/0|8.08e-20|0.0413/0.0586|0.0921/0.0789|
|synthetic-5-pins3|21|3/3|0/0|8.37e-24|0.0352/0.0442|0.0471/0.0665|
|tie-jacobian-only|21|5/5|0/0|1.32e-22|0.0446/0.0544|0.0694/0.0916|

전12개 비선형 비교는 qualified 여부·최종 위치·목적함수 기준을 통과했다. primary는 3 accepted iterations이며 4/8/16을 억지로 추가 실행하지 않았다. convergence.csv와 각 trace.json에 실제 체크포인트, F, 위치 RMS/max, tip 벡터, 위치 correction 및 q stationarity를 저장했다.

|primary iteration|F(J)|RMS to qualified (µm)|position correction(m)|q stationarity|
|---:|---:|---:|---:|---:|
|0 (phase0)|8.36349157954e-09|116.830696|4.708e-08|9.814e-05|
|1 (phase0)|5.08909841148e-09|0.102510477|7.646e-10|5.114e-08|
|2 (phase0)|5.08907861022e-09|0.000209165298|7.569e-16|7.16e-12|
|2 (phase1)|5.08907861022e-09|0.000209165298|7.569e-16|7.16e-12|
|2 (phase2)|5.08907861022e-09|0.000209165298|7.569e-16|7.16e-12|
|3 (phase2)|5.08907861022e-09|0|1.703e-17|1.331e-14|

최종 tip displacement(m)=[-5.7851780927799246e-05, 0.0001735359181832541, -1.5599656012610374e-05]. qualified reference는 비선형 전역 최적해의 증명이 아니다.

타이밍은 Release CPU wall time이며 warm-up3, 측정21회, 기존/블록 순서 교대, 파일쓰기와 trajectory 저장은 timed region 밖이다. 입력 로딩/초기 state 복사는 제외하고 normalize/assemble/diagnose/factor/line search를 포함한다. 작은 내부 타이머 오버헤드는 포함되며 CPU scheduling에 민감하다. GPU 시간으로 환산할 수 없다.

|32-vertex 입력|assembly median 기존/블록(ms)|factor median 기존/블록(ms)|기존 sequential32 median/P95(ms)|
|---|---:|---:|---:|
|heldout-slot0|0.4320/0.5148|0.2062/0.0907|19.4629/21.2338|
|heldout-slot2|0.3802/0.4325|0.1609/0.0665|19.0175/20.0375|
|primary|0.3413/0.3620|0.1281/0.0388|19.7243/21.1569|

primary: 기존 matrix payload535824B→band36864B(약14.5배 감소), 선형 payload541680B→44440B(약12.2배 감소). 이는 배열 payload이며 allocator overhead, 입력/state, 검증 dense/QR storage는 제외한다. 신규 station 크기/지도 저장을 포함한 후자도 O(N)이다. 3회 factor의 multiply-subtract count53580→31287, pivots549 동일. 기존 6 assemblies/1854 residual rows 동일. row-pair 카운터는 기존 경로에서 계측하지 않아 0을 실제 연산 없음으로 해석하면 안 된다. 직접 band assembly는 오히려 느린 사례가 있으므로 전체 이득은 factor 시간만큼 크지 않다.

순차32는 동일한 정확도에 도달한 계산이 아니므로 시간만으로 solver 우열을 말할 수 없다. 기존 진단에서 primary sequential32 RMS≈116.704µm였던 반면 full reference는 실제 reference-directed 변위를 재현한다.

## 재현

프로젝트 루트 PowerShell:

```powershell
python diagnostics/strand-block/generate.py
cmake -S diagnostics/strand-block -B build/strand-block -G "Visual Studio 17 2022" -A x64
cmake --build build/strand-block --config Release --target StrandBlockExperiment
& ./build/strand-block/bin/Release/StrandBlockExperiment.exe --inputs artifacts/strand-block/20261030T141357Z/inputs --out artifacts/strand-block/replay --timing yes
```

새 out 경로를 사용한다. 엄격한 QR 비교 실패 때문에 현재 예상 exit code는1이며 성공으로 숨기지 않는다. exact source/input hashes는 run manifest와 preservation.json, 원시 trial CSV/trajectory/linear-equivalence/timing rawSamples는 final/에 있다.

추가한 파일은 diagnostics/strand-block/의 독립 CMake, generator, 생성 adapter, Backend/Verify, driver, 요약 스크립트와 본 문서 및 신규 artifacts뿐이다. production root CMake는 수정하지 않았다.

## 다음 한 단계와 엄격한 한계

다음 단계는 실패한 자유-twist 1세그먼트 동일 augmented system을 고정해 higher precision 또는 독립 수치 도구로 forward error와 conditioning을 확인하는 것이다. 정규화 증대·pin 추가로 차이를 숨기지 않는다. 이 확인 전 GPU feasibility 승인은 보류한다.

CPU double의 storage/시간 개선 가능성은 관측했으나 GPU register/spill, float conditioning, 병렬화와 실제 시간 예산은 미검증이다. 현재 chain sparsity는 향후 inter-strand contacts를 포함하지 않는다. 5초 dynamics gate, 실시간 GPU 성능, UI/마우스/resize 및 캡처는 이번 CPU 작업에서 실행/검사하지 않았다. 기존 production 상태는 그대로 실행 가능하다.
