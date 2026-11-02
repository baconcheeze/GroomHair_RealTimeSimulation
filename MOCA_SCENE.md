# 모카 캐릭터 / 원본 리그 / 자체 제작 Jolt 헤어

최신 변경: 사용자 요청으로 `Hair_Kemomimi` 동물귀 메시를 변환 대상에서 제외했습니다. 현재 39,582 vertices / 60,714 triangles이며 315개 본과 Jolt 헤어는 유지합니다. 아래 최초 통합의 수치/캡처 기록은 변경 전 기록입니다. 제거 후 Debug/Release 빌드와 배포본 30-step/reset 검사를 통과했고 `artifacts/moca/packaged-test/step-0000.png`를 열어 귀가 없는 것을 확인했습니다.

## 결과

기본 앱을 모카 캐릭터로 교체했습니다. 원본 315개 본과 계층/이름/바인드를 유지하며, 기존 모델의 귀/꼬리도 캐릭터 액세서리로 유지합니다. 두피 헤어는 별도로 제작한 midnight-blue 긴 생머리 + 앞머리입니다. 모카 VRM의 정적 헤어 메시를 흔드는 방식이 아니라 Jolt의 실제 가이드 시뮬레이션과 렌더 헤어 업데이트를 사용합니다.

| 항목 | 값 |
|---|---:|
| 표시/스칼프 vertex / triangle | 41,924 / 64,742 |
| 원본 bones / weights per vertex | 315 / 4 |
| Jolt guides / render strands | 1,600 / 16,000 |
| strand points | 32 |
| head/neck/chest hulls | 3 |
| 자체 demo animation | 240 frames / 60Hz |

VRM 자체에는 애니메이션 클립이 없습니다. 원본 head/neck 본의 작은 회전 루프와 원본 upper-arm 본을 내린 표시 포즈를 작성했습니다. 임의의 2-bone 대체 리그가 아닙니다. full facial morph / Unity toon shader / 액세서리 spring simulation을 가져왔다는 주장은 하지 않습니다.

## 코드 및 데이터

- `scripts/import-moca.py`: stdlib VRM reader, full rig/skin import, native pose baking, original groom authoring. 원본 패키지/VRM에 쓰지 않습니다.
- `assets/imported/moca/`: 추출 VRM, source PSD/material references. 원본 모델 권리는 원작자에게 있습니다.
- `assets/moca/`: 런타임에 필요한 변환 데이터와 텍스처. 포맷/수치/라이선스는 `JOLT_ASSETS.md`, 전체 payload SHA256은 `manifest.json`.
- `src/JoltScene.cpp`: 새 asset 경로/헤어 파일 사용. 기존 native Jolt initialization/update와 material 역할을 유지합니다.
- `src/JoltMain.cpp`: 315-joint normal skinning, rig overlay, 카메라, Moca UI. `saveState`의 explicit readback 전에 graphics fence wait를 추가했습니다. Debug 제어 테스트에서 발견한 queue 간 경합을 해결한 변경입니다.
- `src/CharacterTexture.*`: one-time WIC decode/texture-array upload. 정상 프레임에 full hair CPU readback/re-upload를 추가하지 않았습니다.
- `shaders/JoltDisplay.hlsl`: 원본 UV 텍스처와 4-weight normal skinning. Jolt 물리 셰이더는 바꾸지 않았습니다.
- `scripts/validate-moca.py`, `scripts/package-moca.py`, `scripts/capture-moca-animation.py`: bounded 검사, private runtime staging, 실제 checkpoint GIF.

Jolt v5.6.0 `e77f175595e64cb44218cc9d9d56fc365ad0e36a`, 기존 ImGui 및 SDK DXC 경로를 유지합니다. MSVC VS2022 / Windows SDK 10.0.26100.0에서 Debug/Release 빌드 성공. `build/jolt-only`와 사용자가 사용하던 `build/vs2022`의 Debug/Release target도 갱신했습니다.

## 실제 검사

RTX 5070 Ti hardware에서 Release 120-step, Debug 60-step native Jolt run을 실행했습니다. 가이드/렌더 팁과 scalp/hull pose의 변화, 전체 읽기 데이터 finite, root attachment, exact reset을 검사했습니다.

| 검사 | Release | Debug |
|---|---:|---:|
| guide tip 최고 checkpoint RMS 변화 (hair-local m) | 0.034653 | 0.033863 |
| render tip 최고 checkpoint RMS 변화 (hair-local m) | 0.034373 | 0.033536 |
| 최대 root attachment error (m) | 3.00411e-8 | 3.07250e-8 |
| initial/reset JSON와 PNG 동일 | 통과 | 통과 |
| Pause/Step/Run-Pause/Reset/camera state API | 통과 | 통과 |
| D3D12 debug-layer error | Release layer 꺼짐 | 없음 |

수치는 `artifacts/moca/final/validation.json`에 있습니다. 다른 cwd (`C:\Users\caw20\MyProject`)에서 private runtime의 30-step + reset도 exit 0. 초기 Debug controls의 queue 경합 실패 로그는 `artifacts/moca/validated/`에 보존했고 수정 후 전체 content 검사 결과는 `artifacts/moca/final/`에 분리했습니다.

캡처: `artifacts/moca/final/Release/motion/step-0000..0120.png`, Debug motion, rig/UI, side. 최종 정면 0/60, rig/UI, side 30 이미지를 실제로 열어 얼굴·헤어·회전·본/hull 표시를 확인했습니다. `artifacts/moca/moca-motion.gif`는 Release checkpoint들만 연결했습니다. checkpoint 간 0.25 simulated seconds, 12fps 재생이며 마지막 checkpoint를 0.25초 유지합니다. 데스크톱 스크린샷이나 가짜 변형이 아닙니다.

물리 mouse/keyboard/resize는 자동 API 검사로 검증했다고 주장하지 않습니다. 수동 확인: RMB orbit, MMB pan, wheel, 창 크기 변경, Run/Pause/Step/Reset 클릭. 머리 hull은 imported surface 기반, 목/상체는 근사 convex이므로 production collision fit 검증은 아닙니다. 의상·투명 표현·헤어 끝 모양은 후속 미술 작업 여지가 있습니다.

## 재실행

```powershell
cmake --preset vs2022
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe
# 또는 로컬 배포본
.\artifacts\moca\runtime\HairLabDX12.exe

cmake --build --preset debug --target HairLabDX12 -- /m:1
python scripts/validate-moca.py
python scripts/package-moca.py
python scripts/capture-moca-animation.py
```

View: `--view portrait|bust|side`. 뼈대: UI Imported skeleton 또는 `--bones`. Head/scalp를 끄면 내부 본을 더 쉽게 볼 수 있습니다. 뼈대는 원본 본의 현재 pose로 그리며 hair geometry readback이 아닙니다.

원본 VRM SHA256 `d9371de1ed3080baf927b911b86c332a7683baf8fc63df052cdd06899f50efd2`.
character.bin `073669de52dda6ec769f60477adec42470bc8f3585426331bc54a7e57ca07300`.
studio.hair `d0280506307d870c3ddc569f84efc2b31035701c7e00d49e33dd123fddfe3136`.
Release exe `6b3fd29c04a50055a611c9afda5eef6e1ddc3f31755043567e1435718275519e`.
Debug exe `c2dff18c7f81572f5cb3360e82c0f78036ac4e9ff820ec97599a2085523b5fd5`.

원본 패키지/VRM 및 Unity 프로젝트를 수정하지 않았습니다. 기존 커스텀 물리 코드를 되살리지 않았고, Jolt 알고리즘 개선이나 solver 비교 작업도 하지 않았습니다. 정상 실행에는 원본 패키지, Unity, Python, ffmpeg가 필요하지 않습니다. 패키지는 로컬 검사용이며 캐릭터 원본의 이용/재배포 조건을 유지합니다.
