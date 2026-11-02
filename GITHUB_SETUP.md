# GitHub 연결 마무리

대상: https://github.com/baconcheeze/GroomHair_RealTimeSimulation.git
브랜치: main. 작성자: baconcheeze / caw202000@gmail.com.

로컬 Git 저장소는 초기화했다. 이 작업 세션에서는 .git/config 쓰기 권한이 없어
원격 연결·커밋을 완료하지 못했고, 터미널 GitHub 네트워크 연결도 실패했다.
아래 명령은 일반 Windows PowerShell에서 프로젝트 폴더를 연 뒤 실행한다.

```powershell
cd C:\Users\caw20\MyProject\HairLabDX12
git config user.name "baconcheeze"
git config user.email "caw202000@gmail.com"
git remote add origin https://github.com/baconcheeze/GroomHair_RealTimeSimulation.git
git add -A
git diff --cached --stat
git commit -m "Initial HairLabDX12 Jolt hair application"
git push -u origin main
```

GitHub 인증 화면이 열리면 본인 계정으로 로그인한다. 비밀번호나 토큰을 채팅에 보내지 않는다.
remote origin already exists 메시지가 나오면 git remote -v로 주소부터 확인한다.
문제가 나면 에러 메시지를 그대로 확인하고 force push는 사용하지 않는다.

이 저장소는 **소스 코드 중심 백업**이다. build/artifacts/checkpoints, 구매 캐릭터,
변환된 runtime assets, 원본 FBX/VRM/MetaHuman, authoring 파일과 upstream sample
assets는 제외했다. 파일은 로컬에서 삭제하지 않았다. LOCAL_ASSETS.json은 경로·크기·
SHA-256 목록이며 파일 자체를 백업하는 것이 아니다. 이 디렉터리들은 별도로 보관한다.
새로 clone한 PC에서 실행하려면 허용된 사용자에게 해당 자산을 별도 복원해야 한다.

Jolt/ImGui 등 라이브러리 소스·라이선스는 포함한다. 대용량 자산은 이번 업로드에
포함하지 않으므로 Git LFS 필터/업로드는 설정하지 않았다.
