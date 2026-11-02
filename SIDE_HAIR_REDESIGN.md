# 옆머리 새 디자인 — hand-dressed-side-v10

원본 귀 주변 가닥의 모양을 그대로 유지하던 방식을 중단했다. 새로운 형태는 귀 위를 따라 뒤로 넘어가는 얇은 머리층, 이마 옆을 덮는 긴 옆머리층, 귀 앞의 짧은 구렛나루를 나누어 연결한다. 가운데 앞머리, 뒤통수 덮임, 긴 트윈테일은 기존 형태를 유지한다.

## 실제 제작 내용

- 기존 얼굴 옆머리 761가닥과 귀 옆 117가닥을 새 곡선으로 대체했다. 원본 형태를 보존했다고 주장하지 않는다. 원본 구매 파일은 수정하지 않았다.
- 양쪽 귀 위에 각각 2,400가닥의 얇은 쓸어 넘긴 층을 추가했다. 실제 두상 곡면에서 시작하고 귀 위를 지나 뒤쪽으로 흐른다. 추가 4,800가닥이며 총 렌더 38,312가닥 / 1,225,984점, 실제 Jolt 가이드 22,127가닥이다.
- 긴 옆머리는 이마 양옆의 빈 삼각형을 연결하고, 짧은 구렛나루와 다른 곡선을 쓴다. 끝은 몇 개의 작은 묶음으로 가늘어진다. 원본의 갈고리 모양 잔가닥은 더 이상 사용하지 않는다.
- 실제 앱 캡처를 열어 첫 시안의 두꺼운 판 모양, 다음 시안의 긴 실 모양, 짧게 줄이면서 드러난 이마 양옆을 확인하고 수정했다. 최종 형태의 좌우/정면/아래 측면과 움직이는 이미지를 확인했다. 미완성 시안은 자료로만 보존한다.

## 제작/실행

제작 코드는 `scripts/style-side-hair.py`다. 프로젝트 내부 `authoring/side-style/metahuman.hair`, `.materials`, `.json`은 v9 두상 맞춤을 바탕으로 한 **오프라인 작가용 입력**이다. 런타임 백엔드나 비교 모드가 아니다. 구매 에셋의 라이선스가 그대로 적용된다.

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts/blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/style-side-hair.py
python -X utf8 scripts/check-side-redesign.py --label delivered
.\build\vs2022\bin\Release\HairLabDX12.exe --view audit-right --hide-body --run
```

`style-side-hair.py`는 매번 같은 v9 입력에서 새 형태를 생성하므로 반복 실행해도 가닥이 누적되지 않는다. `import-metahuman.py`만 실행하면 원본 기반 v9 피팅으로 돌아가므로, 새 디자인을 재생성하려면 위 전용 제작 스크립트를 사용한다. 일반 실행에는 Blender/원본 Alembic/authoring 폴더가 필요 없다. 실행 데이터는 기존 `assets/moca/metahuman.*`다. CMake의 기존 데이터 배포를 그대로 사용하며 Debug/Release 네 실행 경로에 반영했다.

## Jolt와 검증 범위

추가한 곡선도 기존 region 3/4와 같은 Jolt 가이드/렌더 업데이트를 사용한다. 물리 파라미터, 질량, 중력, Jolt 알고리즘/셰이더, 렌더러 C++는 변경하지 않았다. CPU에서 매 프레임 새 헤어를 만드는 경로도 없다. 이번 변경은 오프라인 곡선/데이터 제작이므로 기존 실행 파일을 사용한다.

최종 판정 자료는 `artifacts/hair-side-redesign/delivered`와 `regression`이다. 최종 7방향 정지 캡처, Release 120스텝/Debug 30스텝 헤드턴/리셋, 원본 단발/긴머리/컨트롤/걷기 회귀를 구분한다. 초기 시안 폴더는 디자인 도중 데이터 변경과 겹친 실행이 있으므로 최종 입력 일치 검증의 근거로 사용하지 않는다. `delivered/runs.json`은 시작/종료 입력 해시 불변도 검사한다.

정지 상태 귀 내부 점/선분 검사는 0. 최종 헤드턴 캡처의 귀 내부 점은 0이지만 연속 선분 경계 교차는 최대 241개로 남았다(새 층 227개, 기존 cap 24개; 각 최대는 다른 체크포인트일 수 있음). 따라서 모든 동작의 동적 관통까지 해결했다고 주장하지 않는다. 원시 결과는 `delivered/clearance.json`이다. 실제 마우스/키보드/리사이즈 조작은 검증하지 않았다.

비교 이미지: `artifacts/hair-side-redesign/before-after.png`(왼쪽 이전, 오른쪽 새 디자인), `new-side-front.png`(새 좌측/정면/우측). 이 이미지는 모두 실제 앱 렌더 캡처이며 생성 이미지가 아니다.

최종 실행 결과: 고정 입력 9개 시점/동작 실행과 회귀 8개 모두 통과했다. 헤드 로컬 180스텝 팁 변형 RMS는 가이드 3.908mm, 렌더 6.305mm다(헤드의 강체 운동은 제외). Debug D3D12 오류 검사와 별도 작업 디렉터리 패키지 30스텝/리셋도 통과했다. 원본 구매 파일 및 character/bob/studio/NarutoRun 데이터 해시 불변, 네 빌드 경로의 최종 데이터 일치를 확인했다. 자세한 해시는 `artifacts/hair-side-redesign/hashes.json`, 실제 명령과 원시 상태는 `delivered/runs.json`, `regression/validation.json`에 있다. CPU/GPU 성능 벤치마크나 모든 포즈의 무관통 보증은 아니다.
