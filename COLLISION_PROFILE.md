> ?? ??: ? ??? world air resistance ?? ? ????. ?? ??? 1.0/s? ? ? ??? ??????? ??? ???. ???? source hashes? ??? ????.

# 충돌 비용 프로파일링

**달리는 동안 헤어 업데이트의 주된 병목은 GPU 충돌 탐색보다 CPU convex hull 생성·교체다.** GPU 내부에서는 충돌 탐색이 가장 큰 항목이지만, GPU만 최적화하면 현재 CPU 병목은 남는다. 이번 작업은 계측만 추가했고 물리/충돌 알고리즘, hull/헤어 자산, 재생성 기준과 업데이트 순서는 변경하지 않았다.

## 조건과 방법

- AMD Ryzen 7 9800X3D, NVIDIA GeForce RTX 5070 Ti 하드웨어.
- Release, debug layer off. 실제 18,209 guides / 34,392 render strands, 전체 hull 61개.
- 저장된 float h=0.01666666753590107초, 360 iterations/s → 실제 내부 반복 6회/update.
- 기존 `--walk-test` 입력/애니메이션 그대로, 240스텝씩 5번. GPU timestamp on / CPU clock only / on / CPU only / on 순서.
- 초기 1..59스텝 제외. 같은 전방 달리기 60..119스텝, GPU 3회×60=180샘플, CPU-only 2회×60=120샘플. 다른 구간과 원시값도 보존했다.
- GPU: 실제 Jolt compute queue의 D3D12 timestamp queries와 해당 queue의 1,000,000,000Hz frequency. 각 dispatch 앞뒤와 전체 command-list 앞뒤를 기록한다. 기존 ExecuteAndWait fence 완료 후에만 읽는다. 반복 사이 CPU 대기를 추가하지 않았다.
- 개별 kernel 시간에는 바인딩/업로드/배리어가 포함되지 않는다. GPU total은 그 작업을 포함한 실제 command-list 구간이며, timestamp resolve/readback와 graphics draw는 제외한다. 개별 median을 더해 total이라고 하지 않았다.
- CPU: point preparation, convex Create+SetShape+cache 교체, broad-phase query, Jolt CPU recording, submit/wait, advanceStep 전체를 각각 steady_clock으로 측정한다. CSV 출력은 이 구간 밖이다.

## 전방 달리기 결과

단위 ms. P95는 실제 샘플의 nearest-rank 95 percentile.

| 항목 | Median | P95 |
|---|---:|---:|
| CPU hull 생성·교체 | **10.724** | 11.971 |
| CPU skin/점군 준비·cache 검사 | 0.132 | 0.189 |
| CPU hull 준비 전체 | 10.859 | 12.115 |
| CPU broad phase | **0.0038** | 0.0063 |
| CPU Jolt recording/업로드 준비 | 0.156 | 0.233 |
| CPU submit + GPU 완료 대기 | 4.524 | 6.443 |
| CPU simulation update 전체(위 GPU 대기 포함) | **15.806** | 17.809 |
| GPU 충돌면 탐색 | **2.469** | 3.101 |
| GPU hair command-list 전체 | 4.214 | 6.189 |

GPU 시간과 CPU 전체 시간을 더하면 안 된다. CPU 전체에 이미 GPU 완료 대기가 포함된다.

샘플별 비율의 median: convex 생성·교체는 CPU update의 **68.4%**, 충돌 탐색은 GPU hair 시간의 **58.8%**. 매스텝 hull 재생성 28개. broad phase 후 후보는 median **20개**, P95 22개; 면 합계 median 약 3,175개. 전체 61개를 항상 GPU로 넘기는 것은 아니다.

GPU의 다른 큰 항목(각 스텝에서 동일 kernel을 합한 뒤 median): GridAccumulate 0.710ms, UpdateVelocityIntegrate 0.304ms, UpdateStrands 0.180ms, render reconstruction 0.109ms.

## 반복과 변동성

| 실행 | CPU update median | GPU total median | GPU search median | CPU hull refit median |
|---|---:|---:|---:|---:|
| GPU 1 | 15.794 | 4.032 | 2.447 | 10.785 |
| GPU 3 | 15.492 | 4.191 | 2.471 | 10.442 |
| GPU 5 | 15.903 | 4.332 | 2.468 | 11.022 |

CPU-clock-only 두 실행의 update median은 16.161 / 15.803ms, 합친 median 15.862ms였다. GPU timestamps가 켜진 결과의 분류와 큰 차이는 없지만, 이것은 완전한 무계측 대비가 아니며 오버헤드가 0이라는 증명도 아니다. CPU clock과 CSV 기록은 control에도 남는다.

정지 구간 10..29는 양상이 다르다. CPU refit median 2.615ms, GPU search 3.936ms, GPU total 5.813ms, CPU update 9.150ms. 따라서 **정지 상태에서는 GPU 탐색이 더 중요한 비중**을 가진다. 달릴 때는 변형된 body hull 28개를 다시 만드는 CPU 비용이 커진다.

외부 프로그램/GPU 점유와 클럭을 완전히 통제하지 않았다. 사용자가 열어둔 별도 HairLabDX12 프로세스(PID 458192)는 종료하지 않았다. 그래픽 draw/Present/VSync, GPU occupancy/cache/bandwidth counters는 측정하지 않았다. 결론은 현재 앱의 simulation update 경로에 한정한다. 실제 FPS의 모든 비용을 분석한 결과는 아니다.

## 정확성·정상 동작 확인

- GPU timestamps on/off matched 61스텝 비교: guide root/tip, render tip 차이 모두 **0m**, material JSON 동일, 유한 상태.
- Debug에서 timestamp 계측을 켠 40스텝 실행 및 debug-layer error 없음 확인.
- normal startup에는 GPU timestamp heap을 만들지 않는다. CLI를 명시했을 때만 활성화한다.
- HLSL/물성/hull/groom/animation 변경 없음. source/input/exe hashes와 raw CSV를 보존했다.

## 판단

GPU 축별 bitmask는 2.47ms 탐색을 줄이는 합리적인 GPU 최적화 후보다. 하지만 CPU broad phase 자체는 약 4마이크로초라, 그 query를 GPU로 옮기는 것만으로 현재 10.7ms의 hull 생성 비용이 없어지지 않는다. **우선순위는 같은 피팅 품질을 유지하면서 CPU convex 재생성 경로의 비용을 줄이는 것**이다. GPU bitmask 구현이나 추가 최적화는 이번 작업에서 하지 않았다.

## 재현과 파일

```powershell
cmake --build --preset release --target HairLabDX12 -- /m:1 /v:m
python scripts/profile-collision.py
```

단일 실행:
```powershell
.\build\jolt-only\bin\Release\HairLabDX12.exe --walk-test --steps 240 --profile-csv artifacts/collision-profile/manual/profile.csv --log artifacts/collision-profile/manual/app.log
```

`--profile-cpu-only`를 추가하면 GPU queries 없이 CPU control을 기록한다.

- 원자료/실행 명령: `artifacts/collision-profile/measured/*/profile.csv`, `profile.csv.dispatch.csv`, `runs.json`.
- 수치 요약: `summary.json` (모든 통계와 kernel별 결과).
- 환경/단위/source hashes: `provenance.json`.
- 상태 비교: `artifacts/collision-profile/validation/state-comparison.json`.
- 수정: 선택적 ComputeQueueDX12 timestamps, Hair CPU broad-phase/candidate 카운터, JoltScene CPU scope, JoltMain CLI/CSV와 실행 스크립트. solver/shader 변경은 없다.
