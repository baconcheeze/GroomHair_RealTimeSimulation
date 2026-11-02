# Wave Hip Hop Dance

사용자가 제공한 `assets/Wave Hip Hop Dance.fbx`를 기존 Moca 315본 리그로 리타기팅했습니다. 원본 FBX, 캐릭터/헤어 에셋, Jolt 물성은 변경하지 않았습니다. 기존 Naruto Run이 기본값입니다.

## 사용

앱 재실행 → **Animation clip → Wave Hip Hop Dance → Run**. 선택 시 일시정지하고 초기화합니다. Step, Reset 및 Play dance animation 체크를 사용할 수 있습니다. 춤은 입력 없이 시간에 따라 재생되며, WASD로 캐릭터를 이동시키지 않습니다. 기존 이동은 Naruto Run / idle로 돌아가면 사용할 수 있습니다.

```powershell
cmake --build build/vs2022 --config Release --target HairLabDX12
.\build\vs2022\bin\Release\HairLabDX12.exe --animation dance
```

## 데이터와 변환

- `assets/moca/wave-hip-hop.bin`: RUNA v1, 1010프레임, 60fps, 16.8166667초, 315본 로컬 xyzw 회전 및 골반 이동.
- `assets/moca/wave-hip-hop.json`: 원본/출력 SHA256, 매핑, 변환, 바닥 보정 및 루프 경계 기록. 사용자 제공 파일의 라이선스를 새로 부여하지 않습니다.
- `scripts/retarget-wave-hip-hop.py`: 설치된 Blender에서 실행하는 재현 가능한 변환기. 기존 Naruto 리타깃과 같은 월드 회전 차이 매핑 및 Y축 180도 변환을 사용합니다. 타깃 본 길이를 유지하고 원본 수평 골반 움직임을 첫 프레임 기준으로 보존합니다. 스킨된 발의 최저점으로 상수 높이 보정만 적용합니다. IK/프레임별 바닥 투영은 하지 않습니다.
- 원본 시작/끝 회전 차이 최대 약 0.02674rad는 그대로 보존하며, 새 키프레임을 만들어 완전한 루프로 바꾸지는 않았습니다.
- RUNA의 stride 필드는 형식 호환용이며 춤 재생 속도에는 사용하지 않습니다.
- 재생은 `CharacterLocomotion`, 선택은 `JoltMain`, 애니메이션 본 → 기존 Jolt 헤어/충돌체 연결은 `JoltScene`에서 처리합니다.

## 검증

Debug/Release 네이티브 검사에서 기존 달리기와 댄스 전체 클립/루프/Reset을 확인했습니다. 댄스의 최대 본 길이 오차는 약 0.000000179m입니다. 실제 Release 앱 180스텝에서 finite, 루트 부착 오차 최대 0.000000031m, 초기/Reset 상태 일치, 가이드/렌더 팁 이동을 확인했습니다. 120스텝 캡처를 직접 확인했습니다. 물리적인 마우스/UI 조작은 자동화 검사로 검증하지 않았습니다.

캡처와 원시 결과: `artifacts/wave-hip-hop/`. 기존 물리 파이프라인이 사용되며 춤에 맞춘 별도 헤어 물성 튜닝은 하지 않았습니다.
