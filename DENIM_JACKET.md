# Midnight Indigo 청자켓

현재 검은 홀터넥 위에 입는 프로젝트 제작 크롭 인디고 자켓이다. 열린 앞판, 접힌 칼라, 가슴 플랩 포켓, 황동 단추, 따뜻한 색의 스티치, 밑단/커프스, 얕은 팔꿈치 주름을 포함한다. 얇은 워싱과 필터링된 능직 무늬는 셰이더로 표현한다. 별도 이미지/외부 의상 패키지나 새 라이브러리는 필요 없다.

## 자산과 연결

- 제작: `scripts/create-denim-jacket.py`.
- 자산: `assets/moca/denim-jacket.bin`, `denim-jacket.json`.
- 로더/검증: `src/DenimJacket.h/.cpp`.
- 앱 연결/수명/UI: `src/JoltMain.cpp`.
- GPU 스키닝/재질: `JacketVS`, `JacketPS` in `shaders/JoltDisplay.hlsl`.

20,712정점 / 23,584삼각형, 파일 2,188,528바이트. DNMJ v1 헤더 16바이트 다음에 위치 float3(12), 색 float4(16), 기존과 같은 Surface(64), uint32 인덱스 배열을 저장한다. 원본 모카의 오른손 bind 좌표, 미터, +Y up을 사용한다. Spine/Chest/Upper_arm/Lower_arm을 **원본 본 이름으로 조회**하고 최대 4개 스킨 가중치를 저장한다. 자켓을 원본 캐릭터 메쉬나 Jolt Scalp 배열에 합치지 않는다.

정적 bind 메쉬는 시작할 때 한 번 업로드한다. 기존 current-pose × inverse-bind 팔레트를 GPU 정점 셰이더가 읽어 world 위치/법선을 만든다. 이 위치에 헤어의 neck World 행렬을 다시 적용하지 않는다. 표시 경계에서만 Z를 반사한다. 기존 직접 큐, 프레임 펜스, MSAA/캡처 경로를 사용한다. 열린 자켓의 안쪽도 보이도록 자켓 전용 파이프라인만 양면이다. 원본 캐릭터의 컬링은 유지한다.

이는 **본을 따르는 스키닝 의상**이며 옷감 물리 시뮬레이션은 아니다. 주름도 디자인된 정적 형태다. 새 옷감/헤어 충돌 메쉬를 추가하지 않았으며, 모든 미래 포즈나 극단적인 팔 동작의 핏을 보장하지 않는다. 이번 앱의 팔 내린 포즈와 머리 회전 범위에서 앞·옆·뒤 핏을 확인했다.

## 실행

```powershell
python scripts/create-denim-jacket.py
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust
# 측면 / 후면 / 벗기기
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust-side
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust-back
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust --no-jacket
python scripts/validate-denim.py
python scripts/package-moca.py
.\artifacts\moca\runtime\HairLabDX12.exe --view bust
```

자켓은 기본으로 입혀진다. 기존 시작 카메라는 유지했다. **Outfit framing**으로 전체 착장, **Beauty framing**으로 얼굴을 본다. **Denim jacket** 체크로 표시를 바꾼다. Run/Pause/Step/Reset과 기존 헤어 컨트롤은 유지한다.

## 검증

`artifacts/denim/final/runs.json`: 7개 유한 실행 모두 exit 0. Release 120스텝 / Debug 30스텝, 측면/후면, 자켓 off, 기존 컨트롤 API, UI 포함 캡처를 수행했다. `validation.json`: 유한 상태, 두피 부착, 초기/리셋 PNG·상태 정확 일치 통과. 자켓 on/off 0·30스텝의 물리 상태는 표시 플래그를 제외하고 동일하다. Debug layer 오류 없음. 실제 마우스/리사이즈 및 새 체크박스의 물리적 클릭을 검증한 것으로 주장하지 않는다.

캐릭터/헤어/기존 surface 원본 해시는 유지된다. 물리 캐릭터 SHA256 `628dd9826eef4d167315940fa4f35d7b8cebf68000ef8a827e0512d028b5ed46`, 단발 `4d26c46923d4c19eda2b95cc053b85162c4f1bfd494e33b1189b5d3cec74b9a9`. 자켓 SHA256 `338b276928979b87858f12300b8df8dd8f8ba1f91c289dcbb56d15a984524bab`.

실제 캡처: `artifacts/denim/final/Release/motion/step-0000.png`부터 `step-0120.png`까지. `side/`, `back/`, `ui/`도 직접 확인했다. `artifacts/denim/playback.html`은 완료 상태 9장을 15스텝 간격(0.25초/장)으로 보여준다. 가짜 변형이나 영상 보간은 없다. `preview/`, `fitted*/`, `fit2*/`는 초기 핏 수정의 기록이며 최종은 `final/`이다.

원본 VRM·본·캐릭터/두피·헤어·물성·Jolt 알고리즘은 변경하지 않았다. 자켓 형상/스킨/셰이더/UI/빌드 통합만 추가했다. 기존 모카 원본의 이용 조건은 여전히 적용되며 패키지는 로컬 개인 사용용이다.
