# 윗머리·옆머리 볼륨 보강 — flow-v3-volume

현재 앞머리 retention 기본값은 진단 후 **root 0.01 / tip 0.001**로 바뀌었습니다. 아래 단계적 절반 조정은 과거 기록이며, 최신 A/B와 재현 명령은 `FRINGE_MOTION_DIAGNOSIS.md`를 보세요.

최신 사용자 조정: 앞머리 `mGlobalPose`를 root 0.35→0.175 / tip 0.12→0.06으로 절반 낮췄습니다. `mSkinGlobalPose`는 root 1.0→tip 0.25를 유지합니다. Debug/Release 빌드 및 Debug 60-step/reset/debug-layer 검사 통과, 캡처/로그 `artifacts/fringe-pose-half/`.

후속 사용자 조정: 앞머리 `mSkinGlobalPose`를 전 구간 1.0에서 root 1.0 → tip 0.25의 linear gradient로 낮췄습니다. `mGlobalPose` 강도는 유지했습니다. Debug/Release 재빌드 및 Debug 60-step/reset, debug-layer 검사를 통과했고 `artifacts/fringe-follow/`에 해당 캡처/로그가 있습니다.

`create-moca-bob.py`의 authoring 형태만 조정했습니다. 정수리/옆쪽 crown envelope를 넓히고, 두피에 붙은 root에서 10mm 구간 동안 부드럽게 올라오는 lift를 추가했습니다. 뒤쪽은 과도하게 불리지 않도록 lift 방향을 주로 위·옆으로 제한합니다. 아래쪽 cubic은 실제 crown 끝의 접선을 이어받아 볼륨이 갑자기 꺾이지 않게 했습니다.

정수리의 최소 polar angle 때문에 비어 있던 작은 root 영역은 1,116개 crown roots를 재배치해 채웠습니다. root를 공중으로 옮긴 것이 아니라, 변경한 root 역시 실제 보강 두피 삼각형에 raycast합니다. 총 16,000개 strands / 1,600 native Jolt guides와 strand order/counts를 유지합니다. 가르마의 얇은 선과 이마 프레이밍은 남아 있으며, 두피 색을 칠하거나 depth testing을 꺼서 숨긴 것은 아닙니다.

## 변경량

authoring rest points 기준, 이전 전달한 flow-v2와 비교:

- 최고점 +**11.44mm**.
- 전체 좌우 폭 +**27.20mm**, 한쪽 약 **13.6mm**.
- 이전/이후 bounds와 SHA256, root 재배치 개수: `artifacts/moca-bob-volume/bounds.json`.
- 비교용 이전 groom payload: `artifacts/moca-bob-volume/before.hair`.

이 수치는 rest geometry bounds이며, 움직이는 상태의 고정된 부피나 GPU 실행 시간을 뜻하지 않습니다. Jolt material, gravity, collider, head mesh, rig, shaders는 이번 작업에서 바꾸지 않았습니다. 기존 긴 머리 payload도 유지합니다.

## 빌드와 확인

```powershell
python scripts/create-moca-bob.py
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
python scripts/validate-moca-bob.py
python scripts/capture-moca-bob.py
python scripts/package-moca.py
.\artifacts\moca\runtime\HairLabDX12.exe
```

새 결과는 `artifacts/moca-bob-volume/final/`에 저장합니다. 초기 볼륨 실험의 `validated/`와 최종 `final/`은 구분합니다. normal application은 정면·pause 상태로 시작하며 **Head turn + idle sway** 체크 + **Run**으로 움직입니다. 실제 PNG checkpoint GIF는 `artifacts/moca-bob-volume/bob-motion.gif`입니다.

기존 bounded content 검사에서 Debug/Release 움직임, 모든 roots의 mesh 부착, finite state, 정확한 reset, style switch/pause/step API를 검사합니다. 최종 실행 결과와 명령/exit/exe hashes는 `final/validation.json`, `final/runs.json`입니다. mouse/resize를 CLI로 검증했다고 주장하지 않습니다. 기존 long style과 head mesh를 바꾸는 작업은 하지 않았습니다.

최종 검사 실행 완료: Release 단발 120steps, Debug 60steps, side 30steps, rear 60steps, long 30steps 및 양 구성 controller API 통과. root mesh-ray 최대 오차 5.982e-8m, Jolt attachment 최대 오차 약 3.074e-8m. guide tip 최고 checkpoint RMS 변화 약 0.027630m / render tip 약 0.027832m. finite 및 initial/reset JSON·PNG 동일, Debug-layer error 없음. 최종 정면 0/60, side 30, rear 30 PNG를 실제로 열어 확인했습니다. 가르마의 자연스러운 얇은 root 선은 남아 있으며, 넓은 빈 crown disk를 메우고 위·옆의 silhouette를 키운 수정입니다.
