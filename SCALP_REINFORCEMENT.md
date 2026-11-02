# 헤어 숨김 / 뒤통수 두피 보강

현재 앱은 이 보강과 8mm 조정을 유지한 채 새 단발 표시로 돌아왔습니다. 아래는 두피 보강 단계의 기록입니다. 지금 두피만 보려면 `--no-hair --view back`을 사용하세요.

후속 형태 조정: 보강한 뒤통수 중앙을 약 **8mm 안쪽**으로 줄였습니다. 원래 32-vertex 경계는 고정하고 안쪽으로 갈수록 부드럽게 줄여, 얼굴·귀·바인드·기존 정점은 유지합니다. 설정은 `OCCIPITAL_INSET`이며 smooth display normal도 같은 support dome에서 다시 계산합니다. 측면 캡처 `artifacts/scalp/contour/side/step-0000.png`를 열어 확인했습니다. 이 조정 후에도 패치 edge 연결 검사와 Debug/Release 빌드, 15-step/reset 검사 및 Debug layer 검사를 수행했습니다.

현재 앱은 헤어를 숨기고 뒤쪽 카메라·pause 상태로 시작합니다. 단발과 긴 머리 데이터는 그대로 보존했습니다. 필요할 때 UI `Render hair`, `Guide hair` 또는 CLI `--hair`, `--guides`로 표시할 수 있습니다. 카메라는 `--view portrait|bust|side|back`. `Run`은 기존 머리 애니메이션과 Jolt 업데이트를 실행합니다. `--steps N`은 bounded run을 명시적으로 시작합니다.

## 보강한 부위

원본 VRM Face primitive에 있던 32-vertex posterior scalp loop를 찾았습니다. 기존 경계 정점을 직접 재사용하여 **353 vertices / 736 triangles**를 추가했습니다. 뒤통수의 곡면은 support dome에 맞추되 바깥 경계는 원본 위치와 정확히 연결합니다. 기존 얼굴을 구체로 대체하거나 뒤집힌 면을 양면 렌더로 숨긴 것이 아닙니다. Back-face culling은 유지합니다.

새 패치는 head joint **4**에 100% 스키닝합니다. 기존 원본 정점 위치·삼각형 순서·skin weights·315-bone rig·animation/hull payload는 유지합니다. 패치에는 별도 neutral skin 색과 support dome 기반 smooth display normals를 적용했습니다. 원본 atlas의 잘못된 scalp-border 색은 뒤쪽에서 점진적으로 neutral skin으로 섞어 표시합니다. 얼굴의 눈·입 텍스처는 유지합니다. 이번 작업은 UV 미술 완성이나 head collision hull 재피팅 작업은 아닙니다.

최종 캐릭터: **39,935 vertices / 61,450 triangles**. 닫은 부위의 모든 edge는 정확히 두 개의 면에 속함을 1µm geometric weld 기준으로 검사했습니다. 눈·입 등 원본 모델의 다른 개방 경계를 전부 닫았다는 뜻은 아닙니다.

## 재현과 파일

```powershell
python scripts/reinforce-moca-scalp.py
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
python scripts/validate-moca-scalp.py
.\build\jolt-only\bin\Release\HairLabDX12.exe
```

패치 도구는 `assets/moca/character.bin`, `colors.bin`, `surface.bin`에만 적용하며 `scalp-patch.json`과 manifest의 hashes/counts를 갱신합니다. 멱등 실행을 지원하고 예상과 다른 source 변경은 중단합니다. 원래 세 payload의 focused baseline은 `artifacts/scalp/source/`에 있습니다. 원본 VRM에서 전부 재생성하는 경우 `scripts/import-moca.py` 다음에 이 보강 도구를 실행하세요. `studio.hair` / `bob.hair` hashes는 바뀌지 않았습니다.

## 수행한 확인

- Debug/Release 빌드. Release front/side/back 캡처와 back 30-step + reset, Debug back 30-step + reset.
- 새 patch 모든 GPU 정점의 head-local skinning을 원본 inverse-bind 변환과 비교: 최대 **1.35012e-7m (Release), 1.34826e-7m (Debug)**.
- patch world-space 최대 이동 약 **0.018913m**: head pose를 따라 움직임. finite 및 initial/reset JSON/PNG 동일.
- Release controller API pause/step/reset/camera-state 확인. Debug layer error 없음.
- 최종 back step 0/30, front/side PNG를 실제로 열어 확인. 물리적인 마우스/resize 조작은 검사하지 않았습니다.

원시 topology/hashes: `assets/moca/scalp-patch.json`, `artifacts/scalp/topology.json`.
실행 명령/실행파일 hash/exit: `artifacts/scalp/final/runs.json`.
수치 결과: `artifacts/scalp/final/validation.json`.
이미지: `artifacts/scalp/final/Release/{back,front,side}/`.

Jolt 물리 알고리즘과 원본 VRM/패키지에는 변경을 가하지 않았습니다. 정상 실행은 여전히 staged native assets만 사용합니다. 기존 hair pipeline은 보존했고, 현재 화면에서 숨긴 상태입니다.
