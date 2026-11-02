# 접촉용 가닥 설정 연결 수정

`Twin ponytails`(material 2, 5,167가닥) 편집이 별도 `Ponytail / ear contact`(material 6, 10가닥)에 전달되지 않던 UI 범위 문제를 수정했다. 2↔6과 같은 구조인 정수리 0↔5를 연결한다.

UI에서 실제 변경한 pose/skin gradient, collision, LRA, damping, grid averaging, friction 항목만 상대 material에 전달한다. 충돌 반경과 simulation-strand fraction, 형상, 초기 물성은 변경하지 않는다. 양쪽 항목 중 어느 것을 선택해도 연결된 영역에 적용한다. Pause 중에는 Run 또는 Step 후 반영된다.

수정 파일: `src/JoltMain.cpp`, `src/LinkedHairMaterial.h`.

검증: `--control-check`에서 양방향 전달 및 radius/fraction 보존, 무변경 호출 보존을 검사한다. 실행 결과는 `artifacts/material-link-fix/results.json`과 각 구성 app.log에 저장한다. 실제 마우스 조작은 별도 검증하지 않았다.

실행 파일: `build/vs2022/bin/Release/HairLabDX12.exe`.
