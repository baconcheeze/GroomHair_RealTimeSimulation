# 런타임 가이드 수 전환

앱의 **Guide density**에서 Full / 1/2 / 1/4 / 1/8을 선택한다. 현재 기본 groom의 가이드/분절 수는 다음과 같다. Full은 원래 선택된 18,209가닥을 뜻하며 모든 render strand를 시뮬레이션한다는 뜻이 아니다.

| 선택 | Guide | Guide 분절 |
|---|---:|---:|
| Full | 18,209 | 564,479 |
| 1/2 | 9,107 | 282,317 |
| 1/4 | 4,556 | 141,236 |
| 1/8 | 2,280 | 70,680 |

렌더 헤어는 모든 단계에서 **34,392가닥 / 1,066,152분절**로 유지한다. 가닥당 정점 32개/분절 31개를 유지하며, 가닥 내부 정점을 줄이는 옵션은 아니다. 숨김 영역도 수치에 포함된다.

## 동작

`JoltScene::Initialize`에서 기존 그룹별 `mSimulationStrandsFraction`을 1/2/4/8로 나눈 뒤 Jolt의 원래 `InitRenderAndSimulationStrands`로 가이드 선택과 렌더 influence를 다시 만든다. 그룹별 ceil 및 최소 1가닥 때문에 정수 나눗셈과 차이가 있다. Jolt의 기존 고정 seed 선택을 그대로 사용하며 커스텀 solver나 보간 커널은 추가하지 않는다. 기존 MetaHuman root attachment도 재구성한다.

`JoltMain::changeGuideDensity`는 UI와 검사에서 공유한다. 변경 시 graphics/compute fence를 기다리고 이전 자원을 해제한 뒤 다시 생성한다. 실행을 일시정지하고 물리 상태·애니메이션·이동 위치를 초기화한다. **Run을 다시 누르면 재생**된다. 가이드와 영향 관계를 만드는 동안 잠시 멈출 수 있다. 무중단 상태 이관이나 비동기 rebuild는 구현하지 않았다.

전환 시 편집한 물성(공기저항, damping, retention 등)은 보존한다. selection fraction만 새 비율로 교체하므로 여러 번 전환해도 1/2이 누적 적용되지 않는다. 일반 Reset/다른 장면 변경의 기존 물성 초기화 동작은 유지한다. 가이드를 줄이면 렌더 변형의 근사 품질과 grid의 샘플링이 달라질 수 있으며 물성을 자동 보정하지 않는다.

## 실행·검사

```powershell
.\build\vs2022\bin\Release\HairLabDX12.exe --guide-reduction 8
.\build\vs2022\bin\Release\HairLabDX12.exe --guide-density-check --capture-dir artifacts/guide-check --log artifacts/guide-check/app.log
```

기본값은 Full이다. `--guide-reduction`은 1/2/4/8만 허용한다. `--guide-density-check`는 같은 프로세스에서 Full→1/2→1/4→1/8→Full 전환, 렌더 개수·편집 물성 보존, 전환 후 GPU step과 root/finite 검사를 실행한다.

Release에서 runtime 전환 검사 및 각 감소 단계의 댄스 60스텝/Reset 검사를 수행했다. 실제 1/8 재생 PNG도 확인했다. 결과·캡처는 `artifacts/guide-density/`에 있다. 실제 마우스로 콤보를 누르는 검사는 수행하지 않았고 UI와 같은 controller 함수를 사용했다. 기존 에셋과 승인 체크포인트는 변경하지 않았다.

Debug? ?? 5? ?? ??? ??? Full ???? ????? 180? ??? ??? ??????. ?? Debug ??? ??? ???? ????. ? JSON readback? ???? ????, Release ?? ???? ? 29?? ??????. ?? ??? ?? ???????? ?????.

??? Debug 1/8 ???2???Reset smoke? ???? D3D12 debug layer ??? ?????. Debug/Release ? ??? ??? Release ?? ????? ?? ??? ?? ??? ??????.
