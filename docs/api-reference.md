# UAVSim API 사용 매뉴얼

## 항법·동적 추진·바퀴 상태 추가 — 2026-11-18

제품 경로·Python 메서드는 유지하고 `GET /state`와 각 명령의 전체 상태에 아래 필드를 추가했다. 기본 수납식 연구 프로필에 적용하며 기존 Thor/고정기어 파일은 동적 추진을 켜지 않는다. [수식·가정·실제 API19검사·통합 왕복](navigation-physics.md)을 따른다.

| 필드 | 단위·계약 |
| --- | --- |
| `airframe.dynamic_propeller` | bool. true일 때 새 RPM/축동력/토크를 계산한다. false이면 해당 수치는0이며 미적용을 뜻한다 |
| `actuators.propeller_rpm` | 회/min, 로터 상태에서 계산한 연구 RPM. 실측값 아님 |
| `actuators.shaft_drive_power_w`, `shaft_load_power_w` | W, 엔진의 축 입력·프로펠러 부하. 차이는 로터 회전 에너지 변화에 연결 |
| `actuators.shaft_drive_torque_nm`, `propeller_load_torque_nm` | N·m, 양의 축 방향 토크 크기. body 회전 방향은 기체 파일의 spin_direction 사용 |
| `ground.wheel_surfaces_sampled` | bool, true면 바퀴별 지면을 별도 조회했다. 공중에서는false·CG 평면 근사 |
| `ground.wheel_normal_force_n` | number[3], 전륜/좌주륜/우주륜의 접촉 충격량/계산 간격 N. 마지막 계산값이며 최대 하중 아님 |
| `ground.wheel_clearance_m` | number[3], 같은 순서의 바퀴 접촉점과 지면의 부호 있는 법선 간격 m |
| `ground.wheel_lateral_slip_mps` | number[3], 같은 순서의 바퀴 접촉점 횡미끄럼 m/s. 미접촉은0 |
| `navigation.reacquiring` | bool, 놓친 중간 항로점의 고정 진입선 재접근 중 |

`PUT /route`/`set_route`의 **마지막 점은 선회 중심**이다. 중심 관통 대신 원주·접선/높이/속도 조건에서 loiter에 진입한다. 중간 점은 순서를 건너뛰지 않는다. 자동 이착륙은 별도 직선 참조를 사용한다. 수동 입력의 항법 취소와 시뮬레이션 시간/배속 계약은 유지한다.

바퀴 간격은 완전 전개 상태의 접촉 기준점을 투영한 값이다. 수납 중 기구/타이어의 실제 간격을 뜻하지 않으며 `gear.down_locked`와 함께 읽는다. 반력은 계산 전 초기화 직후0일 수 있고 시간 진행 후 실제 접촉 계산값으로 갱신된다. 기존 `ground.height_agl_m`은 계속 CG 법선 거리다.

`PUT /failures`의 engine_power_fraction=0은 동적 모델에서 축 구동0이며, 로터 감속 중 잔류 추력이 생긴다. 기존 모델은 즉시 추력0이다. 엔진컷 기록기 판정도 이를 구분한다. 지면/바퀴 지형이 누락되면 명시적 fault·정지이며 높이0 지면을 대입하지 않는다. 새 구성의 수치 범위/타입 오류는 기존 재읽기 거절·이전 모델 보존 계약을 따른다.

```python
from uavsim_client import UAVSimClient
sim=UAVSimClient()
state=sim.get_state()
print(state['airframe']['dynamic_propeller'])
print(state['actuators']['propeller_rpm'], state['actuators']['shaft_drive_power_w'])
print(state['ground']['wheel_normal_force_n'], state['navigation']['reacquiring'])
```

2026-11-18 현재 기본 선택은 [가상의 수납식 연구 구성](retractable-research.md)이다. `airframe.retractable_gear=true`와 새 id/support_message를 확인한다. 기존 `/gear`·`set_landing_gear`·3초 행정·지상 거절·자동 운항 중 수동 명령 거절 계약을 유지한다. Web UI는 기어 종류를 표시한다. 실제 Unreal22검사·CTest14/14와 전개/중간/수납 화면은 위 안내를 따른다. 기존 고정기어 C208B 기록은 이전 프로필의 결과다.

이 문서는 외부 Python 프레임워크와 Web UI가 함께 사용하는 제품 API의 기준 문서다. 구현은 [serve-api.py](../scripts/serve-api.py), Python 클라이언트는 [uavsim_client.py](../uavsim_client.py)다. Unreal MCP는 에디터 개발용이며 제품 API 호출에 필요하지 않다. 현재는 같은 PC의 기체 한 대를 제어하는 동기식 로컬 HTTP API다.

기본 주소는 `http://127.0.0.1:8765`, 모든 제품 경로의 접두사는 `/api/v1`이다. 아래 표의 경로는 이 접두사를 생략한다. 본문의 수치 범위는 개발 모델의 입력 범위이며 실제 기체 운용 한계나 성능 검증 결과가 아니다. 공력·추진 가정은 [비행 모델](flight-model.md), 기체 파일은 [기체 데이터](aircraft-profile.md)를 따른다.

## 2026-11-17 프로필별 초기화 추가 계약

2026-11-18 지상 상태 추가: `ground.applied_steering_deg`는 실제 전륜각(°), `ground.applied_brake`는 실제 제동 출력(0~1)이다. 기존 `steering_deg`/`brake`는 명령이며 기존 경로·메서드는 유지한다. `GET /state`, `set_ground_controls(...)`, `step(...)`의 전체 상태에서 읽는다. 정지 중에는 명령만 변하고 주차/접촉 계산은 시간 진행 시 적용한다. 기체별 지연·마찰·충돌 기준과 선택 숫자11개는 [기체 파일 안내](aircraft-profile.md)를 따른다.

경로·Python 공개 메서드는 유지한다. 기존 v1은 2600m/19m/s·초기 TAS 5~45m/s와 기존 운용을 유지한다. 새 v2는 **합성 공중 수동 시험 전용**으로 C208B 물리 구현이 아니다. 아래의 고정 초기값 설명은 v1 기준이며 v2는 다음 상태 필드를 따른다.

`GET /state` 및 비행 명령 응답의 `airframe`에 다음 필드를 추가했다. 기존 `id`, `profile`, `fingerprint`, `error`는 유지한다.

| 필드 | 타입·단위·의미 |
| --- | --- |
| `schema_version` | int, 적용 파일 판본 1 또는 2 |
| `initial_speed_mps`, `initial_height_m` | number, 기본 TAS m/s 및 WGS84 타원체고 m |
| `minimum_speed_mps`, `maximum_speed_mps` | number, 허용 TAS m/s |
| `minimum_height_m`, `maximum_height_m` | number, 허용 높이 m |
| `maximum_alpha_deg`, `maximum_beta_deg` | number, 공기각 절댓값 한계 ° |
| `ground_supported`, `navigation_supported`, `automation_supported` | bool, 적용 기체의 운용 지원 여부. 합성 시험체는 false, C208B 연구 구성은 true |
| `support_message` | string, 모델/미지원 설명. 성능 검증 합격 표시가 아님 |

`PUT /initial-condition`의 숫자 입력 경계는 1~250m/s로 넓혔지만 실제 초기 TAS는 적용 프로필의 범위도 만족해야 한다. 초기조건과 `PUT /pose`의 높이, 명시적 alpha/beta도 해당 프로필로 검증하며 범위 밖은 HTTP 400·상태 보존이다. `POST /reset`, `POST /aircraft/reload`와 Web UI 지역 이동 기본값은 적용된 초기 속도/높이를 사용한다. 재읽기 성공의 시간·거리·돌풍·고장 초기화와 일정 바람 유지 계약은 동일하다.

지원 bool이 false인 v2의 해당 지상/기어/항로/자동 이착륙 명령은 HTTP 409와 `support_message`를 반환하며 물리를 바꾸지 않는다. v2는 실행 중 TAS·높이·공기각 또는 보수적 미지원 지면 접촉 경계를 벗어나면 시간 진행을 거부하고 fault·정지한다. 이는 실제 기체 실속·접지 성능을 모의한 결과가 아니다. 직접 Unreal 함수 호출도 같은 지원 플래그를 검사한다.

```python
from uavsim_client import UAVSimClient
sim = UAVSimClient()
state = sim.get_state()
print(state["airframe"]["initial_speed_mps"], state["airframe"]["support_message"])
# v2 테스트 파일 적용은 aircraft/active.json 선택 후, 정지 중 수행한다.
# sim.pause(); state = sim.reload_aircraft_profile()
# 선택 파일을 편집해도 재읽기 전에는 기존 적용 범위가 유지된다.
```

v2 전체 파일·필드 검증은 [기체 파일 안내](aircraft-profile.md), 실제 30/30 결과는 [검사 기록](../artifacts/airframe-api-check.json)을 따른다. UI는 미지원 버튼을 비활성화하고 적용 범위를 표시하며 Python에서도 위 상태/오류를 읽는다. B에서는 endpoint 추가가 없었으며 C~F의 플랩 경로는 아래 추가 계약을 따른다.

## C208B 연구 모델 API 추가 계약 — 2026-11-17

실제 성능 미검증인 [연구 모델](c208b-research.md)을 추가했다. v1 Thor 경로/메서드는 유지한다. 기본값19/18m/s는 Python의 Thor 호환 기본값이므로 연구 모델 호출에서는 목표 TAS를 명시한다.

| 경로·메서드 | 입력·반환·오류 |
| --- | --- |
| `PUT /api/v1/flaps` / `UAVSimClient.set_flaps(degrees)` | JSON `{"degrees":10}`. 유한 숫자0~40 및 적용 airframe.maximum_flap_deg 이하, 초과400. 활성 자동 운항·충돌·fault에서는409. 수동 플랩 입력은 항로 취소. 반환은 전체 state |
| `GET /api/v1/state` 추가 `flaps` | `command_deg`, `actual_deg`, degree. 정지 중 명령만 변하며 step/재생에 따라 실제각이 움직임. 전개율·CL/CD/Cm 효과는 기체 설정 사용 |
| `state.airframe` 추가 필드 | `retractable_gear` bool, `maximum_flap_deg` degree, `route_minimum_speed_mps`, `route_maximum_speed_mps`, `landing_minimum_speed_mps`, `landing_maximum_speed_mps` TAS m/s |
| `PUT /api/v1/gear` | 고정기어의 down=false는409, 상태 보존. Thor의 공중 수납 유지 |
| `PUT /api/v1/route` | 항로점 높이는 airframe.minimum/maximum_height_m, 속도는 route_minimum/maximum_speed_mps. 위반400. 선회 반경의 30° 뱅크 기반 하한 유지 |
| `POST /api/v1/automation/takeoff` | speed_mps는 적용 route TAS 범위. C208B 예제55, Thor 기본19 |
| `POST /api/v1/automation/land` | approach_speed_mps는 적용 landing TAS 범위. C208B 예제40, Thor 기본18 |
| `POST /api/v1/automation/prepare-landing` | 명시적 시험 위치 초기화. C208B TAS44, Thor18. 무이동 준비 명령과 구별 |

```python
sim.pause()
state = sim.reload_aircraft_profile()  # active.json에서 c208b-research.json 선택 후
sim.set_flaps(10)
state = sim.step(1)
assert 0 < state["flaps"]["actual_deg"] <= state["flaps"]["command_deg"]
sim.reset_on_ground()
sim.start_automatic_takeoff(speed_mps=55, climb_height_m=50)
# sim.set_running(True)
```

HTTP 예: `PUT /api/v1/flaps`, Content-Type application/json, 본문 `{"degrees":20}`. Remote Control 경계는 허용 목록의 `AFlightScene::SetFlaps(Degrees)`만 추가했다. 계산 코어는 네트워크와 독립적이다.

검사: Release CTest10/10·UE5.8.4 Development 빌드, [기존 v1/v2 실제 API30개](../artifacts/airframe-api-check.json), [C208B 연구 API 결과](../artifacts/c208b-api-check.json)·[재현 스크립트](../scripts/check-c208b-api.py). 연구 API는 기체 전환·실패 보존·고정기어·플랩·항로·실제 지형 이륙·별도 시험 접근에서 착륙/주차를 검사한다. 평면 코어의 연속 귀환 왕복과 구별한다. UI는 구문/연결만 검사하며 브라우저 직접 조작을 수행하지 않았다.

## 외부 Python 비행시험 도구

기존 공개 경로만 사용하는 [비행시험 기록·비교 도구](flight-tests.md)를 추가했다. 새 endpoint·상태 필드·클라이언트 메서드·Web UI는 없다. 기록기는 `get_state`, `pause`, `set_wind`, `reload_aircraft_profile`, `set_initial_condition`, `reset`, `set_controls`, `step`, 필요한 경우 `set_flaps`/`set_failures`/`set_route`를 사용한다. 명시적 step의 시간은 벽시계 배속과 무관하다.

```powershell
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case elevator-pulse --reset-flight
.\.venv\Scripts\python.exe scripts/report-flight-test.py <saved-run>
```

시험은 현재 비행을 초기화하며 이전 동역학 상태를 복원하지 않는다. 종료 시 선택 파일·일정 바람을 복구하고 원래 프로필의 공중 트림·정지로 끝낸다. 타임아웃 mutation을 재시도하지 않으며 실패 기록과 복구 오류를 남긴다. CLI 옵션·샘플 주기·산출물·검사 결과는 위 사용법을 따른다.

## 2026-11-18: 귀환·접근 단계와 진단 상태

`POST /api/v1/automation/land`는 현재 위치/시간/재생 상태를 유지한 채 귀환을 준비한다. 고고도/원거리에서는 `returning` → `configuring` → `aligning` → `final` → `flare` → `rollout` → `complete`로 진행한다. `go_around`는 구성 상태를 유지하며 재상승하고 두 번의 복행 뒤에는 접근 궤도 대기로 전환한다. 일반 항로의 포착 로직·러더 협조는 이번 변경 범위 밖이다.

기존 요청 필드 `runway_index`, `approach_speed_mps`는 유지한다. 코어 준비 범위는 활주로 기준 높이10~5000m·거리40km 안이며 실제 Unreal은 원점49km 경계·지형 누락/표본 이격/현재 고도/진입 경로를 추가 검사한다. 40m 간격의 개발용 경로 조사이며 모든 지형/장애물 회피나 실제 항공 운항 절차를 뜻하지 않는다. 경로를 준비할 수 없으면409로 거절하고 이전 운항 상태를 유지한다.

추가 `state.automation` 필드:

| 필드 | 단위·의미 |
| --- | --- |
| `rejection` | 마지막 착륙 요청 거절 이유. 현재 운항 진행 message와 별도이며 성공한 착륙 준비에서 지움 |
| `desired_course_deg`, `desired_bank_deg` | 북 기준 시계방향 목표 코스·우측 양의 목표 뱅크 ° |
| `desired_speed_mps` | 현재 단계의 TAS 참조 m/s. 요청 접근 속도로 바로 바뀌지 않음 |
| `desired_height_m` | 현재 위치 부근에서 WGS84로 변환한 높이 참조 m |
| `desired_climb_mps` | 상승 양수인 수직속도 참조 m/s |
| `return_radius_m` | 귀환/접근 대기 궤도 기준 반경 m |
| `path` | 기준 궤도와 접근선의 위경고도/속도 배열. 각 점은 기존 waypoint와 같은 `latitude_deg`, `longitude_deg`, `ellipsoid_height_m`, `speed_mps`. 현재 기체의 전체 실제 궤적이나 내부 강하율 가상 목표점이 아님 |

Python의 `start_automatic_landing()`과 Web UI는 같은 경로를 사용한다. `prepare_landing_approach()`는 명시적인 시험 초기화로1200m 후방·활주로 위70m, Thor18m/s/플랩0°, 연구 모델의 접근 범위 중간 TAS/최대 플랩으로 준비한다. 연속 왕복 검사에는 이 초기화 명령을 중간에 사용하지 않는다.

기록기 형식v2는 `guidance_desired_*` 수치 열을 추가했다. 기존 저장 자료의 오프라인 보고서는 계속 읽을 수 있다. 실제 검사·미검증 범위는 [현재 작업 계획](return-landing-plan.md)의 실행 기록을 따른다.

## 1. 시작과 연결 확인

프로젝트 루트의 Windows PowerShell 5.1 또는 PowerShell 7에서 API 서버를 시작한다. 이미 실행 중이면 중복 실행하지 않는다.

```powershell
Set-Location C:\src\Projects\UAVSim
.\scripts\dev.ps1 api
```

[Web UI](http://127.0.0.1:8765/)의 **Unreal 실행**을 누르면 독립 비행 창이 열리고 활성 지도를 읽는다. `GET /`는 이 HTML 화면을 제공한다. 수동 Play 조작은 필요 없다. **Unreal 종료** 후에도 API와 Web UI는 남아 있다. 개발용 에디터는 `dev.ps1 editor`로 별도 실행하고 Play할 수 있다.

| 설정 | 기본값·사용법 |
| --- | --- |
| API 주소 | `127.0.0.1:8765`; 직접 실행 시 `serve-api.py --port 8765` |
| Unreal HTTP 포트 | `30010`; `--unreal-port 30010` |
| Unreal 설치 경로 | `C:\Program Files\Epic Games\UE_5.8`; `dev.ps1 api -UnrealRoot 'D:\Epic\UE_5.8'`, 환경변수 `UAVSIM_UNREAL_ROOT` 또는 서버 `--unreal-root` |
| 실행 Actor | 기본적으로 `Saved/uavsim-runtime.json`의 `object_path`, `process_id`, `world_kind`를 읽음. 개발 시 `--object-path`로 명시적 PIE 경로 지정 가능 |
| 지도 선택 | `maps/active.json` → 지도 `terrain.json`과 tiles; [지도 준비](maps.md) |
| 기체 선택 | `aircraft/active.json` → 프로필 JSON; [기체 데이터](aircraft-profile.md) |
| 표시 외형 | `aircraft/visual.json` → 상대 STL 또는 내장 외형; [STL 사용 안내](aircraft-model.md) |

Unreal은 고정된 `UAVSim.uproject`를 `UnrealEditor -game`으로 실행한다. API에서 임의 실행 파일·명령줄·지도 경로를 받지 않는다. 직접 서버를 실행하려면 `.\.venv\Scripts\python.exe scripts\serve-api.py`를 사용한다. API 자체를 끌 때는 관리 중인 Unreal을 먼저 정상 종료하고, `dev.ps1 api`가 출력한 API PID를 종료한다.

별도 `/health` 경로는 없다. `GET /engine`은 Unreal이 꺼져 있어도 연결 상태를 반환한다. `ready=true`이면 비행 API와 지형 준비가 끝난 상태다. 실제 계산 오류는 추가로 `GET /state`의 `fault`를 확인한다.

## 2. 공통 요청·응답·오류 규칙

- `GET`은 본문 없이 보낸다. `PUT`·`POST`는 `Content-Type: application/json`과 JSON 객체를 보낸다. 인수가 없는 명령도 `{}`가 필요하다.
- 각 명령에 지정된 필드를 **전부** 보내며 누락·추가·중복 키를 거부한다. 유일한 선택적 쌍은 공중 초기 조건의 `angle_of_attack_deg`, `sideslip_deg`다.
- 숫자는 유한한 JSON number다. 문자열 숫자, bool을 대신 쓴 숫자, `NaN`·`Infinity`를 허용하지 않는다. bool 필드는 `true`/`false`, 활주로 인덱스는 정수다. 명시한 범위는 양 끝을 포함하되 `dt_s`만 0을 제외한다.
- JSON 본문은 1~32768바이트다. chunked 본문은 지원하지 않는다. 경로·메서드는 표와 정확히 일치해야 하며 별도 쿼리 매개변수는 없다.
- 서버는 루프백에만 바인딩한다. Host는 해당 포트의 `127.0.0.1` 또는 `localhost`, 브라우저 Origin은 동일 출처여야 한다. 외부 사이트에서의 CORS 요청은 허용하지 않는다.
- Web UI와 Python은 같은 상태를 변경한다. 요청은 서버에서 순서대로 처리하며 뒤에 적용한 명령이 앞의 값을 덮어쓴다. 여러 호출을 묶는 트랜잭션·제어권 예약·자동 재시도는 없다.

엔진 경로의 성공 응답은 엔진 상태 객체이고, 나머지 JSON API는 적용 후 전체 비행 상태 객체다. `GET /eoir/frame.jpg` 성공만 JPEG 바이너리를 반환한다. 쓰기 성공은 입력이 수락됐다는 뜻이며 목표 도달·착륙 완료를 뜻하지 않는다. 시뮬레이션 실행 중에는 요청 처리와 상태 조회 사이에도 시간이 진행할 수 있다. 재현 실험은 먼저 정지한 뒤 명시적 `step`을 사용한다.

| HTTP | 의미·다음 행동 |
| --- | --- |
| `200` | 요청 처리 성공. `fault`, `ground.crashed`, `automation.phase` 등 반환 상태도 확인 |
| `400` | JSON 형식·필드·타입·범위 오류. 입력 수정 |
| `403` | 허용되지 않은 Host 또는 Origin |
| `404` | 지원하는 HTTP 메서드에서 알 수 없는 경로·메서드 조합 |
| `409` | 현재 상태에서 명령 거부: 재생 중 step/기체 재읽기, 지상 기어 수납, 잘못된 지도·기체·항로·자동 운항 준비 조건 등 |
| `503` | Unreal/Actor 미연결, 응답 오류·타임아웃, 서버 측 파일·실행 오류 |

오류 본문은 `{"error":{"message":"설명"}}`다. 처리하지 않는 HTTP 메서드 자체는 Python HTTP 서버의 501 응답을 낼 수 있다. 네트워크 연결 실패는 HTTP 응답 자체가 없을 수도 있다. 타임아웃은 쓰기가 이미 적용된 뒤 발생할 수 있으므로 상태를 확인하고 다음 행동을 정한다.

형식·범위 오류는 입력을 바꾸지 않는다. 런타임 거부는 오류 문자열·정지 상태를 갱신할 수 있다. 지도 재읽기 실패는 이전 지도·위치를 보존하고 정지한다. 물리 충돌은 충돌 시점까지 진행한 상태를 반환할 수 있다. `reset`은 수평 트림을 만들 수 없으면 HTTP 200이어도 `fault`를 남길 수 있으므로 성공 코드를 모델 정상 상태로 대신하지 않는다.

## 3. 좌표·단위·부호

| 항목 | 정의 |
| --- | --- |
| 위도·경도 | `latitude_deg` −90~90°, `longitude_deg` −180~180° |
| 위치 높이 | `ellipsoid_height_m` −500~20000m, **WGS84 타원체고**. 정표고·AGL과 다름 |
| 방위 | `heading_deg` −360~360°, 북쪽 0°에서 시계방향 |
| 자세 입력 | `pitch_deg` −89~89° 기수 상승 양수, `roll_deg` −180~180° 오른쪽 날개 하강 양수 |
| 속도 | `speed_mps`는 TAS. `ground_speed_mps`는 ENU 수평 지상속도, `vertical_speed_mps`는 ENU 위쪽 양수 |
| 각속도 | 기체 FRD(앞·오른쪽·아래) 축 p/q/r, °/s. Euler 각의 시간 미분과 다름 |
| 조종면 | `elevator_deg` 양수=후연 아래, `aileron_deg` 양수=오른쪽 후연 위·왼쪽 아래, `rudder_deg` 양수=후연 오른쪽. 식별 기준을 사용한 개발 각도이며 현재 고도·속도 트림값과는 구분 |
| 스로틀·브레이크 | 0~1 정규화 입력. 스로틀 0.5가 모든 상태의 트림이라는 뜻은 아님 |
| 바람 | ENU 동·북·위쪽으로 **부는** 속도(m/s). 기상 풍향처럼 불어오는 방향이 아님 |
| 코어·렌더 | 코어 ENU m → Unreal 표시 (동·남·위) cm. 외부 API는 Unreal cm를 사용하지 않음 |
| 시간 | 시뮬레이션 s. 명시적 step에 벽시계 배속을 곱하지 않음 |

공중 초기 위치는 타일 지도의 manifest 위경도 범위와 렌더 원점 1000km 이내에서 정하며 그 위치를 새로운 계산 원점으로 삼는다. 단일 지역 지도에서는 렌더 원점 50km 제한도 적용한다. 이후 물리 비행·항로·최종 선회 전체는 계산 원점 50km 이내다. 전국 지형 표시가 장거리 비행 모델의 검증을 뜻하지 않는다. 지면 자료 누락은 `ground.available=false`이며 높이 0 지면으로 대체하지 않는다.

## 4. Unreal 실행·종료

| 메서드·경로 | 요청 | Python |
| --- | --- | --- |
| `GET /engine` | 없음 | `get_engine()` |
| `POST /engine/start` | `{}` | `start_engine()` |
| `POST /engine/stop` | `{}` | `stop_engine()` |

반환 필드는 `status`, `pid`, `managed`, `ready`, `message`다. `status`는 `stopped`, `starting`, `running`, `stopping`, `error`, `external` 중 하나다. `pid`는 관리 중인 활성 프로세스 ID 또는 null, `managed`는 현재 API 서버가 소유한 실행인지, `ready`는 지형·기체 API 준비 여부다. 시작·종료 요청은 완료 전에 반환하므로 상태를 조회한다. 시작 중·실행 중 중복 시작은 같은 프로세스를 반환하며 종료 완료 후 재종료는 무해하다.

정상 종료는 이 API가 시작한 초기화된 독립 Game 실행만 허용한다. 별도로 연 에디터/실행 창 또는 API 재시작으로 소유 정보를 잃은 실행은 `external`이며 해당 창에서 직접 닫는다. 초기화 전 `starting` 상태의 종료와 실행 중 오류 상태에서 중복 시작은 거부한다. 지형 로드가 실패했더라도 초기화된 소유 실행은 정상 종료를 요청할 수 있다. 종료 후 API 서버는 유지된다.

## 5. 상태 조회·시간·초기화

| 메서드·경로 | 정확한 요청 필드·범위 | Python·부작용 |
| --- | --- | --- |
| `GET /state` | 없음 | `get_state()`; 전체 비행 상태 |
| `PUT /simulation` | `running`: bool | `set_running(running)`, `pause()`; pause는 false의 별칭. fault 또는 지형 미준비에서 재생 거부 |
| `POST /step` | `dt_s`: 0보다 크고 1 이하 | `step(dt_s)`; 정지 중만 허용 |
| `PUT /time-scale` | `factor`: .25~20 | `set_time_scale(factor)`; 벽시계 재생만 변경, 재생/정지 유지 |
| `POST /reset` | `{}` | `reset()`; 현재 계산 원점, 타원체고 2600m·TAS 19m/s·북쪽 수평 트림, 시간·거리 0·정지 |

step은 최대 .01초로 나누고 지면 근처의 접촉 계산은 최대 .002초로 나눈다. 요청한 시간 동안 실제 입력은 서보·엔진·기어 응답을 따른다. 충돌이나 자동 착륙 완료로 중간에 멈출 수 있으므로 실제 진행량은 응답의 `aircraft.simulation_time_s` 차이로 계산한다. `effective_time_scale`은 벽시계 재생 측정값이며 정지 시 0이다. 높은 배속에서 목표 처리속도를 보장하지 않는다.

reset은 항법·자동 운항·충돌 상태와 조종/작동기 초기 조건을 다시 만들지만 일정 바람·돌풍·고장을 자동 해제하는 명령은 아니다. 고장·바람 때문에 트림이 불가능하면 `fault`를 확인한다. 기어는 초기화 시 완전 전개 상태다. 명시적으로 깨끗한 시험을 하려면 바람·돌풍·고장을 먼저 해제한다.

## 6. 공중 초기 조건과 수동 조종

| 메서드·경로 | 정확한 요청 필드·범위 | Python |
| --- | --- | --- |
| `PUT /pose` | 위 좌표 표의 `latitude_deg`, `longitude_deg`, `ellipsoid_height_m`, `heading_deg`, `pitch_deg`, `roll_deg` | `set_pose(...)` |
| `PUT /initial-condition` | pose 6필드 + `speed_mps`: 5~45. 선택적으로 `angle_of_attack_deg`: −180~180 및 `sideslip_deg`: −89~89를 **함께** 추가 | `set_initial_condition(...)` |
| `PUT /controls` | `elevator_deg`, `aileron_deg`, `rudder_deg`: 각각 −25~25; `throttle`: 0~1 | `set_controls(...)` |

pose는 현재 TAS를 사용하여 초기 조건을 다시 만든다. 현재 TAS가 5~45m/s 밖이면 거부될 수 있다. 속도를 지정하려면 initial-condition을 사용한다. 공중 초기 조건은 위치·자세·속도·계산 원점을 교체하고 각속도·거리·항법·자동 운항·지상 입력을 초기화하며 정지한다. **시뮬레이션 시간은 유지**한다. 지면 안에 묻히는 초기 조건은 거부된다. 현재 환경·돌풍·고장은 유지한다.

공기각을 생략하면 해당 조건의 기준 트림 alpha와 beta 0을 사용한다. 둘 중 하나만 지정하면 거부한다. 요청한 피치·롤은 그대로 적용하므로 임의 자세가 수평 트림을 뜻하지 않는다. 계산 가능한 트림이 있으면 그 조종 입력을 사용하고, 없으면 개발 기본 입력을 사용한다. 초기화 직후 실제 작동기도 초기 입력과 맞춘다.

수동 controls는 네 필드를 한 번에 갱신하고 항법·자동 이착륙을 해제한다. 현재 재생/정지는 유지한다. `controls`는 명령값이고 `actuators`는 지연·속도 제한·고착을 거친 실제 출력이다. 정지 중 명령만 바꾸면 실제 면 위치는 시간 진행 전까지 그대로일 수 있다.

## 7. 지상 조종과 랜딩기어

| 메서드·경로 | 정확한 요청 필드·범위 | Python·부작용 |
| --- | --- | --- |
| `PUT /ground-controls` | `steering_deg`: −30~30, `brake`: 0~1, `parking_brake`: bool | `set_ground_controls(...)`; 비행 입력은 유지하고 항법·자동 이착륙 해제. 충돌 상태에서 거부 |
| `POST /ground/reset` | `{}` | `reset_on_ground()`; 첫 가상 시험 활주로 시작점 안쪽 30m. 활주로가 없으면 현재 원점 지면 |
| `PUT /ground/initial-condition` | `latitude_deg`: −90~90, `longitude_deg`: −180~180, `heading_deg`: −360~360, `ground_speed_mps`: 0~15 | `set_ground_initial_condition(...)`; 지면으로 높이·자세 계산 |
| `PUT /gear` | `down`: bool | `set_landing_gear(down=True)`; true 전개, false 수납 |

조향 양수는 오른쪽이다. 지상 초기화는 시간·거리·조종 입력·추력·항법·자동 운항을 초기화하고 정지한다. 지상속도 0이면 주차 브레이크를 건다. 기어는 완전 전개 상태다. 지정 속도는 방위 방향 지면 접선 속도이며 TAS가 아니다. 지면을 구할 수 없으면 거부한다. 일정 바람·돌풍·고장 설정은 유지한다.

기어 명령은 목표만 바꾸며 전체 행정은 **시뮬레이션 시간 3초**다. 중간에 반대 명령을 보낼 수 있고 정지하면 이동도 멈춘다. 세 기어를 함께 제어한다. 지상에서 수납하려 하거나 자동 이착륙 제어가 활성인 동안 수동 기어 명령을 보내면 HTTP 409다. fault·지형 미준비 상태에서도 거부한다. 일반 비행 controls는 기어를 유지한다. 완전히 전개·잠금되기 전에는 바퀴가 하중을 지지하지 않으며 동체·날개 충돌 판정은 유지한다. 착륙 전에는 `gear.down_locked`를 확인한다.

기어의 이동 기구와 항력은 개발 가정이다. 기존 완전 전개 항력을 기준으로 `Cd=max(0, 기존 Cd+.012*(extension-1))`를 사용한다. 세 힌지를 90°까지 접고 완전 수납 시 기어 메시를 숨긴다. 도어·유압·각 기어별 고착·실측 수납 항력은 구현하지 않았다. 기어 상수는 코어에 두며 기존 기체 JSON 50개 매개변수를 늘리지 않는다. 수동 절차와 접촉 한계는 [지상 운용](ground-operations.md), 자동 전개·수납은 [자동 이착륙](automatic-flight.md)을 따른다.

## 8. 항로와 최종 선회

| 메서드·경로 | 정확한 요청 필드·범위 | Python |
| --- | --- | --- |
| `PUT /route` | `waypoints`: 1~64개 배열, `loiter_radius_m`, `clockwise`: bool | `set_route(waypoints, *, loiter_radius_m, clockwise=True)` |
| `POST /navigation/cancel` | `{}` | `cancel_navigation()` |

각 항로점은 `latitude_deg` −90~90, `longitude_deg` −180~180, `ellipsoid_height_m` −500~20000, `speed_mps` TAS 16~24의 네 필드를 가진다. 단일 지점도 배열 한 항목으로 보낸다. 최종 선회 반경은 `max(100, V²/(9.80665*tan(30°)))`~5000m이고 V는 마지막 항로점 TAS다. 모든 점과 최종 선회 전체가 계산 원점 50km 안에 있어야 한다.

요청 수락 시 기존 항로를 통째로 교체하고 자동 이착륙을 해제한다. 재생/정지를 유지하며 위치를 이동시키지 않는다. 지상 접촉·fault 상태에서는 거부한다. 잘못된 항로는 기존 항로를 보존한다. 순서대로 점을 추종하고 마지막 점을 clockwise 방향으로 계속 선회한다. 제어기는 조종면·스로틀을 계산하며 같은 비행역학을 통과한다. 실속 시 회복을 우선하고 `navigation.stall_recovery`로 공개한다. 지형·장애물 회피는 없다.

항법 해제는 현재 조종 명령과 재생/정지를 유지하고 자동 이착륙도 해제한다. 해제 후 기체를 붙잡아 두는 기능은 아니므로 수동 입력 또는 새 제어를 이어서 지정한다.

## 9. 자동 이륙·착륙

| 메서드·경로 | 정확한 요청 필드·범위 | Python 기본 인수 |
| --- | --- | --- |
| `POST /automation/takeoff` | `runway_index`: 정수 0~15, `climb_height_m`: 20~150, `speed_mps`: 16~24 | `start_automatic_takeoff(runway_index=0, climb_height_m=50, speed_mps=19)` |
| `POST /automation/land` | `runway_index`: 정수 0~15, `approach_speed_mps`: 16~22 | `start_automatic_landing(runway_index=0, approach_speed_mps=18)` |
| `POST /automation/prepare-landing` | `runway_index`: 정수 0~15 | `prepare_landing_approach(runway_index=0)` |
| `POST /automation/cancel` | `{}` | `cancel_flight_operation()` |

Python 기본 인수는 클라이언트가 채워 보내며 HTTP에서는 전부 지정한다. 활주로는 활성 지도 옆 `runways.json`의 0부터 시작하는 배열 인덱스다. 존재하지 않는 인덱스는 범위 안의 정수여도 거부한다. climb_height는 **활주로 평면 기준 높이**이며 타원체고가 아니다.

이륙·착륙 준비는 현재 위치·재생/정지를 유지한다. 정지 중이면 `step` 또는 재생 명령으로 진행한다. 항로는 해제된다. 자동 이륙은 완전 전개 잠금을 요구하며 지면 법선 높이 10m 이상에서 상승 중이면 수납한다. 자동 착륙은 수락 즉시 전개 목표를 지정하고 시간 진행 중 전개한다. 최종 접근에서 활주로 평면 위 15m 미만인데 전개 잠금이 되지 않으면 복행을 요청한다. `gear` 상태를 운항 단계와 함께 확인한다.

착륙 시험 위치 명령은 예외적으로 활주로 시작점1200m 뒤·평면 위70m에서 Thor TAS18m/s/플랩0°, C208B 연구 TAS40m/s/플랩30°로 **초기 조건을 교체하고 정지**한다. 자동 제어 자체가 아니다. 시간은 일반 공중 초기 조건처럼 유지한다. 자동 운항 해제는 위치·조종 입력·재생/정지를 유지하며 기어 이동도 임의로 초기화하지 않는다.

자동 운항은 300m 이상 활주로, 일정 횡풍 5m/s 이하·배풍 3m/s 이하, 엔진 출력 비율 .9 이상·면 고착 없음 등의 준비 조건이 있다. 이륙은 바퀴 접촉·방위 오차 10° 이내·중심선 오차 폭의 1/4 이내·잔여 300m 이상·지상속도 10m/s 이하를 요구한다. 착륙은 공중에서 활주로 기준 높이 10~1000m·거리 5km 이내·현재 TAS 12~35m/s 등을 요구한다. 상세 단계와 개발 한계는 [자동 이착륙 안내](automatic-flight.md)를 따른다.

## 10. 환경·돌풍·고장

| 메서드·경로 | 정확한 요청 필드·범위 | Python |
| --- | --- | --- |
| `PUT /environment` | `wind_east_mps`, `wind_north_mps`, `wind_up_mps`: 각각 −30~30 | `set_wind(...)` |
| `PUT /gust` | `east_mps`, `north_mps`, `up_mps`: 각각 −15~15; `delay_s`: 0~600; `duration_s`: .1~120 | `set_gust(..., delay_s=0, duration_s=5)` |
| `PUT /failures` | `engine_power_fraction`: 0~1; `elevator_jammed`, `aileron_jammed`, `rudder_jammed`: bool | `set_failures(...)` |

바람 변경은 관성 위치·속도를 순간 변경하지 않는다. TAS·받음각·공력은 달라질 수 있다. 돌풍은 현재 시뮬레이션 시간+delay부터 duration 동안 단일 1−cosine 펄스를 더한다. 지정 벡터는 중간 시각의 최대값이고 재요청은 이전 펄스를 교체한다. 세 성분을 모두 0으로 보내면 해제한다. 정지 중 벽시계가 지나도 돌풍은 진행하지 않는다.

엔진 출력 비율은 발생 추력에 즉시 곱하며 0은 엔진컷이다. 면 고착은 false→true 순간의 실제 위치를 유지한다. true를 다시 보내도 고착 위치를 새로 잡지 않는다. 해제하면 현재 명령을 향한 서보 응답을 재개한다. `engine_power_fraction=1`과 세 bool false로 고장을 해제한다. 이 API는 기체 위치·시간·자동조종을 초기화하지 않는다. 모든 고장 상태의 자동 회복을 보장하지 않는다.

## 11. 기체·외형·지도 재읽기와 카메라

| 메서드·경로 | 요청 | Python·동작 |
| --- | --- | --- |
| `POST /aircraft/reload` | `{}` | `reload_aircraft_profile()`; 정지 중 `aircraft/active.json` 재읽기 |
| `POST /aircraft/visual/reload` | `{}` | `reload_aircraft_visual(timeout=30)`; 정지 중 `aircraft/visual.json` 재읽기, 표시 외형만 교체 |
| `POST /terrain/reload` | `{}` | `reload_terrain(timeout=60)`; `maps/active.json` 재읽기 |
| `PUT /view` | `mode`: `"aircraft"`, `"overview"`, `"projectile"` | `set_view(mode)`; 기체/지도 전체/진행 중 연출의 외부 시점 |

기체 재읽기 성공은 현재 계산 원점의 2600m·19m/s 공중 트림으로 초기화하고 시간·거리·항법·자동 운항·돌풍·고장을 지운다. 일정 바람은 유지하며 기어는 완전 전개다. 프로필에 맞는 수평 트림이 없으면 거부한다. 재생 중 재읽기 또는 잘못된 프로필은 기존 모델·물리 상태를 보존하고 `airframe.error`에 남긴다.

외형 재읽기는 위치·자세·속도·시간·기어·조종 입력·물리 프로필을 보존한다. 재생 중 요청은 409로 거부한다. ASCII/binary STL과 설정을 먼저 검증하여 실패 시 이전 메시를 유지하고 `visual.error`에 남긴다. 요청은 `{}`이며 파일명·설정은 로컬 `aircraft/visual.json`에서 읽는다. 클라이언트와 서버의 외형 호출 timeout은 기본 30초이며 자동 재시도하지 않는다.

설정은 `schema_version`, `mesh`, `scale_to_m`, `forward_axis`, `up_axis`, `origin`, `color`, `show_builtin_gear`의 8개 필드다. `mesh=""`이면 내장 외형으로 돌아간다. STL 표시를 바꿔도 공력·접촉 형상은 자동 변경되지 않는다. 파일 크기·삼각형 수·좌표 범위와 정확한 축/단위 설정은 [STL 기체 외형 안내](aircraft-model.md)를 따른다.

지도 재읽기 성공은 새 자료로 비행을 초기화하고 정지한다. 기존 계산 원점이 새 지도 범위 안이면 유지하고, 범위 밖이면 지도 원점으로 바꾼다. 시간·거리·항법·조종/작동기 상태를 초기화하고 기어는 전개한다. 일정 바람·돌풍·고장은 유지한다. 실패는 이전 지도·위치를 보존하고 정지하며 `terrain.error`에 이유를 남긴다. 파일 업로드 API가 아니므로 먼저 [지도 준비](maps.md)에 따라 로컬 파일을 작성한다. 긴 로드에 맞춰 이 요청의 클라이언트 timeout을 별도로 지정할 수 있다. 서버의 Unreal 호출은 최대 60초를 기다리며 자동 재시도하지 않는다. timeout을 늘린다고 성공을 보장하지 않는다.

view 변경은 메인 Unreal 창의 카메라만 바꾸고 물리 위치·입력·시간을 바꾸지 않는다. EO/IR 카메라는 아래의 별도 API를 사용한다. 개발용 native `CaptureView()`와 MCP 렌더 캡처도 별도다.

`projectile`은 가장 최근의 `animating` 워게임 도형을 별도 외부 카메라로 따라간다. 진행 중 연출이 없으면 409로 거부한다. 따라보던 연출이 끝나거나 취소·초기화되면 다른 진행 연출을 선택하고, 없으면 `aircraft`로 돌아간다. EO/IR 지향·영상 설정은 유지한다.

### EO/IR 카메라와 가상 표적

[별도 EO/IR 페이지](http://127.0.0.1:8765/eoir)는 API 접두사 없는 `GET /eoir`와 `GET /eoir.html`로 제공한다. 조작 절차·예제·제한은 [EO/IR 안내](eoir.md)를 따른다. 영상은 Unreal 장면이며 IR 모드는 밝기 기반 회색조/반전 필터다. 온도·방사율을 계산하지 않는다.

| 메서드·경로 | 정확한 요청 필드·범위 | Python |
| --- | --- | --- |
| `PUT /eoir` | `enabled`: bool, `mode`: `ccd`/`ir_white`/`ir_black`, `fov_deg`: 수평 1~90° | `set_eoir(*, enabled, mode="ccd", fov_deg=30)` |
| `PUT /eoir/gimbal-mode` | 아래 모드별 필드만 지정 | `set_gimbal_mode(...)` |
| `PUT /eoir/gimbal` | `azimuth_deg`: −180~180°, `elevation_deg`: −90~30°; `body_fixed` 전환 | `set_gimbal(*, azimuth_deg, elevation_deg)` |
| `POST /eoir/track` | `target_id`: 등록 ID이면 `auto`, `""`이면 현재 각도의 `body_fixed` | `track_target(target_id="")` |
| `POST /targets` | `id`, `kind`, `latitude_deg`, `longitude_deg`, `heading_deg` | `create_target(*, id, kind, latitude_deg, longitude_deg, heading_deg=0)` |
| `PUT /targets/pose` | `id`, `latitude_deg`, `longitude_deg`, `heading_deg` | `move_target(*, id, latitude_deg, longitude_deg, heading_deg=0)` |
| `POST /targets/remove` | `id` | `remove_target(id)` |
| `GET /eoir/frame.jpg` | 본문 없음 | `get_eoir_frame()` → JPEG bytes |

`set_gimbal_mode(mode, *, azimuth_deg=None, elevation_deg=None, target_id=None, latitude_deg=None, longitude_deg=None, ellipsoid_height_m=None)`는 `None` 인수를 생략한다. HTTP 본문은 다음 중 하나이며 누락·추가 필드는 400이다. 영상 모드 `eoir.mode`와 짐벌 모드 `eoir.gimbal_mode`를 구분한다.

| 짐벌 `mode` | 정확한 JSON 필드·동작 |
| --- | --- |
| `body_fixed` | `mode`, `azimuth_deg` −180~180°, `elevation_deg` −90~30°. 기체 기준 각도를 고정하므로 기체와 함께 회전한다. |
| `auto` | `mode`, `target_id`(빈 문자열 불가). 등록 표적의 위치를 향한다. |
| `geo` | `mode`, `latitude_deg` −90~90°, `longitude_deg` −180~180°, `ellipsoid_height_m` −500~20000m. 렌더 원점에서 3차원 직선거리 1000km 이내의 WGS84 점을 향하며 지형 높이를 더하지 않는다. 지면 자료는 불필요하다. |
| `protect` | `mode`만 지정. 방위 180°·고각 +30°의 가상 보호 자세로 고정하고 영상을 끈다. 실제 기계적 보호 모델은 아니다. |

보호 중 `PUT /eoir`의 `enabled=true`는 상태를 보존하며 409다. `enabled=false`로 영상 종류·FOV를 바꾸는 것은 허용한다. 다른 짐벌 모드나 기존 `set_gimbal`/`track_target` 명령으로 보호를 해제해도 영상은 꺼진 채 유지하므로 별도 `set_eoir(enabled=True, ...)`가 필요하다. 잘못된 모드 전환은 이전 지향 상태를 보존한다.

짐벌 방위 0°는 기체 전방·오른쪽 양수, 고각은 위쪽 양수다. 이상적으로 즉시 각도를 적용한다. 수동 짐벌은 표적 추적을 해제하며 비행 자동항법을 바꾸지 않는다. `enabled=false`에서도 짐벌·선택 표적 추적은 유지하고 영상만 거부한다. `kind`는 `tank`, `missile`, `marker`의 정적 도형 소품이며 무장·발사 동작이 없다. ID는 `[A-Za-z0-9_-]` ASCII 1~48자, 최대 64개다. 표적 위도 −90~90°, 경도 −180~180°, 북쪽 기준 시계방향 방위 0~360°를 받으며 지형/가상 활주로 높이를 자동 계산한다. 높이는 요청 필드가 아니다. 지형 자료가 있고 렌더 원점에서 수평 1000km 이내여야 하며 기체의 50km 범위와 별도다. 중복 ID 생성·없는 ID 이동/삭제/추적·표적 수 초과·지면 없는 위치는 409와 `eoir.error`로 거부한다. `move_target`은 표적을 즉시 재배치하며 차량·비행 동역학을 계산하지 않는다.

auto는 표적 원점에서 지면 법선 방향 1.5m 위, geo는 지정 위경고도 점 자체를 향하는 기하학적 지향으로 영상 탐지·가림 판정을 하지 않는다. 카메라 장착점은 기체 로컬 앞 20cm·아래 12cm이며 기체 자체 메시는 영상에서 숨긴다. auto 표적 삭제는 ID를 비우고 마지막 각도의 `body_fixed`·`target_missing`으로 전환하며 다른 표적 삭제는 geo에 영향을 주지 않는다. 기체 초기화·pose는 보호를 포함한 짐벌 모드·카메라·표적을 유지한다. 성공한 지형 재읽기는 표적을 비우며 geo 점은 새 렌더 원점의 범위 안이면 유지한다. 범위 밖이면 `body_fixed`·마지막 각도·`target_missing`으로 전환한다. Unreal 재시작 시 실행 상태를 새로 만든다.

프레임은 `image/jpeg` 1280×960·품질 92이며 `Cache-Control: no-store`, `X-Simulation-Time-S`, `X-Sensor-Sequence`, `X-Sensor-Mode` 헤더를 제공한다. Python 편의 메서드는 헤더 없이 bytes만 반환한다. 카메라 비활성/native 캡처 오류는 JSON 오류 409, 손상된 JPEG/base64/메타데이터는 503이다. 프레임 요청 자체로 비행 시간을 진행하지 않으며 렌더·HTTP 주기는 고정 센서 주기를 보장하지 않는다. Web UI는 프레임 완료 후 다음 요청을 예약해 최대 목표 10Hz로 갱신하고 실제 수신 FPS·간격을 표시한다. 확대·원본 크기·FOV 프리셋과 기존 `PUT /simulation`을 쓰는 재생 제어는 [EO/IR 안내](eoir.md)를 따른다.

### 워게임 발사 연출·수동 판정

EO/IR 페이지의 워게임 패널과 다음 API는 표시용 STL의 고정 시간 선형 애니메이션과 운영자 판정을 제공한다. 실제 유도·탄도·충돌 기반 자동 명중·피해량을 계산하지 않는다. 파일 계약·실행 예제는 [워게임 안내](wargame.md)를 따른다.

| 메서드·경로 | 정확한 요청 필드 | Python |
| --- | --- | --- |
| `POST /wargame/weapons/reload` | `{}` | `reload_wargame_weapons()` |
| `POST /wargame/fire` | `weapon_id`, `target_id` | `play_wargame_shot(weapon_id, target_id)` |
| `POST /wargame/resolve` | `shot_id`, `outcome`: `hit`/`miss` | `resolve_wargame_shot(shot_id, outcome)` |
| `POST /wargame/reset` | `{}` | `reset_wargame()` |

ID는 `[A-Za-z0-9_-]` ASCII 1~48자 문자열이다. 발사 연출은 현재 기체 시작점과 표적 끝점을 저장하고 `weapons/catalog.json`의 `animation_duration_s`(0.2~30s) 동안 두 점 사이를 표시한다. 표적이 움직여도 저장된 끝점은 유지한다. `animating`을 최대 16개, 전체 이력을 128개까지 허용한다. 완료 후 `awaiting_result`에서만 명시적 판정할 수 있으며 자동 명중하지 않는다. 결과는 `resolved`, 진행/판정 대기 중 표적 삭제는 `canceled`다.

명중 판정은 `targets[].hit=true`, `hit_count+1`, `last_hit_shot_id`와 빨간 표시를 적용한다. 빗나감 판정은 이전 명중 표시를 유지한다. 형식·타입·필드·ID 문법 오류는 400, 없는 ID·단계 위반·용량 초과는 409이며 `wargame.error`에 이유를 남긴다. 무기 목록 재읽기는 정지 중이고 진행 중인 애니메이션이 없어야 한다. 성공 시 목록만 교체하여 이력은 유지하고, 실패 시 기존 목록을 보존한다.

시간은 수락된 비행 시간만큼 진행한다. 정지·재생 배속을 따르며 명시적 step에는 배속을 다시 곱하지 않는다. 비행 초기화·pose는 워게임 시각·연출·이력을 보존한다. 워게임 reset은 시간 0·연출/기록 삭제·표적 명중 표시 초기화이며 기체·표적 등록·카메라는 보존한다. 성공한 지형 재읽기는 워게임과 표적을 비운다. 엔진 재시작은 실행 기록을 초기화한다. 연출은 비행역학·기체 질량·조종 입력을 바꾸지 않는다.

## 12. 전체 비행 상태 읽기

JSON 상태는 센서 관측이 아닌 시뮬레이터 계산 상태다. JPEG 프레임과 엔진 경로를 제외한 명령은 아래 그룹을 반환한다.

| 최상위 필드 | 의미 |
| --- | --- |
| `model` | 현재 모델 식별자 `thor_nonlinear_6dof_v2`; 검증 등급이 아님 |
| `fault` | 정상은 빈 문자열. 수치/충돌/초기화 오류 설명 |
| `object_path` | 현재 Unreal Game/PIE Actor 경로. 외부 코드에 고정하지 않음 |
| `view_mode` | 메인 Unreal 창의 `aircraft`, `overview`, `projectile` 시점 |
| `aircraft`, `controls`, `actuators`, `gear`, `ground`, `navigation`, `automation`, `airframe`, `visual`, `environment`, `disturbance`, `terrain`, `eoir`, `wargame` | 아래 상태 객체 |
| `targets` | 등록한 가상 지상 표적의 배열 |

### aircraft

| 필드 | 의미·단위 |
| --- | --- |
| `latitude_deg`, `longitude_deg`, `ellipsoid_height_m` | WGS84 위치, °·°·m |
| `heading_deg`, `pitch_deg`, `roll_deg` | 자세 °. quaternion 결과의 Euler 표현이므로 수직 자세 부근에서 등가 각도 표현이 바뀔 수 있음 |
| `speed_mps`, `ground_speed_mps`, `vertical_speed_mps` | TAS, 수평 지상속도, ENU 수직속도 m/s |
| `roll_rate_deg_s`, `pitch_rate_deg_s`, `yaw_rate_deg_s` | 기체축 p/q/r °/s |
| `simulation_time_s`, `running` | 시뮬레이션 시간과 벽시계 재생 여부 |
| `time_scale`, `effective_time_scale` | 설정 배속과 측정된 실제 재생 배속 |
| `distance_travelled_m` | 최근 초기 조건/초기화/지도 재읽기 이후 국소 3D 경로 길이 |
| `terrain_loaded` | 지형 로드 상태 |
| `angle_of_attack_deg`, `sideslip_deg` | 공기 상대속도의 alpha/beta ° |
| `air_density_kg_m3`, `dynamic_pressure_pa` | ISA 기반 밀도 kg/m³, 동압 Pa |
| `lift_coefficient`, `drag_coefficient` | 계산된 CL/CD, 무차원 |
| `flow_separation` | 박리 상태 0~1 |
| `stall_warning`, `stalled` | 개발 모델의 실속 경고·실속 상태 |
| `load_factor` | 공력 하중계수. 지면 반력은 포함하지 않음 |

### controls · actuators · gear

| 그룹·필드 | 의미 |
| --- | --- |
| `controls.elevator_deg`, `controls.aileron_deg`, `controls.rudder_deg`, `controls.throttle` | 조종 명령 세 각도(°)와 스로틀(0~1) |
| `actuators.elevator_deg`, `actuators.aileron_deg`, `actuators.rudder_deg`, `actuators.throttle` | 서보·엔진 응답과 면 고착을 거친 실제 출력 |
| `actuators.thrust_n` | 출력 저하를 포함한 실제 개발 추력 N |
| `gear.command_down` | 요청한 기어 목표, true=전개 |
| `gear.extension` | 실제 전개 비율 0=완전 수납, 1=완전 전개 |
| `gear.down_locked`, `gear.up_locked` | 완전 전개/수납 완료 여부 |
| `gear.moving` | 목표를 향해 이동 중인지. 정지 중에도 미완료 목표이면 true일 수 있음 |

### ground

| 필드 | 의미·단위 |
| --- | --- |
| `available` | 현재 위치의 지면 자료 존재 여부. false일 때 높이 필드를 실제 0m로 해석하지 않음 |
| `on_ground`, `wheels_on_ground` | 바퀴 접촉 여부·접촉 수(0~3) |
| `height_agl_m` | CG의 **부호 있는 지면 법선 거리** m. 수직 높이·바퀴 간격이 아님 |
| `terrain_height_m` | 접촉면의 WGS84 타원체고 m |
| `normal_force_n` | 접촉 법선 충격량/마지막 계산 간격 N. 착륙장치 최대 하중이 아님 |
| `impact_speed_mps` | 최근 접지 최대 법선 접근속도. 접지 뒤에도 기록 유지 |
| `crashed` | 동체/날개 접촉 또는 개발 충돌 기준 초과. 새 초기 조건 필요 |
| `steering_deg`, `brake`, `parking_brake` | 조향·브레이크·주차 입력 |
| `surface` | `terrain`, `development runway shoulder` 또는 설정 활주로 이름 |

### navigation · automation

| 그룹·필드 | 의미 |
| --- | --- |
| `navigation.mode` | `manual`, `waypoint`, `loiter`. 자동 이착륙은 별도 automation 확인 |
| `navigation.stall_recovery` | 항로 제어기의 실속 회복 우선 제어 |
| `navigation.active_waypoint_index`, `navigation.waypoint_count` | 활성 인덱스(0부터, 수동 −1)·항로점 수 |
| `navigation.distance_to_waypoint_m`, `navigation.loiter_radius_m` | 활성 점까지 수평 거리·최종 선회 반경 m |
| `automation.kind` | `none`, `takeoff`, `landing` |
| `automation.phase` | `idle`, `takeoff_roll`, `rotate`, `climb`, `hold`, `returning`, `configuring`, `aligning`, `final`, `flare`, `rollout`, `complete`, `rejected_takeoff`, `aborted`, `go_around`. 이전 `join`은 새 착륙에서 사용하지 않음 |
| `automation.runway_index`, `automation.runway_name` | 선택 활주로 인덱스/이름. 미선택 −1/빈 문자열 |
| `automation.completed` | 이륙 hold 또는 착륙 complete. hold는 공중 대기 비행을 계속함 |
| `automation.message` | 현재 운항 설명 |
| `automation.cross_track_m`, `automation.along_track_m` | 활주로 중심선 오른쪽 양수 횡오차·시작점부터 진행 방향 양수 거리 m |
| `automation.target_height_m` | 활주로 평면 기준 설정 목표 높이 m. 타원체고나 매 순간의 접근 경로 높이가 아님 |

### environment · disturbance · airframe · terrain

| 그룹·필드 | 의미 |
| --- | --- |
| `environment.wind_east_mps`, `environment.wind_north_mps`, `environment.wind_up_mps` | 일정 ENU 바람 m/s |
| `disturbance.gust_east_mps`, `disturbance.gust_north_mps`, `disturbance.gust_up_mps` | 예약한 돌풍 최대 벡터 m/s |
| `disturbance.gust_start_s`, `disturbance.gust_duration_s` | 돌풍 시작 시뮬레이션 시간·지속 s |
| `disturbance.effective_wind_east_mps`, `disturbance.effective_wind_north_mps`, `disturbance.effective_wind_up_mps` | 현재 일정 바람+돌풍 m/s |
| `disturbance.engine_power_fraction`, `disturbance.elevator_jammed`, `disturbance.aileron_jammed`, `disturbance.rudder_jammed` | 출력 비율·면 고착 설정 |
| `airframe.id`, `airframe.profile` | 프로필 ID·선택 파일 |
| `airframe.fingerprint` | 읽은 프로필 텍스트의 `sha1:` 식별값. 보안 서명/검증 표시가 아님 |
| `airframe.error` | 기체 파일 로드 오류 |
| `terrain.dataset`, `terrain.tile_count`, `terrain.manifest`, `terrain.attribution` | 지도 이름·타일 수·설정 경로·출처 |
| `terrain.error` | 지형 재읽기 오류 |

### visual

| 필드 | 의미 |
| --- | --- |
| `source` | `builtin` 또는 `stl` |
| `config` | 외형 설정 `aircraft/visual.json` |
| `mesh` | aircraft 기준 상대 STL 경로. 내장 외형은 빈 문자열 |
| `triangle_count` | 읽은 STL 삼각형 수 |
| `span_m` | 변환된 메시의 기체 로컬 Y축 전체 폭(m). 물리 프로필의 span과 별도 |
| `fingerprint` | 설정 UTF-8 텍스트+STL 원본 바이트의 SHA1. 내장 외형은 설정만 사용 |
| `error` | 외형 재읽기 오류. 이전 표시 상태를 보존한 채 오류만 갱신할 수 있음 |
| `show_builtin_gear` | 내장 기어 표시 여부. 기어 물리·API는 항상 유지 |

### eoir · targets

| 그룹·필드 | 의미 |
| --- | --- |
| `eoir.enabled`, `eoir.mode`, `eoir.fov_deg` | 활성 여부, CCD/회색조/반전 모드, 수평 시야각 ° |
| `eoir.gimbal_mode` | 지향 모드 `body_fixed`, `auto`, `geo`, `protect` |
| `eoir.azimuth_deg`, `eoir.elevation_deg` | 기체 기준 실제 짐벌 각도 ° |
| `eoir.target_id` | auto의 등록 표적 ID. 다른 모드와 삭제 후 빈 문자열 |
| `eoir.point_latitude_deg`, `eoir.point_longitude_deg`, `eoir.point_ellipsoid_height_m` | geo 지정점 °·°·WGS84 타원체고 m. 다른 모드에서는 0 |
| `eoir.tracking_status` | `manual`, `tracking`, `pointing`, `protected`, `limited`, `target_missing` |
| `eoir.limited` | 짐벌 각도 제한으로 auto/geo 점 방향을 그대로 적용할 수 없는지 |
| `eoir.target_in_fov` | auto/geo 점이 시야각 안에 있는지. 가림 없음의 보장이 아님 |
| `eoir.error_deg` | 카메라 실제 방향과 auto/geo 점 방향 사이 각도 ° |
| `eoir.frame_sequence`, `eoir.error` | 프레임 순번과 카메라/캡처 오류 |
| `targets[].id`, `targets[].kind` | 표적 ID·정적 소품 종류 |
| `targets[].latitude_deg`, `targets[].longitude_deg`, `targets[].ellipsoid_height_m`, `targets[].heading_deg` | 표적 위치 °·°·WGS84 타원체고 m, 북쪽에서 시계방향 방위 ° |
| `targets[].hit`, `targets[].hit_count`, `targets[].last_hit_shot_id` | 운영자가 판정한 명중 표시·누적 횟수·마지막 명중 연출 ID |

### wargame

| 그룹·필드 | 의미 |
| --- | --- |
| `time_s`, `error` | 누적 워게임 시간(s)·목록/연출/판정 오류. 비행 초기화로 시간을 지우지 않음 |
| `weapons[].id`, `weapons[].label`, `weapons[].mesh` | 현재 표시 모델의 ID·이름·weapons 기준 STL 경로 |
| `weapons[].animation_duration_s`, `weapons[].triangle_count` | 표시 시간(s)·STL 삼각형 수 |
| `shots[].id`, `shots[].weapon_id`, `shots[].target_id` | 연출·모델·표적 ID. 연출 ID `shot_N`은 워게임 reset 후에도 재사용하지 않음 |
| `shots[].phase` | `animating`, `awaiting_result`, `resolved`, `canceled` |
| `shots[].outcome` | `pending`, `hit`, `miss`, `canceled` |
| `shots[].elapsed_s`, `shots[].duration_s` | 연출 진행 시간·해당 이벤트에 저장한 전체 표시 시간(s) |
| `shots[].resolved_time_s` | 워게임 시계 기준 판정·취소 시각(s). 그전에는 0 |

## 13. 외부 Python 실행 예

외부 프로젝트의 Python 3.9 이상 가상환경에서 클라이언트를 설치한다. 클라이언트 실행에는 외부 패키지가 필요 없다.

```powershell
python -m pip install -e C:\src\Projects\UAVSim
```

아래를 외부 프로젝트의 `example_uavsim.py`로 저장하고 `python example_uavsim.py`로 실행한다. API 서버가 먼저 켜져 있어야 한다. 예제는 환경·초기 조건을 바꾸고 끝에 비행을 정지 상태로 둔다. API/Unreal 프로세스를 종료하지 않는다.

```python
import time
from uavsim_client import UAVSimClient, UAVSimError

sim = UAVSimClient("http://127.0.0.1:8765", timeout=30)
engine = sim.get_engine()
if engine["status"] == "stopped":
    sim.start_engine()
deadline = time.monotonic() + 180
while not sim.get_engine()["ready"]:
    engine = sim.get_engine()
    if engine["status"] in ("error", "external") or time.monotonic() >= deadline:
        raise RuntimeError(engine["message"])
    time.sleep(1)

sim.pause()
sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
sim.set_gust(east_mps=0, north_mps=0, up_mps=0)
sim.set_failures(engine_power_fraction=1, elevator_jammed=False,
                 aileron_jammed=False, rudder_jammed=False)
state = sim.reset()
if state["fault"]:
    raise RuntimeError(state["fault"])
sim.set_controls(**state["controls"])  # 현재 수평 트림 명령 유지
sim.set_landing_gear(down=False)
for _ in range(3):
    state = sim.step(1)
    if state["fault"]:
        raise RuntimeError(state["fault"])
print(state["aircraft"]["simulation_time_s"], state["gear"])

try:
    sim.set_controls(elevator_deg=30, aileron_deg=0, rudder_deg=0, throttle=0)
except UAVSimError as error:
    print(error.status, str(error))  # 범위 밖 입력: HTTP 400
```

`UAVSimError.status`에는 HTTP 상태가 들어간다. 연결 실패·타임아웃은 `URLError`/`OSError` 계열로 전달될 수 있으며, 한 쪽 공기각만 지정한 클라이언트 호출과 JSON으로 표현할 수 없는 숫자는 HTTP 전송 전에 `ValueError`를 낼 수 있다. `get_eoir_frame()`의 JPEG bytes를 제외한 메서드 반환값은 HTTP JSON 객체 그대로다.

항로 예제는 현재 계산 원점 근처의 점을 사용한다. 아래 좌표는 기본 제주 장면용이며 다른 지역 지도에서는 그 지도 좌표로 바꾼다.

```python
sim.set_initial_condition(latitude_deg=33.43, longitude_deg=126.52,
    ellipsoid_height_m=2600, heading_deg=0, pitch_deg=2.05, roll_deg=0, speed_mps=19)
sim.set_route([
    {"latitude_deg":33.435, "longitude_deg":126.52, "ellipsoid_height_m":2600, "speed_mps":19},
    {"latitude_deg":33.435, "longitude_deg":126.525, "ellipsoid_height_m":2600, "speed_mps":19},
], loiter_radius_m=200, clockwise=True)
state = sim.step(1)  # 전체 항로 완료가 아닌 첫 1초
print(state["navigation"])
sim.cancel_navigation()
```

자동 이착륙의 완결 예제와 실제 하네스는 [자동 이착륙](automatic-flight.md), 지상 수동 활주는 [지상 운용](ground-operations.md)을 따른다.

## 14. curl 예

Windows PowerShell 7에서는 `curl.exe`를 지정한다. JSON을 표준입력으로 보내 인용부호 손실을 피한다.

```powershell
curl.exe -sS http://127.0.0.1:8765/api/v1/engine
'{}' | curl.exe -sS -X POST http://127.0.0.1:8765/api/v1/engine/start -H 'Content-Type: application/json' --data-binary '@-'
# GET /engine의 ready=true를 확인한 다음 실행한다.
'{"running":false}' | curl.exe -sS -X PUT http://127.0.0.1:8765/api/v1/simulation -H 'Content-Type: application/json' --data-binary '@-'
'{"down":true}' | curl.exe -sS -X PUT http://127.0.0.1:8765/api/v1/gear -H 'Content-Type: application/json' --data-binary '@-'
'{"dt_s":0.1}' | curl.exe -sS -X POST http://127.0.0.1:8765/api/v1/step -H 'Content-Type: application/json' --data-binary '@-'
curl.exe -sS http://127.0.0.1:8765/api/v1/state
```

`-i`를 더하면 HTTP 상태 코드를 볼 수 있다. `curl` 성공 종료만으로 HTTP 400/409가 없었다고 판단하지 않는다. 해당 버전이 지원하면 `--fail-with-body`로 HTTP 오류를 명령 종료 코드에 반영할 수 있다.

## 15. 검사 기록과 문서 갱신

영상 품질·외부 연출 시점 변경 후 Unreal Development 빌드, [EO/IR API 28/28](../artifacts/eoir-quality-api-check.json)(HTTP 78회), [워게임 API 20/20](../artifacts/wargame-render-api-check.json)(104회·복구 오류 0건)이 통과했다. 1280×960·1° FOV·별도 센서 하늘, 외부 따라보기와 완료 시 복귀를 확인했다. [순차 JPEG 5회](../artifacts/eoir-quality-latency.json)의 중앙 응답은 약 27ms이며 브라우저 실측 FPS가 아니다. [UI 정적 검사](../artifacts/eoir-ui-static-check.json)도 통과했다. 네이티브 캡처와 재실행 방법은 [EO/IR 안내](eoir.md#범위와-검사-기록)·[워게임 안내](wargame.md#검사-기록)를 따른다. 아래는 이전 구현 단계의 기록이다.

워게임 연출·수동 판정 추가 후 Unreal Development 빌드와 실제 API **18/18**이 통과했다. [기록](../artifacts/wargame-api-check.json)은 HTTP 요청 84회·복구 오류 0건이며 모델 재읽기·시간 진행·판정 단계·표적 표시·잘못된 입력 거부와 초기화를 확인했다. Native 캡처에서 [정상](../artifacts/wargame-normal.jpg)→[명중 판정](../artifacts/wargame-hit.jpg)의 색 변화와 [별도 STL 연출](../artifacts/wargame-animation.png)을 직접 확인했다. 계산 코어를 바꾸지 않아 코어 검사는 반복하지 않았다. 재실행과 복구 범위는 [워게임 안내](wargame.md#검사-기록)를 따른다. 아래는 이전 짐벌·EO/IR·비행 구현 단계의 기록이다.

짐벌 4모드 추가 후 Unreal Development 빌드와 실제 모드 API **17/17**이 통과했다. [기록](../artifacts/gimbal-modes-api-check.json)은 API 요청 57회·복구 오류 0건이며 body_fixed·auto·geo·protect 전환, 독립 좌표 지향, 입력 거부·보호 영상 차단과 기존 API 호환을 확인했다. Python AST·UI JavaScript 구문도 통과했다. 코어 수학을 바꾸지 않아 CTest는 반복하지 않았다. 재실행 명령과 초기조건 복구의 한계는 [EO/IR 안내](eoir.md#범위와-검사-기록)를 따른다.

4모드 추가 전 EO/IR 구현에서 Unreal Development 빌드·Release CTest **9/9**, 실제 EO/IR API **27/27**이 통과했다. 당시 프레임은 640×480, FOV 범위는 5~90°였다. [기록](../artifacts/eoir-api-check.json)의 API 요청 76회에서 표적·지향·실제 step 중 추적·JPEG 헤더와 IR 필터·메인 카메라와 독립을 확인했다. [CCD 렌더](../artifacts/eoir-ccd.jpg). 아래는 이전 STL·기어 구현 단계의 기록이다.

STL 외형 추가 후 Unreal Development 빌드·Release CTest **8/8**, 실제 외형 API **28/28**이 통과했다. ASCII 108삼각형·1.8m 외형, binary STL, 단위·축·CG 변환, 실패 시 이전 외형·물리 상태 보존과 내장 기어 표시 선택을 확인했다. [외형 기록](../artifacts/visual-api-check.json), [실제 렌더](../artifacts/stl-aircraft-view.png). 재실행은 `scripts/check-visual-api.py --capture`이며 설정 원본 바이트를 복원하고 내장 외형·지상 주차·정지·1배속으로 복구한다. 상세 사용법은 [STL 외형 안내](aircraft-model.md)를 따른다.

2026-09-13 기어 추가 후 Unreal Development 빌드와 Release CTest **7/7**, 실제 기어 API 하네스 **21/21**이 통과했다. 제품 API 112회와 native 속성 조회 18회에서 3초 행정·중간 반전·재현·입력 거부·자동 착륙 전개·수납 접지 충돌·3개 힌지 표시를 확인했다. [기어 실행 기록](../artifacts/gear-api-check.json), [전개 화면](../artifacts/gear-down.png), [중간 화면](../artifacts/gear-half.png), [수납 화면](../artifacts/gear-up.png).

20배속 자동 왕복에서 시뮬레이션 34.83초의 이륙 hold 상태에 수납 잠금을 확인했고, 착륙 명령 뒤 전개 잠금으로 복귀하여 266.39초에 주차·정지했다. 실제 소요는 약 13.359초였다. [왕복 기록](../artifacts/gear-playback.json), [착륙 화면](../artifacts/gear-automatic-landed.png). 이후 지상 초기화·전개·정지·1배속으로 복구했다. 브라우저 클릭은 검사하지 않았다.

```powershell
# 실행 중인 기본 Thor·가상 제주 활주로에서 입력과 위치를 변경하는 검사
.\.venv\Scripts\python.exe scripts\check-gear-api.py --impact
```

기존 실행 근거는 [개발 계획](development-plan.md)의 자동 운항·지상·실속·항법 기록을 따른다. 영상 품질·외부 연출 시점 변경 후 이 매뉴얼은 소스의 제품 경로 40개·공개 Python 메서드 41개·매핑 필드 이름 118개와 대조하여 누락이 없음을 확인했다. 문서의 실행 예제 자체를 별도로 실행했다고 주장하지 않는다. 위 결과는 개발 검사이며 실기체 성능의 정식 검증이 아니다.

2026-11-18: 경로/메서드 추가 없이 귀환 단계·진단·참고 경로와 실제 지상 출력을 확장했다. endpoint·클라이언트·UI·기록기를 함께 갱신했다. [관련 실제 API173검사](../artifacts/return-contract-check.json)·CCD JPEG·실패 보존·Thor 재읽기와 [최종 연속 왕복](../artifacts/return-flight-final/result.json)이 통과했다. 공개 Python 메서드42개 문서 누락0·변경 문서 로컬 링크 누락0은 [별도 기록](../artifacts/return-doc-check.json)을 따른다. 브라우저 직접 조작·실기체 성능 시험은 수행하지 않았다.

API를 바꿀 때 endpoint, Python 클라이언트, 해당 Web UI, 이 매뉴얼의 예제·상태·범위·부작용·검사 기록을 함께 갱신한다. 공개 메서드나 경로를 추가·삭제할 때 소스와 이 문서의 목록을 대조하고 로컬 문서 링크를 확인한다. 이는 개발 변경과 함께 수행하는 규칙이며 정기 실행 자동화가 아니다.
