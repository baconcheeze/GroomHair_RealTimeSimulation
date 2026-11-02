# NarutoRun FBX 이동 애니메이션

사용자가 제공한 `assets/NarutoRun.fbx`를 Moca의 기존 315개 본에 리타게팅했습니다. WASD 이동은 이전 절차적 보행/발 IK 대신 이 클립을 사용합니다. Jolt Hair와 헤어 물성, 캐릭터 메시, 청자켓은 그대로 유지합니다.

## 실행

```powershell
cmake --build --preset release --target HairLabDX12 WalkingChecks -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe
```

또는 `artifacts\moca\runtime\HairLabDX12.exe`. 장면을 클릭한 뒤 WASD로 이동합니다. 우클릭 드래그로 시점 회전, 가운데 버튼으로 팬, 휠로 확대합니다. Studio는 기존 감상 모드입니다. UI 조작 후에는 장면을 클릭해 키보드 초점을 돌려주세요.

## 클립과 변환

- 원본 액션: `Armature|mixamo.com|Layer0`, 1–39 프레임, 60 FPS, 38프레임 간격의 0.633333초 루프.
- 런타임: `assets/moca/naruto-run.bin` (RUNA v1), 상세 매핑/해시: `naruto-run.json`.
- Mixamo +Z 전방을 Moca −Z 전방으로 Y축 180도 회전합니다. 원본과 대상의 월드 회전 델타를 변환한 뒤 부모 기준 로컬 회전으로 저장합니다. 기존 본 길이를 유지합니다.
- 원본의 선형 XZ 루트 이동만 제거하고 WASD 이동이 대신합니다. 클립 내부 좌우 흔들림과 수직 자세는 보존합니다. 다리 길이 비율로 루트 이동을 맞추고 클립 전체에 일정한 바닥 높이 보정을 적용합니다. 프레임별 발 고정/IK는 없습니다.
- 변환된 보폭 약 2.030913 m, 원래 재생 속도에 맞는 최고 이동 속도 약 3.206705 m/s. 이동 거리로 재생 위상을 진행하므로 가감속 때 재생 속도도 달라집니다.
- 원본의 깊은 상체 숙임과 뒤로 뻗은 팔을 사용합니다. 정지/출발은 기존 대기 자세와 크로스페이드합니다.
- Blender는 오프라인 변환에만 사용합니다. 실행에는 FBX 파서, Blender, Unity가 필요하지 않습니다.

재변환:

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts\blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/retarget-naruto-run.py
```

원본 FBX SHA256: `9ed947eb6fe3be5bfd2ff1f48692a9690369cfd158f1120d1835381e75d4cc1b`.
변환 클립 SHA256: `3df7cb3215a1f1413e6fdccbb204bdc02d35c1dd64610a8e529cc0baf1b27d1b`.
사용자 제공 파일이며 별도의 CC0/재배포 권한을 주장하지 않습니다.

## 검증과 제한

Debug/Release 및 별도 VS2022 빌드 성공. 양쪽 CTest의 walking-cpu 통과. 실제 앱에서 9개 제한 실행 검사(390스텝 이동/정지 비교, Debug 180스텝, 컨트롤러, 리셋, 스튜디오, UI 캡처, 클록)를 통과했습니다. 검사 명령:

```powershell
ctest --test-dir build/jolt-only -C Release --output-on-failure
ctest --test-dir build/jolt-only -C Debug --output-on-failure
python scripts/validate-naruto-run.py
python scripts/package-moca.py
```

원본 캐릭터/단발 자산 해시 불변, 유한 상태, 뿌리 부착 및 리셋을 확인했습니다. 런타임 본 길이 오차 최대 약 1.8 μm, 뿌리 오차 약 0.031 μm. 실제 마우스/WASD 입력은 수동 미검증이며 API 입력으로 검사했습니다.

캡처: `artifacts/naruto-run/final/Release/walk/step-*.png`, 재생: `artifacts/naruto-run/playback.html` (실제 15스텝 간격 캡처, 시뮬레이션 1배속). 달리기 및 정지 이미지를 직접 확인했습니다. 과격한 숙임/일어남과 방향 전환에서 헤어가 크게 튀는 현상이 남습니다. 마지막 정지 상태에서도 헤어가 들린 모습이 관찰되어 헤어까지 자연스럽게 완성됐다고 주장하지 않습니다. 물성을 바꿔 숨기지는 않았습니다.

파일: `scripts/retarget-naruto-run.py`, `src/CharacterLocomotion.*`, `src/JoltMain.cpp`, `tests/WalkingChecks.cpp`, 변환 클립/메타데이터, 검증/패키징 스크립트. 변환 결과와 검사 원자료는 `artifacts/naruto-run/`에 보존합니다.
