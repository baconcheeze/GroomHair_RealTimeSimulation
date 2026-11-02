# 사이드뱅 보강 v15

구렛나루 숨김(`HideSideburns=true`)은 유지하고, 얼굴 양옆으로 길게 내려오는 사이드뱅(region 3)만 보강했다. 기존 761가닥에 양쪽 240가닥씩 더해 1,241가닥이다.

- 실제 빈틈을 채우도록 서로 조금 떨어진 기존 곡선을 짝지어 사이에 새 가닥을 만든다. 단순히 겹쳐 복제하던 첫 시안은 변화가 약해 수정했다.
- 추가 층의 길이는 부모 곡선의 80–98%로 엇갈리게 해 뿌리/중간을 채우고 긴 끝에 한꺼번에 몰리지 않게 했다. 중간의 아주 얇은 바깥층(최대 0.7mm)은 깊이 검사로 새 가닥이 모두 가려지는 것을 줄인다. 끝/모근은 이 볼륨 보정에서 제외한다.
- 사이드뱅 표시 알파는 뿌리/중간 .464에서 끝 .32로 부드럽게 감소한다. 색/물성/질량/Jolt 솔버는 변경하지 않는다. 구렛나루/다른 머리의 표시 규칙은 유지한다.
- 기존 33,912가닥의 위치 바이트는 전부 그대로다. 현재 총 렌더 34,392가닥이며, 추가한 480가닥도 Jolt region 3으로 실제 시뮬레이션한다. 보관한 승인 v13 체크포인트도 수정하지 않는다.

제작: `scripts/strengthen-sidebangs.py`. 고정 입력: `authoring/sidebang-v15/metahuman.*`. 결과: 기존 `assets/moca/metahuman.*`. 반복 생성해도 가닥이 누적되지 않는다. 일반 실행은 Python/Blender/authoring 폴더를 요구하지 않는다.

```powershell
$env:BLENDER_USER_RESOURCES = Join-Path $PWD 'artifacts/blender-user'
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python-exit-code 1 --python scripts/strengthen-sidebangs.py
cmake --build --preset release --target HairLabDX12 -- /m:1
.\build\vs2022\bin\Release\HairLabDX12.exe --view bust-side --run
```

두 실행 경로를 맞추려면 preset 빌드의 실행 파일은 `build/jolt-only/bin/Release/HairLabDX12.exe`, 기존 Visual Studio 솔루션 빌드는 `cmake --build build/vs2022 --config Release --target HairLabDX12 -- /m:1`을 사용한다.

정면/좌우 캡처를 직접 확인했다. 현재 상태 검증은 `python -X utf8 scripts/check-side-redesign.py --label sidebang-v15-final`로 수행한다. 현재 작업은 C++ 변경이 아니며 기존 Debug/Release 실행 파일이 새 에셋과 런타임 raster 셰이더를 읽는다. 실제 정지 두피 검사 470,324선분에서 교차 0. 원시 자료는 `artifacts/sidebang-v15`, `artifacts/hair-side-redesign/sidebang-v15-final`이다. 모든 임의 포즈의 무관통 보증이나 실제 마우스/키보드/리사이즈 조작 검증은 아니다. 기존 걷기 중 두피 교차를 이 작업에서 해결했다고 주장하지 않는다.
