# 앞머리 움직임 진단 및 보정

## 확인된 원인

앞머리 버퍼가 멈춘 것이 아니라, 앱에서 넣은 **매 iteration global-pose 복원**이 너무 강했습니다. 기존 `mGlobalPose = {0.175, 0.06, 0, 1}`은 기본 360 iterations/s에서 적용됩니다. 60Hz당 약 6회이므로 tip의 0.06도 고정 target에 대한 단독 blend를 계산하면 프레임당 약 **31%**를 복원합니다. 중간부는 더 강합니다. 이는 다른 물리 연산을 포함한 수렴률이 아니라, 이 보정만 분리한 설명입니다.

확인한 upstream 경로:

- `HairSettings.h`: mGlobalPose는 **매 iteration** 적용하는 fraction이며 기본 360 iterations/s 기준.
- `Hair.cpp`: dt에 맞춰 `MakeStepDependent` 후 material constants에 업로드.
- `Shaders/HairApplyGlobalPose.h`: 위치와 quaternion 모두 목표 자세로 LERP 후 quaternion normalize.
- `HairUpdateVelocity*.hlsl`: pose 보정 후 collision/velocity 갱신 및 다음 integration.

앞머리 guide는 **320개**이고, 실제 initialized state에서 각 strand의 free vertices는 **31개**입니다. root만 fixed로 생성됩니다. Pose를 끄자 큰 변형이 생겼고 render output에서도 확인되어, 전체 strand pinning / 정지된 GPU 버퍼 / frozen render-hair가 원인이 아님을 확인했습니다.

`mSkinGlobalPose`의 주된 역할은 목표 자세의 skin 추종입니다. 현재 두피는 hair/head-local 좌표에서 거의 강체로 유지됩니다. baseline capture의 head-local root position drift 최대 1.644e-7m, root rotation drift 4.799e-7rad입니다. tip skin-follow를 0.25에서 0으로 바꿔도 rest 대비 fringe RMS는 1.019→1.009mm로 거의 같았습니다. 그래서 이 값부터 낮춘 것은 주원인을 해결하지 못했습니다.

## 동일 입력 A/B

같은 hair/head assets, h=앱의 실제 float 1/60, 같은 120 physical steps / 15-step checkpoints / head animation을 사용했습니다. 현재 가이드의 material index로 앞머리를 분리했으며 원본 파일의 material index로 render strands도 분리했습니다. 모든 case는 explicit capture/readback에서만 측정합니다.

`rest 대비`는 초기 hair-local tips와의 차이로 정적 중력 처짐을 포함합니다. `animation-induced`는 **동일 step의 애니메이션 on/off**를 hair-local 좌표에서 비교한 것으로, 단순한 rigid head 이동은 제외되며 관성 및 중력의 head-frame 변화는 포함됩니다. 표는 checkpoint 중 최대 RMS입니다. global nonlinear 최적화/solver benchmark가 아닙니다.

| Profile (root / tip / end fraction) | rest 대비 guide RMS mm | animation-induced guide RMS mm | 판단 |
|---|---:|---:|---|
| 기존 0.175 / 0.06 / 1 | 1.019 | 0.251 | 시각적으로 거의 고정 |
| pose OFF 0 / 0 / 1 | 51.228 | on/off pair 미실행 | 실제 움직이나 얼굴을 덮음 |
| 0.01 / 0 / 0.3 | 50.737 | 8.567 | 움직임은 크나 스타일 유지 부족 |
| 0.03 / 0.008 / 1 | 5.682 | 0.506 | 반응이 여전히 작음 |
| 0.015 / 0.003 / 1 | 10.516 | 1.081 | 완화되지만 작은 반응 |
| **선택 0.01 / 0.001 / 1** | **16.523** | **2.970** | 얼굴 프레이밍을 남기는 절충 |

선택 profile의 render animation-induced RMS는 **3.014mm**, 개별 최대 차이는 **5.128mm**입니다. 16.5mm의 rest 처짐을 전부 애니메이션 반응으로 주장하지 않습니다. baseline render animation-induced RMS는 0.249mm입니다.

기존 source/shader numerics, gravity, head animation, physical steps, compliance, damping, collider, groom density는 A/B에서 유지했습니다. 정확한 CLI/실행파일 hash는 아래 runs.json, 실제 loaded material/iterations/free vertex 정보는 각 step JSON에 있습니다. root radius/고정점을 바꾸거나 가짜 흔들림을 더하지 않았습니다.

## 전달한 변경

- `JoltScene.h/.cpp`: 앞머리 default global pose **root 0.01 / tip 0.001**, full-length gradient. skin-follow는 기존 **1→0.25** 유지.
- `JoltMain.cpp`: Jolt material settings에 **Pose retention root/tip/end fraction**, **Skin follow tip** 슬라이더. 최초 material 선택은 Styled fringe. Run 중 live 적용, Reset은 source/CLI defaults로 복원합니다.
- CLI `--fringe-pose-root`, `--fringe-pose-tip`, `--fringe-pose-end`, `--fringe-skin-tip`으로 같은 실험을 재현할 수 있습니다. 범위는 0..1, end는 0.01 이상.
- explicit capture JSON에 actual material gradients, material tags, initialized free counts, rest tips, root quaternions를 기록합니다. normal runtime에 geometry readback은 추가하지 않았습니다.

## 재현과 결과 파일

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1

# 원인 분리 6가지, 다음 중간값 4가지, 선택값 2가지 (각 bounded 120steps)
python scripts/diagnose-fringe-motion.py
python scripts/diagnose-fringe-motion.py --balance
python scripts/diagnose-fringe-motion.py --candidate

# 전달한 defaults와 Debug/Release, reset, controller 검사
python scripts/validate-fringe-fix.py

# 이전 강도 재현
.\build\jolt-only\bin\Release\HairLabDX12.exe --steps 120 --fringe-pose-root 0.175 --fringe-pose-tip 0.06 --capture-every 15 --capture-dir artifacts/fringe-old-replay
```

원시 A/B: `artifacts/fringe-diagnosis/material-{ab,balance,candidate}/analysis.json`, `runs.json`, PNG/step JSON. root-frame check: `root-frame-check.json`.
최종 validation: `artifacts/fringe-diagnosis/final/validation.json`, `runs.json`.

전달 build로 Release moving/static 각각 **240steps (4초/animation 한 바퀴)**, Debug moving **120steps**, controller API를 실행했고 모두 exit 0입니다. Reset JSON/PNG 동일, finite/root attachment 검사 및 Debug layer no-error를 확인했습니다. 2초 이후에도 animation-induced RMS 최고 **2.097mm**로, 처음 중력에 처지는 순간만을 움직임으로 해석한 결과가 아님을 확인했습니다. 이 시나리오의 머리 회전이 느려서 반응은 여전히 수 mm 수준이며 과장된 흔들림을 추가하지 않았습니다.

`python scripts/capture-fringe-motion.py`로 실제 완료 PNG만 연결합니다. 전체 GIF `final/fringe-motion.gif`, 확대 GIF `final/fringe-motion-closeup.gif`. 15steps/0.25초 간격이며 최종 checkpoint를 0.25초 더 유지합니다. 확대본은 단순 crop/scale이고 가짜 deformation/interpolation은 없습니다. 최종 default의 step 60/150 PNG도 실제로 열어 확인했습니다.

baseline / pose-off / weak-root / medium / soft / candidate step 60과 candidate step 120 PNG를 실제로 열어 봤습니다. 매우 약한/꺼진 pose에서 얼굴을 덮는 것을 확인하여 default로 선택하지 않았습니다. CLI/API 검사를 실제 mouse/UI drag 또는 resize 검사라고 주장하지 않습니다.

이번 수정은 기존 강한 kinematic-style 보정의 원인 분리 및 낮춘 default 전달입니다. 현재 groom을 모든 상황에서 예쁘게 유지하는 물성 보정이나 장시간 안정성의 보장은 아닙니다. 짧은 앞머리와 현재의 부드러운 4초 head animation에서는 여전히 뒷머리보다 반응이 작을 수 있습니다.
