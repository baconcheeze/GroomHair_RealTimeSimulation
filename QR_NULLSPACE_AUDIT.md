# Free-twist QR 불일치 수치 감사

## 결론

**주원인: multiple identified contributors — 정상행렬 형성 민감도 + 정상행렬 factorization/substitution 민감도.** 동결 binary64 입력에 대한 80/120자리 참조로 두 기여를 분리했다. 비교/retraction 결함이나 QR 구현 결함이 이번 gate 초과를 설명하지 않는다. 기존 band12와 structured block의 일치 및 12개 비선형 재현 결과는 유지된다. 이는 coupled-convergence 방법의 실패 판정이 아니다.

원래 strict 1e-9 rad 기준은 유지한다. 83개 비교 중74 pass, 2개 fixture에 속한9개 checkpoint fail 그대로다. **GPU 포팅 승인은 계속 보류**한다. 물리 모델·lambda·pins·정규화 정책을 바꾸거나 gauge-aware gate로 교체하지 않았다.

## 동결 자료의 출처 한계

이전 run에는 raw J/r/A/M/u가 저장돼 있지 않았다. 따라서 과거 실행의 정확한 행렬 bit를 복구·증명했다는 주장은 할 수 없다. 원래 저장된 JWP/state를 사용해 원래 코드를 계측 재실행했고, 12개 linear-equivalence.json 전체가 기존 파일과 byte-identical함을 확인했다. 그 재실행에서 실제 factorization 직전의 M/c, QR 직전의 A/b, unscale 전 u와 그 후 delta를 동결했다. 이는 재생성된 다른 물리 문제를 고정밀 참조로 삼은 것이 아니라, 재실행의 실제 binary64 입력을 동결한 감사다. 다만 역사적 원시 입력 부재는 남는 provenance 제한이다.

고정밀 첫 비교 A/B는 frozen/*.json의 bit pattern을 exact 값으로 읽는다. C만 frozen primitive J/r/D/lambda로 별도 재구성한다. 고정밀에서 물리 Jacobian을 새로 계산하지 않았다.

모든 double은 16자리 IEEE754 hex bit 문자열로 저장했다(행렬 row-major). J/r/D/lambda, A/b, M/c, H/gradient, x/q/y/rest/mass/계수, active mapX/mapQ, 세 solver의 u/delta, alpha=1 trial x/q/F, 각 solver 첫 승인 alpha/F/q가 포함된다. 원래 checkpoint 결과도 original-results/에 복사했다. SHA256은 hashes.json에 있다.

## 실패 checkpoint 전수

9개 기록이지만 선형 입력의 중복을 제거하면 7개 문제다. 각 fixture의 original initial과 reference phase0 iter0가 같은 입력이다. 각 checkpoint의 실제 lambda를 사용했다. SS-only N=2: physical J는9×n, augmented A는(9+n)×n이다. pins0은 root x/q 모두 free(n=9), pins1은 root x pin, q free(n=6)다.

|fixture / checkpoint|n / A 크기|실제 lambda|기존 raw delta 차이(rad)|trial quaternion 거리(rad)|
|---|---:|---:|---:|---:|
|synthetic-2-pins0 / original initial|9 / 18×9|1e-08|4.8678e-09|4.8643e-09|
|synthetic-2-pins0 / preserved sequential32|9 / 18×9|1e-08|3.6219e-09|3.6193e-09|
|synthetic-2-pins0 / reference phase0 iter0|9 / 18×9|1e-08|4.8678e-09|4.8643e-09|
|synthetic-2-pins0 / reference phase0 iter1|9 / 18×9|2.0000000000000001e-09|7.2984e-09|7.2983e-09|
|synthetic-2-pins1 / original initial|6 / 15×6|1e-08|1.0403e-08|1.0395e-08|
|synthetic-2-pins1 / preserved sequential32|6 / 15×6|1e-08|1.5405e-09|1.5394e-09|
|synthetic-2-pins1 / reference phase0 iter0|6 / 15×6|1e-08|1.0403e-08|1.0395e-08|
|synthetic-2-pins1 / reference phase0 iter1|6 / 15×6|2.0000000000000001e-09|8.3609e-09|8.3609e-09|
|synthetic-2-pins1 / reference phase0 iter2|6 / 15×6|4.0000000000000007e-10|1.1831e-09|1.1831e-09|

## metric·mapping·retraction 감사

기존 Verify.h::stepDifference는 이미 unscale된 delta의 orientation 3-vector 차이 norm의 최댓값을 angle 필드에 넣는다. 즉 maxQRAngleDifference는 raw physical rotation-vector increment difference이며 quaternion state의 geodesic angle이 아니다. scaled u 차이도 아니다. 순서는 u→delta_j=u_j/D_j→3-vector norm이며 w는0이다. 따라서 acos는 이 gate에 전혀 등장하지 않는다.

apply는 theta의 norm을 물리각 radian으로 보고 dq=[sin(|theta|/2) theta/|theta|,cos(|theta|/2)], qNew=normalize(dq*q)를 사용한다. Hamilton xyzw, spatial/left 곱이며 half-angle 증분 혼동은 없다. mapX/mapQ의 -1은 pin 제거다. pin을 큰 가중치로 대체하지 않았다.

실제 저장된 trial q끼리의 거리는 normalize한 relative quaternion의 2 atan2(|xyz|,|w|)로 고정밀 측정했다. 부호동치는 abs(w)로 처리했다. 원래 hair::angle도 acos가 아닌 sign-aware chord/asin이다. 두 물리 측정값 모두 이번 gate 초과가 단순 각도 측정 결함이 아님을 확인한다.

실제 C++ QuaternionMath의 self/sign 및 tiny rotation probe: theta=0,1e-12,1e-9,1e-8. self와 q/-q는 모두0. stored quaternion 기준 120자리 atan2와 double chord/atan2의 최대 차이는 아래 정도다.

|입력 theta(rad)|double chord−HP(rad)|double atan2−HP(rad)|
|---:|---:|---:|
|0.0e+00|0.0000e+00|0.0000e+00|
|1.0e-12|2.8599e-17|-5.2612e-17|
|1.0e-09|2.8595e-17|4.9276e-18|
|1.0e-08|-1.5232e-25|1.6341e-17|

trial q의 double retraction과 120자리 동일 retraction 차이는 원시 결과에 따로 보존했다. gate 초과~1e-9~1e-8 rad와 달리 roundoff 수준이다. 작은 입력을 half-angle으로 오인하거나 sign으로 오판한 증거는 없다.

## 세 가지 고정밀 문제

Python 표준 decimal/libmpdec 4.0.0를 사용했다. Windows long double은 사용하지 않았다. context.prec=80,120 **decimal digits**이며 next_plus(1)-1=1e-79,1e-119를 실제 확인했다.

- A: frozen A64/b64를 exact 값으로 읽고 독립 Householder QR로 LS를 푼다.
- B: frozen M64/c64를 exact 값으로 읽고 partial-pivot Gaussian elimination으로 푼다.
- C: frozen J64/r64/D64/lambda64를 exact 값으로 읽어 K=J/D, A=[K;sqrt(lambda)I], M=KᵀK+lambda I, c=−Kᵀr를 고정밀에서 구성하고 QR로 푼다.

A의 QR 대 고정밀 Gram solve, C의 QR 대 고정밀 rebuilt-normal solve도 독립 확인했다. 정규화가 양수인 문제의 작은 singular value를 truncate하지 않았다. 13개 audit 입력(실패9+control4)의 80→120자리 scaled solution 차이 최대는 5.9330e-75다. 이 수치는 실제 해 정밀도 수렴 근거이며 반복 횟수만으로 reference를 부르지 않았다. 각 oracle의 residual 및 교차 solver 차이는 high-precision-v2/*의 oracleConsistency에 있다.

## 정량적 원인 분리

아래 각도는 physical delta 차이이며, formation=B−A, factor=기존 double band12−B, QR=double QR−A다. 벡터들은 부호를 가진 채 더해지므로 norm을 단순 합하면 안 된다. 전체 signed null/observable 분해는 error-decomposition.csv에 있다.

|fixture / checkpoint|formation(rad)|normal factor(rad)|normal total→A(rad)|QR→A(rad)|
|---|---:|---:|---:|---:|
|synthetic-2-pins0 / original initial|3.1079e-09|1.7600e-09|4.8678e-09|2.8525e-15|
|synthetic-2-pins0 / preserved sequential32|2.3387e-09|1.2832e-09|3.6219e-09|5.4913e-14|
|synthetic-2-pins0 / reference phase0 iter0|3.1079e-09|1.7600e-09|4.8678e-09|2.8525e-15|
|synthetic-2-pins0 / reference phase0 iter1|8.2430e-09|9.4462e-10|7.2984e-09|1.5111e-15|
|synthetic-2-pins1 / original initial|3.1225e-09|7.2804e-09|1.0403e-08|7.0712e-16|
|synthetic-2-pins1 / preserved sequential32|2.0929e-09|5.5242e-10|1.5405e-09|1.8069e-15|
|synthetic-2-pins1 / reference phase0 iter0|3.1225e-09|7.2804e-09|1.0403e-08|7.0712e-16|
|synthetic-2-pins1 / reference phase0 iter1|5.5335e-09|2.8274e-09|8.3609e-09|1.1665e-14|
|synthetic-2-pins1 / reference phase0 iter2|8.2670e-10|3.5638e-10|1.1831e-09|1.2169e-16|

예: pins1 initial은 formation≈3.1225e-9 rad와 factor≈7.2804e-9 rad가 같은 주요 방향으로 작용하여 총1.0403e-8 rad가 된다. pins0 phase0 iter1은 기여가 일부 상쇄한다. independent double QR은 A에 대해 최대 5.4913e-14 rad이며, 자동으로 QR을 ground truth라 둔 것이 아니라 A의 120자리 해와 비교한 결과다.

A↔C 차이는 scaled division/sqrt(lambda) 등 augmented-input 형성 roundoff를 측정한다. B↔A 차이는 normal-matrix/RHS 형성의 효과다. 기존 normal solver↔B는 이미 만들어진 M64의 factor/substitution roundoff다. 물리 residual/Jacobian의 평가 오차는 이 동결입력 감사로 측정하지 않았고 이들과 혼동하지 않는다.

## 스펙트럼과 null 방향

|fixture / checkpoint|sigmaMin(K64)|sigmaMin(A64)|kappa2(A64)|lambdaMin(M64)|kappa2(M64)|
|---|---:|---:|---:|---:|---:|
|synthetic-2-pins0 / original initial|4.7200e-21|1.0000e-04|1.9986e+04|1.0000e-08|3.9944e+08|
|synthetic-2-pins0 / preserved sequential32|1.6584e-21|1.0000e-04|1.9986e+04|1.0000e-08|3.9944e+08|
|synthetic-2-pins0 / reference phase0 iter0|4.7200e-21|1.0000e-04|1.9986e+04|1.0000e-08|3.9944e+08|
|synthetic-2-pins0 / reference phase0 iter1|9.5174e-22|4.4721e-05|4.4717e+04|2.0000e-09|1.9996e+09|
|synthetic-2-pins1 / original initial|6.6750e-21|1.0000e-04|1.7304e+04|1.0000e-08|2.9944e+08|
|synthetic-2-pins1 / preserved sequential32|7.1531e-21|1.0000e-04|1.7304e+04|1.0000e-08|2.9944e+08|
|synthetic-2-pins1 / reference phase0 iter0|6.6750e-21|1.0000e-04|1.7304e+04|1.0000e-08|2.9944e+08|
|synthetic-2-pins1 / reference phase0 iter1|9.9862e-23|4.4721e-05|3.8724e+04|2.0000e-09|1.4996e+09|
|synthetic-2-pins1 / reference phase0 iter2|1.7098e-21|2.0000e-05|8.6590e+04|4.0000e-10|7.4979e+09|

전체 singular/eigenvalue 배열은 raw JSON에 보존했다. K64는 frozen A의 위쪽 residual rows, primitive K는 고정밀 J64/D64다. 이 둘은 구분한다. primitive K의 exact null은 아래 rational 검증으로 확인했으므로 Gram/Jacobi 정밀도 바닥에 나타나는 1e-40/1e-60 정도 값을 실제 비영 singular value로 해석하지 않는다. K64의 작은 singular values는 80/120자리로 재현된다. 정규화 A/M에는 그런 rank truncation을 적용하지 않았다.

물리 후보 nDelta=[0,Rotate(q,e3)]에 대해 실제 J*nDelta를 평가했다. scaled null 후보는 **D*nDelta를 정규화**했다. scaled u를 unscaled tangent에 바로 투영하지 않았다. 원시 JSON에는 각 backend-minus-A/B/C의 signed null 성분, 직교 성분, position/rotation error, updated tangent, true physical trial F 차이를 기록했다.

|fixture / checkpoint|‖J nDelta‖|normal→A null 성분|normal→A 직교 성분|tangent 차이|true trial F 차이(J)|
|---|---:|---:|---:|---:|---:|
|synthetic-2-pins0 / original initial|6.9867e-19|-1.0070e-10|2.8998e-12|3.1846e-10|-1.3127e-14|
|synthetic-2-pins0 / preserved sequential32|3.6314e-19|-7.4812e-11|1.0514e-12|2.3571e-10|-9.8172e-15|
|synthetic-2-pins0 / reference phase0 iter0|6.9867e-19|-1.0070e-10|2.8998e-12|3.1846e-10|-1.3127e-14|
|synthetic-2-pins0 / reference phase0 iter1|5.2888e-19|-3.8872e-11|5.7336e-14|2.0131e-11|-2.3837e-17|
|synthetic-2-pins1 / original initial|6.9867e-19|-2.1491e-10|3.0714e-12|6.8248e-10|-2.8633e-14|
|synthetic-2-pins1 / preserved sequential32|9.3855e-19|-3.1739e-11|7.4461e-13|1.0127e-10|-4.3111e-15|
|synthetic-2-pins1 / reference phase0 iter0|6.9867e-19|-2.1491e-10|3.0714e-12|6.8248e-10|-2.8633e-14|
|synthetic-2-pins1 / reference phase0 iter1|1.1639e-19|4.4136e-11|3.0408e-14|2.2830e-11|2.6045e-17|
|synthetic-2-pins1 / reference phase0 iter2|4.6116e-19|-6.7158e-12|1.4067e-16|8.6524e-14|5.0770e-23|

이 표의 null/직교 성분은 scaled u 좌표이며 position은 m, rotation은 rad로 별도 저장한다. physical trial objective는 정규화 항을 추가하지 않은 동일 inertia+SS+BT다. trial alpha=1 및 각 solver 승인 alpha를 별도로 저장했다. 작은 tangent/F 차이가 full-quaternion gate 초과를 통과로 바꿔 주지는 않는다. null 선형 증분도 유한 크기 retraction에서 tangent에 작은 비선형 영향을 줄 수 있다.

원래 q와 double J 산술 roundoff 때문에 물리 tangent 후보는 약1e-19 수준 Jn을 보인다. 별도로 frozen J의 rotation skew block에서 정확한 null axis를 뽑고 **Fraction exact-rational arithmetic**으로 Jn=0을 증명했다. 9개 모두 true다. 그 축은 physical tangent와 약 roundoff 차이이고, C 해의 exact scaled-null projection은 120자리 roundoff 수준이다. 양의 lambda이므로 해는 유일하며 twist ambiguity로 regularized step 비유일성을 주장하지 않는다. M64의 formation roundoff가 원래 그 직교조건을 약하게 깨고, normal solve 오차가 추가로 증폭된다.

가장 큰 pins1 initial: exact-J-null projection은 C≈−2.7e-122, A≈−1.13e-18, B≈−6.455e-11(scaled u)다. 이로써 exact regularized LS는 null 방향을 자의적으로 택하지 않음을 독립 확인했다. 이 논리는 SS-only one-segment의 특수 대칭이며 일반 groom의 full quaternion 차이를 버릴 근거가 아니다.

normal solve의 작은 backward error(~double epsilon)는 kappa(M64) 약3e8~7.5e9에서 forward accuracy를 보장하지 않는다. error-decomposition.csv의 normwise/componentwise backward, 입력 크기로 정규화한 LS stationarity, 실제 forward error를 함께 확인했다. near-zero RHS는 입력 norm을 포함한 분모와 절대 residual을 함께 보존했으며 residual만으로 통과를 선언하지 않았다.

## 통제 사례

|입력|DOF|normal→A position(m)|normal→A rotation(rad)|QR→A rotation(rad)|
|---|---:|---:|---:|---:|
|primary / initial|183|1.5835e-16|1.4644e-15|3.7099e-16|
|synthetic-2-pins2 / initial|6|6.3829e-15|0.0000e+00|0.0000e+00|
|synthetic-2-pins3 / initial|3|1.7967e-20|0.0000e+00|0.0000e+00|
|synthetic-5-pins0 / initial|27|4.2745e-14|6.3565e-11|3.1338e-15|

primary pinned 32-vertex는 기존 qualified nonlinear 결과를 바꾸지 않고 초기 linear system만 감사했다. N2 pinned-q controls 및 N5 all-free control도 유지됐다. 추가 nonlinear trajectory나 5초 groom을 만들지 않았다.

## 변경 및 검증 상태

수학/metric/mapping/factorization의 구체적 코딩 결함은 발견하지 못했다. 따라서 solver repair는 하지 않았다. 새 진단 출력에서 raw rotation-vector difference와 retracted quaternion distance 이름을 분리했지만 원래 gate나 tolerance는 수정하지 않았다. 이 문제는 **double normal equations의 forward precision 요구를 만족시킬 수치 방법/정밀도 보완이 여전히 필요함**을 뜻한다. 작은 observable error를 이유로 기존 full-quaternion gate를 gauge-aware rule로 자동 대체하지 않았다.

원래 결과: 83/74 pass/9 fail. 재실행 결과: 그대로, exit1. 비선형 비교12/12 재현. 추가 감사는 실패를 설명했으며 strict 성공으로 바꾸지 않는다. 생산/설정/자산 등 90개와 보존 research runtime 37개 해시 일치. 기존 STRAND_BLOCK_SOLVE.md와 원래 실험 결과는 수정하지 않았다.

이후 검토 가능한 최소 방향은 동일 frozen systems에서 normal 형성/factor 단계의 추가 정밀도로 기존 각도 gate를 만족하는지 별도 승인된 CPU 실험으로 확인하는 것이다. runtime QR 구현, lambda 증가, 새 pin, 재료 변경 또는 GPU 포팅은 이번에 하지 않았다.

## 재현

별도 build tree의 Release CPU 전용 도구다. 프로젝트 루트 PowerShell:

```powershell
python diagnostics/qr-audit/generate.py
cmake -S diagnostics/qr-audit -B build/qr-audit -G "Visual Studio 17 2022" -A x64
cmake --build build/qr-audit --config Release --target QRFreeze QRMetricProbe
$env:QRAUDIT_OUT="$PWD/artifacts/qr-nullspace/replay-new/frozen"
& ./build/qr-audit/bin/Release/QRFreeze.exe --inputs artifacts/strand-block/20261030T141357Z/inputs --out artifacts/qr-nullspace/replay-new/replay --timing no
# 기존 strict 실패를 보존하므로 QRFreeze의 예상 exit code는 1
& ./build/qr-audit/bin/Release/QRMetricProbe.exe | Set-Content -Encoding ASCII artifacts/qr-nullspace/replay-new/metric-double.csv
python diagnostics/qr-audit/audit.py artifacts/qr-nullspace/replay-new high-precision-v2
python diagnostics/qr-audit/summarize.py artifacts/qr-nullspace/replay-new
```

새 audit는 Python 표준 라이브러리만 사용한다. 원본 historical raw matrix는 없으므로 최초 freeze를 복구했다고 오해하지 않는다. 이미 동결된 현재 입력만 다시 분석하려면:

```powershell
python diagnostics/qr-audit/audit.py artifacts/qr-nullspace/20261030T145943Z high-precision-recheck
```

현재 자료: [artifacts/qr-nullspace/20261030T145943Z](artifacts/qr-nullspace/20261030T145943Z/), 요약 SUMMARY.json, 전수 CSV, metric-checked.json, high-precision-v2/*, frozen/*, 보존/해시 manifest. 새 코드 diagnostics/qr-audit/*만 추가했다. 초기 계측 build/precision probe 개발 로그도 보존했다.

## 남는 한계와 정지점

분류는 오류를 숨기는 gauge 해석이 아니라 실제 forward error의 두 산술 기여다. frozen replay 외의 역사적 input-bit 완전 증명은 불가능하다. 실제 physical Jacobian 평가 자체의 고정밀 재평가, float32, GPU, 실시간 성능, 장기 dynamics는 검증하지 않았다. 이 감사는 기존 full-strand GN의 전역 최적성이나 5초 dynamics gate를 증명하지 않는다. production 동작과 연구 후보를 그대로 두고 여기서 종료한다.
