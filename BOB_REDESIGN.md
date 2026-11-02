# 단발 흐름 재설계 — flow-v2

이전 단발의 큰 V자 두 덩어리, 관자놀이 고리, 별도로 떠 있는 잔머리, 아래로 몰린 볼륨과 일괄 절단 끝을 정리했습니다. 사진의 가르마와 얼굴 프레이밍을 기준으로 다시 만든 1차 미술 수정입니다. 사진의 사실적인 머리카락을 그대로 재현한 최종 작품이라고 주장하지 않습니다.

## 변경

- `scripts/create-moca-bob.py`: 눈꺼풀/귀의 세부 표면을 따라가던 crown sampling을 제거. 두피에 정확히 붙인 root에서 smooth crown envelope로 이어지고, 옆·뒤는 접선 방향이 이어지는 cubic curve로 내려갑니다. 곡선을 arclength로 32점 resample합니다.
- 앞머리 대부분은 관자놀이와 볼 프레이밍으로 흐릅니다. 소량은 동일한 곡선 계열에서 짧게 끝나게 하여 이마를 성기게 남겼습니다. 독립된 wire/Bezier 잔머리 경로는 없습니다. 옆머리는 위에서 아래로 매끄럽게 이어지며 끝 길이를 layer별로 분산했습니다.
- `shaders/JoltDisplay.hlsl`: 실제 CURRENT render positions로 가닥 방향을 구해 간단한 fiber 명암/광택을 적용합니다. 끝 15%는 alpha를 점진적으로 줄입니다. `JoltMain.cpp`의 line PSO만 alpha blending을 사용하고 head PSO는 opaque입니다. 이는 간단한 display shading이며 정렬된 production transparency/OIT 구현은 아닙니다.
- `scripts/refit-moca-head-hull.py`: 회전 검사에서 보강된 뒤통수가 드러나는 문제가 발견되어, 새 posterior dome을 포함한 2,519 source points로 기존 head-attached Jolt hull을 맞췄습니다. Jolt가 그대로 convexify/충돌 처리합니다. 목/몸 hull과 애니메이션, 승인된 두상 메시 위치는 유지합니다. 보강 전 open skull을 기준으로 만든 hull에 새 두피가 포함되지 않은 것이 큰 원인이었습니다.

16,000 authored/render strands / 1,600 guides, strand당 32 points, 기존 Jolt material 값과 gravity/update 경로를 유지합니다. 기존 긴 머리 `studio.hair`는 hash가 동일합니다. physics algorithm/shader는 수정하지 않았습니다. 이번 수정에는 head collision **geometry** 재피팅이 포함되며, 물리 상태나 머리를 고정해 만든 모션이 아닙니다.

현재 표시 path는 실제 Jolt GPU 출력이며 normal runtime의 full-hair CPU readback/re-upload를 추가하지 않았습니다. source VRM, Unity, 캐릭터 원본에는 변경을 가하지 않았습니다.

## 재현

```powershell
# 완전히 재생성할 때의 순서
python scripts/import-moca.py
python scripts/reinforce-moca-scalp.py
python scripts/refit-moca-head-hull.py
python scripts/create-moca-bob.py

cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
python scripts/validate-moca-bob.py
python scripts/capture-moca-bob.py
python scripts/package-moca.py
.\artifacts\moca\runtime\HairLabDX12.exe
```

현재 staged scalp가 있으면 `refit-moca-head-hull.py`부터 실행할 수 있습니다. head-hull 도구는 같은 입력에 멱등 실행합니다. 최초 데이터에서부터 시작하려면 위 전체 순서를 따르세요. `scalp-patch.json`에 head-hull refit과 output hash가 기록되고, `bob.json`에 실제 character SHA256, revision, groom hash가 저장됩니다.

앱은 정면·pause로 시작합니다. **Head turn + idle sway** 체크 후 **Run**을 누르면 실제 head/hair 움직임을 볼 수 있습니다. Hairstyle로 이전 긴 머리도 선택 가능합니다.

## 증거와 범위

최종 검사/캡처는 `artifacts/moca-bob-redesign/validated/`. `final/`과 `probe/`, `flow/` 등은 조정 중 결과이며, 특히 `final/`의 뒤통수 캡처는 hull을 맞추기 전 실패 증거입니다. 이를 최종 결과로 사용하지 마세요. `hull-fit/back/`에는 수정 직후의 30/60-step rear 캡처도 있습니다.

최종 Debug/Release 단발 run, long-style run, style switch/pause/reset API, side/back animation, root attachment/finite/reset equality를 검사합니다. raw 결과는 `validated/validation.json`, 명령/exit/exe hashes는 `validated/runs.json`, GIF는 `artifacts/moca-bob-redesign/bob-motion.gif`. GIF는 실제 15-step PNG checkpoint를 0.25초 간격으로 연결합니다. 측정한 CPU/GPU 성능이나 full-groom 장기 안정성 주장과는 무관합니다.

실행 결과: Release 단발 120steps / Debug 60steps, long 30steps, side 30steps, rear 60steps, 양 구성 controller API 모두 exit 0. native attachment 최대 오차 약 3.074e-8m, 16,000 root의 mesh-ray 오차 최대 5.98624e-8m. guide tip 최고 checkpoint RMS 변화는 Release 0.014010m / Debug 0.013300m, render tip 0.014466m / 0.013551m. 실행 길이가 다르므로 이 두 수치를 Debug/Release 동등성 오차로 해석하지 않습니다. 모든 readback finite, 초기/reset JSON·PNG 동일, Debug-layer error 없음. `build/vs2022`의 Debug/Release도 새 shader/asset으로 갱신했습니다.

정면 step 0/60, side 0/30, rear 30/60을 실제로 열어 외형과 두피 관통 여부를 확인했습니다. API 검사로 실제 마우스/resize 동작을 검증했다고 주장하지 않습니다. 렌더링은 여전히 얇은 선 기반으로, 완성형 hair-fiber coverage/shadow 재질은 아닙니다. 미세한 part/root 노출과 곡선/재질은 후속 미술 피드백 대상입니다.
