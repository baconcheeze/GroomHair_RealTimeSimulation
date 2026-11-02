> ???: ??? ???? ? ??? ?? ??? ????. ?? ??? artifacts? ?? ?? ???? ?? ??? ???? ???.

﻿# 포니테일 렌더 연결의 역전/부모 전환 수정

## 확인한 문제

기존 `InitRenderAndSimulationStrands`는 곡선의 각 렌더 점에 가장 가까운 가이드 점을 독립 선택한다. 원본이 웨이브인 포니테일에서는 매핑 순서가 뒤로 돌아갈 수 있다. 또한 프로젝트의 `AnchorMetaHumanRenderRoots`가 뿌리를 가까운 별도 가이드에 연결하면서 본체와 다른 부모를 쓰는 연결이 생겼다.

전체 5,177 포니테일 가닥을 감사한 결과 **역전 연결 131개, 부모 전환 연결 2,323개**였다. 이는 가닥 수가 아니라 연속 점 사이의 연결 수다. 이 데이터 결함은 렌더 가닥을 비틀거나 접히게 만들 수 있다. 관찰되는 모든 꼬임의 유일한 원인이라고 단정하지 않는다. 원본에는 웨이브가 있으며 이번 수정은 가닥 간 접촉/교차 방지를 추가하지 않는다.

## 수정

`src/MetaHumanAttachment.h`의 `BindMetaHumanPonytails`가 material 2/6만 처리한다. 기존에 선택된 본체 가이드를 유지하고, 렌더 가닥의 정규화된 누적 길이를 가이드 길이에 대응시킨다. 연속하는 두 가이드 점을 Jolt의 기존 influence 형식으로 등록한다. Jolt가 상대 오프셋과 가중치를 계산하고 기존 GPU render reconstruction을 실행한다. 직접 시뮬레이션하는 가닥의 연결은 유지한다. 원본 위치/asset 파일과 물성, shader/solver는 수정하지 않았다.

수정 후 초기 연결 감사: 역전 0, 부모 전환 0. 이 수치는 Init 이전의 지원 인덱스 감사이며, 이후 가중치 정렬에 의한 influence 슬롯 순서와 혼동하지 않는다.

## 검증

- Release: 같은 머리 애니메이션 120스텝, 0/30/60/90/120 캡처, Reset.
- Release: 기존 `--walk-test` 120스텝, Reset.
- Debug: 30스텝 및 Reset (결과 app.log 참조).
- 동일 입력 0/60/120 비교: 모든 가이드 끝 위치 차이 0m, 포니테일 외 렌더 끝 위치 차이 0m, material JSON 동일.
- 렌더 포니테일 최대 끝 위치 변화: 초기 약 0.196mm, 60스텝 0.406mm, 120스텝 0.396mm. 이것은 이전 버전과의 변화량이며 개선률 지표가 아니다.
- 정상/수정 후 후면 초기 및 120스텝 PNG를 실제 열어 확인했다. 원래 웨이브는 남아 있다. 실제 사용자 마우스 조작은 검증하지 않았다.
- `artifacts/ponytail-twist/validation.json`: 비교 결과 및 자산 해시. 이전 결과를 보존했다.

## 실행

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust-back --hide-body --steps 120 --capture-every 30 --reset-check --capture-dir artifacts/ponytail-twist/replay --log artifacts/ponytail-twist/replay/app.log
```

기본 사용자 실행 경로 `build/vs2022/bin/Release/HairLabDX12.exe`도 갱신했다.
