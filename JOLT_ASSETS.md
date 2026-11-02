# Moca 캐릭터와 자체 제작 헤어

현재 head hull은 보강된 두피를 포함해 다시 맞췄습니다. `scripts/refit-moca-head-hull.py`와 `BOB_REDESIGN.md`가 최신 기록입니다. mesh 보강 후 이 도구를 실행하고 bob을 생성합니다. 기존 목/몸 hull은 그대로입니다.

원본: 사용자 제공 `assets/0828 모카님패키지.unitypackage` 안의 `Assets/모카 vrm/모카님.vrm`.
추출 VRM: `assets/imported/moca/모카님.vrm`.
VRM SHA256: `d9371de1ed3080baf927b911b86c332a7683baf8fc63df052cdd06899f50efd2`.
원본 패키지와 VRM은 수정하지 않았습니다. Unity 프로젝트를 읽거나 실행하지 않았습니다.

VRM은 315개 본, 스킨·바인드 정보, humanoid 매핑을 포함하지만 애니메이션 클립은 없습니다. 원래 본 이름/부모/바인드와 4개 스킨 가중치를 가져왔습니다. 얼굴 표정용 `Face_Alpha`는 VRM에서 불투명으로 잘못 내보내진 별도 표면이어서 중립 장면에서는 그 삼각형만 표시하지 않습니다. 원본 표정 데이터는 추출 VRM에 남아 있습니다. 얼굴·몸·의상·귀·꼬리 원본 메시를 사용합니다. morph 표정, Unity lilToon 및 spring-bone 동작 전체를 이식한 것은 아닙니다.

얼굴의 VRM PBR material에는 메인 텍스처 연결이 빠져 있었습니다. 패키지의 `Assets/0.시오/0.Sio_2/Material/1.Face.mat`에 실제 연결된 GUID `632a70d5d1771cd488f8776b5fa3593a`의 `시오 모카님.psd` 합성 이미지를 ffmpeg로 PNG 변환했습니다. 나머지는 VRM 내장 텍스처입니다. 앱은 WIC로 초기 업로드한 2048×2048×6 GPU texture array를 사용합니다. 표현은 간단한 조명/텍스처이며 Unity 셰이더 재현은 아닙니다.

`scripts/import-moca.py`는 로컬 VRM을 읽고 다음 파일을 생성합니다. 원본 파일에는 쓰지 않습니다.

- `character.bin`: upstream HairTest v5.6.0 schema. head joint 4, world-bind positions, indexed triangles, 315 inverse-bind matrices, 4 weights/vertex, 240 absolute-joint poses, 3 attached hull point sets.
- `rig.json`, `skeleton.bin`: 원본 본 이름/계층/humanoid 및 표시용 parent index. 본을 두 개로 축약하지 않습니다.
- `binds.bin`: 표시용 원본 inverse bind matrices. GPU normal skinning에 모든 실제 weight/joint를 사용합니다.
- `surface.bin`: uint32 count, 각 vertex당 64-byte `{float2 uv; uint texture; uint flags; float4 normal; uint4 joints; float4 weights;}`.
- `colors.bin`: uint32 count + float4 기본색.
- `studio.hair`: 프로젝트에서 만든 16,000×32 strand, HAIR 128-byte header + packed float3. 원본의 기존 스칼프 헤어 메시를 변환한 것이 아닙니다.
- `groom.materials`: uint32 count + strand당 uint32 Jolt material. 12,800 back/side, 3,200 fringe.
- `textures/0..5.png`, `manifest.json`, `LICENSE.txt`.

본 바인드의 `jointWorld * inverseBind` 최대 identity 오차는 약 4.81e-8입니다. VRM의 mesh-node translation은 1µm 미만의 export noise로 검사 후 무시합니다. 단위는 미터, Jolt/VRM +Y 위, 캐릭터 정면은 **-Z**입니다. 시뮬레이션 좌표를 추가 반사하지 않으며 렌더러에서만 world Z를 반사합니다. 카메라와 새 앞머리는 이 정면에 맞췄습니다.

8개 skin의 joint 목록은 동일합니다. inverse-bind 데이터의 skin 간 최대 차이는 6.55651e-7이고, 단일 Jolt rig에는 Face의 inverse-bind를 기준으로 사용합니다. 이는 export의 미세한 차이만 합친 것이며, 원본 각 skin 데이터는 VRM에 보존됩니다. 변환기는 차이가 1e-6 이상이면 중단합니다.

헤어 뿌리는 실제 upper-head 삼각형에 ray/closest-triangle로 부착합니다. Jolt가 native guide selection, scalp skin attachment, collision, render influence/update를 수행합니다. 머리 hull은 원본 머리 표면을 표본화하여 Jolt가 convexify합니다. 목/상체는 크기에 맞춘 debug convex hull이며 원본 정밀 collider를 가져왔다는 뜻은 아닙니다. 각 hull은 실제 head/neck/chest 본에 붙습니다.

원본 VRM metadata는 author-only avatar use, commercial-disallow, redistribution-prohibited로 기록되어 있습니다. 사용자 요청으로 로컬 프로젝트에 통합했으며, 별도 재배포 권리를 주장하지 않습니다. `assets/moca/LICENSE.txt`와 원본 조건을 유지하세요. 자체 헤어/데모 애니메이션과 제3자 캐릭터의 권리는 구분됩니다.
