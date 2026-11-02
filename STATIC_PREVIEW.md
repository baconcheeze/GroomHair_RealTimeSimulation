# 정적 헤어/머리 이관 기록

## 실제 데이터와 포즈

읽기 전용 원본 루트는 `C:/Users/caw20/MyProject/UnityProject/RealTimeHairSimulation`입니다.
`Assets/HairRest/Scenes/HairRestPreview.unity`의 GUID를 .meta로 해석했습니다.

| 씬 필드 | 실제 파일 (Unity 루트 기준) | native 에셋 |
| --- | --- | --- |
| restSnapshot | Assets/HairRest/FemaleHead/FemaleHairRest.bytes | assets/hair/SelectedRest.bytes |
| replacementHeadMesh | Assets/HairRest/FemaleHead/FemaleHead.asset | assets/head/SelectedHead.nhm |
| headSnapshot | Assets/HairRest/Data/JoltHeadRest.bytes | assets/head/JoltHeadRest.bytes |

useReplacementHead=1, 가이드 숨김·렌더 표시·hull 숨김이 현재 씬 설정입니다.
모든 모델의 object transform은 identity입니다. 선택된 원본 표시 메시 자체를
추출했습니다. 원본 Jolt scalp도 `--head original`로 표시합니다.

| 데이터 | 가닥 | 정점 | 선분 | bend |
| --- | ---: | ---: | ---: | ---: |
| Guide | 2,839 | 90,848 | 88,009 | 85,170 |
| Render | 28,381 | 908,192 | 879,811 | 851,430 |

실제 파일은 모두 가닥당 32정점이지만 로더는 가변 길이를 처리합니다.
가이드는 렌더 원본 ID를 보존하고 해당 가닥을 그대로 복사합니다. 렌더에 포함된
가이드를 제거하지 않습니다. ID는 Unity GetSourceStrand와 같은 스냅샷 내 render
ordinal입니다. 스냅샷에 없는 더 이전 authoring 매핑을 추정하거나 만들지 않습니다.

선택 머리: **7,976정점 / 8,346삼각형**. 원본 Jolt scalp(손/팔 포함):
**9,376정점 / 18,479삼각형**. 원본 collision hull: **22개**, joint ID 유지.
원본 hull은 여성 머리에 refit된 충돌체가 아니며 참고용입니다.

## 좌표 변환과 초기화

HairRestData.cs, FemaleHairFitting.cs, FemaleHeadAuthoring.cs, JoltHeadRestData.cs,
ExportJoltHead.cpp, HairInspectionCamera.cs를 확인했습니다.

1. FemaleHairRest의 authoring 계산은 Unity neck-local이지만 저장 시 반사를 역적용해
   **JHRT에는 Jolt neck-local**을 기록합니다. native `joltToNative`에서 `(x,y,-z)`를
   정확히 한 번 적용합니다. 재샘플링·새 subset 선택은 없습니다.
2. FemaleHead.asset은 source scale (0.85,0.90,0.90), offset (0,-1.283,0.015) 후
   inverse(frame0NeckJolt) 및 Z 반사를 거친 **이미 구워진 Unity 메시**입니다.
   serialized position/normal/triangle indices를 그대로 추출했습니다. 추가 반사나
   winding reversal은 없습니다. authoring의 inverse-scale normal도 보존했습니다.
   UV/텍스처는 요청한 회색 재질에 쓰지 않습니다.
3. JHHD scalp는 exported **frame-0 skinned neck-local** 위치를 선택합니다.
   bind vertex와 같다고 가정하지 않습니다. Z 반사와 winding reversal을 한 번씩
   적용하고 변환된 삼각형으로 노멀을 계산합니다. runtime skinning은 없습니다.
4. hull의 원래 점, joint-local 점, world/neck transform, plane, ID/joint를 보존합니다.
   frame-0 neck-local 점에 Z 반사를 적용해 표시합니다. local-to-neck 변환 결과를
   snapshot 점과 모두 비교합니다(허용오차 2e-5 m). wire edge는 exporter의 Jolt
   ConvexHullBuilder 삼각형에서 중복 제거했습니다. 점 순서로 topology를 만들지 않습니다.
5. geometry는 Unity 씬과 같은 neck-local identity pose입니다. 카메라는 Unity처럼
   inverse(H*frame0Neck*H)를 orbit 기준축으로 사용해 upright하게 관찰합니다.
   모델에 neck world transform을 재적용하지 않습니다. scene up은 +Y입니다.

JHHD matrix는 column-major 직렬화/column-vector 의미입니다. H를 양쪽에 곱하고
row-vector 의미로 명시적 전치 구성합니다. 원점·비대칭 점 등 3개 점에 대해 native
결과와 H*M*p 결과를 비교합니다. CPU/HLSL view-projection은 기존대로 row-vector,
row_major, mul(position,matrix)이며 시계 방향 앞면/back-face culling을 유지합니다.

## SHA-256

전체 원본 경로·코드·씬·라이선스의 해시는 `assets/manifest.json`에 기록합니다.

| 파일 | SHA-256 |
| --- | --- |
| FemaleHairRest.bytes / SelectedRest.bytes (동일) | `8d1c6197fd3cb2664d84b0914d61d323115c32d40ffef3a08ecf97815a343bfa` |
| JoltHeadRest.bytes (원본/staged 동일) | `9a66c976c7bcd3123f9870e3c36d8cccc30f715faf07842bab8d2197606f287d` |
| 원본 FemaleHead.asset | `724349cb09d54d25e08d02738a9072f99a7acc0ccbd6d09bfbef57318a3c2aa7` |
| 추출 SelectedHead.nhm | `547972aeb8b6ab53c016dbcae0f9e2448aedc3c0fdf761c693146b0f424a66e5` |

선택적 재이관(모든 출력은 native assets 내부):

```powershell
python scripts/stage-assets.py --source C:\Users\caw20\MyProject\UnityProject\RealTimeHairSimulation
```

Unity exporter/Editor를 실행하지 않으며 Python 표준 라이브러리만 사용합니다.
정상 빌드는 staged 데이터를 실행 파일 옆에 복사합니다.
VRM SOURCE.json·README, Jolt MIT, Cem Yuksel 연구 모델 attribution을
assets/notices에 보존했습니다. 여성 모델 저작권은 pixiv Inc.입니다.

## 바이너리 형식

모두 little-endian, IEEE754 float32, uint32 count/index입니다.

**JHRT v1:** magic JHRT, version, strand count S, vertex count V, guide count G
(20바이트). 이어 S+1 offsets, V packed float3 positions, G sorted unique guide source
IDs. 정확한 길이 `20+4*(S+1)+12*V+4*G`. render IDs는 0..S-1. 순서/경계 보존.

**NHM1 v1:** magic NHM1, version, vertex count V, index count I (16바이트).
정점별 float3 position + float3 normal(24바이트), uint32 triangle indices.
정확한 길이 `16+24*V+4*I`. 이미 Unity neck-local 좌표입니다. Unity asset의 실제
channel layout/stride, stream과 submesh topology를 검사해 추출합니다.

**JHHD v1:** magic JHHD, version, head joint, vertex count, triangle count,
joint count, weights/vertex, source frame count, hull count (36바이트).
inverse-bind-neck/bind-neck/frame0-neck matrix; joint별 inverse-bind/adjusted-inverse-bind/
frame0 matrix; vertex별 weight/joint; vertex별 original/bind-neck/frame0-neck float3;
triangle indices가 이어집니다. hull별 attachedJoint(int32, -1은 world),
vertex/index/plane count, world/neck matrix, vertex별 original/local/neck float3,
triangle indices와 local float4 planes입니다. JHHD 전체를 staged하여 joint/skin/plane
metadata도 보존합니다.

로더는 format/version, 정확한 길이, 유한 값, affine/비특이 matrix, 단조 offsets,
strand당 최소 2정점, guide IDs, mesh/hull indices, skin joint/weight, count/draw
arithmetic을 검사합니다. JSON은 min/max strand 길이, bounds, 첫 root/tip/가닥 총길이와
처음 네 정점도 출력합니다.

## GPU와 캡처

| 리소스 | 개수 | stride | heap/state |
| --- | ---: | ---: | --- |
| GuideRestPosition / GuideCurrentPosition | 각각 90,848 | 12 | default / NON_PIXEL_SHADER_RESOURCE |
| RenderRestPosition / RenderCurrentPosition | 각각 908,192 | 12 | default / NON_PIXEL_SHADER_RESOURCE |
| GuideSegmentStarts | 88,009 | 4 | default / NON_PIXEL_SHADER_RESOURCE |
| RenderSegmentStarts | 879,811 | 4 | default / NON_PIXEL_SHADER_RESOURCE |
| Selected/Original Head vertices | 7,976 / 9,376 | 24 | default / VERTEX_AND_CONSTANT_BUFFER |
| Head indices / hull edge indices | indexed | 4 | default / INDEX_BUFFER |
| Hull positions | packed float3 | 12 | default / VERTEX_AND_CONSTANT_BUFFER |

rest/current는 각각 CreateCommittedResource한 독립 할당입니다. startup staging으로
같은 CPU rest를 각각 한 번 초기화하고 fence 완료 후 upload를 해제합니다.
렌더링은 CURRENT만 읽습니다. root SRV t0=current, t1=SegmentStarts,
SV_VertexID/2=선분, %2=endpoint입니다. 가닥 경계를 잇지 않습니다.
head는 indexed triangles, hull은 indexed lines이며 가닥별 draw/allocation은 없습니다.
sizeof(Float3)==12, sizeof(uint32_t)==4, sizeof(HeadVertex)==24를 검사합니다.
시뮬레이션용 UAV/compute PSO/Dispatch는 없고 큐는 기존 direct queue 하나입니다.

cyan guide, brown render, gray head입니다. head 먼저, hair도 일반 LESS_EQUAL
depth test/write로 그립니다. guide가 뒤에 그려져도 head 가림을 유지합니다.
depth 무시 overlay나 geometry perturbation은 없습니다.

PNG는 ImGui 이전 back buffer를 RT→COPY_SOURCE로 전환하여 readback에 복사하고
RT로 복원한 뒤 UI/Present를 진행합니다. 마지막 fence 완료 후 Map합니다.
GetCopyableFootprints의 padded row pitch와 마지막 행 길이를 지키며 RGBA→BGRA 후
Windows WIC PNG로 저장합니다. desktop screenshot은 쓰지 않습니다.

## 검증 결과

VS 2022 / MSVC 19.44.35228, SDK 10.0.26100.0, CMake 4.3.1,
RTX 5070 Ti에서 Debug/Release 빌드 및 검증했습니다.

- RestDataTests: 길이 2/3/4 가닥 경계/guide subset과 잘림·trailing bytes·version/count
  overflow·잘못된 offsets·NaN·잘못된 guide IDs의 9개 손상 케이스 통과.
- 초기/120프레임 후 모든 guide/render rest/current/segment GPU 배열이 CPU와 memcmp
  일치. 일반 실행에는 readback하지 않습니다.
- Unity 진단과 counts 정확히 일치. bounds/root/tip/length 차이는 1e-6 m 미만
  (최대 약 6.7e-8 m). guide/source 정점은 정확히 일치.
- frame 1과 frame 120의 고정 front PNG가 파일 바이트까지 동일.
- 1023×769 padded row pitch 캡처 및 bootstrap 120프레임 회귀 성공.
- 실행 파일/assets/shaders의 native 격리 복사본을 다른 cwd에서 실행.
  hair asset 없는 격리 실행은 nonzero 실패, cube 폴백 없음.
- Debug D3D12 오류 없음. 정면/측면/후면, head+guide/render/both/hull, 머리 단독,
  원본 scalp 이미지를 직접 확인하고 기존 Unity 정면 캡처와 배치를 비교했습니다.

출력: `artifacts/Debug/`, `artifacts/Release/`의 front/side/back/guides/render/both/
head/hulls/original-head/bootstrap/padded-pitch/before/isolated PNG 및 로그.
front.json, side.json, back.json, isolated.json은 CPU/GPU 검증 리포트입니다.
`artifacts/migration-check.json`에 소스 해시·Unity 진단 비교를 기록합니다.
재현 명령은 README에 있습니다.

입력/UI/resize/minimize/restore는 데스크톱 자동화가 창을 찾지 못해 검증하지 못했습니다.
캡처 성공을 입력 검증으로 간주하지 않습니다. 수동으로 RMB/MMB/wheel/F/R,
토글·scene 선택, ImGui 입력 차단, 창 크기 변경/복원·일반 종료를 확인하세요.
WARP/debug layer 부재 폴백도 별도 실행 검증하지 않았습니다.

Unity 트리에 쓰기·이동·삭제·설정 변경을 하지 않았고 Editor/소스 exporter를 실행하지
않았습니다. 정상 앱은 원본 폴더에 의존하지 않습니다. rest/current는 검증 기간 동안
변하지 않았으며 physics dispatch, reconstruction, scalp animation은 없습니다.
