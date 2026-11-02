# 메타휴먼3 기준 피팅 — reference-photo3-v13

기준 파일은 실제 로컬 `assets/MetaHuman/메타휴먼3.jpg`다. 이미지를 열어 귀 윗선, 귀 앞 가는 곡선, 귀 뒤 낮은 묶음, 머리의 밤색을 대조했다. 이전 v10–v12의 추가 머리층으로 덮는 방식을 폐기하고 원래 33,512가닥에서 다시 제작했다. 이는 다른 두상/렌더러에서도 사진이 픽셀 단위로 동일하다는 주장이 아니다.

## 변경

- 귀 주변 볼록 범위에는 두피가 섞여 있어 윗높이가 약 1.267m였으나, 바깥으로 드러나는 귓바퀴의 실제 윗높이는 1.254962m였다. 넓은 범위의 높이를 사용하면 귀 위에 약 12mm 빈 곳이 남았다. 현재는 실제 외측 귓바퀴 +2mm를 기준으로 기존 cap 가닥의 흐름을 국소적으로 내리고 부드럽게 연결한다.
- 실제 대조에서 묶음도 높았다. (0,-0.020,+0.004)m를 목표 이동으로 삼아 실제 두피에 낮게 재부착하고, 묶이는 모근의 분포만 0.25배로 모은다. 45mm 구간에서 기존 꼬리 몸통으로 이어 주고, 윗부분은 귀 뒤를 지난 뒤 목 아래에서 어깨 앞으로 내려오게 한다. cap의 끝부분도 같은 묶음 위치로 잇는다. 기존 아래쪽 물결/가닥 순서는 유지한다.
- 귀 앞 117가닥은 사진의 작은 곡선과 가는 짧은 구렛나루를 기준으로 다시 만들었다. y/z 곡선은 유지하면서 실제 얼굴의 좌우 표면을 따라 x를 맞추므로, 매 점을 다른 삼각형 법선으로 밀어 생기는 접힘을 피한다.
- 가운데 앞머리(region 1), 긴 얼굴 옆머리(region 3) 761가닥, 목덜미(region 7)의 위치 배열은 프로젝트 내 v9 두상 맞춤 원본과 동일하다. 가닥 추가/삭제 없음. 33,512가닥 / 1,072,384점이다.
- MetaHuman 기본 색을 사진의 밤색으로 바꿨다. 얼굴 옆머리와 귀 잔머리가 불투명한 띠로 보이지 않도록 **표시 알파만** 각각 .32/.22로 한다. 물성/질량/시뮬레이션/스키닝에는 반영하지 않는다. 나머지 헤어스타일의 색/알파는 유지한다.
- 색이 매끈한 덩어리로만 보이지 않도록 MetaHuman 표시에서 고정된 가닥별 색 변화와 따뜻한 하이라이트를 사용한다. 프레임마다 바뀌는 노이즈나 가짜 움직임은 없다.
- 귀 충돌체에서도 윗 두피가 섞인 점을 제외했다(`EarEnvelope`의 선택적 `max_y`, 실제 귓바퀴 위 3mm). 기존 두상 충돌체와 별도 귀 2개라는 구조는 같다. 관통 보정은 제작 시 실제 전체 두피와 기존 물리 두상 표면을 함께 사용한다. 귀를 피하는 최소 이동이 두상 안쪽을 선택하지 않게 하고, cap은 귀 위/포니테일은 귀 뒤 경로를 사용한다. Jolt 런타임 충돌 알고리즘/반경/허용오차를 바꾼 것이 아니다.

## 제작과 실행

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts/blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/match-metahuman3.py
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust-side --run
```

Visual Studio 경로: `build/vs2022/bin/Release/HairLabDX12.exe`. 실제 측면 확대는 `--view audit-right --hide-body --run`. 최종 검증 명령은 `python -X utf8 scripts/check-side-redesign.py --label photo3-accepted`, `python -X utf8 scripts/validate-metahuman.py --accepted`다. 일반 실행은 Blender, 원본 Alembic, Unity에 의존하지 않는다.

오프라인 입력은 `authoring/side-style/metahuman.*`, 결과는 `assets/moca/metahuman.*`다. **현재 형태를 재생성하려면 `match-metahuman3.py`를 사용한다.** 이전 제작 스크립트는 과거 시안을 만든 것이며 현재 사진 피팅 명령이 아니다. Jolt 알고리즘/컴퓨트 셰이더/물성, 캐릭터/본/애니메이션/원본 구매 파일은 수정하지 않았다. 구매 에셋의 라이선스는 그대로 적용된다.

## 표시 경계

`src/JoltMain.cpp`와 `shaders/JoltDisplay.hlsl`에 얇은 머리 표시를 추가했다. 원래 material ID 배열을 초기화 때 uint32 SRV로 한 번 올리고, shader t6 / root slot 7에서 읽는다. cbuffer b0는 176바이트/44 DWORD이며, 마지막 float4 `HairStyle`은 바이트 160에 있다. MetaHuman 플래그/두 알파/예약 0이며 C++ bool을 올리지 않는다. 기존 행렬/좌표 반사 규칙은 같다.

현재 GPU Jolt render positions를 그대로 읽는다. 매 프레임 CPU 전체 헤어 읽기/재업로드 경로는 없다. 스타일 변경/리셋은 기존 queue wait 뒤에 표시 자료를 갱신한다. 가이드만 표시할 때도 유효한 t6를 바인딩하고 스타일 플래그를 끈다. alpha가 실제 물리 반경이나 가닥 제거를 뜻하지 않는다.

## 검증과 제한

최종 파일을 고정한 자료만 `artifacts/hair-side-redesign/photo3-accepted` 및 `artifacts/metahuman3-match/accepted-regression`에서 판정한다. 사진 대조 제작 중의 다른 preview/verified/final 및 `regression`/`development-regression`/`pre-final-regression`은 최종 판정 자료가 아니다. 그중 한 long 리셋 실행의 접근 위반도 원래 로그로 보존했다. 원인을 단정하지 않으며 최종 고정 실행 코드에서 별도 `long-recheck`는 종료 코드 0으로 재실행했다. 개발 도중 입력/셰이더 교체가 겹친 자료를 성공으로 재분류하지 않는다. 초기 제작 시안에서 실제 두피 교차가 발생한 기록도 보존했다. 단순 볼록체 점 검사로 이를 숨기지 않았으며, 최종 경로의 정지 실제 메시 검사에서는 451,456개 선분 중 교차 0이었다.

사진과 앱을 나란히 둔 `artifacts/metahuman3-match/reference3-versus-app.png`는 왼쪽 사진/오른쪽 실제 앱이다. 각각 440×550/512×640 영역을 잘라 같은 종횡비로 확대한 것으로, 피부/형태 보정이나 생성 이미지 합성은 하지 않는다. 다른 캐릭터 두상/얼굴 비율, 단순 스트랜드 셰이딩, 걷기 중 두피 교차는 남는 제한이다. 물리 마우스/키보드/창 리사이즈 검증을 CLI 성공으로 대신 주장하지 않는다.

최종 고정 입력 결과:

- `jolt-only`/`vs2022`의 Debug/Release 빌드 4개 성공. 시점/동작 9개 및 회귀 8개 실행 성공. Debug D3D12 오류 없음. 패키지는 별도 작업 디렉터리에서 실행한다.
- 실제 가이드 17,329 / 렌더 33,512. 헤드 로컬 180스텝 팁 변형 RMS: 가이드 4.352mm, 렌더 6.781mm. 강체 머리 이동만을 움직임으로 계산하지 않는다.
- 정지 실제 두피 451,456개 선분 검사에서 교차 0. 헤드턴 캡처의 귀 내부 점/연속 선분 교차도 0이었다.
- **걷기에서는 실제 두피 교차가 남았다.** 0/30/60/90/120/150/180스텝에서 각각 0/0/102/9,998/3,317/896/7,266개다. 이는 귀 볼록체 점 검사와 다른 실제 삼각형 검사다. 작은 오차라고 단정하거나 모든 동작의 마감 완료로 보고하지 않는다. 이 작업에서 Jolt 솔버/물성 탐색으로 범위를 넓히지는 않았다.
- 사진의 정지 형태를 기준으로 개선했으며, 다른 두상/단순 렌더러에서의 사진 완전 복제나 전 동작의 무관통 결과는 아니다. 비교 캡처로 실제 차이를 확인할 수 있다.

`accepted-summary.json`에 소스/실행 파일/데이터 해시와 원본 불변 검사를 저장한다. 가운데 앞머리/긴 얼굴 옆머리/목덜미 배열은 바이트 단위 불변을 확인한다. `head-turn.gif`는 실제 0/60/120스텝 캡처를 1초씩 표시한 1Hz 체크포인트 자료이며 연속 고프레임 영상이 아니다.
