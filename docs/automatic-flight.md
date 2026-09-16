# 자동 이륙·착륙

자동 이륙과 착륙은 `FlightOperations.h`에서 비행 조종면·스로틀·앞바퀴·브레이크·랜딩기어 명령을 계산한다. 수동 비행과 같은 비행역학·작동기·지면 접촉 계산을 통과한다. 자동 운항 명령은 현재 위치를 바꾸지 않는다. **착륙 시험 위치**만 별도의 초기 조건 설정 명령이다. 전체 경로·상태·오류 계약은 [API 사용 매뉴얼](api-reference.md)을 따른다.

## Web UI 사용

기본 가상 제주 활주로와 정상 Thor 개발 프로필에서 무풍·1배속으로 시작한다. 돌풍·고장 설정이 남아 있다면 먼저 해제한다.

1. **지상 시작**을 누른 뒤 **자동 이륙**을 누른다. 첫 설정 활주로(인덱스 0)를 따라 활주하고, 활주로 평면 위 50m·TAS 19m/s를 목표로 상승한다. 버튼은 준비 명령 성공 뒤 재생을 요청한다. 이륙 완료 후에는 공중에서 고도 유지·대기 비행을 계속한다.
2. 이륙 후 **자동 착륙**을 누르면 같은 활주로에 TAS 18m/s로 접근한다. 바로 최종 접근이 가능한 상태가 아니면 접근 경로에 합류한다. 기체를 순간 이동시키지 않는다. 착지 후 감속하여 주차 브레이크를 적용하고 일시정지한다.
3. 착륙만 시험하려면 **착륙 시험 위치**를 누른다. 기체를 활주로 시작점 600m 뒤, 활주로 평면 위 60m, TAS 18m/s로 초기화하고 정지한다. 이후 **자동 착륙**을 누른다. 이 버튼은 현재 위치·자세·속도 초기 조건을 교체한다.
4. **자동 이착륙 해제**는 운항 제어를 해제하며 재생/정지 상태를 유지한다. 수동 조종면·스로틀 또는 지상 조종 입력도 자동 운항을 해제한다. 조종 입력 패널에서 명령값과 실제 작동기 출력을 비교한다.

활주로 파일과 수동 조작, CG 법선 AGL·접촉 하중은 [ground-operations.md](ground-operations.md)를 따른다. 기체 프로필·돌풍·고장은 [aircraft-profile.md](aircraft-profile.md)에 설명했다.

## API

모든 경로는 `/api/v1` 기준이다. 지정된 필드를 모두 보내며 추가 필드는 거부한다. 활주로 인덱스는 `runways.json` 배열의 0부터 시작하는 정수다.

| 경로 | JSON 필드 / Python 메서드 |
| --- | --- |
| `POST /automation/takeoff` | `runway_index` 0~15, `climb_height_m` 20~150m, `speed_mps` TAS 16~24m/s / `start_automatic_takeoff(...)` |
| `POST /automation/land` | `runway_index` 0~15, `approach_speed_mps` TAS 16~22m/s / `start_automatic_landing(...)` |
| `POST /automation/prepare-landing` | `runway_index` 0~15 / `prepare_landing_approach(...)`; 위치 초기화·정지 |
| `POST /automation/cancel` | `{}` / `cancel_flight_operation()` |

이륙·착륙 API는 제어기를 준비시키고 기존 재생/정지 상태를 유지한다. Python 프레임워크는 정지 상태에서 명시적 step으로 진행할 수 있다. 아래 코드는 1초만 진행하며, 완료까지의 실행 예제는 하네스를 참고한다.

자동 이륙은 기어 완전 전개 잠금에서 준비하고, 이륙 후 **지면 법선 높이 10m 이상·상승 중**이면 수납한다. 자동 착륙은 준비 명령 수락 즉시 전개 목표를 지정하며, 실제 기어는 시뮬레이션 시간 3초에 걸쳐 움직인다. 최종 접근에서 활주로 평면 위 15m 미만인데 완전 전개 잠금이 없으면 복행을 요청한다. 상태의 `gear.command_down`, `extension`, `down_locked`, `up_locked`, `moving`을 확인한다. 정지 중에는 이동도 정지한다.

활성 자동 이착륙 중 수동 `set_landing_gear(...)`는 HTTP 409다. 수동으로 바꾸려면 자동 운항을 먼저 해제한다. 해제 자체는 기어 목표·위치를 초기화하지 않는다. 위치 초기화와 지상 시작은 기어를 완전 전개한다. 이 기구·시간·항력은 개발 가정이며 실제 Thor의 수납식 착륙장치 자료가 아니다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.pause()
sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
sim.set_gust(east_mps=0, north_mps=0, up_mps=0)
sim.set_failures(engine_power_fraction=1, elevator_jammed=False,
                 aileron_jammed=False, rudder_jammed=False)
sim.reset_on_ground()
sim.start_automatic_takeoff(runway_index=0, climb_height_m=50, speed_mps=19)
state = sim.step(1)
print(state["automation"], state["actuators"])
# 벽시계 재생으로 진행하려면 sim.set_running(True)
```

형식·범위 오류는 HTTP 400, 해당 활주로나 기체 상태에서 준비할 수 없으면 409다. 같은 명령을 자동 재시도하지 않는다. 타임아웃 후에는 상태를 조회해 적용 여부를 확인한다.

## 운항 상태

응답의 `automation` 객체에 다음 필드를 둔다. 이 값들은 센서 측정이 아닌 계산 상태다.

| 필드 | 의미 |
| --- | --- |
| `kind` | `none`, `takeoff`, `landing` |
| `phase` | 아래 운항 단계 |
| `runway_index`, `runway_name` | 선택한 활주로. 선택되지 않았으면 −1과 빈 이름 |
| `completed` | 이륙 `hold` 또는 착륙 `complete`이면 true. 이륙 완료는 시뮬레이션 정지를 뜻하지 않는다. |
| `message` | 진입·거부·복행 등 현재 동작 설명 |
| `cross_track_m` | 활주로 중심선 기준 오른쪽 양수 횡방향 오차(m) |
| `along_track_m` | 활주로 시작점을 0으로 하여 이륙 진행 방향 양수 종방향 거리(m) |
| `target_height_m` | 운항에서 설정한 활주로 평면 기준 목표 높이(m). WGS84 타원체고나 매 순간의 접근 경로 높이가 아니다. |

| 단계 | 의미 |
| --- | --- |
| `idle` | 자동 운항 없음 |
| `takeoff_roll` → `rotate` → `climb` → `hold` | 이륙 활주 → 기수 들기 → 상승 → 이륙 완료 후 대기 비행 |
| `join` → `final` → `flare` → `rollout` → `complete` | 접근 경로 합류 → 최종 접근 → 플레어 → 착륙 감속 → 주차·정지 |
| `rejected_takeoff` → `aborted` | 이륙 중단·제동 → 정지. 활주로 밖 접지의 정지 처리에도 사용 |
| `go_around` | 불안정 접근으로 복행. 현재 제어기는 최대 두 차례 복행 뒤 대기 비행으로 전환할 수 있다. 착륙 성공으로 표시하지 않는다. |

단계 전환은 조종 명령과 실제 기체 상태로 결정된다. `ground.crashed` 또는 `fault`도 함께 확인한다. 충돌·운항 종료 시 요청한 step 전체 시간이 진행되지 않을 수 있으므로 반환된 `simulation_time_s`를 사용한다.

## 준비 조건과 개발 한계

자동 운항은 현재 준비된 가상 활주로와 그 주변의 국소 비행 범위를 사용한다. 활주로 모델 자체는 100~3000m를 받지만 **자동 운항은 300m 이상**을 요구한다. 일정 횡풍 성분은 5m/s 이하, 배풍은 3m/s 이하, 엔진 출력 비율은 .9 이상이고 조종면 고착이 없어야 준비할 수 있다. 이 수치는 제어기의 개발 범위이며 검증된 기체 운용 한계가 아니다.

이륙은 바퀴 접촉, 방위 오차 10° 이내, 중심선 오차 활주로 폭의 1/4 이내, 잔여 길이 300m 이상, 지상속도 10m/s 이하에서 준비한다. 가속·정렬·잔여 활주로 조건이 나빠지거나 이륙 활주 중 추진 출력이 크게 줄면 제동을 요청한다. 이미 이륙한 모든 고장 상황을 처리하는 비상 제어기는 아니다.

착륙은 공중에서 활주로 기준 높이 10~1000m, 거리 5km 이내, 현재 TAS 12~35m/s 등의 준비 조건을 요구한다. 임의 비행 위치에서 가장 가까운 공항을 찾아주거나 장애물 없는 접근 경로를 계획하지 않는다. 낮은 최종 접근의 속도·정렬·뱅크·높이가 기준을 벗어나면 복행을 요청하지만 모든 고장·바람·진입 자세에서 회복을 보장하지 않는다.

접근 경로 치수, 상승·접근·플레어 제어, 제동 계수, 작동기 응답은 개발 모델에 맞춘 가정이다. 활주로 경사·실제 포장·타이어·완충장치 성능을 실측 보정한 모델이 아니며 센서·외부 비행제어컴퓨터·SIL/HIL 시간 동기화는 아직 없다. 위치를 알고 있는 내부 제어기를 이용한 개발 시험이다.

## 실행 하네스

기어 전개·수납 추가 후 Release CTest 7/7과 Unreal Development 빌드, [기어 API 21/21 검사](../artifacts/gear-api-check.json)가 통과했다. [20배속 자동 왕복](../artifacts/gear-playback.json)은 이륙 후 34.83초의 hold에서 수납 잠금, 착륙 준비 후 전개 잠금을 확인하고 266.39초에 주차·정지했다. 실제 소요는 약 13.359초였다. [착륙 완료 화면](../artifacts/gear-automatic-landed.png). 이후 지상 주차·전개·1배속·정지로 복구했다. 이는 해당 개발 시나리오의 결과이며 실기체 성능이 아니다.

API와 Unreal이 실행되고 기본 Thor 프로필·첫 가상 활주로가 준비된 상태에서 실행한다. 정상 종료와 예외 종료 모두 원래 기체 파일 선택을 복원하고 무풍·돌풍/고장 해제·지상 주차·정지·1배속으로 복구를 시도한다. 검사 중 `aircraft/active.json`과 `aircraft/runtime-check.json`을 수정하지 않는다.

```powershell
.\.venv\Scripts\python.exe scripts\check-automatic-api.py
```

`artifacts/automatic-api-check.json`에 작동기 해석해 비교, 돌풍·재현, 프로필 오류의 상태 보존, 자동 이륙 후 착륙, 명시적 접근 준비 후 착륙, 이륙 중단 결과를 남긴다. 운항 단계가 바뀔 때만 상태를 기록하고, `rollout` 관측 시 위치를 설정 활주로 길이·폭과 비교한다. 실제 접지 순간을 센서로 계측하는 검사는 아니다. 실패 또는 복구 오류는 0이 아닌 종료 코드다.

2026-09-13 기어 전개·수납 추가 전 Unreal Development 빌드와 Release CTest 7/7이 통과했고, 실제 하네스 **27/27 항목·366회 API 요청**이 성공했다. 기체 선택·임시 프로필의 원래 파일 바이트 복원과 지상 주차·정지·1배속 복구를 확인했다. [실행 기록](../artifacts/automatic-api-check.json). 아래 시간은 당시 고정 전개 형상의 개발 결과이며 기어 변경 이후 실측 성능으로 해석하지 않는다.

| 기본 무풍 시험 | 시뮬레이션 시간 결과 |
| --- | --- |
| 자동 이륙 → `hold` | 출발 후 33초 |
| 같은 기체의 귀환 착륙 → `complete` | 이륙 시작부터 합계 261.25초 |
| 명시적 착륙 시험 위치 → `complete` | 준비 후 42.32초 |
| 출발 1초 뒤 엔진컷 → `aborted` | 출발 후 2.03초에 정지 |

두 착륙의 첫 `rollout` 관측은 활주로 시작점으로부터 약 113.36m·112.23m였고 폭 안에 있었다. 기록된 법선 접지 속도는 각각 약 .569m/s다. 같은 개발 모델 내부의 실행 결과이며 실기체 착륙거리·충격 하중·인증 성능을 뜻하지 않는다. 이후 시험 데이터 반영은 [개발 계획](development-plan.md)을 따르며, 센서·외부 자동조종장치 연동은 사용자 결정에 따라 범위에서 제외했다.

추가로 UI 버튼과 같은 `set_running(True)` API 경로에서 20배속 왕복 재생을 확인했다. 실제 약 13.313초 동안 시뮬레이션 265.47초를 진행해 착륙·주차·자동 일시정지를 완료했다. 브라우저 클릭 검사는 수행하지 않았다. [재생 기록](../artifacts/automatic-playback.json), [Unreal 착륙 완료 화면](../artifacts/automatic-landed.png).
