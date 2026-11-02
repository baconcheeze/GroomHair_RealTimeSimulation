# Native Reference Demo

기존 HairLabDX12 창에 **Native Reference Demo** 실행 모드를 추가했다. 실제 guide 위치가 CPU의 검증된 full-strand manifold Gauss–Newton reference로 진화하고, 완료된 상태만 DX12 line renderer로 전달된다. GPU 이식·레이아웃·성능 탐색은 진행하지 않았다.

## 바로 실행

```powershell
cmake --preset vs2022
cmake --build build/vs2022 --config Release --target HairLabDX12 NativeReferenceIntegrationTests -- /m:1

# 기본 paused 상태. Execution mode에서도 Native Reference Demo 선택 가능.
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode reference

# 실제 32개 가이드, 즉시 Run. 물리 h는 그대로다.
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode reference --reference-guides 32 --reference-run

# 검증된 세 가닥의 tip 확대
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode reference --reference-guides 3 --reference-tip --reference-run
```

Run/Pause, Step, Step N, Reset, **Apply velocity kick ONCE**를 제공한다. kick 입력은 world m/s이며 한 번만 속도에 더한다. 기본 입력값은 `(0,0.05,0)`이고 버튼을 누르기 전에는 적용되지 않는다. roots의 position/orientation은 고정한다. 3/32/128 선택은 Reset을 수반한다.

움직임이 작을 때 **Close up tip: source 9852**를 누른다. cyan은 실제 current guide, gray는 선택 가능한 rest ghost, orange는 실제 완료된 tip 궤적이다. **Frame selected guides**로 머리 전체 보기로 돌아간다. 기존 RMB orbit/MMB pan/wheel/F/R을 유지했다. render hair는 이 모드에서 숨기며 **body/self collision unavailable**를 명시했다.

## 실제 캡처

근거: `artifacts/native-reference-demo/20261031T065303Z/`.

- `delivered/guides-32/actual-motion.gif`: 원래 head와 실제 32가닥 움직임.
- `delivered/guides-128/actual-motion.gif`: 실제 128가닥 움직임.
- `delivered/tip-3/actual-motion.gif`: 검증된 세 가닥의 작은 움직임 확대.
- 각 폴더의 `step-0000.png`… 및 동일 이름 JSON: 정확한 completed substep의 화면과 authoritative double x/q/v.
- `reset.png/json`: Reset 결과. 초기 PNG 및 x/q/v와 정확히 같다.

GIF는 앱 소유 PNG만 연결했다. 4서브스텝마다 저장한 프레임을 200ms씩 보여 주는 약 6배 느린 검사용 재생이며, 중간 움직임을 보간·합성하지 않았다. 정상 Run 속도나 GPU 성능을 뜻하지 않는다. desktop/window discovery screenshot은 사용하지 않았다. PNG 저장은 기존 DX12 readback/fence/WIC 경로다.

번호 이미지 시퀀스 재현:

```powershell
& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode reference --reference-guides 32 --reference-steps 16 --reference-every 4 --reference-kick 0,0.05,0 --reference-reset-check --reference-capture-dir artifacts/reference-demo-new/32 --log artifacts/reference-demo-new/32/app.log --report artifacts/reference-demo-new/32/summary.json

& .\build\vs2022\bin\Release\HairLabDX12.exe --solver-mode reference --reference-guides 3 --reference-steps 48 --reference-every 4 --reference-kick 0,0.05,0 --reference-tip --reference-reset-check --reference-capture-dir artifacts/reference-demo-new/tip --log artifacts/reference-demo-new/tip/app.log

& .\build\vs2022\bin\Release\NativeReferenceIntegrationTests.exe .\build\vs2022\bin\Release\assets artifacts/reference-demo-new/tests
```

`--reference-steps N`은 N개의 고정 h 물리 서브스텝이다. `h/N`이나 rendered frames가 아니다. 시퀀스 모드는 checkpoint마다 작업을 요청·캡처한 뒤 다음 작업을 요청하므로 빠른 worker 때문에 checkpoint를 놓치지 않는다. `--frames`, 기존 `--capture`, `--reference-run`과 혼합하지 않는다. 별도의 `--reference-reset-check`가 초기 상태로 돌아오는 것도 캡처하고 자동 종료한다.

`diagnostics/reference-demo/evaluate.py RUN_DIRECTORY`는 저장된 결과를 검사하고, 설치된 ffmpeg가 있으면 GIF를 만든다. Python/ffmpeg는 native 실행이나 PNG 캡처에 필요하지 않다.

## 계산·소유권

- `NativeReferenceCore`는 **변경하지 않은** `diagnostics/native-ls/NativeLS.cpp`와 기존 reference 코드를 앱에서 링크한다. 기존 diagnostic CMake의 상호 포함과 target 충돌을 피하기 위한 앱용 target 이름이며 다른 solver가 아니다.
- `NativeReferenceDemo.cpp::Entry::step`은 기존 `diagnostics/native-ls/Bridge.cpp::simulate`의 guarded rest-start wrapper를 단일 서브스텝 형태로 연결했다. `y=x+h*v+h*h*g`, q warm start, FullPrediction/InertialOnly 중 기존 energy guard, NativeExtended 5 refinement corrections, 기존 qualification/line search, `v=(xFinal−xOld)/h`를 그대로 사용한다.
- authoritative x/v/q와 history/target은 **CPU binary64**다. QD extended residual은 기존 NativeExtended 구현 그대로다. state 자체를 float나 새로운 extended state로 바꾸지 않았다.
- renderer는 immutable completed snapshot의 x만 float Vertex로 변환한다. float display buffer나 GPU readback을 solver에 돌려주는 경로는 없다.
- 물리 h는 기존 float coefficient `1.f/120`의 값(약0.008333333768초), baseSS=1, baseBT=1e-5 및 기존 mass/radius/rest 자료를 유지한다. 화면 효과를 위해 물성을 조정하지 않았다.
- world gravity `(0,-9.81,0)`를 기존 staged inverse frame-0 neck rotation으로 변환한다. solver vector는 기존 검증의 `(-9.699665069580078, 0.06558697670698166, -1.4657117128372192)`와 같다. 추가 Z flip/translation/camera-based gravity는 없다.
- 첫 세 가닥은 source9852/14/59, 실제 guide indices1023/0/2다. `assets/reference-demo/*.jwp`는 기존 검증 artifact의 정확한 복사이며 groom rest positions와 ID/크기·byte 일치를 검사한다. 기존 assets를 수정하지 않았다.
- 32/128은 세 가닥을 포함해 actual groom root의 deterministic farthest-point 선택으로 확장한다. 기존 물리 배열을 그대로 slice하며 resampling·density 변경·재생성·실패 strand 제외는 없다. 32개 선택은 128개의 prefix다.

전용 worker 하나가 전체 선택 batch를 처리한다. GPU/Win32/ImGui 호출은 worker에서 하지 않는다. atomic shared completed snapshot으로 UI와 연결하고, per-frame float vertex upload는 두 buffer를 사용해 해당 frame fence 뒤에만 재사용한다. 한 strand라도 실패하면 batch를 완료로 발표하지 않고 이전 completed 화면과 오류를 유지하며 Pause한다. 내부 partial trial은 성공 상태로 부르지 않으며 Reset이 필요하다.

Run은 h보다 빨리 계산되면 pace하고, 느리면 느린 만큼 wall time이 걸린다. physical h를 키우거나 catch-up으로 누락된 시간을 숨기지 않는다. Pause는 in-flight batch가 끝날 수 있지만 이후 진행을 멈춘다. reset generation이 바뀌면 이전 작업을 새 상태로 publish하지 않는다. 최소화 시 Pause하며 bounded capture가 최소화되면 실패 종료한다.

UI에는 active count, substep/simulated time, wall since Reset(includes pause), CPU batch time, sim-time/CPU-work ratio, mm displacement를 구분한다. 128개가 느리면 명확히 표시한다. CPU batch 시간은 prediction/solve/velocity reconstruction의 wall duration이며 GPU 시간이 아니다.

## 검증 결과

|실행|완료|물리 시간|저장 checkpoint의 CPU batch ms|최종 max tip 이동|
|---|---:|---:|---:|---:|
|3 rest-start|16|0.13333334s|0.646–0.906|0.908mm|
|3 one-shot kick|48|0.40000002s|0.329–0.883|0.880mm|
|32 one-shot kick|16|0.13333334s|7.300–9.135|2.266mm|
|128 one-shot kick|16|0.13333334s|28.555–37.668|2.324mm|

이 표는 소수 integration checkpoint의 관측값이다. performance campaign이나 GPU timing이 아니다. 128개는 CPU work 총0.551초에 simulated0.133초여서 현재 실시간보다 약4배 느리다. strand를 생략하거나 timestep을 늘리지 않았다. UI rendering은 worker와 독립적으로 계속됐다(128 시퀀스87 rendered frames였던 실행도 보존).

- **native16 비교:** 세 rest-start 가닥의 matching checkpoint x/q/v가 보존된 native reference와 최대 차이 **0**.
- **finite/roots:** 모든 시퀀스의 선택 가닥 수·source IDs·finite x/q/v와 고정 root x/q/v를 확인했다. 실패/누락 없음.
- **reset:** 모든 시퀀스에서 초기 x/q/v와 Reset이 정확히 같고 PNG bytes도 같다.
- **render boundary:** 시퀀스 종료 후 기존 guide/render GPU rest/current positions/connectivity 및 기존 physical rest/velocity가 변하지 않았음을 explicit readback으로 확인했다. Native mode의 별도 display VB만 움직인다.
- **controller integration:** Run→Pause 정지, Step, reset/replay exact, display-float copy 비피드백, 32/128 선택 유일성·prefix·한 서브스텝을 전용 CPU integration test에서 확인했다.
- **build/runtime:** Release/Debug 빌드 성공. Debug의 세 가닥4-step+Reset 및 D3D12 audit 오류0. 기존 Release bootstrap120-frame, Release/Debug static hair120-frame+rest validation 통과 기록을 보존했다.
- **self-contained:** executable-relative assets/shaders를 갖춘 `runtime/Current/`를 다른 working directory에서 실행했다. Unity/원본 Unity 폴더를 사용하지 않는다.
- **시각 검사:** 32/128 전체 head view와 3가닥 tip close-up의 생성 PNG 내용을 직접 확인했다. mouse/UI 클릭·resize를 직접 자동 조작해 검사했다고 주장하지 않는다. 해당 버튼과 같은 controller API는 테스트했다.

`DELIVERY_VALIDATION.json`, `integration-delivered/integration.json`, 각 폴더의 invocation/app.log/summary.json이 원시 근거다. 이전 개발 중 캡처·빌드 오류도 삭제하지 않았다. 최초 Main의 잘못된 require namespace 컴파일 오류를 수정한 뒤 성공 빌드를 확인하고 실행했다. 기존 83/12 연구 suite나 전체 groom 장시간 실험은 다시 돌리지 않았다.

추가/변경 파일은 `NativeReferenceDemo.*`, `NativeReferenceView.*`, `NativeReferenceIntegrationTests.cpp`, `Main.cpp`, `AppOptions.*`, `WindowSession.cpp`의 mode/UI 연결, root CMake 및 새 fixture 사본/문서/평가 스크립트다. NativeStructuredLS 수치 소스·기존 shader·기존 assets·과거 GPU 후보/보고서는 그대로다. 각 EXE/fixture/source hash 및 보존 확인은 이 run의 provenance manifest에 있다.

충돌, moving head, angular velocity/inertia, render-hair reconstruction, GPU nonlinear/dynamics port를 추가하지 않았다. 여기서 종료한다.
