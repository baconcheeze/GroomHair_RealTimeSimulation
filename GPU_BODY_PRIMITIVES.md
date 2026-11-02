> 2026-11-02 ????: ?? ???? ?? primitive? 124????. ?? 58? ????????? ??? ?? ?? ?????. ?? ??? ??? [SHOULDER_FIT.md](SHOULDER_FIT.md)? ?????.

# GPU 몸통 충돌체와 XYZ bitmask

몸통·팔의 CPU 스키닝/convex 재생성을 GPU 캡슐 갱신으로 교체했다. Jolt 헤어 적분·retention·충돌 보정·마찰·렌더 헤어 갱신은 유지한다. 머리·귀 3개는 기존 convex hull 경로를 유지한다. 과거 승인 체크포인트는 수정하지 않았다.

## 실제 구현

- 기존 피부 전용 `body-colliders.bin`의 58개 분할 점군을 시작 시 한 번 캡슐로 피팅한다. 가장 긴 로컬 축을 사용하고, 횡단 반경과 구형 끝단 조건으로 입력점을 포함하는 끝점을 계산·검사한다. 현재 장면은 58개 캡슐이다. 길이 0인 구도 커널에서 지원하며 합성 검사에 포함했다. 별도 유한 원통은 추가하지 않았다.
- `src/BodyPrimitiveFit.h`: 피팅과 끝점의 본 가중치 선정. 변형 부위는 가장 가까운 원본 피부 정점의 최대 4개 가중치를 사용하고, 손가락은 기존 소유 본에 고정한다. 두 끝점을 독립적으로 스키닝하므로 전체 도형을 매번 재생성하지 않는다.
- 두피와 동일하게 **목 본 bind 좌표로 입력을 변환한 다음**, 기존 GPU scalp joint matrix를 공유한다. Jolt 내부 미터/좌표계를 유지하며 Z 반전은 렌더 경계에서만 수행한다.
- `HairBodyPrepare.hlsl`: primitive 하나당 GPU 스레드 하나로 끝점을 스키닝하고, 이전 끝점을 현재 hair frame으로 변환해 접촉 속도를 계산한다. 끝점 속도로 중심 이동과 축 방향 변화에 따른 각속도를 근사한다. 축 자체의 비틀림과 표면의 비강체 변형 속도까지 재현하는 모델은 아니다. Reset/teleport에서는 이전 속도를 버린다.
- `HairBodyField.hlsl`: X/Y/Z 각각 32개 bin, bin당 uint32 4워드(최대 128개 primitive). 각 슬롯의 단일 소유 스레드가 AABB 구간을 검사해 마스크 전체를 덮어쓴다. clear/atomic 경합은 없다.
- field 범위는 hair-local 각 축 [-4,4]m, bin 폭 0.25m. 범위 밖 좌표는 끝 bin으로 포화시킨다. primitive AABB는 0.25m 확장한다. 포화는 false positive를 늘릴 수 있지만 경계 구간을 누락시키지는 않는다.
- `HairBodyQuery.hlsl`: 입자별 `X & Y & Z` 마스크에서 켜진 비트만 순회하고, 구/캡슐의 최근접점·signed distance로 접촉 평면을 구한다. 기존 머리/귀 접촉과 가장 깊은 접촉을 선택해 Jolt의 원래 보정 경로에 전달한다. 고정 루트는 건너뛴다.
- 기존처럼 접촉 평면을 update당 한 번 만들고 내부 반복에서 재사용한다. 확장 AABB는 CCD나 swept collision이 아니다. 빠른 운동의 연속 충돌을 보장하지 않는다. 이전에 제거한 hull 예측 이동량은 여전히 0이다.

## 소유권·메모리·표시

`Hair::SetBodyPrimitives`가 시작 시 입력과 GPU 작업 버퍼를 생성한다. 한 direct compute queue의 기존 barrier/fence 경로로 Prepare → Field → 기존 hull plane → primitive Query → 기존 Jolt solver를 연결한다. 보정 반복 중 CPU 왕복, per-frame primitive 정점 업로드, CPU hull 생성은 없다. 기존 본 행렬 업로드는 남아 있다. CPU의 기존 ExecuteAndWait 계약은 변경하지 않았다.

입력은 primitive당 float4 6개(96B), 상태는 float4 4개(64B), field는 uint32 384개(1536B), context는 16B다. 현재 58개에서 이 네 항목의 논리 데이터는 10,832B다. GPU wire 표시 버퍼는 float3 11,600개로 139,200B이며 표시를 꺼도 준비된다. 합계 논리 크기는 **150,032B**다. 이는 D3D12 committed resource의 정렬/heap 및 기존 Jolt 자원을 포함한 물리 할당량이 아니다.

`Collision hulls / GPU body capsules`를 켜면 기존 head hull은 주황색, 실제 GPU 끝점에서 생성한 몸통 캡슐은 녹색으로 보인다. 일반 렌더링은 GPU 데이터를 직접 읽는다. JSON 캡처 요청에 한해서만 상태/field를 readback하고 독립 CPU 스키닝과 비교한다.

## 검증

결과는 `artifacts/gpu-primitives/`에 보존했다.

- `--body-check`: 1/31/32/33/63/64/65/127/128개, 2049개 입자, tail lane, 다중 워드, sphere/axis-center, 범위 밖 좌표, 혼합 본 가중치, 이동하는 프레임의 접촉 속도/이전 상태 검사. Debug와 Release 통과. 모든 field 비트를 CPU와 비교했다. 후보 누락 0/3,781, narrow phase 1,558건 최대 거리 오차 1.49e-7m. GPU 합성 끝점 오차 0.
- 실제 달리기 120스텝, 댄스 180스텝, Debug 댄스 30스텝: finite, 58개 유지, Reset 초기 상태 일치. 실제 끝점 GPU/CPU 최대 오차 5.21e-7m, hair root 부착 오차 3.34e-8m 이하.
- controller/API 검사: pause, step, run/pause, reset, hair physics toggle, hairstyle 변경과 기존 그룹 물성 동작 통과. 실제 마우스·리사이즈 조작은 미검증.
- Debug layer 오류 없음. 초기 개발 중 dummy immutable buffer의 null 초기 데이터 assertion을 발견해 유효한 zero record로 수정했다. 초기 실패 로그도 보존했다.
- 기존 캐릭터/헤어/충돌 원본/애니메이션 에셋 해시를 승인 체크포인트와 비교해 불변 확인.
- `aligned/step-0030.png`, `dance/step-0180.png`에서 실제 GPU 충돌체 정렬과 움직이는 헤어를 직접 확인했다. 초기 좌표 변환 누락은 수정했다.

이는 primitive 근사다. 변형된 피부 전체를 매 프레임 정확히 감싸는 hull과 동일하지 않으며, 피부/옷 표면의 모든 관통이나 겨드랑이의 빈 공간까지 해결했다고 주장하지 않는다. 반경은 bind fit에서 고정되며 별도 원통·GPU convex 재생성은 구현하지 않았다.

## 제한된 성능 비교

RTX 5070 Ti, Release, GPU timestamp 활성, debug layer off. 같은 실제 18,209 guides / 34,392 render hairs, air resistance 1/s, `--walk-test --steps 120`. 승인 체크포인트 baseline 실행 후 candidate 실행의 **한 쌍**이다. 60..119스텝의 60샘플, 로깅/캡처 밖의 기존 update 계측을 사용했다. 전용 성능 환경이나 장기 통계는 아니다.

| 항목, 중앙값 ms | 기존 hull | GPU primitive |
|---|---:|---:|
| CPU hull 재생성 | 9.274 | 0 |
| CPU update 전체(GPU 대기 포함) | 13.975 | 2.731 |
| GPU hair command list 실제 전체 | 4.207 | 2.305 |
| 기존 hull 탐색(head/ears만 잔류) | 2.464 | 0.525 |

새 GPU 단계 중앙값: endpoint skin/wire 0.0182ms, bitmask 0.00669ms, primitive query 0.0696ms. 개별 중앙값의 합을 전체 시간으로 사용하지 않았다. 전체 P95는 CPU update 14.639→2.981ms, GPU command list 4.709→2.579ms. 실행 순서/다른 GPU 부하의 영향이 있을 수 있다. 충돌 형상도 바뀌므로 동일 충돌 품질에서의 순수 구현 속도비로 해석하면 안 된다.

## 재현

```powershell
cmake --build build/vs2022 --config Release --target HairLabDX12
.\build\vs2022\bin\Release\HairLabDX12.exe --hulls
.\build\vs2022\bin\Release\HairLabDX12.exe --body-check --capture-dir artifacts/body-check --log artifacts/body-check/app.log
.\build\vs2022\bin\Release\HairLabDX12.exe --walk-test --steps 120 --capture-every 60 --reset-check --profile-csv artifacts/body-run/profile.csv --capture-dir artifacts/body-run --log artifacts/body-run/app.log
```
