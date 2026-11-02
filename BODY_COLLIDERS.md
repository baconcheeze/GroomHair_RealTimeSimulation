> ?? ???? 58? ?? ??? 124? GPU ??? ?????. ?? CPU hull ?? ??? ??? ?? ?? ?????. ?? ??: [GPU_BODY_PRIMITIVES.md](GPU_BODY_PRIMITIVES.md). ?? ???? ???? ?????.

# 몸통·팔 충돌 hull — 실제 피부 크기 피팅

이전 자켓/의상 외곽 피팅은 `artifacts/body-hulls-refined/before/`에 보존했다. 현재는 원본 VRM의 **Body / Body_2** 정점만 사용한다. 청자켓, 상의, 바지, 속옷과 꼬리는 충돌 크기 산정에서 제외한다. 캐릭터·옷·헤어·애니메이션 파일 자체는 수정하지 않았다.

## 현재 구성

`assets/moca/body-colliders.bin` (BCHL v2) / `body-colliders.json`:

- 몸통·목·어깨·팔·손·손가락·경계 hull **58개**
- 기존 머리·귀 3개를 포함하면 **61개**
- 몸통은 좌우/높이로, 위팔·아래팔은 길이 방향으로 분할한다.
- 분할 경계에는 실제 메시 삼각형의 이웃 정점까지 포함한다. 단순히 점군을 자르면 정점 링 사이에 틈이 생기므로 one-ring overlap으로 연결한다.
- 전체를 일정 비율로 축소하거나 임의 구/캡슐로 바꾸지 않았다.

이전 외곽 대비 입력점 전체 bounds 변화(밀리미터, 부위 전체 범위이며 개별 단면 직경은 아님):

| 부위 | 이전 | 실제 몸 기준 |
|---|---|---|
| 허리 XYZ | 230.6 / 181.6 / 198.4 | 181.0 / 155.8 / 120.9 |
| 가슴 XYZ | 255.6 / 245.1 / 231.0 | 206.2 / 240.4 / 169.6 |
| 위팔 Y/Z 범위 | 114.0 / 114.0 | 74.3 / 73.5 |
| 아래팔 Y/Z 범위 | 96.2 / 96.2 | 53.5 / 57.7 |

## 움직임과 구현

`create-body-colliders.py`가 원본 메시의 스킨 가중치와 inverse bind에서 bone-local 점군을 생성한다. 실제 convex 생성은 Jolt가 담당한다(반경 0, hull tolerance 0.25mm). 좌표는 native Jolt 미터이며 별도 Z 반전이 없다.

피부에 밀착시킨 몸통·팔·손 등 28개 hull은 해당 원본 피부 정점 부분집합을 CPU 스키닝하여 갱신한다. bone-local 형상이 0.01mm 이하로 동일하면 shape 재생성을 생략한다. 손가락 30개는 본에 고정된다. GPU 헤어/전체 메시 readback이나 렌더 float의 물리 피드백은 없다. hull shape 변경은 Jolt의 SetShape, 위치/회전은 기존 kinematic 갱신 경로를 사용한다. CPU 스키닝/convex 생성 비용이 추가되며 성능 개선을 주장하지 않는다.

헤어의 retention/compliance/damping, 초기 groom, hull 예상 이동량 제외 및 retention → collision 순서는 유지한다. 형상이 변하는 hull 표면의 별도 정점별 속도 모델을 추가한 것은 아니다.

파일 형식은 BCHL v2 그대로다: magic/version/count 뒤 각 hull의 joint/pointCount, packed float3 local points, skinVertexCount와 원본 skin vertex ID가 온다. 인덱스·파일 길이·유한값·Jolt shape 결과를 검사한다.

## 확인한 결과 / 한계

- 자켓을 숨긴 정면·측면 캡처에서 실제 피부 윤곽과 hull을 직접 확인했다.
- 최종 분할 경계 수정 후 0/15/30/45/60 스텝에서 GPU-skinned Body 피부와 활성 hull 평면을 비교했다. 최대 양의 면 초과량 약 **0.66mm**, 1mm 초과 점 0개. 이는 hull 바깥쪽 면 초과량이며 정확한 유클리드 표면 거리나 hull이 오목한 공간을 채우는 정도의 측정은 아니다.
- convex 근사이므로 피부 삼각형과 완전히 같은 충돌체는 아니다.
- 의도적으로 피부 크기이므로 헐렁한 자켓은 hull 바깥으로 나온다. 옷 표면 충돌은 이번 설정의 목표가 아니다.
- 최종 Release 달리기 120스텝/Reset 및 Debug 30스텝/debug-layer 검사 결과는 `artifacts/body-hulls-refined/delivered/validation.json`과 app.log를 참조한다.
- 실제 마우스/UI/resize 조작은 별도 수행하지 않았다.

## 실행

앱을 재실행하고 **Collision hulls**를 켠다. 몸 크기를 확인할 때 **Denim jacket**을 잠시 끄면 된다.

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
.\build\jolt-only\bin\Release\HairLabDX12.exe --hulls --no-jacket --no-hair --view bust
```

오프라인 재생성(일반 실행에는 Blender 불필요):
```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background --factory-startup --python scripts/create-body-colliders.py
```

이전 버전, 중간 분할 결과, 최종 결과를 함께 보존했다. 최종 포즈 검사는 `artifacts/body-hulls-refined/final-pose-fit-seams.json`, 원자료는 `final-motion-seams/`, 정면 캡처는 `body-front-final/`이다. 일반 실행 경로 `build/vs2022/bin/Release/HairLabDX12.exe`도 갱신한다.
