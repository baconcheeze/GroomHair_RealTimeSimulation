# Salon-v5: 커트와 공통 물성 조정

## 현재 적용: 달리기용 구역별 물성

후속 원형 수정: `salon-v9-forehead-fit`에서는 물성은 그대로 유지하고 앞머리 곡선을 실제 이마에 맞췄다. 기존 앞머리는 이마보다 약 20mm 앞에 있었고 중력 처짐을 예상해 짧게 만들어져, 복원력을 강화한 뒤 리셋에서 특히 떠 보였다. 새 원형의 끝은 이마 표면 전방 3–3.5mm이며 눈썹 부근까지 내려온다. 아래 수치 비교는 수정 전 가닥으로 실시한 물성 비교 기록이다. 새 리셋·120스텝·달리기 검사와 원자료는 `artifacts/fringe-contact/`에 있다. 실제 GPU 출력의 모든 지점에 일정 간격을 강제하는 제약을 추가한 것은 아니다.

2026-11-01: 앞머리의 들림을 줄이도록 공통 물성을 구역별 설정으로 교체했다. 아래 v5 공통 설정은 과거 기록이다. `Jolt material settings → Hair region`에서 `Side / back`과 `Bangs`를 선택해 독립적으로 편집한다. 매 프레임 앞머리 설정을 덮어쓰던 공통 복사도 제거했다. Reset은 각 구역 기본값을 복원한다.

| 설정 | 옆·뒷머리 | 앞머리 |
|---|---|---|
| 선형 / 각 감쇠 | 6 / 6 | 9 / 10 |
| Grid velocity 뿌리 / 끝 | 0.25 / 0.1 | 0.25 / 0.1 |
| GlobalPose 뿌리 / 끝 / 끝 도달 비율 | 0.01 / 0.0006 / 0.65 | 0.02 / 0.003 / 0.75 |
| SkinGlobalPose 뿌리 / 끝 / 끝 도달 비율 | 1 / 0.1 / 0.45 | 1 / 0.35 / 0.65 |
| WorldTransformInfluence 뿌리 / 끝 | 0 / 0.8 | 0 / 0.55 |
| Bend compliance | 1e-7 | 6.5e-8 |
| 길이별 bend compliance 배율 | 1 / 65 / 65 / 1 | 1 / 30 / 30 / 1 |

Jolt의 기존 물성만 조절했다. 솔버·타임스텝·중력·애니메이션·질량·가닥 자산은 변경하지 않았다. 옆·뒷머리는 끝의 움직임을 남기고, 앞머리는 형태 복원/추종/감쇠를 강하게 준다. 긴 머리 옵션은 기존 기본값을 사용한다. `--fringe-pose-*`, `--fringe-skin-tip`은 이제 실제 앞머리 구역에 적용된다.

실제 앱 캡처/원자료: `artifacts/run-styling/{baseline,held-fringe,balanced,idle,Debug}`. 동일한 390스텝 출발·회전·정지 스크립트로 비교했다. 120스텝의 초기 대비 헤드 로컬 가이드 팁 RMS 변위는 앞머리 129.85 → 40.78 mm, 옆·뒷머리 91.65 → 65.00 mm였다. 이는 흔들림 진폭이나 물리 정확도 오차가 아니라 각 설정의 초기 팁 위치 대비 변위다. 모양/중력 처짐도 포함하며, 단순히 69%의 흔들림 제거로 해석하면 안 된다. 움직임과 캡처를 함께 검토했다. 과격한 자세 전환에서 끝이 벌어지는 현상과 앞머리 묶음 모양은 여전히 남는다.

재현: `HairLabDX12.exe --walk-test --steps 390 --capture-every 30 --reset-check --capture-dir artifacts/run-styling/replay --log artifacts/run-styling/replay.log`. CLI 입력 검사이며 실제 마우스/키보드 조작 검증은 아니다.

현재 기본 커트는 후속 [LAYERED_BOB.md](LAYERED_BOB.md)의 salon-v6다. 이 문서의 공통 물성과 표시 설정을 그대로 사용하며, 아래 컷/캡처는 v5 기록이다.

앞머리는 붙고, 옆·뒷머리 기준으로 바꾸면 얼굴을 덮는 두 극단 사이에서 실제 Jolt 캡처를 보며 조정했다. Jolt Hair만 사용한다. 캐릭터/두피/본과 기존 긴 머리 파일은 보존했다.

## 커트와 표시

`scripts/create-moca-bob.py`에서 긴 얼굴 옆머리와 뒷머리를 하나의 단발 끝선으로 연결했다. 짧은 앞머리 캡과 고립된 이마 잔가닥을 없앴고, 큰 쪽 커튼은 관자놀이를 조금 더 낮게 지나가도록 비대칭으로 빗었다. 끝은 약 4mm 안쪽으로 감기는 작은 턴언더이며 정수리 볼륨은 유지했다. 렌더 32,000가닥, 가닥당 32점, Jolt 가이드 1,600개다. 가이드 선택과 렌더 헤어 갱신은 Jolt가 한다.

`shaders/JoltDisplay.hlsl`은 현재 위치의 중앙차분 접선으로 광택 방향을 구하고, 금속처럼 밝던 하이라이트를 낮췄다. `src/JoltMain.cpp`는 지원되는 장치에서 씬 색상/깊이 4× MSAA를 사용한다. 씬을 스왑체인으로 resolve한 뒤 기존 ImGui/PNG 경로를 사용한다. 지원이 없으면 1×로 기록하고 실행한다. 새 MSAA 리소스는 기존 큐/펜스와 resize 수명을 따른다. 물리나 전체 헤어 CPU 왕복을 추가하지 않았다.

이는 선 기반 실시간 헤어의 개선이며, 생산용 헤어 셰이딩이나 완전한 안티앨리어싱은 아니다. 아주 가까이 보면 가르마와 끝선의 선 표현은 남는다.

## 공통 스프레이 강도

모든 영역이 **같은 물성**을 쓴다. 재질 ID는 가이드/렌더 연결 영역을 구분하기 위해서만 남겨 두었다. 기본값:

| 설정 | 값 |
|---|---|
| GlobalPose | root 0.01 → 길이 65%에서 0.00025, 이후 유지 |
| SkinGlobalPose | root 1 → 길이 45%에서 0.06, 이후 유지 |
| 충돌 반경 / gravity preload | 1mm / 1 |
| bend / stretch compliance | 1e-7 / 1e-8 |
| 선형 / 각 감쇠 | 2 / 2 |

UI **Jolt material settings → Styling spray**의 0 / 0.5 / 1은 아래 약/중/강 설정을 보간한다. 기본은 0.5다. 고급 슬라이더를 직접 바꾸면 이 프리셋보다 우선하며 Reset은 기본값으로 돌아간다. 스프레이는 Jolt의 기존 형태 유지/스키닝 추종 계수를 조절하는 UI 비유이며 새 물리 알고리즘이 아니다.

| 비교 | pose tip / end fraction | skin tip | 120스텝 렌더 끝점 RMS 변화 |
|---|---|---|---|
| 약 | 0.0001 / 0.5 | 0.03 | 26.68mm |
| 중 | 0.00025 / 0.65 | 0.06 | 20.09mm |
| 강 | 0.0005 / 0.8 | 0.12 | 15.64mm |

`artifacts/salon/study/results.json`과 PNG가 동일한 초기 커트에서의 비교다. 변화량은 초기 상태 대비 중력과 애니메이션을 포함하며, 미적 품질 점수나 순수 탄성 지연량은 아니다. 약은 처짐이 크고 강은 단단해 보여 중을 선택했다. 이후 같은 중 물성에서 관자놀이 커트를 한 번 더 낮췄다. 그러므로 위 표를 최종 커트의 정밀한 A/B 수치로 재사용하지 않는다.

## 실행과 확인

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --view hair
python scripts/validate-salon.py artifacts/salon/delivered
python scripts/package-moca.py
```

실행 후 **Run**, 필요하면 **Full hair framing**. 기본은 일시정지다. 기존 헤드 턴과 Run/Pause/Step/Reset, 카메라, 긴 머리 선택을 유지했다.

원래 상태는 `artifacts/salon/baseline/`에 필요한 파일만 저장했다. 1차/관자놀이 수정 캡처는 각각 `final/`, `final-refined/`에 보존하고, **MSAA 포함 전달 버전**의 실제 캡처와 검증은 `artifacts/salon/delivered/`에 별도로 저장한다. Release는 240스텝(4초), 15스텝 간격 캡처다. Debug 60스텝, 측면/후면, 정적 머리, 기존 긴 머리 및 컨트롤 API도 확인한다. 모든 스냅샷에 영역별 재질 동일성, 유한 상태, 루트 부착과 리셋을 검사한다.

정면·측면·후면과 머리 회전 중 여러 완료 상태를 직접 보았다. `artifacts/salon/playback.html`은 전달 PNG를 0.25초 간격으로 재생한다. 보간/합성 움직임은 없다. 물리적 마우스/키보드/리사이즈 조작과 새 슬라이더의 실제 클릭은 자동 캡처만으로 검증했다고 주장하지 않는다.

최종 커트의 MSAA 전 애니메이션/비애니메이션 동일 시간 비교(`final-refined/animated-versus-static.json`)에서 30스텝 렌더 끝점 차이는 RMS 6.77mm, 최대 9.92mm였다. 포즈와 동적 반응을 함께 포함하므로 이를 순수 지연으로 부르지 않는다. 정확한 최종 전달 수치와 실행 파일 해시는 `delivered/validation.json`, `delivered/runs.json`이 기준이다.

변경: groom 생성기와 bob.hair/bob.json/manifest, JoltScene 공통 설정, 앱의 스프레이 UI·MSAA, 표시 셰이더, 검증/패키징 문서. Jolt 알고리즘·컴퓨트 셰이더, 캐릭터 메쉬/텍스처/본, 기존 긴 머리, Unity 소스는 변경하지 않았다.
