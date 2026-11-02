> 충돌은 예측 접촉 추가 전의 기존 방식 하나만 사용합니다. Predictive 모드·선택 UI·전용 GPU 경로는 제거했습니다.

> ?? ??: [SIDE_BANGS_V15.md](SIDE_BANGS_V15.md). ???? ??? ???? ????? ??/??? ??????. ?? ?? ??? ????? 480??? ??????.

> ?? ????? ??? ??? ????. ? ? ?? ??(region 4)? ??? ???? ?? v13 ?????? ?????. `shaders/JoltDisplay.hlsl`? `HideSideburns`? ?? `true`???. ???/? ???? ?????.

> ??? ???? ?? v14: [SIDEBURN_V14.md](SIDEBURN_V14.md). ??? v13? `checkpoints/photo3-v13-approved/runnable/HairLabDX12.exe`? ?? ??????. ?? 33,512??? ??? ?? ???? 400??? ??????.

> ?? ??? **????3.jpg**???. [METAHUMAN3_MATCH.md](METAHUMAN3_MATCH.md). ?? ?? ??? ??? ?? 33,512???? ? ?? ?? ??? ?? ?????. ?? ?? ??? ???? ??? ?? ??/41,072?? ??? ?? ?????.

> ? ?? ??? ??: [EAR_FILL.md](EAR_FILL.md). ???/? ???? ?? ???? ????, ? ??? ?? ????? ??????. ?? ?? ??? 41,072?????.

> ?? ?? ??: [EAR_ONLY_RESTORE.md](EAR_ONLY_RESTORE.md). ? ?? ??? 761??? ?? ?? ??? ??????. ?? ??? ? ??? ?????.

> ? ??? ???: [SIDE_HAIR_REDESIGN.md](SIDE_HAIR_REDESIGN.md). ? ?? ?? ??? ?? ??????, ?? ? ? ???? ? ???/?? ????? ??????. ?? ?? ???? 38,312?????. ?? ?? ??? ?? ?/?? ?? ?? ???? ? ??? ?????.

> ? ?? ??? ?? ??: [EAR_WRAP.md](EAR_WRAP.md). ?? ?? ???? ? ????? ?? ??????.

> ??? ???? ?? ??: [NAPE_COVERAGE.md](NAPE_COVERAGE.md). ?? ??? ? 25mm ?? ??? ?? ?? ?? ??? ?????.

> 최신 마감: [HAIR_SALON_FINISH.md](HAIR_SALON_FINISH.md). 윗머리·귀 뒤·아래 시점의 연결을 다시 제작했습니다. 정지 및 헤드턴 검사와 별도로, 걷기 일부 자세의 미세 관통은 남아 있습니다. 이전 상세 설명보다 최신 문서를 우선합니다.

# HairLabDX12 — Moca / Jolt Hair

카메라는 **방향키 ↑↓←→**로 화면 기준 상하좌우 이동한다. **Shift + 방향키**는 3배 빠르다. UI 입력 중에는 동작하지 않으므로 UI 조작 뒤 빈 장면을 클릭한다. WASD 캐릭터 이동과는 별개이며, RMB 회전·MMB 팬·휠 확대·F 프레이밍·R 카메라 리셋도 유지한다.

몸을 가리고 헤어를 보려면 메인 패널의 **Hide body (head + hair only)**를 체크한다. 얼굴·두피·헤어는 유지하고 몸과 청자켓의 표시만 숨긴다. 본·헤어 시뮬레이션·충돌체는 계속 동작하며, 체크를 해제하면 기존 의상 표시 선택도 복원된다. CLI: `--hide-body`.

## 새 기본 헤어: 가져온 Wavy Twin Ponytail

사용자 제공 `assets/MetaHuman/long_wavy_twin_ponytail_hair/Long_Wavy_Twin_Ponytail_Hair.abc`를 현재 Moca 두피에 맞춰 **Jolt Hair**에 연결했다. 현재는 귀·헤어라인을 마감한 `salon-drape-v7`다. `메타휴먼1.jpg`, `메타휴먼2.jpg`, `메타휴먼3.jpg`를 기준으로 중앙 가르마·긴 옆머리·귀 앞 구렛나루·낮은 트윈테일 연결부를 보호한다. 원본 8개 그룹 전체를 유지한다. 기본 실행은 트윈테일이며, UI `Hairstyle`에서 기존 단발/긴 머리도 선택할 수 있다. 최신 마감 설명과 검사 범위는 [SIDE_HAIR_FINISH.md](SIDE_HAIR_FINISH.md)를 참고한다.

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --hairstyle metahuman --view bust --run
```

패키지 실행: `artifacts\moca\runtime\HairLabDX12.exe --hairstyle metahuman --view bust --run`. 인수 없이 실행하면 기존 WASD 플레이그라운드에서 새 헤어를 사용한다. Run/Pause, Step, Reset, 헤드 애니메이션, Guide/Render, Head/Hulls와 카메라는 유지한다. `Inspect left ear` / `Inspect right ear`로 귀를 확대한다. CLI에는 `--view ear-left|ear-right|ear-back-left|ear-back-right`도 있다. 8개 물성 구역 중 귀 접촉 덮개/꼬리는 각각 기존 덮개/꼬리와 같은 물성을 쓰되 직접 가이드와 충돌 여유를 사용한다.

- 원본 **33,512가닥 / 1,337,985 제어점**, 8개 곡선 그룹을 모두 사용한다. **삭제 0가닥, 커트 0가닥**이다. 검사 스크립트도 이 조건을 요구한다.
- `D_MAIN_HAIR_002` 10,166가닥, `D_FRONT_00` 602가닥, `D_MAIN_HAIR` 16,588가닥, `D_FRONT_01` 761가닥, `D_PONY_TAIL` 5,177가닥, `D_OTHER_01` 101가닥, `D_SIDE_01` 97가닥, `D_SIDE_02` 20가닥을 보존한다. 특히 `D_FRONT_01`의 원래 긴 길이와 `D_SIDE_*`의 귀 앞 곡선을 복원했다.
- Blender 5.1.2가 NURBS를 평가하고 가닥당 32점으로 샘플링한 뒤 국소 피팅한다. 렌더 **33,512가닥 / 1,072,384점**, Jolt 가이드 **15,871가닥**. 귀 움직임 범위 안의 가닥, 긴 옆머리 761가닥, 귀 주변 117가닥은 직접 시뮬레이션해 다른 가이드의 오프셋 보간으로 두상을 뚫지 않게 했다. 가이드 수 증가는 GPU 부하를 늘린다. 원본 가이드 플래그는 Blender에서 노출되지 않아 Unreal의 기존 가이드 선택을 복제한 것은 아니다.
- 네이티브 자산: `assets/moca/metahuman.hair`, `metahuman.materials`, `metahuman.ears`, `metahuman.json`, `metahuman.NOTICE.txt`. 원본 `.abc`와 `.mhpkg`는 수정하지 않았다. 실행에는 Blender/Unreal/원본 폴더가 필요 없다.
- 좌표 맞춤: 원본 Z-up/−Y 전방 좌표를 `(0.010*x, 1.270+0.0091*(z−160.56), 0.022+0.0085*(y−0.5))` 미터로 변환한다. 실제 두피에 맞춘 뿌리 이동을 가닥 전체에 동일하게 적용해 원본 커브의 방향·컬·끝을 보존한다. 일괄적인 방사형 평탄화/이마 내림/옆머리 벌림은 제거했다. 실제 두상 간격을 침범하는 점만 1mm 여유로 국소 보정한다. 렌더러의 Z 반사는 기존 한 곳에서만 수행한다.
- 크기 변환 및 가닥 전체 이동을 제외한 앞머리/옆머리의 국소 변화는 변환 단계 RMS 약 0.139mm, 최대 약 3.78mm다. 원본과 사진이 픽셀 단위로 같다는 의미는 아니며 실제 렌더 질감은 여전히 다르다. 원자료는 `metahuman.json`에 기록한다.
- 두상 위 머리는 두피 추종과 복원력을 높였다. 트윈테일은 기존 Jolt GlobalPose/SkinGlobalPose로 구매 에셋의 웨이브를 더 유지하도록 조절했다. Jolt 알고리즘·시간 간격·중력·질량은 바꾸지 않았다. 기존 단발 물성도 그대로다.
- 실제 귀와 두피를 기준으로 귀 안쪽/목에 붙었던 뿌리를 뒤통수 헤어라인으로 옮겼다. 덮개는 두피에서 약 3.5~4.5mm의 얇은 층으로 정리하고 가닥의 점뿐 아니라 연결 선분도 귀를 피하도록 마감했다. 피팅을 망가뜨리던 로딩 중 중복 재샘플링은 제거했다. 렌더 뿌리는 Jolt의 기존 influence로 고정 가이드 뿌리에 연결한다. Head에 붙은 귀 충돌체 2개를 포함해 충돌체는 5개다. Jolt 솔버와 셰이더는 수정하지 않았다. 검사는 `scripts/verify-side-finish.py`, `scripts/check-side-mesh.py`를 Blender로 실행한다. 확인한 캡처 밖의 모든 포즈를 보증하는 검사는 아니다.
- 기존 Moca 스켈레톤·애니메이션·충돌체를 사용한다. `.mhpkg`의 헤드/Unreal 바인딩/물리 에셋을 가져온 것은 아니다. 가이드와 렌더 헤어 모두 Jolt의 실제 GPU 출력으로 움직인다.
- 현재 검은색 스트랜드 재질이다. Unreal 전용 셰이더 및 RootUV 핑크/블루 텍스처는 재현하지 않았다. 실제 청자켓 표면과 충돌체는 일치하지 않으므로 일부 옷 관통이 남는다. 최종 머티리얼/충돌 피팅 완성을 주장하지 않는다.

재변환(설치된 Blender 사용, 런타임 불필요):

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts\blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/import-metahuman.py
cmake --build --preset release --target HairLabDX12 -- /m:1
```

검증: `python scripts/validate-metahuman.py`. Release 정면/측면/달리기, Debug 정면, 컨트롤러 API, 기존 단발/긴 머리, UI 캡처를 제한 실행한다. 다른 작업 폴더에서 실행하며 뿌리 오차 <1e-5m, 유한 상태, 저장된 초기 물리 상태의 정확한 리셋, 모든 그룹과 가닥 수를 확인한다. PNG 바이트 일치는 별도로 기록한다. v4 첫 실행에서는 초기 상태 JSON 전체가 같지만 두 픽셀의 RGB 값이 1/255 달라 PNG 바이트 검사는 실패했다. 원인 미특정이며 실패 자료를 `artifacts/metahuman-silhouette/reset-byte-difference/`에 보존한다. 이를 바이트 일치 통과로 바꾸어 기록하지 않는다. 물리적 마우스/키보드/리사이즈는 별도 수동 미검증이다.

원본·사진·변환 해시와 그룹 범위: `assets/moca/metahuman.json`. 현재 실행 기록: `artifacts/metahuman-finish/verified/{runs,validation,clearance}.json`. 양쪽 귀·뒤쪽 실제 확대 캡처: `artifacts/metahuman-finish/verified-close/`. 이전 작업과 이번 중간 시도도 보존하며 최종 판정은 `verified` / `verified-close`만 사용한다. 원본은 구매 에셋이며 Jolt MIT 라이선스로 재배포 가능한 자산이라고 주장하지 않는다. 패키지는 로컬 비공개 사용용이다.

---

앞머리 Pose retention 기본값은 현재 UI 최대치인 뿌리/끝 **0.4 / 0.4**다. Reset·재실행에도 이 값을 사용하며 옆·뒷머리 물성은 유지한다. Styling spray를 움직이면 선택 구역의 별도 프리셋 값으로 바뀐다.

현재 단발은 달리기용 구역별 물성이다. 앞머리는 강하게 잡고 옆·뒷머리에는 움직임을 남겼다. `Jolt material settings → Hair region`에서 구역별 편집: [SALON_STYLE.md](SALON_STYLE.md).

앞머리 리셋 원형의 이마 간격을 수정했다. `salon-v9-forehead-fit`은 앞머리 끝을 실제 이마 곡면에서 3–3.5mm 앞으로 맞추며, 이전의 중력 처짐을 예상한 짧은 높이 보정을 제거한다. 숱·뿌리·옆뒷머리·구역별 물성은 그대로다. 캡처: `artifacts/fringe-contact/front/step-0000.png`, `artifacts/fringe-contact/side/step-0000.png`.

현재 앞머리는 후속 `salon-v10-soft-fringe`다. 여섯 개의 넓은 구획을 제거하고 앞머리 숱을 2,134 → 1,067가닥으로 줄였다. 눈썹 길이의 가는 끝과 한쪽으로 약하게 흐르는 가르마를 사용한다. 이마 맞춤·기존 옆뒷머리·구역별 물성은 유지한다. 실제 캡처: `artifacts/fringe-soft/final/step-0000.png`, `artifacts/fringe-soft/final/step-0120.png`.

현재 WASD 이동은 사용자 제공 `assets/NarutoRun.fbx`의 실제 애니메이션을 사용합니다. 실행 및 제한 사항: [NARUTO_RUN.md](NARUTO_RUN.md).

기본 실행은 이제 **WASD 보행 플레이그라운드**입니다. 카메라 기준 이동, 쿼터뷰·자유 오빗/줌/패닝, 격자 타일 바닥, 실제 Head 월드 변환에 연결된 Jolt 헤어를 제공합니다. 기본 Run 상태이며 UI 조작 뒤에는 장면을 클릭해 WASD 초점을 돌려주세요. **Follow character**로 추적을 끄고, **Studio** 또는 `--view bust`로 이전 착장 보기로 돌아갑니다. [WALKING_DEMO.md](WALKING_DEMO.md).

**크롭 인디고 청자켓**을 기본 착장에 추가했습니다. **Outfit framing**으로 전체를 보고 **Denim jacket** 체크로 벗길 수 있습니다. 기존 카메라/헤어 동작은 유지합니다. 바로 보기: `artifacts\moca\runtime\HairLabDX12.exe --view bust`. [DENIM_JACKET.md](DENIM_JACKET.md).

동물 꼬리(`Hair_Tail`)도 기본 표시에서 제외했습니다. 원본 메쉬와 본은 보존하며 헤어 시뮬레이션에는 영향이 없습니다.

상의와 안쪽 브라의 메쉬 교차를 정리했습니다. 원본/물리 메쉬는 유지하고 현재 의상에서 브라만 표시하지 않습니다. [CLOTHING_FIX.md](CLOTHING_FIX.md).

최신 기본 커트는 **salon-v6-layered**입니다. 바깥 단발 실루엣을 유지하며 안쪽 졸업 커트·얼굴 주변 레이어·끝 질감을 추가했습니다. 공통 물성과 스프레이 기본값은 그대로입니다. [LAYERED_BOB.md](LAYERED_BOB.md), 검증: `python scripts/validate-salon.py artifacts/salon-layered/final`.

최신 기본은 **salon-v5**입니다. 짧은 앞머리 층을 긴 얼굴 옆머리와 연결하고 전체 공통 물성을 중간 강도로 조절했습니다. **Jolt material settings → Styling spray**로 부드러움/형태 유지를 함께 조절할 수 있습니다. 아래 이전 설정 설명보다 [SALON_STYLE.md](SALON_STYLE.md)의 값이 우선합니다. 최신 검증: `python scripts/validate-salon.py artifacts/salon/delivered`.

전체 헤어의 물성을 통일했습니다. 앞머리·옆머리·뒷머리가 같은 설정을 사용하며 Jolt material settings는 전체에 함께 적용됩니다. [UNIFORM_HAIR.md](UNIFORM_HAIR.md).

현재 기본 스타일은 **editorial-v4 검은색 블런트 단발**입니다. `assets/Reference Hair Image.avif`를 참고해 정수리·옆 볼륨과 밀도를 늘렸습니다. 렌더 32,000가닥 / 가이드 1,600개, 흰 스튜디오 배경과 뷰티 구도입니다. **Run**으로 움직이고 **Full hair framing**으로 전체 머리를 확인하세요. 최신 검증과 실행법은 [EDITORIAL_STYLE.md](EDITORIAL_STYLE.md)를 보세요. 아래의 이전 스타일 설명보다 이 설정이 우선합니다.


앞머리 움직임 진단 후 `GlobalPose` 기본값은 **root 0.01 / tip 0.001**입니다. **Jolt material settings → Styled fringe**에서 Pose retention root/tip/end와 Skin follow tip을 직접 조절할 수 있습니다. Run 중 적용되며 Reset은 기본값/CLI 값으로 돌아갑니다. 원인과 같은 입력 A/B는 `FRINGE_MOTION_DIAGNOSIS.md`에 있습니다.

현재는 **윗머리·옆머리 볼륨을 보강한 단발 editorial-v4**을 정면·일시정지 상태로 표시합니다. **Run**으로 움직임을 시작합니다. 기존 긴 머리는 Hairstyle에서 선택할 수 있습니다. 두피만 보려면 `Render hair`를 끄거나 `--no-hair --view back`으로 실행하세요. 현재 수정은 `BOB_VOLUME.md`, 앞선 흐름 수정은 `BOB_REDESIGN.md`, 두피 보강 기록은 `SCALP_REINFORCEMENT.md`를 보세요.

사용자가 제공한 **모카 캐릭터의 메시·텍스처·315개 본**을 사용합니다. 머리카락은 이 프로젝트에서 별도로 만든 단발 또는 긴 생머리이며, **Jolt Physics Hair**가 가이드 시뮬레이션부터 렌더 헤어 갱신까지 실행합니다.

기본 헤어는 `assets/단발.webp`를 참고한 갈색 가르마 C컬 단발입니다. UI **Hairstyle**에서 `Gloss black / blunt bob` ↔ `Long straight (original)`로 교체합니다. 교체 시 일시정지하고 새 스타일을 초기화하므로 **Run**을 누르면 다시 움직입니다. 기존 긴 머리 파일은 그대로 보존했습니다. CLI: `--hairstyle bob|long`. 단발 생성/검증: `python scripts/create-moca-bob.py`, `python scripts/validate-editorial.py`. 자세한 내용은 `BOB_SCENE.md`.

```powershell
cmake --preset vs2022
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe
```

Debug 빌드: `cmake --build --preset debug --target HairLabDX12 -- /m:1`.
Visual Studio에서는 `build/jolt-only/HairLabDX12.sln`을 열고 **HairLabDX12**를 시작 프로젝트로 지정한 뒤 Release/x64 또는 Debug/x64에서 F5를 누릅니다. 실행 중인 이전 exe는 빌드 전에 종료하세요.

Run/Pause · Step · Reset · Head turn/idle sway · Render/Guides/Both · Head/scalp · Collision hulls · Imported skeleton을 제공합니다. RMB 회전, MMB 이동, 휠 확대, F 프레이밍, R 카메라 리셋. 기본 화면은 얼굴과 상체입니다. `--view bust` 또는 `--view side`도 지원합니다.

```powershell
# 실제 Jolt 움직임을 15스텝마다 PNG/JSON으로 캡처하고 리셋 검사
.\build\jolt-only\bin\Release\HairLabDX12.exe --steps 120 --capture-every 15 --reset-check --capture-dir artifacts/moca-new --log artifacts/moca-new/app.log

# 제한된 앱/리그/뿌리/움직임 검사
python scripts/validate-moca.py

# 로컬 배포 폴더 작성 및 다른 작업 디렉터리에서 실행 검사
python scripts/package-moca.py
```

캐릭터: 두피 보강 포함 39,935 vertices / 61,450 triangles, 315 bones, 4 weights/vertex. Jolt 헤어: 1,600 guides / 32,000 render strands, strand당 32 points. 원본 모델에는 애니메이션 클립이 없어, 원본 본 계층 위에 240-frame/60Hz 머리·목 회전 루프와 팔을 내린 포즈를 만들었습니다. 사용자 요청으로 동물귀 메시를 제외했습니다. 꼬리와 원본 본은 유지합니다.

에셋은 `assets/moca/`, 원본 추출본은 `assets/imported/moca/`에 있습니다. 정상 실행에는 Unity·원본 패키지·VRM·Python이 필요하지 않습니다. 모카 모델의 원래 이용 조건은 그대로 적용되며, 이 로컬 배포 폴더를 공개 배포할 권리를 부여하지 않습니다.

구조·포맷·원본 출처: [JOLT_ASSETS.md](JOLT_ASSETS.md), 작업 및 검증: [MOCA_SCENE.md](MOCA_SCENE.md).

Jolt 초기화/업데이트: `src/JoltScene.*`. 앱/표시: `src/JoltMain.cpp`, `src/CharacterTexture.*`, `shaders/JoltDisplay.hlsl`. Jolt 소스/헤어 셰이더는 `third_party/JoltPhysics/Jolt/Physics/Hair/`, `third_party/JoltPhysics/Jolt/Shaders/`에 있습니다. Jolt v5.6.0 commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a`를 유지합니다.

몸통·팔 충돌체는 실제 캐릭터/청자켓 메시에서 피팅한 hull을 사용합니다. `Collision hulls`로 표시할 수 있습니다. 구성·재생성·검증 범위는 [BODY_COLLIDERS.md](BODY_COLLIDERS.md)를 참조하세요.

성능 계측은 `--walk-test --steps 240 --profile-csv artifacts/profile.csv`로 실행할 수 있습니다. GPU timestamp와 CPU hull 생성 시간을 분리한 결과는 [COLLISION_PROFILE.md](COLLISION_PROFILE.md)를 참고하세요.

월드 기준 공기저항은 헤어 그룹별 `World air resistance`로 조절합니다(기본 1.0/s, 0=off). 사용법과 좌표 처리/검증은 [AIR_RESISTANCE.md](AIR_RESISTANCE.md)를 참조하세요.

### ?? ?? ?????

`Animation clip ? Wave Hip Hop Dance` ?? ? Run. ?? ??: `HairLabDX12.exe --animation dance`. ?? Naruto Run / idle? ?????. [?? ? ???](WAVE_HIP_HOP.md).

### GPU ?? ??

????? 124? GPU ?? + XYZ 32-bin bitmask ??? ?????. ???? hull 3?? ?????. `Collision hulls / GPU body capsules`? ??? ? ????. [????????](GPU_BODY_PRIMITIVES.md).

????????? ??? ??: [SHOULDER_FIT.md](SHOULDER_FIT.md).

### ??? ??

?? **Guide density**? Full / 1/2 / 1/4 / 1/8? ??? ? ????. ?? ?? ?? ?? ??? ???? ?????? ???????????. [??](GUIDE_DENSITY.md).

### ?? ??? ??

**Base hair / black scalp cap**(?? ??)? ?? ?? ??? ??? ?? ????? ??? ?? ?? ??? ? ??? ???. ????? ???? ?? Moca? head-local ????? ?? ?? ??? ?? ?? ??? ????. CLI `--no-base-hair`? ? ? ????. ???/?? ?? ???? ?? ??? ???? ????. ?????????? ??? ?? ??? `artifacts/base-hair/`, 1/8 ??? 30?? on/off?? root/tip/scalp ?? ?? ??? `invariance.json`? ??????.

### Long straight ?? ?? (??? ??)

Side/back: bend compliance log10 **-3**(1e-3), air resistance **2/s**, linear damping **6/s**, angular damping **3/s**, friction **0.44**, retention root **0.0377**, tip **0.0003**. ???? retention/skin follow 1 ? collision off? ?? pose? ?????. ?? ?? ????? ???? ????. Long? ????? Reset?? ????, density ????? ?? ???? ?????. ???? ?? rest?? ? ?? skin pose? ?? ? ? 0.75mm? ?? ??? ????, 30?60??? head-local tip ??? ? 4.9e-8m????. ?? rest?? ?? ??? ???? ????.

### ??? GPU ?? ??

`--profile-csv artifacts/profile.csv`? ???? compute kernel? timestamp? graphics draw ?? timestamp? ?????. `profile.csv`? update ??/CPU, `.dispatch.csv`? ? compute dispatch, `.graphics.csv`? ??? ?????. graphics readback? ?? fence ?? ? ???? ? CPU ??? ???? ????. ?? ????? ??? query heap? ??? ????.

```powershell
cmake --build build/jolt-only --config Release --target HairLabDX12
.\build\jolt-only\bin\Release\HairLabDX12.exe --hairstyle long --animation dance --steps 240 --profile-csv artifacts/profile-long/profile.csv --log artifacts/profile-long/app.log
python scripts/summarize-gpu-stages.py artifacts/gpu-stages/measured
```

2026-11-02 ?? ??: RTX 5070 Ti, Release/1280x800/4x MSAA, ?? ???, ????? ?? 6?, 3? ?? ?? 60..239??(540??/??). Long? ??? 1,600/?? 16,000, MetaHuman? 18,209/34,392???. ?? ?? ??? 0.431/2.332ms, ??? ?? 0.270/0.347ms. ?? ?? median/P95? `artifacts/gpu-stages/measured/summary.csv`, ?????? ???? ?? ???????. ?? ?? ??? ??? ??? ??? ??? ??? ??? ?? ?????. GPU ?? ??? ??? ?? ?? ??? ?? ? ????. ??/??? ???? ? command list? ????? ?? ?? ???? ?? ?? ????. ?? velocity ?? ??? ???retention?grid ??? ?? ???? ???? ?????. ???/teleport? ??? ??? draw? ?? ?? ??? ?? ???? ?????.
