# Salon-v6: 얕은 레이어와 끝 질감

균일한 단발 끝선이 두꺼운 판처럼 보인다는 요청에 따라 커트만 다듬었다. **salon-v5의 전체 공통 물성, 중간 스프레이 강도, Jolt 계산과 표시 셰이더는 그대로**다.

## 커트

`scripts/create-moca-bob.py`에서 기존 깊이 표본을 재사용한다. 바깥층의 단발 실루엣은 남기고 안쪽일수록 최대 9mm 짧게, 3mm 더 안쪽으로 넣었다. 얼굴 가까운 커튼은 최대 12mm 짧게 하되 옆머리 쪽으로 차이가 사라지도록 연결했다. 끝은 최대 4.5mm의 작은 포인트 커트 질감을 더했다. 짧은 앞머리 캡이나 별도 물성 레이어는 만들지 않았다.

전체 헤어 끝의 실제 높이 범위는 1.17111–1.19488m다. 이전 컷 대비 평균 6.23mm 짧아졌고 최대 높이 변화는 23.50mm다. 겹치는 세 조정의 합계이며, 모든 가닥을 23.5mm 줄였다는 뜻은 아니다. 32,000개 루트는 이전 파일과 비트 단위로 동일하다. 재질 ID와 물성 소스도 동일함을 해시로 확인했다. `artifacts/salon-layered/cut-validation.json` 참조.

Jolt가 새 커트를 읽어 가이드 1,600개와 렌더 32,000가닥을 구성한다. 전체 가닥 수는 유지하며, 가닥 제거·밀도 축소·별도 렌더 헤어 보간 방법은 추가하지 않았다. 캐릭터, 보강 두피, 315개 본, 기존 긴 머리와 원본 패키지는 수정하지 않았다.

## 빌드·실행

```powershell
python scripts/create-moca-bob.py
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --view hair
python scripts/validate-salon.py artifacts/salon-layered/final
python scripts/package-moca.py
```

패키지 실행: `artifacts/moca/runtime/HairLabDX12.exe`. Run으로 머리 회전과 실제 헤어 움직임을 시작한다. Styling spray 기본 0.5와 전체 공통 적용은 유지한다.

## 캡처와 검증

이전 커트와 필요한 생성/물성 파일은 `artifacts/salon-layered/before/`에 보존했다. `preview/`는 120스텝 시안이며 정면 초기/회전/복귀 캡처를 직접 확인했다. 최종 Debug/Release 실행 결과와 해시는 `final/runs.json`, 루트·움직임·리셋 검증은 `final/validation.json`에 저장한다.

최종 Release는 240스텝, Debug는 60스텝이다. 측면/후면과 컨트롤 API, 기존 긴 머리도 유한 실행으로 확인한다. 실제 가이드·렌더 헤어 움직임, 두피 부착, 영역별 동일 물성, 정확한 리셋을 검사한다. `artifacts/salon-layered/playback.html`은 실제 완료 상태 PNG를 15스텝 간격, 0.25초/프레임으로 재생한다. 합성 움직임이나 영상 보간은 없다.

가벼운 안쪽 졸업 커트와 끝 질감을 선택했으며 과한 샤기컷은 아니다. 아주 가까이 보면 선 기반 렌더의 거친 가장자리는 남는다. 실제 마우스/리사이즈 조작 검증을 자동 캡처로 대체했다고 주장하지 않는다. 이번 작업은 커트 변경이며 새 물성 튜닝이나 Jolt 알고리즘 변경은 없다.
