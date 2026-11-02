# 승인 버전 보존 + 구렛나루 보강 v14

승인된 v13은 `checkpoints/photo3-v13-approved`에 독립 실행 파일/전체 런타임 에셋/관련 코드/복원 스크립트/해시로 저장했다. 현재 폴더는 Git 저장소가 아니므로 Git 브랜치라고 표시하지 않는다. 파일 링크를 공유하지 않는 복사본이며, 그 실행 파일을 별도 작업 디렉터리에서 실행해 확인했다.

```powershell
# 승인된 예전 버전
.\checkpoints\photo3-v13-approved\runnable\HairLabDX12.exe --view bust-side --run
# 현재 구렛나루 보강 버전
.\build\vs2022\bin\Release\HairLabDX12.exe --view bust-side --run
```

앱을 닫고 `powershell -File checkpoints/photo3-v13-approved/restore.ps1`로 해당 헤어를 프로젝트에 복원할 수도 있다. `-IncludeSource`는 저장한 코드도 덮어쓰므로 필요할 때만 사용한다. 정확한 승인 버전의 실행은 위 독립 실행 파일을 쓰면 된다. 기존 걷기 일부 자세의 관통 한계까지 포함한 원래 상태를 보관했다.

## 새 형태

기존 **33,512가닥의 위치 바이트 전체를 그대로 유지**하고 귀 앞 구렛나루만 양쪽 200가닥씩 추가했다. 길이가 다른 세 묶음과 짧은 안쪽 층을 사용하며, 끝이 균일하게 잘린 선처럼 보이던 첫 시안을 부드럽게 휘고 길이가 조금씩 달라지도록 수정했다. 실제 관자놀이/볼 표면을 따라 가볍게 붙인다. 원래 귀 앞 컬, 앞머리, 긴 옆머리, 윗머리, 낮은 묶음, 포니테일은 변경하지 않았다.

현재 렌더 33,912가닥 / 가이드 17,729가닥이다. 신규 가닥도 기존 Jolt region 4로 직접 시뮬레이션한다. 물성/솔버/색/셰이더/C++ 변경은 없다. 현재 v13 실행 파일이 새 데이터를 읽는다.

제작 스크립트: `scripts/strengthen-sideburns.py`. 읽기 전용 입력은 승인 체크포인트의 Assets/Moca이며 결과는 기존 `assets/moca/metahuman.*`다. 매번 동일한 승인 버전에서 생성하므로 가닥이 누적되지 않는다. 일반 실행에는 체크포인트/Blender/Python이 필요 없다.

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts/blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/strengthen-sideburns.py
python -X utf8 scripts/check-side-redesign.py --label sideburn-v14-final
python -X utf8 scripts/validate-metahuman.py
```

## 실제 검증

- 정면/좌우/아래 시점 캡처를 직접 확인했다. 비교: `artifacts/sideburn-v14/before-after.png`(왼쪽 승인 v13 / 오른쪽 v14).
- 시점/동작 실행 9개, 기존 기능 회귀 8개 통과. Debug/Release의 새 데이터 실행, 헤드턴/리셋, 실제 가이드/렌더 변형, 기존 단발/긴머리/컨트롤/UI를 확인했다. C++/셰이더는 변경하지 않았으므로 새 코드 빌드를 주장하지 않는다.
- 정지 실제 두피 463,589개 선분 검사에서 교차 0. 확인한 헤드턴 캡처의 귀 내부 점/연속 선분 교차 0. 모든 임의 포즈를 보장하는 결과는 아니다.
- 승인 체크포인트의 모든 저장 파일 SHA-256 불변 및 기존 33,512가닥 전체의 바이트 일치를 검사했다. `artifacts/sideburn-v14/baseline-invariants.json`.
- 원시 결과: `artifacts/hair-side-redesign/sideburn-v14-final`, `artifacts/sideburn-v14/regression`. 물리 마우스/키보드/리사이즈 직접 조작 검증은 아니다. 기존 걷기 중 교차를 이 작업에서 해결했다고 주장하지 않는다.

구매 에셋의 라이선스는 그대로 적용되며 보관본과 파생 데이터는 비공개 로컬 사용 자료다.
