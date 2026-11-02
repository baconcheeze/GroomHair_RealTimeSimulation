# 월드 기준 공기저항

## 사용

**Jolt material settings → Hair region → World air resistance**에서 그룹별 조절한다.

- 앱 기본값 **1.0 /s**, 범위 0..20 /s. 0은 비활성화.
- 값이 클수록 정지한 월드 공기에 대한 입자 속도를 더 강하게 줄인다.
- 앞머리/사이드뱅/포니테일 등 독립 설정. 정수리 0↔5, 포니테일 2↔6은 기존 접촉용 그룹 연결을 유지한다.
- 기존 linear/angular damping, retention, 질량, compliance, h, collision 설정은 유지한다.
- Run/Step의 다음 업데이트에 적용된다. Reset은 장면 기본값, Reset velocity settings는 선택 그룹의 air/damping/grid 값을 기본값으로 복구한다. Styling spray는 air 값을 바꾸지 않는다.
- Hair physics를 끄거나 retention이 1인 구간에서는 저항을 높여도 형태 변화가 가려질 수 있다.

이는 **선형·등방성 translational drag**다. 계수 k의 단위는 1/s이며 a_world = -k v_particle_world (힘으로 쓰면 -mass*k*v)이다. 공기는 월드에서 정지해 있다. 바람장/이차 항력/회전 항력/새 유체 solver는 추가하지 않았다.

## 속도와 좌표 처리

Jolt의 WorldTransformInfluence가 이미 월드 보존 움직임을 처리하므로 head 속도를 무조건 더하지 않는다.

- H: 전체 hair update 시간 (현재 저장된 float 1/60).
- D: 이전 hair frame 좌표를 현재 frame으로 바꾸는 기존 cDeltaTransform.
- x: 그 변환을 적용하기 전 입자 local position.
- w: 기존 WorldTransformInfluence (0=머리를 따라감, 1=월드 위치 보존).

좌표계 변경으로 추가된 실제 운반 속도를 현재 hair basis로 표현한다:

    vCarry = (1-w) * (x - D*x) / H
    vAirWorld = R_current * (vJolt + vCarry)

머리의 평행이동과 회전이 기존 D 안에 모두 들어 있다. w=1인 월드 정지 입자에 head 속도를 중복 추가하지 않는다. w=0이면 머리를 따라 실제 이동한 속도를 포함한다. 정지 프레임에는 vCarry를 0으로 지운다. Teleport/Reset도 이 기록을 GPU에서 0으로 초기화한다.

등방성 항력은 회전과 교환하므로 shader에서 큰 월드 좌표로 바꾸지 않고 다음 동일한 속도 갱신을 수행한다:

    gain = -expm1(-k * inner_dt)
    vJolt_new = vJolt - gain * (vJolt + vCarry)

gain은 CPU에서 material별로 계산하여 업로드한다. 0일 때는 원래 velocity를 그대로 반환한다. 기존 Integrate의 gravity/damping 앞에 적용하며 현재 내부 시간 간격은 update H를 나눈 1/360초다. 목표 포즈나 rest 위치를 수정하는 방식이 아니다.

## GPU 소유권 / 레이아웃

- Hair 소유 default-heap RW float3 transport buffer 하나. 12 × GetNumVerticesPadded() 바이트 (현재 약 6.67MiB).
- Teleport: clear, ApplyDeltaTransform: 기록, Integrate/UpdateVelocityIntegrate: SRV 읽기.
- 기존 ComputeQueue의 상태 전이/UAV 동기화와 fence 수명을 사용한다. per-frame full CPU geometry readback/upload는 없다.
- JPH_HairMaterial은 176바이트, 추가 float4 AirResistance offset 160: x=gain, y=k, z/w=0.
- JPH_HairUpdateContext는 272바이트. AirFrame offset 256: x=1/H, z=frame changed(0/1), 나머지 0. 정지/시간 0 경계는 구분한다. sizeof/offsetof static_assert를 추가했다.
- 기존 HairSettings Material의 ObjectStream 및 binary save/restore에 새 필드를 포함했다. 내부 HairSettings binary layout은 확장되었지만 staged .hair/.materials/character/animation 포맷과 자산은 바꾸지 않았다.

## 실행한 검증

- GPU analytic probe 12개: 정지 frame, head translation, 부분 following, 월드 보존 시 중복 방지, 회전, 이동 입자, k=0, 정지 frame 기록 지우기, 60/120/360 inner step 비교, 월드 속도 0. 동일 shader helper를 직접 실행했다.
- 최대 velocity/transport 오차 약 9.54e-7 m/s, 사전 tolerance 5e-5 m/s 이내. Release/Debug 통과.
- k=0으로 동일 61스텝 실행: 기능 추가 전 baseline 대비 guide root/tip 및 render tip의 최대 차이 **0m**. 전체 buffer의 bitwise 비교를 주장하는 것은 아니다.
- k=1 기본값 120스텝 달리기/Reset, k=20 61스텝, k=0 61스텝 통과. 유한 위치/뿌리 부착 오차 <1e-5m 확인.
- group live controller: ponytail/contact=4, side bangs=2, 다른 그룹 기본값 유지 확인. pause/run/step/reset, hair-physics off/on, style switch API 검사 통과. 물리 마우스/UI 입력을 실제 조작한 검증은 아니다.
- Debug 40스텝/Reset 및 debug-layer 오류 없음.
- default/strong 달리기 PNG를 실제 열어 확인했다. 캡처는 `artifacts/air-resistance/`에 보존했다. 완성된 공력/스타일 튜닝이나 장시간 안정성 평가를 주장하지 않는다.

## 재현

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
.\build\jolt-only\bin\Release\HairLabDX12.exe
.\build\jolt-only\bin\Release\HairLabDX12.exe --air-check --capture-dir artifacts/air-check --log artifacts/air-check/app.log
.\build\jolt-only\bin\Release\HairLabDX12.exe --walk-test --steps 120 --air-resistance 1 --capture-every 60 --reset-check --capture-dir artifacts/air-run --log artifacts/air-run/app.log
```

`--air-resistance 0..20`은 CLI 시작 기본값을 모든 그룹에 지정한다. 실행 중에는 UI로 개별 그룹을 조절한다. 기존 사용자 경로 `build/vs2022/bin/Release/HairLabDX12.exe`도 갱신했다.

주요 수정: HairSettings/Material serialization, HairStructs, Hair update resource/binding, HairAirResistance.h, HairApplyDeltaTransform/Teleport/Integrate, LinkedHairMaterial, JoltScene 기본값, JoltMain UI/CLI, AirResistanceChecks 및 CMake shader dependencies.
