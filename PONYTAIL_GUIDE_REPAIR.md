# 포니테일 가이드 초기 접힘 수정

## 원인과 범위

렌더 연결 수정은 철회된 상태로 유지한다. 이번 변경은 `assets/moca/metahuman.hair`의 포니테일(material 2/6) 초기 형상만 다듬는다. 귀 주변 피팅이 개별 점을 크게 이동시켜 초기 곡선에 최대 163.22°의 급격한 접힘을 만들었다. 실제 선택된 포니테일 가이드 269개 중 84개에도 이 접힘이 포함됐다.

## 수정 방법

`scripts/repair-ponytail-kinks.py`는 이전 `authoring/side-style/metahuman.hair`의 연속된 초기 곡선에 부드러운 부착 오프셋을 적용하고, 남은 뿌리 부근 훅만 국소적으로 fairing한다. 변경 대상은 급격한 꺾임이 있는 포니테일 1,910가닥의 점 1..11이다. 각 뿌리(점 0), 점 12..31, 가닥 수/순서, 다른 영역은 바이트 단위로 유지한다. 실행 중 projection이나 새로운 solver를 추가하지 않았다. 원본 구매 파일, rest orientation 알고리즘, 물성, retention, compliance, shaders, 렌더 연결은 변경하지 않았다. Jolt가 새 rest 형상에서 프레임을 초기화한다.

직전 파일은 `artifacts/guide-twist/repair/before/`에 보존했다. 승인 체크포인트 `checkpoints/photo3-v13-approved`도 해시로 불변 확인했다.

## 결과

| 검사 | 이전 | 수정 후 |
|---|---:|---:|
| 전체 포니테일의 90° 초과 접힘 | 1,673 | 0 |
| 전체 최대 꺾임 | 163.22° | 69.97° |
| 실제 선택된 가이드의 90° 초과 접힘 | 84 / 269 | 0 / 269 |
| 선택된 가이드 최대 꺾임 | 160.26° | 65.23° |
| 초기 포니테일 전 구간과 실제 머리 메시 교차 | 0 | 0 |

Release: 가이드/렌더 각각 애니메이션 120스텝, 걷기 120스텝, Reset, 정면/측면 정지 캡처 통과. 유한 위치 및 root attachment error < 1e-5m 검사 통과. 다른 작업 디렉터리에서 실행했다. Debug: 30스텝/Reset 및 debug-layer 감사 결과는 `runs/debug/app.log` 참조.

실제 캡처의 귀 주변 렌더 구간/같은 시점의 GPU-skinned 머리 메시 교차 검사: 0/30/60/90/120 모두 0건. 이는 검사한 구간과 시점의 결과이며 모든 자세나 self collision 보장은 아니다. 전체 가닥 간 비관통 기능을 추가한 작업이 아니다. 실제 마우스/UI 조작은 별도 수행하지 않았다.

가이드 초기/120스텝, 렌더 초기와 측면 PNG를 실제 열어 확인했다. 결과는 `artifacts/guide-twist/repair/runs/`, 수치/불변 검사는 `validation.json`, 형상 검사는 `candidate.json`, 귀 주변 교차 검사는 `runs/render/mesh-crossings.json`에 있다.

## 재현

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background --factory-startup --python scripts/repair-ponytail-kinks.py -- --apply
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust-back --hide-body --no-hair --guides --steps 120 --capture-every 30 --reset-check --capture-dir artifacts/guide-twist/replay --log artifacts/guide-twist/replay/app.log
```

일반 실행: `build/vs2022/bin/Release/HairLabDX12.exe`. 다른 헤어 영역과 물성 UI는 유지한다.
