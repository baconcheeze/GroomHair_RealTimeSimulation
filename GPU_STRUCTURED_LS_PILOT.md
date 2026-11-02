# GPU Structured LS Pilot

## 제한된 판정

- **GPU FP64 linear accuracy: 통과.** 실제 RTX5070Ti에서 Debug/Release G64 각83/83(1/2/3/5 correction).
- **FP32-factor/FP64-residual: 이번 설정에서 부적합.** 5회 후58/83, pivot/factor 실패16개와 추가 정확도 실패9개.
- **Hybrid nonlinear equivalence: 통과.** CPU nonlinear / GPU linear의12/12, matching native16-step screening도 통과.
- **Batch throughput: 직접 측정.** 세 종류의 real linear system을 복제한2,839개 batch이며 groom2,839가닥의 dynamics 검증이 아니다.
- **남은 GPU 단계:** physical residual/Jacobian/scale 조립, nonlinear trial control, 완전한 dynamics는 미포팅. production-ready/all-float/full-groom/frame-budget 승인 없음.

## 하드웨어·컴파일러

장치: **NVIDIA GeForce RTX 5070 Ti**, DXGI driver32.0.15.9186. DoublePrecisionFloatShaderOps=true, 최대 shader model6.8(0x68), timestamp frequency1,000,000,000Hz. software adapter는 건너뛰며 WARP/CPU fallback은 없다. Debug는 실제 debug layer=true였고 최종 모든 context의 error-level 메시지를 검사해0개였다.

```text
DXC=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe
dxcompiler.dll: 1.9(5184-21d28f72)(1.9.2602.17) - 1.9.2602.17 (21d28f727)

Profile=cs_6_0
Flags=-HV 2021 -Gis -O3
```

프로파일은 기존 cs_6_0, -HV2021 -Gis를 유지했다. Debug=-Od -Zi -Qembed_debug, Release=-O3. -Gis로 reassociation/contraction을 허용하지 않으며 실제 G64 DXIL에도 fma/float sqrt/double→float narrowing은 없었다. compiler.exe 및 dxcompiler.dll hash는 compiler.json, shader/source/DXIL hash는 manifest와 dxil-audit.json에 있다. SDK/driver/TDR/power/security 설정은 바꾸지 않았다.

## 산술/DXIL 감사

`sqrt(double)`을 그대로 쓴 UnsafeProbe는 double→float fptrunc, dx.op.unary.f32 sqrt, float→double fpext를 실제 방출했다. 추가 -Wconversion 빌드에서도 narrowing 경고를 수집했다. 따라서 double 변수 선언만으로 sqrt가 double이라고 가정하지 않았다.

DoubleMath.hlsli::DSqrt는 bit exponent로 입력을 normalize해 m∈[1,4)를 만들고, **double seed1.5와7회의 double Newton y=0.5(y+m/y)**만 사용한다. float seed는 없다. subnormal은2^1022로 scaling한 후2^-511로 보정한다. 0/−0, positive infinity, negative/NaN 처리도 분리했다. factor는 sqrt 호출 전 finite positive pivot을 검사한다. 모든 intermediate m/y/add/multiply는 DXIL double로 확인했다.

최종5021개 probe에는83개 시스템의 실제 CPU factor pivot(3.2014e-11~1.0000e+00), denorm_min/DBL_MIN/DBL_MAX/1e±300 등의 extremes, NaN/±inf/−0와 비이진 나눗셈이 포함된다. bit transport error0, comparison/finite flag error0, arithmetic classification error0; sqrt 최대 상대오차=2.2204e-16, add/multiply/divide 최대 상대오차0이었다. 완전 correctly-rounded sqrt 보증 대신 실제 helper 오차와 solver forward gate를 검증했다.

초기 확대 probe에서 +inf를 NaN으로 돌려주는 helper 경계 처리 오류1개를 발견해 고쳤다. 실제 finite pivot 경로의 수학은 바꾸지 않았고 최종 Debug/Release probe가 모두 통과했다. UnsafeProbe 실패와 초기 결과도 보존했다.

|DXIL entry|G64 double→float|float sqrt|double div instructions|
|---|---:|---:|---:|
|Probe|0|0|19|
|UnsafeProbe|1|1|10|
|Form|0|0|0|
|Factor|0|0|11|
|Initial|0|0|2|
|Correct|0|0|2|

## GPU layout·소유권

선택한 구현은 **한 thread가 complete system 하나를 serial 소유**한다. numthreads(1,1,1), dispatch당 최대100 systems이며 독립 system은 disjoint offsets를 사용한다. cooperative/PCR/다른 factorization은 추가하지 않았다. 초기 구현이 성능 최적이라는 주장은 하지 않는다.

|항목|실제 byte layout|
|---|---|
|Meta|16 uint32=64B; work offset36, map offset52|
|Block|size/start uint32 pair=8B|
|Local row|88B: count/pad(8), columns6(24), b64@32, coefficient64[6]@40|
|Active map|station/slot/kind/physical scalar index,16B|
|Bands/factors|station당6×6 doubles,288B, diagonal/lower 별도|
|Status|8 uint32=32B, code/stage/index/corrections/lost/stagnation/done|

C++ sizeof/offsetof static assertions 및 exact-bit probe로 검증했다. scale/active maps도 upload했다. physical root pins는 upstream layout에서 제거하며 zero-active root와 terminal position-only block을 지원한다. CPU164452B object를 shader local/shared 배열로 memcpy하지 않았다.

scratch는 L,C,u,y,du,s,5개 correction-size scalar,status,별도 formed diagonal/lower/RHS로 나뉜다. 32-vertex/183-DOF 한 system은 실제 used scratch44256B다. source상의 local array와 groupshared는0이며 scalar Meta/주소/loop temporaries만 둔다. 실제 register/spill 수는 측정하지 않았고 register residency를 주장하지 않는다. G32F64R도8B slot에4B float payload를 쓰므로 이번 실험에는 compact-factor memory 절감이 없다.

## factor·원래 LS refinement

목표는 저장된 A64/b64의 min||A64u−b64||²다. M64/c64는 초기·correction solve용 근사 normal system이다. regularization row가 A에 실제 coefficient로 포함돼 있으므로 lambda를 재가산하지 않는다.

Factor→Initial→Correct×k를 GPU 안에서 실행한다. factor는 요청 solve당1회이고 모든 correction이 재사용한다. rho=b−Au와 COMPLETE Aᵀrho를 binary64로 누적하고 같은 factor로 du를 구해 u+=du도 binary64로 한다. c−Mu로 바꾸지 않았다. fixed budgets0/1/2/3/5는 각각 같은 입력을 새로 재생한 complete solve이며, correction 사이 CPU readback이나 refactor는 없다.

오류 code/stage/index는 sticky하다. nonpositive/invalid pivot, nonfinite factor/solve/residual/update를 명시 검출한다. lost updates와 완전 정체도 별도 count하며 convergence로 바꾸지 않는다. pivot clamp/λ증가/extra pin/rank truncation/CPU fallback은 없다.

## G64 83-case 결과

|configuration / correction|stored-M G64 pass|row-formed G64 pass|
|---|---:|---:|
|Debug / 0|74/83|77/83|
|Debug / 1|83/83|83/83|
|Debug / 2|83/83|83/83|
|Debug / 3|83/83|83/83|
|Debug / 5|83/83|83/83|
|Release / 0|74/83|77/83|
|Release / 1|83/83|83/83|
|Release / 2|83/83|83/83|
|Release / 3|83/83|83/83|
|Release / 5|83/83|83/83|

0-correction의74/83은 보존된 원래 오차를 남기는 control이며 pilot 전체 실패로 해석하지 않았다. 최종 output binary64를 CPU에서 같은 column scale로 unscale하고 기존 position/raw physical rotation-vector/scaled increment/LS stationarity gate를 적용했다. tangent-only gate로 바꾸지 않았고 factor bitwise 일치를 요구하지 않았다.

|Release G64 corrections|position max error|raw angle max error|LS stationarity max|factor/solve failures|
|---:|---:|---:|---:|---:|
|0|1.1350e-13|8.2216e-09|9.3891e-17|0|
|1|4.7979e-15|2.4643e-12|9.6161e-17|0|
|2|4.8069e-15|2.4655e-12|5.4675e-17|0|
|3|4.7991e-15|2.4645e-12|5.4675e-17|0|
|5|4.8017e-15|2.4662e-12|5.5446e-17|0|

모든 case/budget의 forward/scaled error, normal backward 및 absolute residual, original-LS stationarity, correction 크기, lost/stagnation, GPU-vs-NativeExtended 차이는 linear-results.csv와 개별 JSON에 있다. 작은 backward residual만으로 정확도를 승인하지 않았다.

GPU Form 실험은 **original local A rows로 rounded AᵀA/Aᵀb bands를 만드는 선형 조립**이다. physical residual/Jacobian GPU 조립이 아니다. GPU-formed 대 동일 순서 CPU-formed bands/RHS 차이는0이었다. 그러나 preserved M와는 최대6.661338147750939e-16 차이가 있으며 target A/b는 그대로다. 이 두 실험을 같은 normal-matrix 입력이라고 섞지 않았다.

## G32F64R 음성 결과

A/b·rho·transpose accumulation·u·최종 unscale는 binary64이고 M 입력의 factor 시점 rounding/factor/triangular solve만 binary32다. RHS는 correction-solve 경계에서만 binary32로 변환한다. 기존 λ를 그대로 사용했고 FP64 factor fallback을 하지 않았다.

|correction|Debug pass|Release pass|factor/solve failures|
|---:|---:|---:|---:|
|0|11/83|11/83|16|
|1|25/83|25/83|16|
|2|36/83|36/83|16|
|3|58/83|58/83|16|
|5|58/83|58/83|16|

5회 후58/83, factor 실패16개와 code0이지만 정확도 미달9개다. 실패한 fixture를 denominator에서 빼지 않았다. mixed-failures.json에 모든 checkpoint ID/n/code/stage/index가 있으며 각 실패의 input.bin/workspace.bin도 저장했다. 실패 후 solution slot은 유효해가 아니므로 그 forward metric을 성공 근거로 쓰지 않는다. Mixed CLI는 최종 accuracy 미달을 exit1로 반환했고 이는 passing G64를 무효화하지 않는다. mixed timing은83개 heterogeneous case의 단발 trace만 있으며 passing G64 throughput과 동등한 성공 timing으로 제시하지 않는다.

## Hybrid nonlinear 및16-step

CPU는 기존 physical rows/Jacobian·scale/λ 정책을 유지하고 GPU만 block factor/linear refinement를 수행한다. callback은 기존 native nonlinear adapter에서 linear solve 호출만 치환한 별도 hybridMinimize다. source function의 Mode 인자는 API compatibility용이며 GPU는 항상 G64다(QD shader가 아님). CPU unscale/retraction/physical-energy trial acceptance/qualification은 그대로다.

|configuration / mode|qualified|native와 position max 차이(m)|q max 차이(rad)|
|---|---:|---:|---:|
|Release / hybrid5|12/12|4.8572e-17|1.4037e-14|
|Release / hybrid1|12/12|3.7367e-17|5.0824e-14|
|Debug / hybrid5|12/12|4.8572e-17|1.4037e-14|
|Debug / hybrid1|12/12|3.7367e-17|5.0824e-14|

source9852의 kicked first-failure 문제는 기존 JWP arrays 그대로 포함했고 seed kick을 새로 만들지 않았다. old erroneous increment bit-for-bit 재현을 요구하지 않았지만 전체 raw quaternion 차이는 남겼다. exact per-case objectives/residuals/accepted/rejected는 hybrid-comparison.json에 있다. 각 새 linearization/λ trial은 새 matching A/b/M/c를 upload하고 factor를 새로 만든다. CPU/GPU transfer+wait가 있는 validation hybrid이지 end-to-end GPU/실시간 구조가 아니다.

hybrid5의12개 문제에는47개 linear calls가 있었다. 각 call의 GPU timestamp, upload+wait wall, solve/readback+wait wall을 linear-calls.json에 분리했다. GPU kernel time에 roundtrip wall을 섞지 않았고, CPU nonlinear 전체 wall에는 조립/전송/대기가 모두 포함된다.

16-step은 native screening과 동일한 rest-start/zero-v/zero history/guarded(3), 기존 float h=1/120 값·SS/BT·pins·gravitySolver=(-9.699665069580078,0.06558697670698166,-1.4657117128372192)를 사용한다. y는 evolving x/v로 step마다 새로 만들고, q warm-start 및 v=(xNew−xOld)/h 정책을 유지한다. GPU는 linear solve만 맡았다.

|source / Release16|native trajectory RMS(m)|max(m)|velocity max difference|q max difference|
|---|---:|---:|---:|---:|
|primary|2.7138e-17|8.0921e-17|9.0067e-15|1.2559e-15|
|heldout-slot0|2.1305e-17|8.4718e-17|8.4327e-15|1.0450e-15|
|heldout-slot2|0.0000e+00|0.0000e+00|0.0000e+00|0.0000e+00|

Debug도 같은 기준을 통과했다. 0.1mm RMS/1mm max의 matching trajectory screening을 적용했으며 linear1e-9rad increment gate를 전체 궤적에 강제하지 않았다. 식/프레임이 다른 과거 궤적과 비교하지 않았다. 실제 velocity/tip motion/strain/에너지는 raw trajectory JSON에 있다.48개 physical substeps일 뿐5초/full-groom 검증은 아니다.

## GPU timestamp와 throughput

Release, resident input, fixed5 corrections, warm-up2 + measured7. queue frequency로 변환한 GPU milliseconds다. factor/initial/refinement/total을 분리했고, timestamp total은 모든 tile을 포함한다. GPU→readback copy 및 CPU map/memcpy는 total-device timestamp 뒤이며 host wall에 포함했다. input upload는 별도 wall이다. parsing/reference generation/score validation/file writing은 device timer 밖이다.

|systems / DOF|factor median/P95(ms)|initial median/P95|5 corrections median/P95|total median/P95|
|---|---:|---:|---:|---:|
|1 / 183|1.644/1.649|0.356/0.623|4.357/7.547|6.587/9.554|
|10 / 1830|1.646/1.650|0.360/0.635|4.183/7.306|6.165/9.269|
|100 / 18300|1.663/1.668|0.362/0.533|3.952/7.368|6.062/9.399|
|2839 / 519537|14.793/15.038|6.525/9.790|33.561/37.217|54.662/58.450|

|systems|upload+wait wall(ms)|solve/copy/map/wait wall median/P95|uninstrumented host wall(ms)|
|---|---:|---:|---:|
|1|0.367|7.266/9.794|9.597|
|10|0.436|9.050/9.590|6.253|
|100|2.295|9.841/10.813|10.662|
|2839|71.148|75.549/77.453|76.834|

분포는 frozen initial source9852/14/59의 round-robin **복제 systems**다. 2,839개의 독립적으로 검증한 groom strands라고 하지 않는다. n183, numthreads1, max100 groups/dispatch tile; full batch는29 tiles/stage와203 dispatches(1 factor+1 initial+5 correction)이다. 각 slot은 서로 다른 scratch/address로 write/read했다. 각 unique input에 full strict CPU gate를 적용하고, 반복 slot 전체를 certified same-input GPU output과 bitwise 검사했다. warmups 포함25,551 result slots와 추가 uninstrumented2,839 slots를 읽었다.

instrumented는5개 timestamp+resolve, uninstrumented는 동일 kernel/barrier/dispatch work에 query만 생략한1회 결과 검증이다. uninstrumented GPU 시간을 CPU wall로 바꿔 부르지 않았다. 단발 control로 timestamp 오버헤드를 정량 결론내리지 않는다. 혼합정밀도 실패를 제외한 유리한 subset throughput도 제시하지 않는다.

이 serial/global-scratch G64의 큰 batch linear solve만 이미 수십 ms다. 이 결과로 physical/nonlinear/dynamic full-step frame budget이 된다고 주장할 수 없다. 한 system 시간을 곱해 전체 groom 시간으로 추정하지 않았다.

## 실제 allocation

GPU logical scratch는 per32-vertex system44256B, input67904B다. scratch에는 형성 실험용 bands/RHS도 항상 예약해 포함했다. 일반83/hybrid case engine은 input256KiB, scratch128KiB를 재사용하며 per-correction D3D resource allocation은 없다.

|batch|input logical / committed|scratch logical / committed|
|---|---:|---:|
|case capacity|262144 / 262144 B|131072 / 131072 B|
|1|67904 / 131072 B|44256 / 65536 B|
|10|679040 / 720896 B|442560 / 458752 B|
|100|6790400 / 6815744 B|4425600 / 4456448 B|
|2839|192779456 / 192806912 B|125642784 / 125698048 B|

committed 값은 실제 생성한 resource desc에 대해 GetResourceAllocationInfo로 구했다. upload/readback도 logical payload와 같은 capacity로 별도 존재하며 committed bytes는 memory.json에 있다. timestamp readback은 별도64KiB allocation이고 queryheap/PSO/driver-internal memory·실제 register spills는 별도 미측정이다. 초기 scratch-zero upload는 fence 후 해제한다. CPU vector capacity나 native164452B를 GPU memory 추정으로 쓰지 않았다.

## 재현·분리 runtime

```powershell
cmake -S diagnostics/gpu-structured -B build/gpu-structured -G "Visual Studio 17 2022" -A x64
cmake --build build/gpu-structured --config Release --target GpuStructuredProbe GpuStructuredLSCandidate GpuStructuredMemory
cmake --build build/gpu-structured --config Debug --target GpuStructuredProbe GpuStructuredLSCandidate
$exe=Resolve-Path artifacts/gpu-structured/20261031T035307Z/runtime/Release/GpuStructuredLSCandidate.exe
$probe=Resolve-Path artifacts/gpu-structured/20261031T035307Z/runtime/Release/GpuStructuredProbe.exe
$data=Resolve-Path artifacts/gpu-structured/20261031T035307Z/runtime/inputs
& $probe $data artifacts/gpu-structured/replay/probe
& $exe replay $data artifacts/gpu-structured/replay/g64
& $exe formation $data artifacts/gpu-structured/replay/formation
& $exe mixed $data artifacts/gpu-structured/replay/mixed # expected exit1 for measured negative result
& $exe hybrid5 $data artifacts/gpu-structured/replay/hybrid5
& $exe hybrid1 $data artifacts/gpu-structured/replay/hybrid1
& $exe dynamics $data artifacts/gpu-structured/replay/dynamics16
& $exe benchmark $data artifacts/gpu-structured/replay/benchmark
```

runtime/Release를 working directory로 바꿔 executable-relative DXIL과 ../inputs만으로 probe/replay/hybrid5/dynamics를 실제 실행해 exit0을 확인했다. Python/Unity/DXC는 실행에 필요 없다. offline automation은 validate.py이며 PowerShell.ps1 실행정책 차단은 변경/우회하지 않았다.

최종 근거는 audited/final-Release와 audited/final-Debug다. 개발 중 host-wall helper edit의 C++ build 실패 후 stale executable이 실행된 구간을 발견했다. 해당 이전 결과는 최종 근거에서 제외했으며, 최종 두 설정은 빌드 exit check와 실행 binary/DXIL SHA 검증 후 전수 재실행했다. 실패 로그·초기 probe 결과도 삭제하지 않았다.

보존 검증: production/default/asset 관련90개, NativeExtended/historical run687개, 보존 research runtime37개 SHA 일치. GPU production shaders, application startup/defaults, materials/assets, Unity source는 수정하지 않았다.

다음 GPU 단계 검토를 위한 **정확도 근거**는 확보했다. 그러나 FP32 factor는 현 조건에서 부적합하고, 현재 serial G64 cost는 최적화되지 않았다. physical assembly/nonlinear GPU control/완전 dynamics/충돌·OGC·velocity field·reconstruction은 구현하지 않고 여기서 종료한다.
