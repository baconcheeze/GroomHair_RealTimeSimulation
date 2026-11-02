# 귀 주변만 창작, 긴 옆머리 복원 — v11

직전 v10은 가운데 앞머리(region 1)는 유지했지만, 얼굴 옆으로 내려오는 원본 기반 긴 머리(region 3) 761가닥까지 새로 만들었다. 사용자의 범위 수정에 따라 이 761가닥을 프로젝트 내 v9 두상 피팅 데이터에서 그대로 복원했다.

현재 새로 만든 부분은 귀 앞의 짧은 117가닥(region 4)과 귀 위에서 뒤로 흐르는 추가 4,800가닥이다. 가운데 앞머리, 긴 얼굴 옆머리, 기존 윗머리/뒷머리/포니테일/목덜미는 v9 기반 데이터와 비트 단위로 동일함을 제작 스크립트에서 검증한다. 여기서 원본 기반이란 **현재 두상에 맞춰 배치한 원본 형태**이며, 구매 Alembic의 변환 전 좌표라는 뜻은 아니다.

제작: `scripts/style-side-hair.py`. 입력: `authoring/side-style/metahuman.*`. 결과: `assets/moca/metahuman.*`. 이 스크립트에서 긴 옆머리 창작 함수를 제거해 재생성할 때도 복원이 유지된다. Jolt/물성/셰이더/C++는 변경하지 않았다.

```powershell
python -X utf8 scripts/check-side-redesign.py --label ear-only-v11
.\build\vs2022\bin\Release\HairLabDX12.exe --view audit-front --hide-body --run
```

검증/캡처: `artifacts/hair-side-redesign/ear-only-v11`. 7방향 정지, Release 120스텝 헤드턴/리셋, Debug 30스텝 헤드턴/리셋을 기록한다. 원본 비귀 영역의 비트 일치 검사는 `assets/moca/metahuman.json`의 `sideDesign.nonEarBasePositionsBitwiseEqual`에 기록한다. 실행 파일은 기존 파일이며 새 데이터를 Debug/Release 네 경로 및 로컬 패키지에 배치한다. 원본 에셋 파일은 변경하지 않았다. 기존 동적 미세 교차를 모두 해결했다는 주장은 아니다.
