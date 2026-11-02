# 옆머리 / 귀 / 헤어라인 마감

현재 기본 프로필은 `ear-finish-v6`다. 원본 Alembic의 8개 그룹, 33,512가닥, 길이와 중요한 앞머리/옆머리를 유지했다. 원본 파일과 사진, 캐릭터 메시·본·애니메이션, 기존 단발/긴 머리는 수정하지 않았다.

## 실제 수정

- 귀 안쪽과 목 피부에 붙어 있던 뿌리를 귀 위 또는 귀 뒤의 실제 뒤통수 두피로 옮겼다. 총 7,069개 부착 위치를 보정했다. 단순히 귀 표면에서 바깥으로 미는 것만으로는 귀 안쪽 뿌리와 첫 선분의 문제를 해결할 수 없었다.
- 귀 뒤 덮개를 두피에서 약 3.5~4.5mm의 얇은 층으로 정리했다. 원래 정수리 볼륨과 긴 꼬리를 일괄 평탄화하지 않는다. 실제 머리 표면과 가닥 중간 구간도 재검사한다.
- 귀의 볼록 외피를 통과하는 선분은 양 끝이 같은 바깥 반공간에 놓이도록 국소 마감한다. 뿌리는 두피에 그대로 둔다. 커트·삭제는 없다.
- 이미 피팅한 32점을 로딩할 때 또 32점으로 재샘플링하던 처리를 제거했다. 이전 입력/실행 초기 위치 차이는 최대 약 22mm였다. 이 수치는 점의 매개화 이동도 포함하며 전체 곡선의 표면 거리라고 주장하지 않는다.
- `MetaHumanAttachment.h`는 Jolt의 기존 `SVertexInfluence`를 설정한다. 렌더 뿌리는 자유 중간 점이 아닌 같은 구역의 고정 가이드 뿌리를 따른다. 최초 15mm에는 기존 가이드와 뿌리 influence를 함께 사용한다. 가중치/상대 위치 계산과 GPU 렌더 업데이트는 원래 Jolt가 수행한다. 직접 시뮬레이션되는 가닥의 매핑은 유지한다.
- 접촉 영역과 긴 옆머리/구렛나루는 직접 가이드로 시뮬레이션한다. 최종 **22,549 guides / 33,512 render strands**다. 렌더 밀도를 줄여 숨기지 않았다. 직접 가이드 증가 때문에 GPU 부하는 이전 1,789개 구성보다 크다. 새 성능 벤치마크나 GPU 시간 주장은 하지 않는다.
- 묶인 덮개만 GlobalPose 0.15→0.08, SkinGlobalPose 1, WorldTransformInfluence 0→0.15로 안정화했다. 귀 접촉 덮개/꼬리의 충돌 반경은 뿌리 1mm에서 15% 지점의 5mm까지, 긴 옆머리/구렛나루는 3mm까지 증가한다. 이는 Jolt가 제공하는 재질 설정이다. 긴 꼬리의 중력·재질은 유지한다. 질량·중력·시간 간격·Jolt 알고리즘/셰이더는 변경하지 않았다.

## 파일과 재현

오프라인 피팅은 `scripts/import-metahuman.py`, `side_hair_fit.py`, `ear_surface.py`에 있다. 생성물은 `assets/moca/metahuman.{hair,materials,ears,json}`다. `.ears`는 기존 EARH v1 형식을 유지하며 실제 귀 형상 2개를 담는다. 양쪽 귀 충돌체는 Head를 따라가며 총 충돌체는 5개다. 런타임 연결은 `src/JoltScene.cpp`, `MetaHumanAttachment.h`, 검사/카메라는 `src/JoltMain.cpp`에 있다. 좌표는 native meters/+Y up/−Z forward이며 추가 Z 반사는 없다.

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --hairstyle metahuman --view ear-right --hide-body --run
```

VS 출력 갱신은 `cmake --build build/vs2022 --config Release --target HairLabDX12 -- /m:1`이다. UI의 **Inspect left ear / Inspect right ear**로 확대하며 방향키·RMB·MMB·휠을 유지한다. `--view ear-back-left`와 `ear-back-right`도 제공한다. 실행 중인 이전 앱은 종료하고 새 실행 파일을 연다.

```powershell
python scripts/validate-metahuman.py
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts\blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/verify-side-finish.py
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/check-side-mesh.py -- artifacts/metahuman-finish/verified/Release/walk
```

## 검사 범위

Debug/Release 및 VS2022 Debug/Release를 빌드했다. Release 정면180/측면120/달리기180스텝, Debug 정면60스텝, 컨트롤러 API, 단발/긴 머리 회귀, UI 캡처를 검사한다. 별도로 좌우·뒤쪽 4개 확대 시점을 각각180스텝 실행했다. 뿌리 부착·유한 상태·리셋과 가이드/렌더 헤어 변형을 검사하며 정지 PNG와 움직임 캡처를 직접 확인한다. 정확한 결과는 `artifacts/metahuman-finish/verified/validation.json`, `verified/clearance.json`, `verified/Release/walk/mesh-crossings.json`, `verified-close/clearance.json`에 있다.

귀 외피는 점과 연속 선분을 모두 검사한다. 실제 머리 삼각형 검사는 같은 프레임의 **GPU 스키닝된 두피**와 렌더 선분을 같은 좌표로 비교한다. 플레이그라운드 초기 world에는 바닥 높이/방향이 있으므로 고정 bind transform과 혼동하면 안 된다. `read_head_bind`가 저장된 frame-0 Head를 읽는다. 이전 중간 진단의 잘못된 world/bind 혼용 결과는 최종 근거에서 제외한다. 실제 삼각형 교차는 30µm 끝점 접촉 제외를 명시하며 총 교차 수도 보존한다. 임의 포즈, 모든 프레임 사이의 연속 시간, 옷 충돌 전체의 무관통을 보장하지 않는다. 물리적인 마우스·키보드·리사이즈 조작은 수동 미검증이다.

최종 결과 폴더는 `verified` / `verified-close`다. `v6*`, `final*`, `polish*`, `contact*`, `completed-check`, `finished-views`, `delivered`는 중간 시도다. 특히 `delivered` 마지막 보고의 staged hash는 변환과 겹친 구버전이므로 최종 근거로 사용하지 않는다. 최종 하네스는 시작 시 기록한 입력 해시를 결과에 사용한다. 과거 실패 자료는 삭제하거나 통과로 바꾸지 않았다.

최종 수치: 8개 통합 검사 통과, 귀 주변 39개 캡처의 점/선분 교차 0건. 달리기 0~180스텝/30스텝 간격의 실제 GPU 스키닝 두상에 대해 총 3,306,535개 선분을 검사해 교차 0건이었다. 최대 뿌리 부착 오차는 약 3.34e-8m다. 머리 기준 팁 RMS 변형은 가이드 약 6.89mm, 렌더 약 7.78mm로 실제 변형이 유지된다. 초기 물리 상태와 리셋 상태가 같고, 이번 캡처의 리셋 PNG도 바이트 일치했다. `summary.json`에 입력·소스·실행 파일 해시와 원본 보존 검사가 있다. `before-after.png`는 왼쪽 이전/오른쪽 최종의 동일 초기 시점이며, `side-finished.gif`는 실제 0/60/120/180스텝 이미지와 마지막 홀드로 만들었다.
