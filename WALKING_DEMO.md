> 최신 이동 애니메이션은 [NARUTO_RUN.md](NARUTO_RUN.md) 참조. 아래 절차적 보행/IK 및 1 m/s 설명은 이전 구현 기록입니다.

# WASD 보행 플레이그라운드

기본 실행은 쿼터뷰의 보행 플레이그라운드다. 카메라 기준 WASD, 가감속·방향 전환, 기존 본의 절차적 보행, 격자 타일 바닥, 추적/자유 오빗 카메라를 연결했다. Jolt Hair만 사용하며 헤어 물성·원본 자산은 바꾸지 않았다.

## 조작

- 기본 실행은 Run 상태다. **WASD**로 카메라 기준 이동한다. UI를 만진 뒤에는 **장면/바닥을 클릭**해 키보드 초점을 돌려준다. UI가 키보드를 소비하거나 창이 비활성이면 이동 입력은 중립이다.
- 우클릭 드래그 회전, 가운데 드래그 패닝, 휠 줌. **Follow character**를 끄면 카메라가 캐릭터를 따라가지 않는다. **Quarter view**는 기본 구도, F는 현재 프리셋 프레이밍, R은 카메라 리셋이다.
- Run/Pause, Step, Reset 유지. Reset은 캐릭터 위치·속도·보행·헤드 이력·Jolt 상태를 초기화한다. **Walking playground / Studio** 전환도 정지 후 재초기화하며, 순간이동 관성이 남지 않게 한다. 전환 후 Run으로 재개한다.
- Beauty / Full hair / Outfit 프리셋은 플레이그라운드에서도 클로즈업에 사용 가능하다. 이전처럼 `--view bust` 등으로 시작하면 스튜디오다.

## 구성과 좌표

`src/CharacterLocomotion.*`: 미터·오른손 +Y up의 캐릭터 위치/속도/yaw, 거리 기반 보행 위상, 지지/스윙 발 궤적과 2-bone IK, 팔 스윙을 만든다. 최대 속도 1m/s, 가속 3.5m/s², 감속 5m/s², 회전 최대 3.8rad/s. 대각선 입력을 정규화한다. 이동 거리에 맞는 0.56m 보행 주기와 60% 지지 구간을 사용한다. 발 목표는 평지 전용이고 다리의 실제 길이에 맞춰 IK의 도달 범위를 처리한다. 새 걸음 클립을 구매/추출한 것이 아니라 기존 본으로 제작한 절차적 포즈다. 접지는 고급 모션캡처 수준을 주장하지 않는다.

`scripts/prepare-walk-rig.py` → `assets/moca/walk-rig.bin/json`: 기존 본 이름으로 Hips/다리/발/팔 인덱스와 원본 발바닥 높이를 기록한다. 315개 원본 본과 기존 애니메이션 파일은 그대로다. 원본 발바닥 최저 y=0.071764417m를 배우 월드 변환에서 내려 바닥 y=0에 맞춘다.

`JoltScene::worldPose()`는 `ActorWorld × posedSkeletonJoint`를 가진다. 현재 월드 포즈를 두피 스키닝, 청자켓 팔레트, 충돌 헐, 디버그 본에 공통으로 전달한다. 헤어 중심은 실제 **Head(index 4)**이며 오래된 변수명 `neck_transform`이 남아 있어도 Neck 본으로 바꾸지 않았다.

`src/JoltMain.cpp`는 카메라의 수평 forward/right를 계산한 뒤 표시 LH→Jolt RH의 Z 반사를 입력 경계에서 한 번만 한다. 카메라 추적은 배우의 XZ 이동을 따라가며 보행의 수직 흔들림을 카메라에 복사하지 않는다. `GroundVS/GroundPS`는 월드 고정 0.5m 타일과 2.5m 구분선을 그린다. 부드러운 바닥 음영은 단순한 표시 근사이며 그림자 맵은 아니다. 바닥은 평면 표시/보행 기준이다. 벽·경사·계단·점프를 위한 캐릭터 충돌 컨트롤러는 추가하지 않았다.

## 헤드 가속도와 Jolt 관성

고정 간격 h에서 실제 포즈의 Head 월드 위치로 다음을 기록한다.

```
vHead = (pHead - previousHeadPosition) / h
aHead = (vHead - previousHeadVelocity) / h
```

리셋/모드 전환에서 위치 이력을 새 포즈에 맞추고 속도·가속도 이력을 0으로 초기화한다. 카메라 변환은 계산에 들어가지 않는다. 출력 단위는 m/s², Jolt 월드 좌표다.

**별도의 −aHead 힘은 추가하지 않았다.** Jolt `Hair::InitializeContext`는 이전/현재 헤어 월드 변환으로 delta transform을 만들고, `HairApplyDeltaTransform`이 위치·방향·속도를 변환하여 월드 관성을 유지한다. 매 업데이트의 `Hair::SetPosition/SetRotation`에 실제 Head 월드 포즈를 연결했다. 중복 관성 힘, 물성 증폭, 카메라 유래 중력, 추가 Z 변환은 없다. reset은 `Hair::OnTeleported`로 초기화한다. 헤어 업데이트·kinematic hull·PhysicsSystem 순서는 기존 통합 계약을 유지한다.

## 시간과 실행

고정 h는 기존 Jolt 입력인 float `1.f/60`이다. 일반 Run은 벽시계 accumulator로 전체 h만 실행한다. 프레임당 최대 3회, 입력 벽시간 최대 0.1초다. 더 오래 밀리면 시간을 버리고 **discarded wall time**으로 표시/로그한다. h를 키우거나 늦어진 시뮬레이션을 실시간이라고 부르지 않는다. Pause는 accumulator를 비우고, 최소화는 정지한다. 재개 시 밀린 시간을 몰아 실행하지 않는다. Jolt 내부 반복 수나 물성은 바꾸지 않았다.

`--steps` 및 컨트롤 검사는 정확히 한 번씩 명시적으로 진행한다. 테스트에 실제 눌린 키가 섞이지 않도록 bounded CLI에서는 물리 키 입력을 무시한다. `--walk-test`만 정해진 입력 스크립트를 사용한다. 일반 실행은 실제 WASD다.

```powershell
python scripts/prepare-walk-rig.py
cmake --build --preset release --target HairLabDX12 WalkingChecks -- /m:1
cmake --build --preset debug --target HairLabDX12 WalkingChecks -- /m:1
ctest --test-dir build/jolt-only -C Release --output-on-failure
ctest --test-dir build/jolt-only -C Debug --output-on-failure
.\build\jolt-only\bin\Release\HairLabDX12.exe
# 일시정지 시작 / 이전 의상 스튜디오
.\build\jolt-only\bin\Release\HairLabDX12.exe --paused
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust
# 재현 가능한 실제 걷기/헤어 캡처
.\build\jolt-only\bin\Release\HairLabDX12.exe --walk-test --steps 390 --capture-every 15 --reset-check --capture-dir artifacts/walking/replay --log artifacts/walking/replay/app.log
python scripts/validate-walking.py
python scripts/package-moca.py
.\artifacts\moca\runtime\HairLabDX12.exe
```

## 검증

`artifacts/walking/final/runs.json`, `validation.json`은 Debug/Release 실행 파일 해시와 9개 bounded 테스트의 결과다. Release 걷기 390스텝(6.5초)과 정지 비교, Debug 180스텝, 양 구성의 컨트롤 API, 스튜디오·기존 컨트롤·UI 캡처·벽시계 smoke를 포함한다. 스크립트는 0–29 정지, 30–119 전진, 120–149 정지, 150–209 옆 이동, 210–239 정지, 240–299 대각선, 300–329 정지, 330–359 반대 방향, 360–389 정지다. 이는 실제 키 누르기 녹화가 아니라 같은 이동 경로를 사용하는 결정적 입력이다.

확인한 결과:

- CPU CTest Debug/Release 통과. 직선 정상 속도의 지지 발 최대 수평 미끄러짐 2.87e-5m/s, 목표 높이 오차 1.05e-7m, 다리 길이 오차 2.69e-7m. **CPU IK 관절 검사** 수치이며 GPU 피부 표면의 접촉 정밀도라는 뜻은 아니다. 최초 테스트 harness의 Jolt allocator 초기화 누락을 고친 후 통과했다.
- Release/Debug에서 속도 최대 1m/s, 유한 헤어 상태, 초기/리셋 PNG·데이터 일치, 최대 루트 부착 오차 약 3.10e-8m. Release의 헐/본 위치 오차 약 1.24e-7m. 기록된 헤드 위치/속도로 재계산한 미분 오차 최대 약 5.22e-7.
- 동일 헤드 애니메이션의 정지 실행과 비교한 **헤드 로컬 렌더 끝점 RMS 차이 최대 약 45.5mm**. 단순한 배우의 강체 이동을 렌더 헤어 변형으로 세지 않았다. 이동·회전·보행과 그 이력을 함께 비교한 수치이며 순수 병진 가속도만의 분리 측정은 아니다.
- 컨트롤 API에서 카메라 기준 직교 입력, 이동, 감속, Pause, free camera, reset 이력, Studio 복귀 통과. Debug layer 오류 없음.
- 벽시계 smoke는 90 렌더 프레임에 실제 기록된 simulated/discarded 시간을 보고한다. 프레임 수를 60Hz 물리 스텝 수로 간주하지 않는다. 성능 벤치마크나 GPU 시간 측정은 아니다.

원본 캐릭터/헤어/물성/청자켓 형상은 보존했다. 추가는 보행 코드, 메타데이터, 현재 월드 포즈 연결, 입력/고정 시계/카메라/바닥/진단/테스트다. Jolt 알고리즘·셰이더나 Unity 소스는 수정하지 않았다. 일반 표시에서 전체 헤어 CPU readback/reupload는 추가하지 않았다.

실제 PNG는 `artifacts/walking/final/Release/walk/`에 0–390스텝, 15스텝 간격으로 있다. 여러 출발·정지·전환 포즈와 UI를 직접 확인했다. `artifacts/walking/playback.html`은 해당 완료 상태 PNG 27장을 0.25초 간격으로 재생한다. 합성/보간 동작은 없다.

물리적인 WASD 키·마우스·리사이즈/최소화 조작은 이번 도구 환경에서 검증하지 않았다. 수동 확인: 장면 클릭 후 WASD, 회전 후 W의 방향, 휠 근접 보기, Follow off 후 이동, Pause/Resume, Reset, 최소화 후 Run, 창 크기 변경. 벽/경사, 향후 다른 포즈, 생산용 보행 애니메이션 품질은 범위 밖이다.
