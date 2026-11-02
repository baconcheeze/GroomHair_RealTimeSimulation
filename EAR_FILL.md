# 귀 주변 연결층 보강 — v12

v11의 앞머리/긴 옆머리 원본 복원은 유지한다. 귀 위의 기존 쓸어 넘긴 층만으로는 귀 앞/뒤 층과 연결이 약해 피부가 넓게 빈 것처럼 보였다. 귀 윗테두리를 따라 뒤로 내려가는 900가닥/측면, 귀 앞의 짧은 연결 가닥 480가닥/측면을 추가했다. 총 2,760가닥 추가이며 전체 렌더 41,072가닥이다. 귀 전체를 가리는 커튼 대신 귀 둘레를 잇는 구조다.

`scripts/style-side-hair.py`가 프로젝트 내 작가용 입력에서 반복 생성한다. 비귀 영역은 v9 두상 맞춤 원본 데이터와 동일한 배열인지 검사하며, 가운데 앞머리/긴 옆머리/윗머리/뒷머리/포니테일은 수정하지 않는다. Jolt 물성/셰이더/C++ 변경도 없다.

```powershell
python -X utf8 scripts/check-side-redesign.py --label ear-fill-v12
.\build\vs2022\bin\Release\HairLabDX12.exe --view audit-right --hide-body --run
```

새 데이터는 Debug/Release 네 실행 경로에 배치한다. 검증/캡처는 `artifacts/hair-side-redesign/ear-fill-v12`, 제작 자료/이전 상태는 `artifacts/hair-ear-fill`에 보존한다. 정면/좌우/아래/위/뒤 7시점, Release 120스텝 및 Debug 30스텝 헤드턴/리셋을 검사한다. 좌우 측면 렌더를 직접 열어 확인했다. 이는 실제 마우스/키보드/리사이즈 검증이나 모든 동작에서의 무관통 보증은 아니다. 원본 구매 파일과 캐릭터/본/애니메이션은 변경하지 않았다.
