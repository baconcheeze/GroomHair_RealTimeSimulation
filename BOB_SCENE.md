# 사진 참고 단발 / 긴 머리 선택

최신 버전은 **flow-v2**입니다. 가닥 흐름/실루엣을 다시 만들고 머리 hull을 새 두피에 맞췄습니다. 현재 결과와 재생성 순서는 `BOB_REDESIGN.md`를 보세요. 아래는 이전 버전의 기록입니다.

## 최신: 보강한 두피에 다시 맞춘 단발

뒤통수 경계를 막고 8mm 다듬은 현재 `character.bin`을 기준으로 단발을 다시 작성했습니다. 뿌리는 이전 긴 머리에서 복사하지 않고 **현재 두피 삼각형에 16,000개를 raycast**합니다. crown은 실제 두피를 샘플링한 49×128 support grid를 이용해 적당한 두께를 더하며, 턱선 부근 C컬과 가르마, 소량의 얇은 앞머리를 별도로 만듭니다. root는 정확한 삼각형 위에 있고 grid는 authoring에만 사용합니다. Jolt 시뮬레이션/스칼프/렌더 재구성 대체물이 아닙니다.

보강한 두피와 긴 머리 파일은 이번 단발 작업에서 수정하지 않았습니다. 새 generator `scripts/groom_surface.py` 및 수정한 `create-moca-bob.py`가 native asset만 읽으며 Unity/원본 VRM을 다시 읽거나 쓰지 않습니다. `bob.json`에 실제 scalp SHA256을 저장합니다. 과거 버전의 'long groom과 root bits 동일' 조건은 새 scalp에 재배치한다는 요청에 따라 적용하지 않으며, 대신 모든 새 root가 현재 scalp에 1µm 이내로 놓였는지 검사합니다. 기존 motion/root/pause/reset 검사 허용치는 유지합니다.

정면+단발 표시+pause로 시작합니다. Run으로 머리 애니메이션과 실제 Jolt 움직임을 실행합니다. `--no-hair --view back`은 두피 확인용입니다. 현재 결과는 `artifacts/moca-bob-refit/final/`, GIF는 `artifacts/moca-bob-refit/bob-motion.gif`에 분리하며 이전 캡처는 보존합니다.

원본부터 재생성 순서: `import-moca.py` → `reinforce-moca-scalp.py` → `create-moca-bob.py` → build. 이미 staged scalp가 있으면 마지막 groom 생성만 실행하면 됩니다.

이번 refit 검증: Debug/Release 빌드 성공, Release 단발 120steps / Debug 60steps, 긴 머리 30steps, bob→long→bob 교체와 pause/reset API 통과. 16,000개 root의 현재 두피와의 최대 오차 **5.96031e-8m**, native Jolt root attachment 최대 약 **3.074e-8m**. guide tip 최고 RMS 변화 약 **0.016877m**, render tip 약 **0.018194m**. finite/reset equality 및 Debug-layer no-error 확인. 정면 0/60, side, UI PNG를 열어 확인했습니다. 실제 mouse/resize 조작은 검증하지 않았습니다. 이전 root 복사 방식의 기록/수치/이미지는 그대로 남겨 두었습니다.

아래는 보강 전 최초 단발의 구현/검증 기록입니다.

`assets/단발.webp`의 가르마, 얼굴을 감싸는 턱선 부근 볼륨, 안으로 말리는 끝을 참고한 1차 strand groom입니다. 갈색 단발이 기본이고, 이전 midnight-blue 긴 머리는 UI **Hairstyle → Long straight (original)**로 선택합니다. 스타일 변경 시 pause/reset하며 **Run**으로 다시 움직입니다. CLI는 `--hairstyle bob|long`입니다.

사진의 모든 잔머리나 사실적인 재질을 재현한 최종 미술 결과는 아닙니다. 캐릭터 비율에 맞춘 형태이며 앞머리 밀도와 층/끝 모양은 후속 조정 가능합니다. 실제 side/움직임 캡처도 남겼습니다.

## 구현

- `assets/moca/studio.hair`: 기존 긴 머리 그대로. SHA256 `d0280506307d870c3ddc569f84efc2b31035701c7e00d49e33dd123fddfe3136`.
- `assets/moca/bob.hair`, `bob.json`: 별도 단발 데이터와 생성/참고 provenance. 기존과 동일한 16,000 roots의 float bits를 그대로 보존합니다. 16,000 strands ×32 points, Jolt 1,600 guides.
- `scripts/create-moca-bob.py`: 기존 scalp roots에서 왕관부 볼륨, 가르마 앞머리, 안쪽 C컬을 authoring. back/sides + fringe의 기존 Jolt material 구분을 사용합니다.
- `JoltScene::bob`: 선택된 groom만 upstream Jolt 초기화에 전달. Jolt 알고리즘/물리 shader/계수 변경 없음.
- `JoltMain.cpp`: Hairstyle UI, CLI, 스타일별 색, fence 완료 후 reset 교체, 진단 JSON의 hairstyle 필드.

## 실행/생성

```powershell
python scripts/create-moca-bob.py
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe
# 이전 머리
.\build\jolt-only\bin\Release\HairLabDX12.exe --hairstyle long

cmake --build --preset debug --target HairLabDX12 -- /m:1
python scripts/validate-moca-bob.py
python scripts/capture-moca-bob.py
python scripts/package-moca.py
```

## 수행한 확인

Debug/Release 빌드 성공. 사용자 사용 경로 `build/vs2022` Debug/Release도 업데이트했습니다. Release 단발 120steps, Debug 단발 60steps, Release 긴 머리 30steps를 실행했습니다. Debug/Release controller API에서 bob → long → bob 교체, pause/step/run-pause/reset, camera-state 분리를 검사했습니다. 실제 마우스로 dropdown/resize를 조작한 검사는 아닙니다.

- 단발 guide tip 최고 checkpoint RMS 변화: 약 0.026101m, render tip 약 0.026588m (hair-local).
- 최대 root attachment error: Release 3.00411e-8m, Debug 3.07250e-8m.
- 모든 검사용 geometry finite, 초기/reset JSON 및 PNG 동일.
- Debug D3D12 layer error 없음.
- 긴 머리 payload hash 불변 및 모든 단발 root bits 동일 확인.

원시 결과 `artifacts/moca-bob/final/validation.json`, 실행 명령/exit/실행파일 hashes `runs.json`. 최종 정면 0/60, UI, side 30 PNG를 실제 열어 확인했습니다. `artifacts/moca-bob/bob-motion.gif`는 실제 Release 15step 간격 PNG만 연결한 것으로 checkpoint 간 0.25 simulated seconds, 마지막 프레임을 0.25초 더 유지합니다. GPU timestamp나 실시간 성능을 측정한 실험은 아닙니다.

Jolt 시뮬레이션과 렌더 헤어 업데이트는 계속 활성입니다. 애니메이션에 따라 컬이 펴지거나 끝 모양이 바뀌는 것은 실제 물리 출력입니다. 기존 캐릭터/본/asset 이용 조건은 `JOLT_ASSETS.md` 및 `assets/moca/LICENSE.txt`를 따릅니다.
