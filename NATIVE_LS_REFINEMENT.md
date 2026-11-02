# Native Structured LS Refinement 및 짧은 Dynamics Bridge

## 분리 판정

- **Native strict accuracy: 통과.** NativeExtended 83/83 선형, 12/12 비선형, 원래 동결13개 HP 통제 통과.
- **Compact storage: 구현·검증됨.** live solve는 local rows와 station bands만 사용한다. 고정 fixture의 dense-to-local 변환은 진단 전용이다.
- **Short dynamics: 통과한 screening.** 3가닥을16→60 substeps 실행했다. 전체 groom/5초 안정성 증명이 아니다.
- **CPU cost: in-process Release로 계측.** GPU, float32 또는 production-ready 판정으로 묶지 않는다.

## 실행 파일·library·재현

빌드 결과는 [NativeLSCandidate.exe](build/native-ls/bin/Release/NativeLSCandidate.exe)와 [NativeStructuredLS.lib](build/native-ls/Release/NativeStructuredLS.lib)다. 분리 실행 묶음은 [artifacts/native-ls/20261031T022012Z/runtime](artifacts/native-ls/20261031T022012Z/runtime/)이며 exe, 동결입력83개, JWP12개, 라이선스/자산 출처를 포함한다. 다른 working directory인 이 runtime 폴더에서 아래 replay/nonlinear/dynamics를 실제 실행해 모두 exit0을 확인했다.

```powershell
cmake -S diagnostics/native-ls -B build/native-ls -G "Visual Studio 17 2022" -A x64
cmake --build build/native-ls --config Release --target NativeLSCandidate
$exe = Resolve-Path artifacts/native-ls/20261031T022012Z/runtime/NativeLSCandidate.exe
$data = Resolve-Path artifacts/native-ls/20261031T022012Z/runtime/inputs
& $exe replay $data artifacts/native-ls/replay/linear
& $exe nonlinear $data artifacts/native-ls/replay/nonlinear
& $exe dynamics $data artifacts/native-ls/replay/dynamics
& $exe benchmark $data artifacts/native-ls/replay/timing
& $exe precision . artifacts/native-ls/replay/precision
```

exe 안의 solve에는 Python/pipe/subprocess/JSON/oracle lookup이 없다. 파싱·검증·쓰기·독립 참조 실행은 diagnostic driver가 solve 밖에서 한다. standalone 후보이며 app나 GPU readback/upload loop에 연결하지 않았다. Python summarize.py는 결과 평가용 오프라인 도구뿐이다.

## 산술·dependency

[QD v2.3.24](https://github.com/BL-highprecision/QD/tree/v2.3.24)의 dd_real/dd_inline/inline 헤더를 third_party/qd-2.3.24에 고정했다. LICENSE(BSD-3-Clause)와 COPYING을 보존했고, source hash는 dependency.json에 기록했다. 산술 본문은 upstream 그대로이며 qd_config/fpu는 x64/SSE2 static 구성 adapter다. 설치나 실행 중 다운로드가 없다.

NativeExtended는 nominal 약106-bit double-double **software** 산술이다. upstream numeric_limits는 보수적으로104 digits를 보고한다. 실제1+2^-105−1 probe가 2.4651903288156619e-32를 보존함을 실행으로 확인했다. IEEE binary128/hardware 가속이나 Windows long double로 표현하지 않는다.

|항목|정밀도|
|---|---|
|물리 residual/Jacobian, column scale, bands|binary64, 기존 수식/순서|
|factor 및 모든 triangular solve|기존 double block Cholesky 산술|
|NativeDouble r와 transpose scatter|binary64 control|
|NativeExtended r와 COMPLETE transpose scatter|고정 QD double-double|
|완전히 누적한 correction RHS|그 후 binary64로 반올림|
|u+=du, unscale, q retraction, nonlinear state|binary64|

목표는 항상 min||A64 u−b64||²다. frozen 모드는 실제 A64/b64/M64/c64의 hex bits를 그대로 읽으며 shortened decimal로 재생성하지 않는다. 해당 frozen bits는 예전 기록을 byte-identical하게 재현한 계측 replay에서 얻은 것으로, 역사적 raw bits 복구가 아니라는 한계를 유지한다. A의 regularization rows를 그대로 보존하며 lambda를 두 번 추가하지 않는다. M/factor는 preconditioner이고 목표를 normal system으로 바꾸지 않는다.

fixed5 correction을 유지했다. 같은 linearization/lambda trial의 factor1개로 초기 solve와5개 correction solve를 수행한다. 새 문제에만 refactor한다. stagnation/lost update는 기록하지만 자동 종료/수렴 판정이나 gauge projection에 사용하지 않는다.

## Frozen 및 보존 repaired 결과 비교

|correction 수|NativeDouble pass/83|NativeExtended pass/83|
|---:|---:|---:|
|0|74|74|
|1|83|83|
|2|83|83|
|3|83|83|
|4|83|83|
|5|83|83|

원래9개 strict 실패(2개 fixture,7개 distinct linear inputs)는 역사적 run에 그대로 있다. 후보의0회는 같은74/83을 재현했고, frozen u0는 이전 blockU와 bitwise 같았다. fixed5 Extended의 preserved decimal 후보 대비 최대 raw rotation-vector 차이는 0.0000e+00 rad였다. 필요한 기준은 기존1e-9 rad 및 position/scaled 조건이며 관측된1e-17 수준을 새 gate로 만들지 않았다.

오프라인 관찰상1회도 이번83개 gate를 통과했지만 후보 기본5회는 바꾸지 않았다. NativeDouble도5회 후 이번 gate를 통과했다. 따라서106-bit 정밀도가 이 고정 집합에 필수였다는 결론은 내리지 않는다. HP13개 A oracle 및 이전80자리 보정과의 각 mode/iteration 오차는 frozen-HP-comparison.json과 correction-curves.csv에 보존했다. 전체83개 HP oracle을 새로 풀어 root-cause 조사를 반복한 것은 아니다.

## Direct-local 구현과 검증

station z_i=[dx_i,dtheta_i], terminal=[dx_S], pin 성분은 실제 제거한다. Row는 최대6개 supported column을 고정 배열에 보관한다. SS/BT 수식은 src/AutonomousReference.cpp에서 가져온 emitter 그대로이고 vector row builder만 stack SmallRow로 치환했다. 모든 nonzero를 보존하며 sparsification tolerance는 없다. inertia/SS/BT physical row의 station span≤1을 검사하고 예상 밖 coupling이면 실패한다.

Workspace는 raw local rows, scaled original-LS rows, H의 diagonal/lower, scaled M의 diagonal/lower, factor L/C, RHS/scale/u/du/scratch, DD transpose scratch, trial state, layout metadata를 소유한다. local original-LS rows와 rounded normal bands는 별개다. lambda trial마다 실제 double sqrt(lambda) row를 정확히 한 번 추가한다. frozen converter는 저장 row coefficient를 다시 sqrt로 바꾸지 않는다.

live12개 초기 상태의 energy/H/gradient/RHS/scaled local row는 기존 dense diagnostic과 bit-exact였다. Au와 Aᵀr도 nonzero probe로 확인했다. 최대 관측 차이는 아래와 같으며 local row 내 support 순서와 dense column 순서에 따른 DD 마지막 자리 차이를 숨기지 않았다.

|검사|최대 차이|
|---|---:|
|Au|1.5407e-33|
|Aᵀr|3.8519e-34|

frozen83에서는 A/M 재구성 equality와 refined output을 확인했다. core NativeLS.cpp solve path에는 global dense J/A/H/M/L allocation이 없다. frozen JSON converter와 독립 기존 dense reference는 driver의 진단 경로로만 남아 있다.

## 같은 비선형 방법

물리 objective·Hamilton xyzw spatial/left physical-angle radian increment·closest pole·coefficient·pin·scale·lambda retry·strict physical-energy decrease·qualification을 유지한다. 기존1500 cap, retry16, line16, lambda1e-8→accepted*.2/rejected*10 및 기존 tolerance phase를 그대로 썼다. temporary trial state만 재사용했다. 초기 guess나 물성을 바꾼 보정이 아니다.

|입력|accepted|position max vs repaired(m)|q max vs repaired(rad)|qualified|
|---|---:|---:|---:|---|
|heldout-slot0|7|0.0000e+00|0.0000e+00|true|
|heldout-slot2|6|0.0000e+00|0.0000e+00|true|
|primary|3|0.0000e+00|0.0000e+00|true|
|synthetic-2-pins0|4|0.0000e+00|0.0000e+00|true|
|synthetic-2-pins1|4|0.0000e+00|0.0000e+00|true|
|synthetic-2-pins2|2|0.0000e+00|0.0000e+00|true|
|synthetic-2-pins3|2|0.0000e+00|0.0000e+00|true|
|synthetic-5-pins0|4|0.0000e+00|0.0000e+00|true|
|synthetic-5-pins1|4|0.0000e+00|0.0000e+00|true|
|synthetic-5-pins2|3|0.0000e+00|0.0000e+00|true|
|synthetic-5-pins3|3|0.0000e+00|0.0000e+00|true|
|tie-jacobian-only|5|0.0000e+00|0.0000e+00|true|

12/12 qualified이고 기존 forward/objective 비교도 통과했다. 원래 잘못 반올림된 free-twist q를 bitwise 강요하지 않았으며 raw q 차이를 별도로 남겼다. factor/refinement failure는0, fallback oracle은없다.

## 짧은 Dynamics 실험의 고정 설정

predeclared 입력은 source9852/14/59, rest x/q, zero v 및 zero acceleration, kick 없음이다. 과거 first-failure kick fixture는 frozen replay로 정확히 검사했고, 새 rest-start trajectory에 그 kick을 재생성하지 않았다. 이 세 가닥은 이미 관찰된 자료이며 blind data가 아니다.

initializer=guarded(3), 기존 연구 후보의 FullPrediction/InertialOnly 중 같은 inertia+SS로 선택한다. BT는 같은 q에서 상수이므로 guard 비교에 넣지 않는다. y=x+h v+h² g는 선택과 무관하게 고정이며 q는 warm start다. h는 저장된 float1/120을 double로 올린0.008333333767950535 s, SS=1, BT=9.999999747378752e-6, roots x/q 모두 pin이다. actual gravitySolver=(-9.699665069580078,0.06558697670698166,-1.4657117128372192) m/s²이며 기존 world-down의 neck-frame 변환값을 그대로 사용했다. 좌표를 다시 반사/회전하지 않았다.

각 새 step에서 y를 evolving x/v로 새로 만들고, 그 step 내에서는 고정한다. end에서 v=(xNew−xOld)/h, aHistory=(vNew−vOld)/h를 한 번만 재구성한다. guard3는 history를 소비하지 않지만 reset/update 정책은 보존했다. native x/v/q/predictor가 binary64라는 점을 명시하며 float GPU predictor와 bitwise 같다고 하지 않는다. damping/clamp/추가 pin은 없다.

먼저 기존 N2 pin4조합과 N5 zero-gravity/translation/free-fall16을 검사했다. 모든 invariant/finite/qualification 검사 후 실제 가닥16→60으로 확장했다.16-prefix reset/replay의 x도 bitwise 재현했다.60*h=0.5000000260770321 s다.

|analytic16|position max error(m)|velocity max error(m/s)|
|---|---:|---:|
|zero gravity rest|0.0000e+00|0.0000e+00|
|uniform translation|1.7347e-17|1.3235e-16|
|uniform free fall|8.7540e-17|1.5156e-15|

위 free fall은 continuous ballistic 해가 아닌 기존 discrete 식 v_n=v0+n h g, x_n=x0+n h v0+0.5 n(n+1)h²g와 비교했다. 이전5e-6m/2e-4m/s 기준을 유지했다.

|source|trajectory RMS to tighter(m)|max(m)|최대 abs axial strain|tip displacement(m)|
|---|---:|---:|---:|---|
|9852|2.3873e-08|1.1937e-07|1.2167e-05|[-0.0001392565736332077, 0.0008536303505144172, -0.00016539095075526755]|
|14|3.2646e-08|1.4501e-07|1.3288e-05|[-1.2812791571018e-05, 0.0002650734176311287, -6.855526319837421e-05]|
|59|2.1185e-09|2.5084e-08|9.7185e-06|[-4.131603015419816e-06, 2.5007685318149953e-05, -1.2415444740225545e-05]|

실제 motion과 velocity history가 있으며 tip을 동결해 strain을 낮춘 결과가 아니다. position pin error는 전 step0이고 q root도 exact target 유지, unit q/finite/고정 rest 검사를 통과했다. 각 step의 Fbefore/Fafter는 그 step의 고정 y에 대해서만 비교했다. 서로 다른 step의 F를 하나의 단조 objective로 취급하지 않는다. x/q/v/y/history, tip, strain/SS-vector error, kinetic energy, nonlinear iterations/corrections/residual/CPU duration은 trajectory JSON에 있다.

tighter trajectory는 **같은 새 backend의 self-refinement**(p1e-13m, q1e-11 vs 기본p1e-11m,q1e-9)다. 독립 구현이라고 부르지 않는다. 1/8/16/60 checkpoint는 preserved original band12 Gauss-Newton reference로 같은 guess/y를 새로 풀어 cross-check했고12/12 qualified였다. 이 호출은 timed candidate 실행 밖이다. 기존 물리 정의를 공유하는 독립 linear backend이지 전역 최적성 증명은 아니다.

독립 checkpoint 최대 position 차이: 4.1913e-11m, q 차이: 5.3014e-10rad. trajectory의0.1mm RMS/1mm max screening 기준은 통과했으나 linear1e-9rad gate를 전체 궤적에 적용하지 않았다.

기존 production-window의 저장 궤적에는 이번 rest/zero-v/guard3/gravity 조건과 exact match를 확인한 자료가 없어 비교에 재사용하지 않았다. 다른 gravity/kick 이력을 native 성능 개선 근거로 섞지 않았다. 이 부분은 historical comparison 미실행이다.

## 실제 CPU 비용

Release /fp:precise, in-process warm-up2 + sample9, median/P95(ms)다. parsing/logging/file write/reference 생성은 timer 밖이다. reusable workspace를 먼저 만들고 성공 경로 new/new[] allocation은0회였다. 첫 계측에서 invariant 문자열 임시 allocation을 발견해 const-char 검사로만 바꿨고 수학은 유지했다. 초기 결과/시간도 보존했다.

|frozen183 DOF residual mode / corrections|median(ms)|P95(ms)|allocations|
|---|---:|---:|---:|
|NativeDouble / 0|0.0084|0.0084|0|
|NativeDouble / 1|0.0119|0.0119|0|
|NativeDouble / 2|0.0154|0.0155|0|
|NativeDouble / 3|0.0189|0.0189|0|
|NativeDouble / 5|0.0283|0.0569|0|
|NativeExtended / 0|0.0084|0.0084|0|
|NativeExtended / 1|0.0273|0.0287|0|
|NativeExtended / 2|0.0460|0.0490|0|
|NativeExtended / 3|0.0647|0.0779|0|
|NativeExtended / 5|0.1024|0.1144|0|

|same primary nonlinear / corrections|median(ms)|P95(ms)|position RMS to reference(m)|
|---|---:|---:|---:|
|0|0.1571|0.1645|1.0169e-17|
|1|0.2156|0.2351|1.2391e-17|
|2|0.2767|0.3299|1.2391e-17|
|3|0.3369|0.3478|1.2391e-17|
|5|0.4622|0.5478|1.2391e-17|

|nonlinear5 component|median(ms)|P95(ms)|
|---|---:|---:|
|rows|0.0701|0.0760|
|bands|0.0543|0.0743|
|factor|0.0194|0.0227|
|initial|0.0066|0.0070|
|residual|0.2619|0.3046|
|corrections|0.0326|0.0359|
|trial|0.0037|0.0061|
|nonlinear|0.4620|0.5475|

같은 first dynamic substep의 median/P95=0.2854/0.2990ms, allocations=0.

각 nonlinear sample은 primary3 accepted iterations, factor3회, solve18회(initial3+correction15)다. component medians는 독립 marginal 통계이므로 합이 total median과 같을 필요가 없다. chrono 계측 overhead는 포함하며 GPU 시간으로 환산하지 않는다.

이전 Python/IPC prototype의 동일 primary A64/M64와5-correction median12.388ms에 대해 native fixed5 frozen median은 0.1024ms였다. 단, Python80-decimal과 QD106-bit의 precision 및 setup/IPC도 함께 바뀌었다. Python은 residual setup을 timed region에 포함했고 native는 workspace를 재사용한다. 따라서 이 차이를 순수 IPC 제거 효과나 GPU 속도로 부르지 않는다. 필요한 gate 통과 여부와 cost는 별도로 보고한다.

## 모든 workspace 저장량

|vertices / DOF|전체 Workspace bytes|factor-only bytes|caller state/target/v/history/previous payload|
|---|---:|---:|---:|
|2 / 3|6932|1152|352|
|5 / 21|22684|2880|928|
|32 / 183|164452|18432|6112|
|64 / 375|332548|36864|12256|
|128 / 759|668284|73728|24544|

Workspace 수치는 sizeof(객체), raw/scaled row vector capacity, H/M/factor6개의 block arrays, 모든 double scratch8개, DD scratch, trial x/q, layout/station mapping까지 포함한다. allocator header와 immutable caller input은 제외해 경계를 명시한다. primary는164452B이며 예전14.5x factor-storage 비율을 전체 workspace에 적용하지 않았다. payload 증가는 strand 길이에 선형이다.

caller mutable payload 열은 current x/q+target+previous+v+acceleration history의(6N−1)*32B다. source rest/material/topology는 별도 읽기 전용 입력이다. 진단용 JWP loader는 historical reference/gpu snapshots도 보관하며, dense frozen JSON parser 및 independent reference는 검증 모드에서 추가 메모리를 쓴다. 이를 live library solve의 O(N) 저장으로 숨기지 않는다. trajectory logger는 최대61개 full state/guess/v/y/history snapshot을 미리 할당하는 O(steps*N) 검증 저장이며 실제 library workspace가 아니다. standalone process RSS 전체가 위 표와 같다는 주장을 하지 않는다.

## 변경·보존·정지

새 파일은 diagnostics/native-ls/*와 third_party/qd-2.3.24/*, 본 문서 및 신규 run이다. production/source/runtime 90개, 이전 repair 산출물 886개, 보존 연구 runtime 37개 해시가 그대로다. 역사적9개 실패 및 모든 기존 보고서를 덮어쓰지 않았다. dependency/source/binary/input/output hash는 run manifest에 보존한다.

native strict accuracy, compact implementation, 짧은 dynamics, CPU cost를 각각 판정했고 여기서 종료한다. HLSL/float32/GPU 승인, production default 교체, material tuning, 충돌/OGC/재구성 및 전체 groom 장기 benchmark는 하지 않았다.
