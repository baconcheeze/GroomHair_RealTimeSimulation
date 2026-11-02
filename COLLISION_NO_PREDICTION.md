> ?? ??: hull ?? ??? 0 ??, retention ? collision ?? ??? ??. ?? ?? ? ??: artifacts/collision-no-prediction/original-order.

# 충돌면 예상 이동량 제외 실험

`HairUpdateVelocity.h`의 `plane_movement`를 0으로 설정했다. 충돌 보정 거리 = 헤어 반경 - 캐시된 충돌면까지의 거리다. 충돌면은 여전히 업데이트 초기에 한 번 계산한다. 매 반복 최신 hull 전체를 다시 검사하는 변경은 아니다.

선속도와 각속도로 계산한 `plane_velocity`는 유지한다. 접촉이 발생하면 기존 상대속도/마찰/비탄성 처리를 그대로 수행한다. 기존 '충돌 → retention' 순서 및 포즈 속도 반영, 물성, timestep, hull 포즈 업데이트 순서와 자산은 유지한다.

예상 이동량이 사라지면 두 velocity 셰이더에서 gIterationContext가 사용되지 않아 DXC reflection에서 제거된다. Hair.cpp의 해당 두 SetConstantBuffer 호출도 제거했다. UpdateRoots의 반복 상수 바인딩은 유지한다.

첫 Release 실험은 이 바인딩을 남겨둔 상태로 실행해 0xC0000005로 실패했다. 원래 실패 로그는 `artifacts/collision-no-prediction/Release`에 남겼다. 바인딩 수정 후 실행은 `Release-fixed-binding`이다. Release 120스텝 달리기/Reset/유한 위치 및 뿌리 오차 검사 통과. Debug 결과는 `Debug/app.log`를 참조한다. 통통 튐의 개선 정도를 수치로 입증한 결과는 아니다. hull 예상 이동을 제거했을 때의 관통/충돌 반응도 사용자 동작에서 추가 관찰해야 한다.

원본 두 파일과 해시는 `artifacts/collision-no-prediction/before`에 있다. 이번 실험만 롤백:

```powershell
.\scripts\rollback-collision-prediction.ps1
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
cmake --build build/vs2022 --config Release --target HairLabDX12 -- /m:1 /v:m
```

기본 실행 파일: `build/vs2022/bin/Release/HairLabDX12.exe`.
