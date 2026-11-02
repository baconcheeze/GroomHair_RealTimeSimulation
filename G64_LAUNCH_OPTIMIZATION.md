# G64 실행 묶음·배치 최적화

판정: **launch/batching materially improves throughput**. 선택한 실험 구성은 **G32 / tile 2839 / correction 1**이다. production window solver, 앱 기본값, 연구 후보, 자산은 변경하지 않았다. 원본 pilot 및 G1/tile100/correction5 대조군은 그대로 실행 가능하다.

근거 디렉터리: `artifacts/gpu-launch/20261031T053500Z/`. 원시 측정은 `study/timing.json`, 집계는 `timing-summary.csv` 및 `timing-summary.json`, 전체 판정은 `STATUS.json`에 있다.

초기 `20261031T050847Z` 측정은 corpus 순서가 파일명순(14→59→9852)이었음을 마지막 감사에서 발견했다. 그 결과·바이너리·초안은 그대로 보존하고 `EVIDENCE_SCOPE.json`으로 최종 성능 근거에서 제외했다. 이후 원본 pilot과 정확히 같은 **9852→14→59** 순서·복제 분포로 재빌드하고 전체 제한 측정 행렬과 정확도 검사를 다시 실행했다. 아래 수치는 이 재실행만 사용한다.

## 변경 범위와 수치 보존

추가한 `diagnostics/gpu-launch/`만 실험 대상으로 사용했다. CMake 타깃은 `G64LaunchCandidate`, 메모리 조회 보조 타깃은 `G64LaunchMemory`다. 기존 pilot의 호스트/검증 기반을 별도 타깃으로 분리하고 다음만 변경했다.

- `Packed.hlsl`: compile-time GROUP_SIZE=1/32/64, 타일·전체 개수 경계 검사.
- `Engine.cpp`, `Layout.h`: 그룹/타일 선택, ceil dispatch, 검사 전용 구간 지정·가드 초기화.
- `Ownership.cpp`: 소유권·가드·실패 전파 검사. 별도 검사 PSO만 status의 미사용 padding에 정수 카운터를 기록한다.
- `Study.cpp`: 단계별 비교, 교대 측정, 측정 기반 그룹/타일 선택.
- 별도 CLI·hybrid 연결 코드: 선택한 launch를 사용하고 dynamics의 실제 correction 수를 명시한다.
- `experiment.py`, `summary.py`, `README.md`: 오프라인 실행·평가·재현. native 실행/solve에는 Python이 필요 없다.

A64/b64 목표, M64/c64, FP64 factor/solve/residual/u 누적, 원본-LS refinement, DSqrt, 나눗셈, pin/active mapping, 물리 모델, regularization, nonlinear policy는 그대로다. 수치 본문의 토큰을 launch guard/검사 계수만 제거해 비교했으며 **원본과 동일**하다. DSqrt 파일은 바이트 동일하다. Release의 Legacy Form/Factor/Initial/Correct DXIL 네 개도 기존 pilot과 SHA256이 동일하다. `numerical-preservation.json`에 비교 근거를 남겼다.

입력 provenance도 기존 한정을 유지한다. frozen binary64는 과거 원시 비트를 소급 복구한 것이 아니라, 앞선 계측 replay가 기존 기록을 byte-identically 재현할 때 저장한 입력이다. 이번에는 그 저장된 배열을 그대로 사용했다. `input-provenance.json`에서 원본 선형 83개와 nonlinear 입력/metadata 13개 및 두 staged 사본의 해시 일치를 확인했다.

G32F64R는 기존 **5회 보정 58/83, factor 실패 16개 및 추가 정확도 실패 9개**라는 부정 결과를 유지한다. 새 혼합 정밀도 실험은 수행하지 않았다.

## 장치·빌드

실제 장치: NVIDIA GeForce RTX 5070 Ti, driver **32.0.15.9186**, FP64 지원=true, shader model **6.8**, queue timestamp frequency **1,000,000,000 Hz**. WARP/CPU fallback은 사용하지 않았다.

MSVC **19.44.35229**, Windows SDK **10.0.26100.0**, DXC `C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe`, compiler **1.9.2602.17 (21d28f727)**. `cs_6_0 -HV 2021 -Gis`, Release `-O3`, Debug `-Od -Zi -Qembed_debug`를 유지했다. DXIL에서 double→float narrowing, float sqrt, FMA가 없음을 재검사했다. NumThreads=(1/32/64,1,1)도 실제 DXIL에 반영됐다. wave 크기·occupancy·register 수·spill은 측정하지 않았다.

초기 병렬 MSBuild가 exit 1로 끝났지만 상세 compiler error는 출력하지 않았다. 이 실패 뒤 실행하지 않았고 `/m:1` 재빌드의 exit 0을 확인했다. 최종 모든 실행은 성공한 빌드의 EXE/DXIL 해시를 저장·재검사한 후 수행했다. 해당 실패 및 성공 로그를 보존했다. Debug와 Release 빌드·검증 모두 통과했으며 Debug layer=true의 error-level audit에서 오류가 검출되지 않았다.

## 기존 100개 제한의 이유와 명령 순서

100은 `Engine::run`의 타일 루프에 하드코딩된 **보수적인 진단 실행 분할**이었다. 버퍼 allocation, descriptor, per-system offset, indexing의 100개 제한은 없었고 기존 버퍼도 2,839개를 모두 보유했다. 특정 필수 하드웨어 제한이라는 근거는 발견하지 못했다.

기존 구현도 하나의 command list에 모든 타일을 기록하고 한 번 제출·완료 대기한다. 타일마다 CPU wait나 UAV barrier가 있는 구조가 아니다. 각 단계의 모든 타일 뒤 Work UAV barrier를 두며 Factor→Initial→Correct 및 다음 Correct의 의존성을 보장한다. 타일 개수만으로 겹쳐 실행되거나 직렬 실행된다고 판정하지 않았다.

새 코드도 이 순서와 배리어를 유지한다. readback·timestamp resolve는 마지막 solve timestamp 뒤이며, CPU wait는 전체 solve 끝에만 있다. 타일별 scratch를 재활용하지 않고 모든 시스템이 배치 전체 수명 동안 독립된 영역을 가진다.

## 인덱싱·소유권

한 스레드가 한 시스템 전체를 처리한다. `SV_DispatchThreadID.x >= TileSystemCount`를 먼저 검사하고, `system=BaseSystem+tid.x` 후 전체 개수를 다시 검사한다. dispatch group 수는 `ceil(TileSystemCount/GROUP_SIZE)`다. 세 단계 모두 같은 규칙이다. 수치 루프·누적 순서는 변경하지 않았다. cooperative lane, groupshared, group barrier, float atomics는 없다.

Debug/Release 각각 tile100에서 G1/G32/G64와 counts **1,31,32,33,63,64,65,99,100,101**, 정상/의도적 invalid 입력을 검사했다. 설정별 60회씩, 유효 출력 3,504개씩 통과했다. 이후 선택 G32에서 tile512 및 tile2839의 counts **101,511,512,513,2839**를 정상/invalid로 검사해 각각 10회·유효 출력 8,947개씩 통과했다. 합계 **140회·유효 출력 24,902개**다.

모든 검사에서 BaseSystem=3, 앞뒤 비참여 시스템 3개씩, 시스템 사이 64-byte guard, 섞인 83개 frozen case의 가변 DOF/pin 배치를 사용했다. 첫/마지막 guard와 비참여 frame 전체를 비교했다. 검사 전용 정수 카운터로 Factor 1회, Initial 1회, Correct 요청 횟수를 확인했다. invalid 시스템은 실제 활성 diagonal에 음수를 넣어 factor 실패를 유발했으며, 후속 단계가 이를 지우거나 이웃을 손상시키지 않았다. invalid 입력은 원본 파일을 변경하지 않은 메모리 사본이다.

모든 유효 출력이 독립 strict gate 및 Legacy G64 대조를 통과했고, 검사 출력은 Legacy와 **비트 동일**, 최대 scaled 차이 **0**이었다. `ownership-Release/`, `ownership-Debug/`, `study/tile512-ownership/`, `study/tile2839-ownership/`에 원시 결과가 있다. 꼬리 lane의 early return은 이 커널에 cross-thread barrier가 없으므로 안전하다.

## 측정 방법과 단계별 결과

Release, debug layer=false, resident 입력, 설정당 **warm-up 2회 + 측정 11회**. 매 반복마다 설정 순서를 정방향/역방향으로 교대했다. source9852/14/59의 저장된 32-vertex·183-DOF initial 선형 시스템을 round-robin 복제했다. 이는 2,839개의 독립 groom strand dynamics 검증이 아니다.

모든 warm-up/측정의 모든 status와 u 슬롯을 읽어 검증했다. 각 고유 입력의 첫 슬롯을 독립 strict 기준으로 평가하고, 복제 슬롯은 해당 값과 바이트 비교했다. 다른 바이트가 나오면 해당 슬롯을 별도로 strict 평가하는 경로를 사용한다. 검증/파일 기록은 GPU timer 밖이다. 측정에는 5개 timestamp가 활성화됐으며 소유권 계수 instrumentation은 꺼져 있다. 새 구성의 uninstrumented 성능은 별도로 측정하지 않았다.

모든 시간은 **ms**다. total은 배리어와 모든 타일을 포함한 실제 전체 timestamp 차이이며 stage 중앙값을 더한 값이 아니다. P95는 nearest-rank 방식으로, 표본 11개에서는 최댓값이다. 작은 표본 및 장치 clock 상태 미고정에 따른 변동이 있으므로 정확한 고정 배속을 약속하지 않는다.

원본 보존 EXE/DXIL로 별도 재측정한 7표본 대조군의 2,839개·5회 결과는 median **46.104**, P95 **47.264**였다. 같은 새 benchmark 안에서 교대 측정한 B0가 아래 최종 비교의 기준이다. 시점이 다른 값을 혼합해 배속을 계산하지 않았다.

### 2,839개, 변경 요소를 분리한 비교

|단계|그룹/타일/보정|solve dispatch 수|GPU total median / P95|
|---|---|---:|---:|
|보정 횟수 대조 B0|1 / 100 / 5|203|46.138 / 46.889|
|보정 횟수만 B1|1 / 100 / 1|87|23.990 / 24.298|
|packing 대조 B1|1 / 100 / 1|87|23.976 / 24.154|
|packing C32|32 / 100 / 1|87|7.515 / 8.454|
|packing C64|64 / 100 / 1|87|9.399 / 10.527|
|tile 대조|32 / 100 / 1|87|7.224 / 7.925|
|tile 확장|32 / 512 / 1|18|5.774 / 6.669|
|tile 확장|32 / 2839 / 1|3|5.546 / 6.396|

G32를 실제 측정으로 선택했다. tile100에서 단계당 group 수는 G1=2839, G32=114, G64=57이다. G32 tile512/2839는 모두 단계당 **89 groups**지만 dispatch는 각각 6회/1회다. 더 적은 group 수가 반드시 더 빠르지는 않았다.

tile512→2839의 median 차이는 약 4%이며 측정 변동 범위와 겹친다. tile2839는 이번 표본의 선택이고, 보편적인 최적 타일 크기가 확정됐다는 뜻은 아니다.

### 최종 교대 비교: 2,839개 stage 중앙값

|구성|Factor|Initial|Correction 합계|실측 total median / P95|host 기록·대기·readback median|
|---|---:|---:|---:|---:|---:|
|B0 G1/t100/c5|14.766|3.993|27.510|46.069 / 46.909|66.789|
|B1 G1/t100/c1|14.535|3.997|5.304|23.762 / 24.165|43.889|
|선택 G32/t2839/c1|2.708|1.188|1.613|5.523 / 6.179|25.159|
|선택 G32/t2839/c5|2.714|1.079|8.863|12.757 / 13.861|32.874|

동일 보정 횟수에서 launch/batch의 median 비율은 c1 약 **4.30배**, c5 약 **3.61배**였다. B0→선택 c1의 약 8.34배에는 보정 연산 감소도 포함되므로 launch만의 개선이라고 부르지 않는다. 입력 업로드는 이 단계에서 배치당 한 번 약 **68.2ms**였고 resident solve timer에 포함되지 않는다. host 값은 GPU 시간에 더할 별도 장치 시간이 아니라 기록·큐·복사·대기·매핑을 포함한 wall time이다.

### 배치별 최종 GPU total median / P95

|시스템 수|B0 G1/t100/c5|B1 G1/t100/c1|선택 G32/t2839/c1|선택 G32/t2839/c5|
|---:|---:|---:|---:|---:|
|1|5.954 / 6.375|2.912 / 3.341|3.032 / 3.598|6.192 / 6.820|
|10|6.025 / 6.533|3.049 / 3.373|3.289 / 3.528|7.092 / 7.484|
|100|6.325 / 6.531|2.977 / 3.397|3.900 / 4.449|9.076 / 9.470|
|2839|46.069 / 46.909|23.762 / 24.165|5.523 / 6.179|12.757 / 13.861|

1/10/100개에서는 packed 구성이 더 느렸다. 선택은 큰 복제 배치 처리량에 관한 것이며 모든 작은 호출에 최적인 설정이라는 뜻이 아니다. 각 크기의 stage·host·upload 원시 값도 CSV/JSON에 보존했다. 전체 배치를 한 타일로 처리할 수 있었고 resource/TDR 제한으로 중단한 경우는 없었다. 20초 GPU fence timeout, launcher의 bounded timeout을 유지했다.

## 정확도와 hybrid 재검증

선택 G32/t2839에서 Debug/Release 각각 다음을 재실행했다. 기존 pin/free-twist/variable-block 83개 전체를 포함한다.

|경로|0회 보정 대조|1회 보정|5회 보정|factor/solve 실패|
|---|---:|---:|---:|---:|
|stored M/c|74/83|83/83|83/83|0|
|GPU row-formed M/c|77/83|83/83|83/83|0|

2/3회도 모두 83/83이다. GPU formed 값은 같은 순서 CPU formation과 최대 차이 **0**이었다. 모든 83개·모든 보정 예산의 u가 같은 설정의 과거 pilot 출력과 비트 동일했다. c1/c5 최대 위치 increment 오차는 약 **4.83e-15m**, raw 물리 회전벡터 오차 **2.47e-12rad**, original-LS stationarity **9.62e-17 이하**다. gate·허용오차·실패 의미는 변경하지 않았다. 원시 scaled-relative 최대값 1인 near-zero 해는 기존 absolute-error 대체 조건으로 판정하며 이 값도 숨기지 않고 기록했다. tangent-only 비교로 full quaternion gate를 대체하지 않았다.

B1 원본 launch 구성도 Release에서 별도로 `replay` **83/83(c1,c5)**, `hybrid1` **12/12**를 재확인했다(`B1-original-layout/`). 보정 횟수만 줄인 대조군의 정확도를 선택 구성의 결과로 대신하지 않았다.

`hybrid1`은 Debug/Release **12/12** qualification을 통과했다. matching NativeExtended 결과와 최대 위치 차이 **3.74e-17m**, sign-aware quaternion chord 차이 **2.55e-14**, objective 차이 **1.28e-21J 이하**였다. nonlinear acceptance/convergence policy는 동일하다.

3가닥(source9852/14/59)의 16-substep screening도 두 빌드에서 통과했다. 이번 실행은 **매 linear solve correction 1회**, 총 **80 linear solves**다. 과거 screening은 5회였으므로 같은 보정 횟수였다고 가정하지 않았다. rest-start, guarded initializer/history, h≈1/120, solver gravity **(-9.699665069580078, 0.06558697670698166, -1.4657117128372192)** 및 기존 핀/물성을 유지했다. y는 evolving state로 매 서브스텝 생성하는 기존 native wrapper를 그대로 사용했다.

matching native trajectory에 대한 전체 checkpoint 위치 RMS 최대 **2.43e-17m**, 최대 위치 차이 **7.61e-17m**, 속도 차이 **7.31e-15m/s**, quaternion chord 차이 **6.89e-16**, root error **0**이었다. primary의 최종 tip displacement는 **(-0.0001423582453, 0.0008802468520, -0.0001700426638)m**, 최대 axial strain은 **1.21675e-5**였다. 다른 두 가닥 및 step별 원시 x/q/v/y·residual·에너지는 `trajectory-comparison.json`과 `Release/dynamics16-c1/`, `Debug/dynamics16-c1/`에 있다.

이는 **CPU nonlinear / GPU linear hybrid 검증**이다. 단일 시스템 호출의 업로드·readback·wait를 포함하므로 end-to-end GPU dynamics나 실시간 구조가 아니다. native reference도 입증된 nonlinear global optimum은 아니다. 한 번의 보정이 향후 임의 입력에 충분하다는 보장은 하지 않는다.

## 메모리·수명

초기 GPU 레이아웃을 유지했다: Meta64B, Row88B, Block8B, Map16B, Status32B, double8B. root constants는 total/base/formation/tile-count 네 uint32, 16B다. 시스템별 offset은 그대로이며 global scratch는 N32/183DOF당 **44,256B**다. Factor/Initial/Correct의 scratch·factor·u 영역은 시스템 간 겹치지 않는다. 기본/업로드/readback heap 및 완료 후 해제·재사용 규칙도 원본과 동일하다.

실제 생성한 resource desc에 `GetResourceAllocationInfo`를 호출했다(`memory/memory.json`).

|개수|입력 logical / committed|scratch logical / committed|
|---:|---:|---:|
|1|67,904 / 131,072 B|44,256 / 65,536 B|
|10|679,040 / 720,896 B|442,560 / 458,752 B|
|100|6,790,400 / 6,815,744 B|4,425,600 / 4,456,448 B|
|2839|192,779,456 / 192,806,912 B|125,642,784 / 125,698,048 B|

각각 같은 크기의 persistent upload/readback이 별도로 존재한다. 2,839개 총 committed buffer는 이 네 개와 timestamp readback64KiB를 합쳐 **637,075,456B**다. query heap, 25개 PSO 및 driver 내부 메모리는 이 수치에 포함하지 않았다. startup scratch 초기화 upload는 fence 뒤 해제한다. shader에 명시적 shared/local array를 새로 두지 않았고, 실제 register 배치나 compiler spill은 측정하지 않았다. 2,839개 배치 전체를 수용하며 correction 중 allocation은 없다. 검사 전용 guard/seed upload는 측정 경로 밖이다.

## 재현 명령·해시

프로젝트 root에서:

```powershell
cmake -S diagnostics/gpu-launch -B build/gpu-launch -G "Visual Studio 17 2022" -A x64
cmake --build build/gpu-launch --config Release --target G64LaunchCandidate G64LaunchMemory -- /m:1
cmake --build build/gpu-launch --config Debug --target G64LaunchCandidate -- /m:1

# 새 결과 경로를 사용한다. 과거 결과를 덮어쓰지 않는다.
python diagnostics/gpu-launch/experiment.py --phase packing --run artifacts/gpu-launch/replay-new
python diagnostics/gpu-launch/experiment.py --phase accuracy --run artifacts/gpu-launch/replay-new

$exe = Resolve-Path artifacts/gpu-launch/20261031T053500Z/runtime/Release/G64LaunchCandidate.exe
$linear = Resolve-Path artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/linear
$nonlinear = Resolve-Path artifacts/gpu-launch/20261031T053500Z/runtime/Release/inputs/nonlinear
& $exe replay $linear artifacts/gpu-launch/manual-new/replay 32 2839
& $exe formation $linear artifacts/gpu-launch/manual-new/formation 32 2839
& $exe hybrid1 $nonlinear artifacts/gpu-launch/manual-new/hybrid1 32 2839
& $exe dynamics $nonlinear artifacts/gpu-launch/manual-new/dynamics16-c1 32 2839 1
& $exe replay $linear artifacts/gpu-launch/manual-new/control legacy 100
& $exe benchmark $linear artifacts/gpu-launch/manual-new/study
& build/gpu-launch/bin/Release/G64LaunchMemory.exe artifacts/gpu-launch/manual-new/memory
```

EXE는 staged DXIL을 executable-relative로 읽는다. runtime/Release와 runtime/Debug를 작업 디렉터리로 바꾼 상태에서 staged inputs만으로 모든 최종 suite를 실행했다. normal app/Unity/DXC/Python이 native 실행에 필요하지 않다. QD license/provenance도 runtime에 보존했다. 기존 pilot 원본 실행 파일은 `artifacts/gpu-structured/20261031T035307Z/runtime/Release/GpuStructuredLSCandidate.exe`에 그대로 있다.

실행 파일 SHA256:

- Release: `adc0287d187b8492a202e26078d8af4b06a8bc8ad3690d41431441bfb86221df`
- Debug: `063b50a39c478660d25d2d29489bd2abbc62de625b7cb6859cba31a3530acc70`
- Release Factor_g32: `4508d4cd06fa9632308001ac289049d20545c7747846864b46d9e5447ff4effa`
- Release Initial_g32: `4bda3f003ba19d1d2e6dd858c2b839c640969868ea1ccc4bdbdf08cae6c1545d`
- Release Correct_g32: `32202058f64047f44de890646e58dd2004c02e8953faf9031bd390a013fdfab2`

모든 EXE/DXIL/compiler/source/input 해시는 실행별 manifest와 최종 `hashes.json`에 있다. production/default/assets90개, 이번 시작 snapshot102개, 기존 pilot2286개, native reference687개, 보존 research runtime37개의 hash가 모두 일치했다. 과거 실패·예상 출력·보고서는 덮어쓰지 않았고 Unity는 접근하지 않았다.

## 한계와 다음 질문

큰 복제 배치의 처리량 개선은 확인했다. 작은 배치의 느려짐, 많은 host 왕복 비용도 함께 측정했다. 이 시간만으로 FP64 saturation, occupancy, register pressure 또는 메모리 병목을 특정할 수 없다. 다음 profiling 질문은 **선택 G32 구성에서 Factor/Correct의 instruction dependency, register/spill 및 strided memory 접근 중 무엇이 비용을 지배하는가**다. 이 작업에서는 해당 구현 변경이나 추가 탐색을 시작하지 않는다.

새 GPU physical assembly, nonlinear trial control, 전체 dynamics port, float32 전환, collision/OGC/reconstruction, production 기본값 교체는 하지 않았다. full-groom 5초 검사, UI/마우스/resize 및 새 캡처 검사는 이 CPU/GPU 선형 진단 범위에서 실행하지 않았다. 전체 동역학 frame budget 또는 production 준비 완료를 주장하지 않는다.

보조 미사용 파일 정리를 포함한 명령 하나는 자동 승인 검토가 sandbox 승인 불가 사유로 거부해 생략했다. 계산·빌드·검증은 영향을 받지 않았으며 해당 보조 파일은 그대로 두었다.
