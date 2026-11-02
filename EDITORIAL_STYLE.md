# 검은색 에디토리얼 단발

이 문서는 editorial-v4 기록이다. 현재 salon-v5 커트·공통 물성·MSAA는 [SALON_STYLE.md](SALON_STYLE.md)를 따른다.

사용자 제공 `assets/Reference Hair Image.avif`와 뷰티 화보 지시를 현재 모카 캐릭터의 스타일링에 적용했다. 실사 인물로 교체하지 않았다. 참고 이미지 SHA256: `64c676b9c6d219cf06cf0ce91c5fe6083eb83513a2ea2f1ff944487b01796e76`.

## 구현

- `scripts/create-moca-bob.py`: editorial-v4. 렌더 32,000가닥, 가닥당 32점. Jolt의 5% 가이드 선택으로 실제 1,600가이드. 정수리와 옆 볼륨, 비대칭 가르마, 소량의 가는 앞머리, 직선적인 블런트 끝선.
- `assets/moca/bob.hair`, `bob.materials`, `bob.json`: 새 단발. 기존 긴 머리와 보강한 두피, 315개 본은 보존했다.
- `shaders/JoltDisplay.hlsl`: 검은 머리 방향성 광택, 흰 배경, 키/필/림 조명 근사, 얼굴 하이라이트, 텍스처 선형화 및 출력 톤 매핑. 실제 카메라나 사진 조명 시뮬레이션은 아니다.
- `src/JoltMain.cpp`, `OrbitCamera.h`: 눈 중심 28도 FOV 뷰티 구도와 전체 헤어 보기. 뷰 상수 160바이트를 C++/HLSL에서 일치시켰다.
- Jolt Hair 계산과 렌더 헤어 갱신 경로는 유지했다. 기존 앞머리 설정 root 0.01 / tip 0.001, 스키닝 tip 0.25와 물성은 변경하지 않았다. 정상 렌더링에 전체 헤어 CPU 왕복은 없다.

## 실행

```powershell
cmake --preset vs2022
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe
# 전체 실루엣 / 측면
.\build\jolt-only\bin\Release\HairLabDX12.exe --view hair
.\build\jolt-only\bin\Release\HairLabDX12.exe --view side
python scripts/validate-editorial.py
python scripts/package-moca.py
```

기본은 일시정지다. **Run**으로 머리 회전과 헤어를 움직인다. **Beauty framing / Full hair framing**으로 구도를 바꾼다. Hairstyle에서 기존 긴 머리도 선택 가능하다.

## 검증과 캡처

`artifacts/editorial/final/runs.json`: Debug/Release, 측면/후면, 기존 긴 머리, 컨트롤 API 등 8회 모두 exit 0. Release 120스텝과 Debug 60스텝에서 실제 가이드/렌더 끝점 움직임을 확인했다. 최대 RMS 변화는 각각 Release 33.42/33.59mm, Debug 31.74/31.89mm이며 중력과 머리 애니메이션을 모두 포함한 변화다. 머리 움직임만의 분리 측정은 아니다.

32,000개 루트의 두피 표면 오차 최대 0.060µm, 실행 중 루트 부착 오차 최대 0.031µm. 유한 상태와 초기/리셋 데이터·PNG 일치 통과. 상세 수치와 해시는 `artifacts/editorial/final/validation.json`에 있다. 실제 마우스/리사이즈 조작은 이번 검증에 포함하지 않았다.

이미지: `artifacts/editorial/final/Release/beauty/step-0000.png`, `motion/step-0000.png`부터 `step-0120.png`까지 15스텝 간격. side/back 폴더도 실제 실행 캡처다. 뷰티, 전체 헤어, 측면 및 후면 이미지를 직접 확인한다. PNG 시퀀스는 실제 Jolt 완료 상태이며 합성 움직임이 아니다.

캐릭터의 기존 이용 조건은 그대로 적용된다. 원본 VRM 및 Unity 프로젝트는 수정하지 않았다.
