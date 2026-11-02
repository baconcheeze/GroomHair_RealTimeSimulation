# 상의 겹침 정리

후속 사용자 요청으로 같은 display-only flags 경로에서 `Hair_Tail` 1,028정점 / 1,707삼각형도 숨겼다. `scripts/prepare-moca-outfit.py`와 importer 모두 이 정책을 적용한다. 원본과 본은 남아 있으며 이후 출처 기록은 `artifacts/appearance/`에 별도로 저장한다. 아래 브라 문제의 진단 기록은 그대로 보존한다.

상의에 나타나던 흰 점/얼룩은 깊이 정밀도 문제가 아니라 **Underwear_Bra가 Costume_Halter_top을 뚫고 나오는 메쉬 교차**였다. `artifacts/clothing/material-probe/step-0000.png`에서 재질별 단색 표시로 확인했다. 상의는 주황, 브라는 초록이며 얼룩 위치가 초록 메쉬와 일치한다. 진단용 셰이더는 실행 후 복원했다.

현재 고정된 홀터넥 의상에서는 안쪽 브라만 숨긴다. 원본 VRM이나 삼각형/정점·본을 삭제하지 않는다. 기존 64바이트 Surface의 flags bit 0을 사용하고 HeadPS가 해당 면을 discard한다. 다른 옷/피부/속옷 하의는 유지한다. 깊이 바이어스, 카메라 클립 범위, 옷 크기를 바꿔 증상을 가리지 않았다.

`scripts/prepare-moca-outfit.py`는 원본 VRM의 실제 노드/프리미티브를 읽어 대상을 찾고 native 정점이 일치하는지 확인한다. 현재 대상은 정점 37,000–38,795의 1,796개, 삼각형 3,084개다. surface.bin의 해당 flags만 변경했다. 원래 UV·텍스처·법선·스킨 가중치는 비트 단위로 보존했다. `scripts/import-moca.py`도 같은 정책을 내보낸다. 기존 두피 보강 재생성 시에는 마지막에 `python scripts/prepare-moca-outfit.py`를 실행한다. 다른 의상으로 바꾸면 이 가시성 정책도 다시 검토해야 한다.

캐릭터 물리 메쉬 SHA256 `628dd9826eef4d167315940fa4f35d7b8cebf68000ef8a827e0512d028b5ed46`, 현재 단발 `4d26c46923d4c19eda2b95cc053b85162c4f1bfd494e33b1189b5d3cec74b9a9`는 그대로다. 설정·헤어 계산과 위치도 바꾸지 않았다. 자세한 출처/해시는 `assets/moca/outfit.json`, `artifacts/clothing/outfit-validation.json`에 있다.

Release 120스텝, Debug 30스텝 상체 캡처와 초기/리셋 PNG·물리 상태 일치, 유한 상태, 루트 부착을 확인했다. 수정 전후 초기 물리 JSON도 동일하다. Debug layer 오류 없음. 정면 상체와 머리 회전 중 캡처를 직접 확인했으며 모든 카메라 각도나 물리적 마우스/리사이즈 조작까지 검증한 것은 아니다.

```powershell
python scripts/prepare-moca-outfit.py
cmake --build --preset release --target HairLabDX12 -- /m:1
cmake --build --preset debug --target HairLabDX12 -- /m:1
.\build\jolt-only\bin\Release\HairLabDX12.exe --view bust
python scripts/package-moca.py
```

수정 전: `artifacts/clothing/before/step-0000.png`. 수정 후: `artifacts/clothing/after/step-0000.png`, `step-0030.png`부터 `step-0120.png`까지. 원본 표면 데이터는 `before/surface.bin`에 보존했다. 실행 패키지는 `artifacts/moca/runtime/HairLabDX12.exe`다.
