> ?? ??: ??? ???? ?? ?? ??? ????. retention ? collision ?? ??? ????, hull ?? ??? ??? ????. ??? ?? ?? ????.

# Retention-after-collision 실험

기존: ApplyGlobalPose → ApplyCollisionAndUpdateVelocity → LimitVelocity.

실험: ApplyCollisionAndUpdateVelocity → ApplyGlobalPose → 포즈 보정 속도 반영 → LimitVelocity.

마지막 반복의 HairUpdateVelocity와 중간 반복의 HairUpdateVelocityIntegrate 모두 변경했다. 중간 반복에서는 이후 기존 Grid/Integrate를 계속 수행한다. 물성, assets, iteration 수, timestep, hull 업데이트 순서는 변경하지 않았다.

충돌의 마찰/비탄성 속도 처리를 유지한 뒤 retention의 위치 변화/dt를 더하고, 회전 속도는 최종 orientation에서 계산한다. 이것은 순서 변경 후 포즈 이동이 속도에 누락되지 않도록 하는 동반 변경이다. 단순히 호출 두 줄만 교환한 버전과는 구분한다.

Retention이 마지막 우선순위이므로 hull 안으로 되돌아갈 수 있다. 이번 실행 성공은 통통 튐의 원인이 확정되거나 모든 관통이 해결됐다는 뜻이 아니다. hull 포즈 업데이트 시점 문제는 이 실험에서 변경/해결하지 않았다.

원본: artifacts/retention-after-collision/before (hashes.json 포함).

롤백 및 재배포:
```powershell
.\scripts\rollback-retention-order.ps1
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
cmake --build build/vs2022 --config Release --target HairLabDX12 -- /m:1 /v:m
```

실행 검사/캡처: artifacts/retention-after-collision/Release 및 Debug.
