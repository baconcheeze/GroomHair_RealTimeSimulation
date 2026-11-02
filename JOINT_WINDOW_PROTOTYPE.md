# CPU joint x–q window prototype

**분류: no meaningful improvement. GPU 포팅 NO-GO.**

한 가지 가설만 시험했다: 위치–회전 mixed block을 유지한 local Gauss-Newton 보정이 sequential P/Q 및 mixed-block 제거군보다 첫 실패를 더 잘 줄이는가. 이번 정책/창 크기/32회 범위에서는 **그 가설을 지지하지 못했다.** 에너지와 일부 잔차는 개선됐지만 주 fixture의 위치 오차와 reference-directed motion은 개선되지 않았다. 5초 groom의 해결책이라고 주장하지 않는다.

원본: [artifacts/joint-window/20261030T130153Z](artifacts/joint-window/20261030T130153Z/), [RESULT.json](artifacts/joint-window/20261030T130153Z/RESULT.json), [전체 error-versus-work CSV](artifacts/joint-window/20261030T130153Z/error-versus-work.csv). 기존 GPU solver·기본값·연구 후보·assets·expected outputs는 변경하지 않았다.

## 입력과 baseline 재현

DIAGNOSIS.md, matched 결과, fixture/provenance, 기존 double manifold reference 및 Jacobian 검증 코드를 읽고 재사용했다. 주 입력은 source **9852**, guide index **1023**, 첫 substep, 32 vertices의 원래 HFD다. **kick을 seed로 재생성하지 않았다.** 저장된 initial x/q, y, 물성, h, 경계 배열을 그대로 승격해 사용했다.

| 재현 항목 | 이번 CPU 재현 |
|---|---:|
| 저장 GPU vs 동일 순서 double 32 RMS | 0.0733671 µm |
| 저장 GPU vs 저장 coupled reference RMS | 116.712654 µm |
| GPU q에서 exact-position polish vs reference RMS | 115.279663 µm |

[baseline-reproduced.json](artifacts/joint-window/20261030T130153Z/inputs/baseline-reproduced.json)에 저장했다. 단순 소수점 일치 대신 위치 크기에 맞는 1–50 nm 재현 허용범위를 사용했다. 기준해는 DIAGNOSIS에서 residual/refinement로 자격을 확인한 **국소 비교해**이며 비선형 전역 최적해가 아니다.

h=0.008333333767950535, SS=1, BT=9.999999747378752e-6, root x/q pin=true를 유지했다. 중력/예측을 다시 계산하지 않고 **저장 y**를 사용한다. 원래 solver 중력·변환·v/history는 복사한 `primary-original.hfd`에 그대로 있다. 추가 damping, clamp, 물성 변경, rotational inertia, 새 pin은 없다.

물리 objective는 다음 그대로이며 primary F_ref는 약 **5.08907861e-9 J**다.

```text
Σv .5*m[v]/h²*|x[v]-y[v]|²
+ Σs .5*(SS*L0[s])*|(x[s+1]-x[s])/L0[s]-Rotate(q[s],e3)|²
+ Σb .5*(4*BT/L0[left])*|conjugate(qLeft)*qRight-phi*qRestRelative[b]|²
phi = runtime closest pole
```

## 블록과 순회

두 segment 창 i의 자유 변수는 `x[i+1], q[i], q[i+1]`: **3+3+3=9 DOFs**. 실제 root pin DOF는 제거하므로 첫 창은 6 DOFs다.

1. prologue: 자유로운 x[0]만 방문. pinned이면 명시적 no-op.
2. root-to-tip overlapping windows: 내부 위치와 해당 segment들의 q를 한 시스템으로 보정.
3. tail: 자유 tip x[S]와 마지막 q[S-1]의 공동 블록.
4. 한 segment는 x[0], x[1], q[0]의 자유 DOF를 한 블록으로 처리.

일반 width w에서는 위치 `x[i+1..i+w-1]`, 회전 `q[i..i+w-1]`가 active다. stride=1, overlap=w-1이다. 최대 내부 DOFs는 w=2/4/8에서 **9/21/45**다. 임시 창 endpoint와 halo는 그 방문 중만 고정되며 영구 pin이 아니다. 전체 strand를 active로 바꾸지 않았다.

active 위치의 inertia, 모든 incident SS, 내부 BT와 active–fixed 경계를 넘는 BT를 포함했다. 매 방문 최신 state로 다시 linearize한다. touching-term H/g가 기존 전체 objective evaluator에서 같은 active columns를 선택한 H/g와 일치함을 검사했다. root/tip/one-segment 및 모든 pin 조합의 **모든 자유 DOF 방문**을 확인했다.

한 방문은 **Jacobian 1회, accepted GN step 최대 1회**다. 실패 시 같은 linearization에서 제한된 regularization/line trial만 수행하며 local convergence까지 몰래 반복하지 않는다.
32 macro-sweeps 동안에도 같은 h/y의 한 고정 문제이며, 32 physical substeps를 진행하는 것이 아니다.

## manifold, Jacobian, mixed system

기존 reference와 같은 Hamilton xyzw, solver 좌표의 **spatial/left 물리 rotation vector θ [rad]**를 사용했다.

```text
dq = [sin(|θ|/2) θ/|θ|, cos(|θ|/2)]
qNew = normalize(dq * q)
δRotate(q,e3) = θ × d
δC_ss = (δxRight-δxLeft)/L0 + [d]cross θ
```

small-angle 분기는 `[θ/2,1]`을 정규화한다. half-angle optimization coordinates가 아니다. BT는 **full relative quaternion residual**이며 log curvature로 바꾸지 않았다. 한 Jacobian에서 pole을 고정하고, trial acceptance는 진짜 closest-pole F로 판단한다.

float fixture q에는 약 1e-7의 norm 편차가 있다. active q의 unit chart는 **복사본**에서 만든다. 정규화 효과까지 포함한 trial 전체가 이전 실제 F를 낮출 때만 commit한다. 실제 initial 배열이나 pinned q를 사전 변경하지 않았으며 full/ablation에 같은 정책을 썼다. 이 횟수도 raw work에 기록했다.

`H=JᵀJ`, `g=Jᵀr`를 구성하고 full A/B/C를 작은 dense Cholesky로 함께 풀었다. 기존 reference의 scaled Cholesky 방식을 full local matrix에 적용했으며 inverse를 만들지 않았다. column scale은 `sqrt(Hjj)`로, position [m]과 angle [rad]의 임의 norm 혼합을 피했다. raw residual/physical weighting은 바꾸지 않았다.
이는 approximate nonlinear block step이며 exact joint minimum이나 논문의 closed-form single-q update가 아니다. 검증된 robust single-q helper는 수정하지 않았고 기존 baseline으로 유지했다.

ablation은 **B와 Bᵀ만 0**으로 만든다. 변수·r·g·A·C·diagonal scale·globalization은 같다. raw B Frobenius 최대값은 width 2/4/8에서 약 2.000/3.464/5.292로, 원래 mixed coupling이 실제로 존재함도 확인했다. ablation의 해당 work 값은 **제거 전 B**의 크기다.

정책은 실행 전 [PREDECLARED.json](artifacts/joint-window/20261030T130153Z/PREDECLARED.json)에 고정했다: scaled-system λ=1e-8, 실패 시 ×10, 최대 8 linear solves; alpha=1..2^-15의 최대 16 trial. **acceptance는 slack 없는 실제 F 감소**다. λ 항은 reported physical F에 추가하지 않았다. 무감소는 reject이며 converged로 표시하지 않는다. negligible decrease도 별도 집계했다.

최종 검증은 residual Jacobian FD **359,568 scalar comparisons**, 최대 normalized error **3.67e-11**이었다. analytic H/g와 독립 전체 evaluator 비교 및 crossing-BT/coverage도 통과했다. tie fixture에서는 frozen-pole Jacobian과 true closest-pole acceptance를 별도 probe했다. full의 6 trials 중 2회 거절, 4회 accept; near tie 1회, 실제 pole change 0회였다. 이 tie probe를 qualified convergence 사례로 부르지 않는다.

## 주 fixture: 정확도 대 sweep

사전 screening target은 RMS **약 11.6713 µm 이하**(약 10× 감소), ≤32 macro-sweeps다. 새로운 최종 물리 acceptance gate가 아니다. 표는 RMS µm이며 모든 curve를 보존했다.

| macro | sequential | joint w2 | B=0 w2 | joint w4 | B=0 w4 | joint w8 | B=0 w8 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 |116.766|116.814|116.719|117.127|116.606|120.217|116.506|
| 2 |116.760|116.837|116.646|117.481|116.505|122.358|116.535|
| 4 |116.754|116.883|116.572|118.078|116.446|124.804|116.768|
| 8 |116.745|116.968|116.565|118.999|116.480|127.168|117.293|
| 16 |116.730|117.120|116.689|120.209|116.593|129.189|117.968|
| 32 |116.704|117.368|116.938|121.455|116.695|130.254|118.405|

수렴 curve는 w2 실패 후 w4, 다시 실패 후 w8로만 확장했다. generic Jacobian/coverage 검증은 세 width를 검사했다. width별 다른 λ나 수리 방법을 탐색하지 않았다.

| 32 macro 결과 | F−F_ref [J] | 최대 위치 오차 [µm] | position correction [m] | q stationarity |
|---|---:|---:|---:|---:|
| sequential |1.15783e-9|176.553|3.93888e-8|2.99691e-6|
| joint w2 |1.02385e-9|176.632|2.36918e-8|1.00393e-6|
| joint w4 |7.20865e-10|176.665|3.29567e-8|3.84862e-7|
| joint w8 |6.05213e-10|177.932|4.16006e-8|9.09356e-7|

에너지와 q 잔차 감소를 위치 정확도 개선으로 오인하면 안 된다. reference tip 이동 벡터는 **(-57.852, 173.536, -15.600) µm**인데 joint w8은 **(11.538, 14.670, 24.482) µm**다. 주된 reference-directed motion을 재현하지 못한다. 각 checkpoint의 tip vector/error, quaternion 차이, axial strain, SS vector error, 최종 잔차는 CSV/curve.json에 모두 있다.

## 작업량과 CPU 시간

각 curve는 별도 프로세스의 **instrumented CPU wall time 1회 측정**이다. local step과 trial CSV 작성은 포함, checkpoint diagnostics/입출력 준비와 reference 생성은 제외했다. median/P95 또는 GPU 시간으로 부르지 않는다. sequential Q는 GPU ordinary helper가 아니라 기존 독립 double oracle이므로 CPU 시간으로 GPU 성능을 추론할 수 없다.

| 방법 @32 | Jacobian/linear solves | active DOFs 처리 | CPU ms | RMS µm |
|---|---:|---:|---:|---:|
| sequential | 0 / 0; P 1,984 + Q 1,920 |11,712|19.323|116.704|
| joint w2 |992 / 992|8,736|11.425|117.368|
| B=0 w2 |992 / 992|8,736|12.884|116.938|
| joint w4 |928 / 928|18,912|22.235|121.455|
| B=0 w4 |928 / 928|18,912|20.689|116.695|
| joint w8 |800 / 800|34,656|86.685|130.254|
| B=0 w8 |800 / 800|34,656|93.071|118.405|

동일 sweep은 동일 비용이 아니다. 예를 들어 w8의 8 macro도 CPU 약 20.58 ms와 8,664 active DOFs 처리에 RMS 127.17 µm로, sequential 32 macro의 약 19.32 ms / 116.70 µm보다 좋지 않았다. 각 joint visit은 행렬 조립/solve와 full physical F 평가를 하며 scalar P/Q 한 번과 비용이 다르다.

주 fixture의 joint/ablation은 모두 첫 λ=1e-8, alpha=1에서 accept했다. 거절 0은 convergence 인증이 아니다. 반면 near-optimal one-segment synthetic에서는 무감소가 반복되어 최대 29 visits / 3,712 trials를 reject했고 이를 숨기지 않았다. 전체 trial λ/alpha/사유는 각 `trials.csv`에 남겼다.

## 고정된 다른 입력과 reference 한계

입력은 첫 joint curve 전에 해시를 고정했다. 두 추가 문제는 원래 frozen substep **9**, 저장된 guarded **iterate 1**의 x/q와 저장 y를 사용한다. rest/physical arrays만 변경되지 않은 native assets/초기화에서 복원했다. kick 재생성이나 새 dynamics 실행은 없다. 이들은 **이번 prototype에 대한 held-out**이며 이전 12회 실험에서는 train이었으므로 blind set이라고 부르지 않는다.

| source ID | sequential RMS @32 | joint w2 | joint w4 | joint w8 | B=0 w8 |
|---:|---:|---:|---:|---:|---:|
|14 (guide0)|14.867 mm|14.801 mm|12.632 mm|10.959 mm|9.820 mm|
|59 (guide5)|13.897 mm|13.874 mm|11.220 mm|8.836 mm|8.457 mm|

더 넓은 창은 이 두 문제를 일부 개선했지만 mixed coupling의 위치 정확도 이점은 일관되지 않았다. 두 reference는 기존 double 구현으로 residual/refinement 자격을 확인했으며 추가 refinement 최대 위치 변화는 각각 **12.65 / 8.94 nm**다. reference는 여전히 비선형 전역 최적해라고 주장하지 않는다.

N=2/5, nonuniform lengths, 네 root pin 조합과 부호 조합의 synthetic도 사전 고정했다. 모든 free DOF 방문/핀 불변성과 Jacobian 검사는 통과했다. 모든 qualified synthetic reference와의 결과를 저장했지만 일반적인 성공은 아니다: N5 unpinned의 joint w2 RMS는 약 **355 µm**였다. 한 segment의 회전 null mode에서는 reference q가 유일하지 않을 수 있으므로 quaternion 차이만으로 실패 판정하지 않는다.

**최종 판단:** 실제 joint mixed block을 구현했음에도 이 제한된 family/policy는 screening target을 충족하지 못했다. “더 큰 창만 있으면 해결”이라고도 주장하지 않는다. 다음 행동은 **GPU 포팅을 진행하지 않고 이 CPU 실험을 negative control로 보존하는 것**이다. 새로운 설계/추가 실험을 자동 시작하지 않는다.

## 재현과 보존

```powershell
cmake -S diagnostics/joint -B build/joint-cpu -G "Visual Studio 17 2022" -A x64
cmake --build build/joint-cpu --config Release --target JointPrototype
& .\build\joint-cpu\bin\Release\JointPrototype.exe --mode run --input artifacts/joint-window/20261030T130153Z/inputs/primary.jwp --out artifacts/joint-window/20261030T130153Z/repro --width 2 --method joint
# --method sequential / ablation, --width 4 / 8로 기록된 제한 비교를 재현할 수 있다.
& .\build\joint-cpu\bin\Release\JointPrototype.exe --mode verify --input artifacts/joint-window/20261030T130153Z/inputs --out artifacts/joint-window/20261030T130153Z/repro-verify
python diagnostics/joint/summarize.py  # screening 미달로 exit 1
```

전체 raw state/checkpoint/trial/curve, input hashes, predeclared policy, reference 자격은 결과 디렉터리에 있다. 최종 바이너리는 verify 출력만 보강했으며 `final-binary-smoke`에서 기존 curve의 모든 수치/카운터가 CPU 시간 외에는 정확히 같은 것을 확인했다. 최초 curve 실행 바이너리와 당시 소스도 `runner-curves.exe`, `curve-source/`에 보존했다.

생산 소스/셰이더/default/assets/exe 및 이전 연구 패키지 해시, 고정 입력 해시는 전후 일치한다. HLSL 포트, GPU layout, UI 옵션, collision, 전체 5초 suite 실행은 하지 않았다. GPU 측정/성능 향상 또는 production 안정성 개선을 주장하지 않는다.
