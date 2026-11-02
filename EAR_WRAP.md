# 귀 테두리 옆머리 마감 — ear-wrap-v9

이전 `nape-coverage-v8`의 뒤통수 덮임을 유지하며, 귀 앞·위·뒤의 옆머리를 다시 내렸다. 기존 제작 코드의 극각 1.43rad 제한은 귀 주변의 넓은 두피를 비워 두는 원인이었다. cap에만 1.85rad 경계를 적용하고, 아래쪽 실제 곡선에 부드러운 측면 드레이프를 추가했다. 귀 자체는 기존 측정된 충돌/접촉 경계로 피한다. 정면 앞머리·귀 옆 제어 컬과 가닥 수는 유지한다.

제작 코드: `scripts/groom_drape.py`, `scripts/import-metahuman.py`. 실행 데이터: `assets/moca/metahuman.*`. Jolt/네이티브 C++/셰이더/물리 알고리즘 변경은 없다. 원본 구매 Alembic와 Moca 캐릭터/본/애니메이션은 수정하지 않았다.

```powershell
.\build\vs2022\bin\Release\HairLabDX12.exe --view audit-right --hide-body --run
python -X utf8 scripts/check-ear-wrap.py
```

새 데이터는 기존 `jolt-only`/`vs2022`의 Debug/Release 실행 폴더에 배치한다. `artifacts/hair-ear-wrap/runs.json`은 각 실제 실행의 명령, 종료 코드, 실행 파일/데이터 해시를 기록한다. 좌우/아래 좌우/뒤/정면/위 7방향, Release 120스텝 헤드턴 및 Debug 30스텝 헤드턴/리셋을 검사한다. 각 캡처는 유한 상태, 모근 오차 10µm 미만, 전체 렌더 영역 표시를 확인한다. 실제 측면/아래 측면 이미지를 직접 열어 확인했다. 물리 마우스/키보드/리사이즈 확인과 동일한 검증은 아니다.

가닥 추가/삭제 없이 33,512가닥을 유지한다. 귀가 전부 머리에 가려져야 한다는 가정 대신, 귀 윗선을 머리가 따라 내려오고 귀 자체는 드러나는 실루엣이다. 아래에서 보이는 머리/귀/얼굴 피부와 머리카락 사이 경계를 포함해 확인한다. 기존 걷기 일부 자세의 포니테일 관통까지 모두 해결됐다고 주장하지 않는다.

이전 자료는 `artifacts/hair-ear-wrap/before`와 기존 마감 폴더에 보존한다. 비교 이미지는 `artifacts/hair-ear-wrap/before-after.png`이며 왼쪽 v8, 오른쪽 v9다. 원본 구매 에셋의 라이선스는 그대로 적용된다.

실행 결과: 9개 bounded 실행 모두 종료 코드 0, Debug D3D12 오류 0, 별도 폴더 패키지 30스텝/리셋 성공. 귀 접촉 검사에서 정지 원본 및 캡처의 내부 점/선분은 0이었다. 움직이는 캡처는 내부 점 0이지만, 샘플 사이 연속 선분 검사에서는 최대 24개 귀 경계 교차가 남았다. 따라서 이 수정이 모든 동적 관통을 없앴다고 보고하지 않는다. 원시 결과는 `artifacts/hair-ear-wrap/clearance.json`에 보존한다.
