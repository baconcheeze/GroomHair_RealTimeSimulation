# 헤어 그룹별 Bend compliance UI

Jolt material settings → Hair region 선택 → Bend compliance log10.

- -7 = 1e-7 (기존 기본값), -8 = 1e-8 (더 단단함), -6 = 1e-6 (더 부드러움).
- 기본 compliance와 길이 배율, retention 및 모든 초기 물성은 유지한다.
- 정수리 0↔5, 포니테일 2↔6은 접촉용 가닥까지 연결한다. 그 외 그룹은 독립적이다.
- Run/Step의 다음 Jolt 업데이트에서 GPU material 버퍼에 반영된다.
- retention이 root/tip 모두 1이면 형태 복원이 굽힘 차이를 가린다. Hair physics를 끄면 물성 차이에 의한 움직임은 나타나지 않는다.
- Reset은 장면 기본값으로 복구한다. 앱 종료 후 설정 저장은 추가하지 않았다.

검증 경로: artifacts/bend-compliance-ui/new-release. 상위 디렉터리의 최초 controller 실행은 VS Release 링크 실패 뒤 기존 실행 파일로 실행되어 새 기능 검증에서 제외한다.
