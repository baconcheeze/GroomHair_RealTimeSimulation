# G64 메모리 레이아웃 A/B

판정: **resident improvement but conversion currently offsets it**.

현재 연결 경로는 **기존 system-major G32 / tile2839 / correction1을 유지**한다. WorkInterleaved32(B)는 작은 resident GPU 개선이 반복됐지만 CPU 변환을 포함하면 크게 느려졌다. WorkAndInputInterleaved32(C)는 B보다 추가 이득을 보이지 않았다. 두 후보는 별도 `G64MemoryCandidate`에서 재현 가능하며 production 코드·기본값·자산·기존 후보는 변경하지 않았다. **G32는 스레드 그룹 크기 32이며 FP32가 아니다.**

최종 근거: `artifacts/gpu-memory/20261031T054929Z/`. `STATUS.json`, `timing-summary.csv`, `study/timing.json`, `paired-variation.json`, `memory-summary.json`에 판정·원시 표본·메모리를 보존했다.

## 보존한 계산과 실행

한 스레드가 한 시스템 전체를 소유한다. 모든 비교를 G32/full tile2839로 고정했다. 기본 저장-M 경로는 Factor→Initial→Correct의 **3 dispatch**, correction5는 **7 dispatch**다. 2,839개에서는 단계당 89 groups다. 단계별 UAV barrier, 단일 direct queue, 전체 solve 완료 뒤 fence/readback을 유지했다. 단계 사이 CPU wait나 kernel fusion은 없다. row-formed 검증에는 별도 Form dispatch 하나가 추가된다.

A64/b64 목표, M64/c64, regularization 행, scaling/pin/active mapping, FP64 factor·삼각해·원본-LS residual·u 누적, DSqrt/division, per-system loop와 누적 순서를 보존했다. 수치가 아닌 저장 주소만 변경한다. shader의 `Block` 함수 이후 수치 본문은 원본과 문자 동일하고 DSqrt는 바이트 동일하다. 새 baseline의 Release Form/Factor/Initial/Correct DXIL도 기존 G32 DXIL과 **SHA256 동일**하다. `numerical-preservation.json`과 `baseline-dxil-equality.json`이 근거다.

목표는 저장된 `min ||A64*u-b64||²`이며 A64가 이미 regularization 행을 포함한다. lambda를 다시 더하지 않는다. M64/c64 factor는 초기/보정 해에 재사용하고 residual은 계속 `Aᵀ(b−Au)`로 평가한다. `c−Mu`로 대체하지 않았다.

장치: **RTX 5070 Ti**, driver **32.0.15.9186**, FP64=true, SM6.8, timestamp frequency **1,000,000,000Hz**. compiler는 SDK10.0.26100.0의 DXC **1.9.2602.17**, profile **cs_6_0**, 공통 `-HV 2021 -Gis`, Release `-O3`, Debug `-Od -Zi -Qembed_debug`다. MSVC19.44.35229/VS2022 x64를 사용했다. 새 precision variant, FMA/reassociation 허용, regularization 변경은 없다. Debug/Release DXIL에서 double→float narrowing·float sqrt·FMA가 없음을 검사했다.

## 원래 주소 패턴과 profiler 범위

`Packed.hlsl`의 `Metadata`, `AddressesOf`, `InD`, `WD/SD`, `Matrix`, `Solve`, row residual 루프를 실제 CPU Pack metadata와 대조했다. 아래는 같은 소프트웨어 저장 타일 안의 시스템 0→1에서 같은 논리 값을 접근할 때의 **바이트 주소 차이**다. source9852→14→59의 동일 크기 실제 선형 시스템을 사용했다.

|접근|A baseline|B work32|C workinput32|
|---|---:|---:|---:|
|Factor: M diagonal|67,840|67,840|8|
|Factor: L factor|44,256|8|8|
|Initial: RHS c|67,840|67,840|8|
|삼각해: C factor/y/du|44,256|8|8|
|Correct: row b/a[0]|67,840|67,840|8|
|Correct: u/transpose scratch s|44,256|8|8|
|최상위 Meta|64|64|64|
|정수 row count/indices, block/map|67,840|67,840|67,840|
|정수 status|44,256|32|32|

최상위 metadata는 이미 인접해 있었지만 FP64 배열은 인터리브되어 있지 않았다. 원래 입력 시스템 payload는 67,840B, metadata 포함 67,904B이며 scratch frame은 44,256B다. `codec/addresses.csv`에 시스템 0~33의 실제 주소를 기록했다.

저장 타일 경계인 시스템 31→32에서는 B/C의 L 주소 차이가 1,416,072B, C의 M/row-b는 1,565,000B다. 각 타일 안의 +8B와 경계 점프를 구분해야 한다. status는 명시적인 별도 uint32 AoS라 경계 차이가 1,415,328B다. 이것은 **오프라인 주소 추적**이며 cache transaction/DRAM traffic 측정이 아니다.

설치 목록에는 Nsight Systems2024.6.2, Compute2025.1, Visual Studio Edition2025.1이 있었지만 **Nsight Graphics는 없었다**(`profiler-inventory.json`). CUDA-only Compute 명령은 사용하지 않았다. Graphics capture, 실제 register 수, spills/local memory, memory/instruction dependency stalls, occupancy, cache hit rate, FP64 saturation은 **미측정**이다. 도구 설치·보안/드라이버/전원 변경은 하지 않았다.

## 정확한 저장 매핑

새 파일은 `diagnostics/gpu-memory/` 아래에만 추가했다. `Interleave.h`가 원래 CPU Pack과 wire layout 사이를 변환하고 `Engine.cpp`가 encode/upload 및 readback/decode를 소유한다. 원래 CPU consumer의 canonical 주소와 데이터 형식은 유지한다.

global system ID에 대해 `tile=id/32`, `lane=id%32`를 쓴다. dispatch BaseSystem과 무관한 **전역 ID**다. 이는 소프트웨어 저장 타일이고 hardware wave 크기를 가정하지 않는다.

```text
physicalByte = planeTileBase + 8 * (elementIndex * 32 + lane)
```

32B Tile 정수 record는 `workBase/statusBase/maxBlocks/maxN/inputBase/maxRows/pad/pad`다. 원래 Meta64B의 미사용 `pad0`에 record의 입력 byte offset을 저장한다. 다른 metadata/정수 row/map/block byte 표현은 보존했다. C++ sizeof/offsetof를 assert하며 실제 encode된 metadata/record도 별도 감사했다.

**B의 FP64 plane:** L, C, u, y, du, s, 5개 correction-step norm, formedDiag, formedLower, formedRhs. per-tile 최대 block 수 B와 DOF 수 N을 사용해 각각의 plane 시작을 공통으로 정한다. 전체 숫자 슬롯은 `144*B + 5*N + 5` doubles/lane이다. integer status는 별도로 `statusBase + lane*32`, 8×uint32다. double-slot 변환을 status에 적용하지 않는다.

**C의 추가 입력 plane:** Mdiag, Mlower, RHS c, scale D, 각 local row의 b 및 a[6]. 숫자 슬롯은 `72*B + 2*N + 7*R` doubles/lane이다. D도 보존해 옮기지만 현재 shader의 unscaling은 CPU이므로 D 자체의 GPU 성능 효과를 주장하지 않는다. row count/column indices는 원래 Row88B 주소에서 읽고, FP64 부분만 translated address를 사용한다.

C는 integer 주소 표현을 보존하기 위해 원래 numerical input 자리도 **예약된 빈 공간**으로 남긴다. 그 자리의 FP64 값은 0으로 지우고 packed plane만 읽는다. 이 공간은 절약된 메모리가 아니며 아래 수치에 전부 포함했다. compact integer-only representation은 구현하지 않았다.

각 저장 타일의 B/N/R은 최대 길이지만 수치 루프는 **원래 m.blocks/m.n/m.rows 및 실제 block size**만 사용한다. 추가 DOF/피벗/행/핀은 생기지 않는다. zero-active root와 terminal position block도 같은 규칙이다. 원래 시스템 순서나 source strand를 크기별로 정렬하지 않았다. FP64 값은 uint64 bit pair/memcpy로 옮기며 float나 축약 decimal 변환은 없다. 64-bit CPU allocation 산술과 `<UINT32_MAX` 검사를 거쳐 실제 raw GPU byte-address 범위를 보장한다.

## 독립 packing·소유권 검증

- CPU round trip **28개**: 14개 크기×B/C. finite 데이터와 ±0/subnormal/NaN/infinity를 포함한 별도 bit probe를 정확히 왕복했다. invalid bit probe는 GPU 성능 입력에 넣지 않았다. 과대 allocation도 명시적으로 거부했다.
- 별도 CPU executable `G64CodecAudit`: **28개 통과**. decode 함수가 보관한 원본 metadata에 의존하는 비교만으로 끝내지 않고, 실제 wire metadata/Tile bytes, 모든 numerical 8-byte pair, 물리 주소의 유일성, integer bytes, status 분리, 고유 work tag를 검사했다.
- 경계 counts: **1,31,32,33,63,64,65,99,100,101,511,512,513,2839**. supplied shuffled heterogeneous order, BaseSystem=3, 양쪽 inactive 이웃 3개씩, canonical guard와 physical padding/guard를 검사했다.
- Release B/C, Debug B 각각 **28회** 정상/invalid 실행과 동일 scratch reset/replay를 통과했다. 각 구성에서 유효 슬롯 9,914개를 두 번씩 확인해 합계 **59,484개 출력**을 검사했다. 검증한 출력 모두 baseline과 비트 동일, 최대 scaled 차이 0이다.
- 별도 비계측 `G64BatchOutputAudit`에서도 성능 corpus의 1/10/100/2839개, c1/c5, B/C를 baseline과 전 슬롯 비교했다. **11,800개 슬롯의 non-bitwise 차이 0**이며 차이가 생기면 각 increment의 양쪽 uint64 bits를 기록하도록 했다. 이 검사는 추가 성능 표본으로 사용하지 않았다.
- 검사 전용 PSO의 integer stage counter가 Factor 1회/Initial 1회/Correct 요청 횟수를 확인한다. 음수 active pivot을 의도적으로 넣은 시스템은 실패를 유지하고 후속 단계를 실행하지 않았으며 이웃을 손상시키지 않았다. 이 예상된 probe 실패와 실제 accuracy corpus의 실패 0을 구분한다.

`codec/`, `codec-audit/`, `ownership/`, `ownership-Debug/`에 원시 근거가 있다. timed PSO에는 검사용 atomics가 없다. tail lane은 metadata 접근 전에 return하며 cross-thread barrier 의존성이 없다.

## 성능: resident solve

최종 launch run `artifacts/gpu-launch/20261031T053500Z/`의 정확한 **9852→14→59 round robin** 순서를 재사용했다. 2,839개에는 각각 947/946/946개가 들어간다. 각 시스템은 32 vertices·183 active DOFs·32 blocks·492 augmented LS rows다. 독립 저장소를 가진 복제 시스템 처리량이며 전체 groom dynamics가 아니다.

Release/debug layer=false, profiler off, 설정당 warm-up2, primary11 samples. A→B→C와 C→B→A 순서를 번갈아 실행했다. 선택 B의 별도 7-sample pass는 시작 순서를 뒤집었다. 모든 결과/status 슬롯을 readback해 검사했으며 CPU scoring/file IO는 timer 밖이다. 고유 입력의 첫 슬롯을 strict score하고 복제 슬롯은 byte 비교, 다르면 별도 strict score한다.

동일한 5 timestamp 계측을 모든 후보에 적용했다. total은 모든 stage와 필요한 UAV barrier를 포함한 **실제 timestamp 차이**다. stage median을 더한 값이 아니다. 아래는 ms, median/P95다. nearest-rank P95는 11개·7개 표본 모두 sample max가 되므로 tail 추정은 제한적이다. uninstrumented 성능이나 profiler-enabled 성능을 별도로 측정하지 않았다.

|시스템 수|A system-major c1|B work32 c1|C workinput32 c1|
|---:|---:|---:|---:|
|1|3.005 / 3.390|3.595 / 4.028|4.244 / 4.712|
|10|3.425 / 3.838|3.829 / 4.185|4.427 / 5.106|
|100|4.032 / 4.660|4.049 / 4.818|4.457 / 4.928|
|2839|5.301 / 5.792|4.872 / 5.406|5.092 / 5.808|

2,839개 stage median:

|구성|Factor|Initial|Correction|실측 total|
|---|---:|---:|---:|---:|
|A c1|2.719|0.798|1.628|5.301|
|B c1|1.975|1.034|1.655|4.872|
|C c1|2.118|0.941|1.950|5.092|

B의 관측상 이득은 주로 Factor 시간에 있고 Initial/Correct가 모두 빨라진 것은 아니다. 이것은 stage 시간의 관측이며 고유한 hardware 병목의 증명은 아니다.

역순 확인에서는 A **5.435/5.665**, B **4.666/5.143**이었다. primary에서 10/11, 역순에서 7/7 paired samples의 baseline−B 시간이 양수였다. paired median 차이는 각각 0.400ms/0.603ms다. 기술적 paired bootstrap 95% 구간은 [0.173,0.871]ms/[0.589,0.892]ms이나 sample exchangeability 가정이 있으며 장치 clock/다른 실행 조건을 완전히 통제한 실험은 아니다. 분포는 겹친다. 반복된 소폭 방향성은 기록하되 보편적인 고정 배속이나 C의 작은 차이를 확정적인 개선으로 선택하지 않는다.

correction5 재현 대조(7 samples, 직접 측정):

|시스템 수|A c5 median/P95|B c5 median/P95|
|---:|---:|---:|
|1|6.165 / 6.979|7.567 / 8.827|
|10|7.142 / 8.097|8.430 / 9.406|
|100|9.097 / 9.734|9.916 / 10.399|
|2839|13.191 / 13.840|12.614 / 13.011|

5회 보정에서도 큰 배치의 차이는 작고 분포가 겹친다. 1회 시간을 5회 값으로부터 나눠 추정하지 않았다. 작은 배치에서는 후보가 느리거나 사실상 차이가 없다.

## 변환과 실제 연결 경로 비용

현재 producer는 기존 CPU Pack을 만들고 **추가 CPU Wire packing**을 거쳐 upload한다. consumer도 CPU이며 readback을 **전체 canonical scratch**로 decode한다. 따라서 GPU pack/unpack kernel bridge는 필요하지 않아 구현하지 않았다. 미래 GPU physical assembler가 packed layout을 직접 생성하는 경로도 없다.

resident timer는 준비를 한 번만 한 뒤 GPU solve만 잰다. fresh-input replay는 같은 frozen 문제를 매번 prepare/pack/upload하고 solve/readback/decode까지 포함한다. 원래 residual/Jacobian assembly 및 공통 canonical Pack 생성은 이 replay 측정 밖이다. 추가 변환 비용과 실제 hybrid 경로의 한계를 구분한다.

2,839개 fresh-input replay(7 samples)의 별도 CPU wall median:

|항목|A|B|C|
|---|---:|---:|---:|
|추가 host packing|0.0001|60.060|152.807|
|upload 및 완료 대기|10.732|10.778|18.333|
|추가 CPU unpack|0.0003|54.598|55.016|
|GPU solve total|5.336|4.585|4.857|
|**실측 integrated wall**|**34.116**|**153.112**|**252.423**|

위 marginal median을 합쳐 integrated 값을 만들지 않았다. A의 unpack 항목이 거의 0인 것은 이미 canonical인 vector를 이동하기 때문이며, 공통 readback/map/copy 비용은 host wall에 포함돼 있다. 변환은 CPU scalar byte-copy 경로이고 software 비용을 GPU kernel 시간으로 부르지 않는다.

|시스템 수|A integrated wall|B integrated wall|C integrated wall|
|---:|---:|---:|---:|
|1|3.549|4.233|5.093|
|10|3.954|4.777|5.288|
|100|5.236|9.691|13.064|
|2839|34.116|153.112|252.423|

역순 fresh replay도 A **32.797ms**, B **151.577ms**였다. 입력이 이미 resident여도 readback/decode를 포함한 host wall은 A23.937/B84.728/C85.519ms다. 현재 full-scratch decoder가 큰 비용을 차지한다. 이 수치를 모든 가능한 decoder의 하한으로 해석하지 않는다. direct packed CPU producer, 결과 일부만 decode, GPU assembler의 직접 출력은 구현·측정하지 않았다.

packing은 새 prepare/linearization에만 수행하고 correction마다 immutable data를 다시 옮기지 않는다. GPU Factor/Initial/Correct 사이 allocation은 없고 factor도 재사용한다. resource 성장은 prepare 중 이전 fence 완료 후만 가능하며 steady samples는 preallocate/warm-up 후 측정했다. CPU Wire vector/map은 prepare마다 재구성되는 현재 구현의 allocation 비용을 포함한다. production 앱에 CPU bridge/readback을 설치하지 않았다.

## 실제 메모리

주소 공간은 그대로 raw SRV input + raw UAV work다. 추가 GPU conversion buffer는 없다. metadata64B/system은 유지하고 Tile32B/packing-tile를 input 안에 추가했다. Work status32B/system은 별도 integer 영역이다. 2,839개에는 Tile89개·descriptor2,848B가 있다.

|개수/구성|input 사용 B|scratch 사용 B|4개 persistent buffer+timestamp committed B|
|---|---:|---:|---:|
|1 A|67,904|44,256|458,752|
|1 B|67,936|1,416,384|3,211,264|
|1 C|1,633,216|1,416,384|6,225,920|
|10 A|679,040|442,560|2,424,832|
|10 B|679,072|1,416,384|4,390,912|
|10 C|2,244,352|1,416,384|7,536,640|
|100 A|6,790,400|4,425,600|22,609,920|
|100 B|6,790,528|5,665,344|25,100,288|
|100 C|13,051,520|5,665,344|37,683,200|
|2839 A|192,779,456|125,642,784|637,075,456|
|2839 B|192,782,304|126,052,544|637,861,888|
|2839 C|332,089,408|126,052,544|916,520,960|

committed 값은 각 실제 resource desc의 `GetResourceAllocationInfo` 결과다. 4개는 input/work default heap, upload/readback이며 timestamp readback은 추가64KiB다. query heap·PSO21개·driver 내부 메모리·실제 spill allocation은 별도 미측정이다. 모든 capacity/각 resource committed는 `memory-summary.json`에 있다. 초기 work zero-upload는 fence 뒤 해제한다.

2,839개에서 B/C의 scratch padding/guard 증가는 **409,760B**다. 실제 숫자 work125,551,936B와 status90,848B에 대해 9개 tail lane 및 guard가 추가된다. C의 실제 numerical input은138,861,168B이며 같은 크기의 원래 numerical 자리도 reserved holes로 남는다. packed 숫자 padding·metadata·guard는 추가448,784B다. 접근 locality를 메모리 절약이라고 주장하지 않는다.

heterogeneous CPU correctness corpus의 2,839개에서는 검사 가드를 포함한 canonical scratch43,971,080B가 packed126,052,544B가 됐다. 작은/큰 시스템을 정렬하지 않고 같은 저장 타일에 두기 때문에 padding이 크다. 그 경우 B input64,674,272B, C203,981,376B이며 세부 값은 `codec/codec.json`에 있다. 지원에서 제외하거나 수학적 크기를 변경한 시스템은 없다.

CPU 변환도 공짜가 아니다. 큰 배치에서 Wire input vector capacity는 B289,169,184B/C433,753,776B, mapping/header/tile capacity는 각각24,815,708B다. 공통 canonical input의 logical192,779,456B도 따로 존재한다. candidate readback/decode 중 physical raw126,052,544B와 canonical125,642,784B의 임시 CPU 결과가 함께 존재한다. 검사 전용 seed/guard vectors는 정상 성능 경로에서 만들지 않는다. 이 수치를 GPU VRAM 또는 전체 process peak RSS와 혼동하지 않는다.

## 정확도·hybrid

|검사|결과|
|---|---|
|B Release/Debug stored-M 83개, c1/c5|모두83/83|
|B Release/Debug row-formed 83개, c1/c5|모두83/83|
|C Release stored-M 및 row-formed, c1/c5|모두83/83|
|B Debug/Release hybrid1|각12/12|
|B Debug/Release dynamics screening|각3가닥×16 steps, correction1|
|보존된 원본 G32 EXE의 fresh replay|83/83(c1/c5)|

2/3 corrections도 모두83/83이다. 0-correction 대조는 이전처럼 stored74/83, row-formed77/83이며 실패를 보존했다. 모든 검사한 u는 같은 예산의 기존 G32 결과와 **83/83 bitwise 동일**하다. GPU row formation도 기존 CPU 비교에서 최대 차이0이다. c1/c5 최대 위치 increment 오차 약4.83e-15m, raw rotation-vector 오차2.47e-12rad, original-LS stationarity9.62e-17 이하이며 원래 gate/실패 의미를 그대로 적용했다. near-zero 해의 scaled relative 최대1도 기록하고 기존 absolute 조건을 유지했으며 tolerance를 바꾸지 않았다.

matching native reference와 hybrid 최종 위치 차이는 최대3.74e-17m, 16-step 위치 차이는 최대7.61e-17m다. root error0이며 raw x/q/v/y와 native 비교는 `hybrid-comparison.json`, `trajectory-comparison.json`, 각 config의 결과에 있다. correction count는 STATUS 및 linear-call/step 기록에 **1**로 명시돼 있다. 기존 gravity frame·h·pins·initializer/history·binary64 native state·비선형 acceptance/scaling은 변경하지 않았다.

이는 **CPU nonlinear / GPU linear** 검증이며 end-to-end GPU dynamics, full-groom 안정성 또는 nonlinear global optimum의 증명이 아니다. C의 Debug/hybrid/dynamics 전체 suite는 선택 후보가 아니어서 실행하지 않았다. production 앱의 기존 smoke 전체, full-groom 5초, UI/마우스/resize/capture, hardware counter 검사는 수행하지 않았다.

## 재현·해시·종료

프로젝트 root에서 새 결과 경로를 사용한다:

```powershell
cmake -S diagnostics/gpu-memory -B build/gpu-memory -G "Visual Studio 17 2022" -A x64
cmake --build build/gpu-memory --config Release --target G64MemoryCandidate G64CodecAudit G64BatchOutputAudit -- /m:1
python diagnostics/gpu-memory/experiment.py preflight --run artifacts/gpu-memory/replay-new
& build/gpu-memory/bin/Release/G64CodecAudit.exe artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/linear artifacts/gpu-memory/replay-new/codec-audit
& build/gpu-memory/bin/Release/G64BatchOutputAudit.exe artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/linear artifacts/gpu-memory/replay-new/batch-bit-audit
python diagnostics/gpu-memory/experiment.py benchmark --run artifacts/gpu-memory/replay-new
python diagnostics/gpu-memory/experiment.py final --run artifacts/gpu-memory/replay-new
python diagnostics/gpu-memory/summary.py artifacts/gpu-memory/replay-new

$exe = Resolve-Path artifacts/gpu-memory/20261031T054929Z/runtime/Release/G64MemoryCandidate.exe
$linear = Resolve-Path artifacts/gpu-memory/20261031T054929Z/runtime/Release/inputs/linear
$nonlinear = Resolve-Path artifacts/gpu-memory/20261031T054929Z/runtime/Release/inputs/nonlinear
& $exe replay $linear artifacts/gpu-memory/manual-new/work32 work32
& $exe formation $linear artifacts/gpu-memory/manual-new/workinput32 workinput32
& $exe hybrid1 $nonlinear artifacts/gpu-memory/manual-new/hybrid work32
& $exe dynamics $nonlinear artifacts/gpu-memory/manual-new/dynamics work32 1
& $exe replay $linear artifacts/gpu-memory/manual-new/baseline baseline
```

DXIL은 executable-relative로 찾는다. staged inputs를 사용하고 runtime/Release·Debug를 작업 디렉터리로 바꾼 상태에서 실제 실행했다. native 실행에 Python/IPC/oracle process/Unity/DXC는 필요하지 않으며 QD notice도 동봉했다. 원래 baseline은 `artifacts/gpu-launch/20261031T053500Z/runtime/Release/G64LaunchCandidate.exe`에 그대로 보존했다.

Release EXE SHA256: `50d380baa921e0a41748f5dffba86b656bc85dec630a377f12375ca39d5c2834`

Debug EXE SHA256: `acd2236779bd717beb4fdf2ca026ada7f22f4837a580cd29f64bcb1118ba11af`

빌드 exit0 및 실행 직전 EXE/DXIL hash를 매번 확인했다. Debug layer=true 검사에서 error-level 오류가 검출되지 않았다. `executed-*-hashes.json`, `input-provenance.json`, `compiler-hashes.json`, `hashes.json`에 전체 소스·입력·실행 해시를 보존했다. 기존 source snapshot125개, launch run921개, pilot2286개, native687개, production/default/asset90개, research runtime37개의 hash가 모두 일치했다. 기대 출력·과거 negative 결과를 덮어쓰지 않았고 Unity에는 접근하지 않았다. 이전에 거부된 cleanup 작업을 재시도하지 않았다.

**현재 선택은 baseline 유지다.** B의 resident 방향성은 기록하되 현 연결 경로의 큰 변환 비용을 이득으로 포장하지 않는다. 새로운 register cache, arithmetic/precision 방법, cooperative solve/PCR, launch sweep, GPU physical/nonlinear/dynamics port는 시작하지 않고 여기서 종료한다.
