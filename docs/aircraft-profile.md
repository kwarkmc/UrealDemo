# 기체 파일·작동기·돌풍·고장

## 2026-11-17: 프로필 v2와 C208B 연구 구성

기존 v1/50개 물리 필드와 Thor의 2600m/19m/s 기본 동작을 유지한다. v2는 같은 `parameters`에 `operating`과 `support_message`를 추가한다. [전체 합성 예제](../tests/fixtures/synthetic-large.json)는 **수치 전달 검사 전용 1000kg 시험체이며 C208B나 실측 대형기 모델이 아니다**. 아래 기존 설명의 19m/s·2600m는 v1 기준이다.

| v2 operating 필드 | 의미·검증 |
| --- | --- |
| `initial_speed_mps`, `initial_height_m` | 기본 초기 TAS와 WGS84 타원체고. 최소/최대 안에 있어야 함 |
| `minimum_speed_mps`, `maximum_speed_mps` | 초기화 및 시간 진행 TAS 범위. 0~250m/s 수치 경계 안에서 최소<최대 |
| `minimum_height_m`, `maximum_height_m` | 초기화/시간 진행 높이 범위. -500~20000m 안에서 최소<최대 |
| `maximum_alpha_deg`, `maximum_beta_deg` | 받음각·옆미끄럼 절댓값 한계. 각각 (0,180]°, (0,89]° |
| `ground_supported`, `navigation_supported`, `automation_supported` | 선택한 모델의 지원 여부. 자동 운항 true에는 지상·항로도 true 필요 |

`schema_version: 2`, 정확히 위 8개 숫자·3개 bool의 `operating`, 1~512자 `support_message`가 필요하다. 숫자에 bool/문자열·비유한 값을 쓰거나 필드 누락/추가·잘못된 관성·불능 트림이면 기존 적용 모델과 물리 상태를 보존하고 거부한다. 선택 파일 편집과 실제 재읽기 적용을 구분한다. 공력계수 정의의 `reference_speed_mps`와 초기 속도는 별개다.

로드·재읽기·reset·Web UI 지역 이동은 적용 프로필의 초기 조건을 사용한다. 명시적 초기조건의 TAS·높이·공기각도는 적용 범위로 검사한다. v2의 시간 진행은 RK 중간 단계에서도 범위를 검사해 이전 유효 상태에서 fault·정지하며 속도/각도를 몰래 잘라 맞추지 않는다. 시간 진행 높이 검사는 ISA와 같은 `origin_altitude_m + ENU Up` 근사이고, 외부 위치 API는 계속 WGS84 타원체고다. v1의 기존 전각 개발 거동에는 v2 시간 진행 범위를 소급 적용하지 않는다.

지원 bool이 false인 운용은 API와 Unreal 양쪽에서 거부한다. 합성 공중 시험체는 지상 입력·지상 초기화·기어·항로·자동 이착륙을 지원하지 않는다. 지면 CG 법선 거리가 `span_m + 현재 지상속도 크기*계산 간격` 이하이면 미지원 접촉 전에 정지한다. 멀리서는 Thor 접촉 모델을 사용하지 않는다. 이 미지원 경계는 접촉 모델을 제공하지 않는다. 지원 모델은 아래 configuration의 접촉점을 사용한다.

B 당시 검사 결과: Release CTest **9/9**, UE 5.8.4 Development 빌드 성공, 실제 API **30/30** 통과. [기록](../artifacts/airframe-api-check.json), [하네스](../scripts/check-airframe-api.py). 하네스는 선택·임시 파일과 일정 바람을 복원하고 원래 프로필의 공중 트림·정지로 끝난다. 원래 위치·비행 시간은 복원하지 않으므로 진행 중인 실험에서 실행하지 않는다. C208B 적용·정식 시험 완료는 아니다.

비행역학의 기체 제원·공력계수·추진 및 작동기 응답 값을 JSON으로 읽는다. Thor는 `aircraft/thor.json`, 별도 연구 모델은 `aircraft/c208b-research.json`이다. 외부 비행역학 구현을 불러오는 기능이 아니며 자체 코어가 이 값으로 힘·모멘트·응답을 계산한다.

## v2 configuration

2026-11-18: 필수18개 숫자+bool 구성에 **선택 숫자15개**를 제공한다. 누락하면 Thor 기본값이며 알 수 없는 이름·잘못된 타입·범위 위반·바퀴와 동체 간섭은 재읽기를 거절하고 기존 모델을 보존한다. [항법·추진·바퀴 통합 수식/검사](navigation-physics.md)를 따른다.

| 선택 필드 | 단위·입력 범위 / Thor 기본값 / C208B 연구값 |
| --- | --- |
| `propeller_nominal_rpm` | 회/min, 0~10000 / 0(기존 모델) / 수납식 연구1900. 양수는 shaft_power_w>0 필요. 실제 조속기 RPM 아님 |
| `propeller_inertia_kg_m2` | kg·m², .1~1000 / 1 / 수납식 연구8 |
| `propeller_spin_direction` | 무차원 -1 또는+1, 기본/연구+1. 동적 모델에서 다른 값은 거절 |
| `tire_cornering_per_rad` | 1/rad, 0~100 / 0(기존 접촉) / 수납식 연구8 |
| `wheel_radius_m` | m, .005~2 / .03 / .32. `cg_height_m-body_half_height_m`보다 작아야 함 |
| `propeller_radius_m` | 표시 반경 m, .01~5 / .15 / 1.3 |
| `thrust_z_m` | CG 기준 FRD 아래 방향 m, -5~5 / 0 / .05. `My=z*T`에 연결 |
| `rolling_friction`, `brake_friction`, `lateral_friction` | 무차원, 0~1 / 0~2 / .01~2. 기본·연구값 모두 .025/.7/.8 |
| `impact_limit_mps` | 법선 충돌 기준 m/s, .1~10 / 4 / 4 |
| `steering_tau_s`, `steering_rate_deg_s` | 초 0~5 / 0 / .25; °/s .1~360 / 120 / 30. tau=0은 기존 즉시 적용 |
| `steering_limit_accel_mps2` | 연구용 조향 보호값 m/s², 0~10 / 0(해제) / 4 |
| `brake_tau_s` | 초 0~5 / 0(즉시) / .2. 주차 브레이크는 즉시 잠금 |

연구값은 모두 **개발 가정**이다. 조향 목표 제한은 `atan(a_limit*wheelbase/max(Vx²,1))`, 조향은 기존 제한율/1차 지연 식, 브레이크는 `b_cmd+(b_old-b_cmd)*exp(-dt/tau)`다. 명령과 실제값은 `ground.steering_deg/brake`, `ground.applied_steering_deg/applied_brake`로 구분한다. `thrust_z_m`의 모멘트는 트림 계산에도 포함한다. 동적 추진을 켜면 프로펠러 반경은 운동량 디스크 면적에도 사용하고, propeller_efficiency는 축동력→이상 디스크 동력의 손실계수로 사용한다. 기존 추력 식은 이전 효율 의미를 유지한다.

[C208B 연구 모델](c208b-research.md)은 사용자 승인한 개발 가정을 포함하며 실제 성능 미검증이다. 선택적 `configuration`을 생략하면 기존 Thor 구성값을 쓴다. 제공하면 아래 **숫자18개와 bool1개 전부**를 넣고, 알 수 없는 필드를 허용하지 않는다. v1은 configuration을 받지 않는다. 원본 [전체 JSON](../aircraft/c208b-research.json)을 복사해 단위와 provenance를 함께 확인한다.

| 필드 | 의미·수치 입력 범위 |
| --- | --- |
| `retractable_gear` | bool; false는 수납 거부, 항상 완전 전개 잠금 필요 |
| `nose_x_m`, `main_x_m`, `half_track_m`, `cg_height_m` | CG 기준 FRD 접촉점: nose(x,0,h), main(x,±track,h). 각각 .01~20, -20~-.01, .01~20, .05~10m |
| `body_half_length_m`, `body_half_height_m` | 동체 대표 충돌점 반치수 .1~40m/.01~5m. 반높이는 CG 지상 높이보다 작아야 함. 날개끝 ±span/2 |
| `route_min_mps`, `route_max_mps` | 항로·이륙 목표 TAS, 각각5~200, 최소≤최대, 지원 시 operating TAS 안 |
| `landing_min_mps`, `landing_max_mps` | 접근·플레어 TAS, 각각5~200, 최소≤최대, 자동 운항 시 route 범위 안 |
| `controller_time_scale` | 개발 제어기의 응답 시간/패턴 배율 .5~10, 실제 자동조종장치 식별값이 아님 |
| `shaft_power_w`, `propeller_efficiency` | 0~5MW, .01~1. power=0은 기존 Thor 추력 식. 양수는 효율×밀도비×출력/V와 정지 추력의 작은 값 |
| `maximum_flap_deg`, `flap_rate_deg_s` | 대칭 플랩0~40°, 최대각도0은 미지원. 전개 속도.1~60°/s |
| `flap_lift_per_rad`, `flap_drag_per_rad2`, `flap_pitch_per_rad` | CL 증가/rad (0~5), CD 증가/rad² (0~5), Cm 증가/rad (-2~2). 동일 attached 식·실제 플랩각을 사용 |

새 구성도 유한성·양의 관성·트림·교차 범위 검사를 통과한 뒤 원자적으로 적용한다. 재읽기/초기화는 플랩0°·기어 전개이며 실패 시 기존 상태를 보존한다. 지상에서 작은 수치 속도(<1m/s)의 alpha/beta는 무의미하므로 시간 진행의 각도 범위 검사에서 제외한다. 공중 초기화는 여전히 TAS≥1 및 트림 가능 조건이다. 전각 수치 범위는 공력 정확도 보증이 아니다.

C~F 연구 범위의 Release CTest **10/10**, UE5.8.4 빌드와 [실제 연구 API 검사](../artifacts/c208b-api-check.json)가 통과했다. B의 30개 회귀 검사와 실제 성능 검증을 구분한다.

## 파일 선택과 적용

`aircraft/active.json`은 같은 폴더의 프로필 파일 이름을 지정한다.

```json
{"profile":"thor.json"}
```

새 기체는 `thor.json`을 복사한 뒤 근거가 있는 수치를 수정하고 `active.json`에서 선택한다. 프로필 전체 형태는 다음과 같다. 아래 `parameters`의 빈 객체는 구조 설명용이며 실제 파일에는 원본의 모든 필드가 필요하다.

```json
{
  "schema_version": 1,
  "id": "my-aircraft-development",
  "parameters": {},
  "provenance": {"status": "development assumptions; replace with measured data"}
}
```

Web UI에서 일시정지한 뒤 **기체 파일 다시 읽기**를 누르거나 Python `sim.reload_aircraft_profile()`을 호출한다. HTTP는 `POST /api/v1/aircraft/reload`와 `{}`다. 성공하면 현재 계산 원점의 타원체고 2600m·TAS 19m/s 수평 트림으로 초기화하고 시간·거리·자동항법·돌풍·고장을 초기화하며 정지한다. 일정 바람은 유지한다. 준비 가능한 19m/s 트림이 없는 프로필은 거부한다.

선택 파일은 1KiB 이하, 프로필은 64KiB 이하이며 `profile`은 `aircraft` 바로 아래 JSON 파일 이름만 허용한다. 경로 구분자·상위 경로 이동은 허용하지 않는다. `schema_version`은 1, `id`는 비어 있지 않은 80자 이하 문자열이다. `parameters`는 모든 정의된 숫자 필드를 정확히 포함해야 한다. 누락·알 수 없는 매개변수·비유한 숫자·잘못된 관성·범위 밖 값은 실패한다. `provenance`는 사람이 근거를 기록하는 메타데이터이며 계산값의 정확성을 자동 판정하지 않는다.

응답 `airframe`에는 `id`, `profile`, `fingerprint`, `error`가 있다. fingerprint는 Unreal이 읽은 JSON 텍스트의 `sha1:` 식별값이며 보안 서명이나 검증 합격 표시가 아니다. 일시정지 상태의 잘못된 파일 재읽기는 HTTP 409와 오류를 반환하고 기존 모델·상태를 유지한다. 재생 중 재읽기도 거부된다.

## 매개변수와 단위

수치 필드는 50개다. 정확한 필드·기본값은 [thor.json](../aircraft/thor.json), 허용 범위와 관성 검사는 [AircraftParameters.h](../Source/FlightCore/AircraftParameters.h)에 둔다. JSON에서 일부 값만 덮어쓰는 병합 방식은 지원하지 않는다.

| 필드 그룹 | 의미·단위 |
| --- | --- |
| `reference_speed_mps`, `reference_pitch_rad` | 공력 기준 속도(m/s), 기준 받음각으로 사용하는 각도(rad) |
| `mass_kg`, `wing_area_m2`, `span_m`, `chord_m` | 질량(kg), 면적(m²), 날개폭·평균 시위(m) |
| `ix`, `iy`, `iz`, `ixz` | 기체 FRD축 관성(kg·m²). 관성 행렬 xz 원소는 `-ixz` |
| `lift_ref`, `lift_alpha`, `lift_elevator`, `lift_pitch_rate` | 양력 기준값과 받음각·엘리베이터·무차원 피치율 미계수 |
| `pitch_alpha`, `pitch_speed`, `pitch_rate`, `pitch_elevator` | 피칭 모멘트 계수 |
| `side_beta`, `side_roll_rate`, `side_yaw_rate`, `side_aileron`, `side_rudder` | 측력 계수 |
| `roll_beta`, `roll_rate`, `roll_yaw_rate`, `roll_aileron`, `roll_rudder` | 롤 모멘트 계수 |
| `yaw_beta`, `yaw_roll_rate`, `yaw_rate`, `yaw_aileron`, `yaw_rudder` | 요 모멘트 계수 |
| `drag_zero`, `induced_drag`, `sideslip_drag` | 개발 항력 극선·옆미끄럼 항력 계수 |
| `stall_onset_rad`, `fully_separated_rad`, `separation_time_s`, `reattachment_time_s` | 박리 전환 각도(rad), 박리·재부착 시간상수(s) |
| `plate_lift`, `plate_drag`, `plate_pitch`, `separated_control_fraction`, `separated_damping_fraction` | 박리 후 평판 근사 및 조종·감쇠 잔여 비율 |
| `maximum_thrust_mps2`, `constant_power_above_mps` | 최대 추력/질량(m/s²), 일정 출력 근사 전환 TAS(m/s) |
| `engine_tau_s`, `servo_tau_s`, `servo_rate_deg_s` | 엔진·서보 시간상수(s), 조종면 각속도 제한(°/s) |

공력계수는 무차원이며 각도 미계수에는 rad를 사용한다. 각속도 항은 `pb/(2V)`, `qc/(2V)`, `rb/(2V)`로 무차원화한다. API의 조종 각도는 °이므로 경계에서 변환한다. 양력·항력·모멘트 계산식과 Thor 수치의 근거는 [flight-model.md](flight-model.md)를 따른다.

질량·면적·관성 등 양수 제약, 관성 행렬 조건, `fully_separated_rad > stall_onset_rad` 등을 검사한다. 시간상수는 .001~10s, 서보 속도는 1~1000°/s를 허용한다. 이러한 입력 범위는 수치 검사의 범위이며 해당 크기의 실제 기체에 유효함을 뜻하지 않는다.

현재 지면 접촉점·마찰·충돌 기준과 기어 전개 시간 3초·항력 보정 .012는 `GroundContact.h`에 별도로 남아 있다. 기어는 세 개가 함께 전개·수납하며 완전 전개 시에만 바퀴가 지지한다. 내장 표시 외형과 접촉 모델은 1.27m 개발 기체다. STL 외형은 [별도 설정](aircraft-model.md)으로 교체할 수 있지만 표시 메시나 프로필의 날개폭을 바꿔도 접촉 모델은 자동 재구성되지 않는다. `reload_aircraft_visual()`은 물리 상태를 유지하는 외형 명령이며, 공중 트림을 초기화하는 프로필 재읽기와 다르다. [기어 API와 상태](api-reference.md#7-지상-조종과-랜딩기어)를 따른다.

## 작동기와 추진 응답

기존 `controls`는 명령값이다. 새 `actuators`에는 실제 `elevator_deg`, `aileron_deg`, `rudder_deg`, `throttle`, `thrust_n`을 반환한다. 각도·스로틀을 직접 순간 이동시키는 대신 시간 진행에 따라 실제 출력이 명령을 따른다. 지상·공중 초기화는 초기 조건에 맞는 작동기 상태를 함께 설정한다.

엔진 스풀은 `x(t)=u+(x0-u)*exp(-t/engine_tau_s)`다. 정상 프로필 시간상수 .3s에서 0→1 명령을 주고 .3s 진행하면 스풀은 약 .6321이다. 실제 추력은 이 값, 개발 추진식과 엔진 출력 비율로 계산한다. RPM·전압·전류·배터리·프로펠러 토크를 재현한 실측 모터 모델은 아니다.

서보는 1차 지연과 속도 제한을 함께 사용한다. 기본 시간상수 .08s, 최대 120°/s이며 큰 오차에서는 먼저 속도 제한으로 움직이다가 지연 응답으로 이어진다. 위치 명령 한계는 ±25°다. 백래시·데드밴드·전원·실측 히스테리시스는 없다. 값과 시간상수는 개발 가정이다.

## 돌풍

`PUT /api/v1/gust`는 `east_mps`, `north_mps`, `up_mps` 최대 성분(각 ±15m/s), `delay_s` 0~600s, `duration_s` .1~120s를 모두 받는다. 현재 시뮬레이션 시간에 지연을 더해 시작 시간을 정한다. 세 최대 성분을 0으로 설정하면 바람에 더해지는 돌풍을 제거한다.

```python
sim.set_gust(east_mps=2, north_mps=1, up_mps=.5, delay_s=.2, duration_s=1)
# 정지 상태에서 .7s 진행하면 돌풍 최고점: .2s 지연 + .5s
state = sim.step(.7)
print(state["disturbance"])
```

돌풍은 시간 영역의 단일 펄스 `0.5*(1-cos(2*pi*t/duration))`로 일정 ENU 바람에 더한다. 시작·종료에서 0, 중간에서 지정 최대값이다. 공간 난류장·확률 난류·인증용 돌풍 시험을 구현한 것은 아니다. 같은 시뮬레이션 시간·입력에서 재현된다.

`disturbance`의 `gust_east_mps`, `gust_north_mps`, `gust_up_mps`는 설정한 최대값이고 `gust_start_s`, `gust_duration_s`는 시간 설정이다. `effective_wind_east_mps`, `effective_wind_north_mps`, `effective_wind_up_mps`는 현재 순간의 합성 바람이다. 모두 m/s 또는 s이며 양수는 동·북·위쪽으로 부는 바람이다. 초기화로 시뮬레이션 시간을 되돌릴 때는 남은 돌풍 설정도 확인한다. 프로필 재읽기 외의 초기화가 돌풍을 자동 해제한다고 가정하지 않는다.

## 고장

`PUT /api/v1/failures`는 `engine_power_fraction` 0~1, `elevator_jammed`, `aileron_jammed`, `rudder_jammed` bool을 모두 받는다. 누락·추가 필드와 숫자로 대신 보낸 bool은 거부한다.

```python
sim.set_failures(engine_power_fraction=0, elevator_jammed=True,
                 aileron_jammed=False, rudder_jammed=False)
# 고장 해제
sim.set_failures(engine_power_fraction=1, elevator_jammed=False,
                 aileron_jammed=False, rudder_jammed=False)
```

엔진 출력 비율은 현재 개발 추력에 즉시 곱한다. 0이면 스풀 출력이 남아 있어도 추력은 0이다. 고착은 false→true가 되는 순간의 실제 조종면 각도를 잡아 유지한다. true를 다시 보내도 고착 각도를 다시 잡지 않는다. 고장을 해제하면 현재 명령을 향한 작동기 응답이 재개되며 비행 위치·자세는 초기화하지 않는다. 네 설정값은 응답 `disturbance`에도 포함한다.

고장 확률·고장 발생 스케줄러·고장 탐지 센서·중복 제어기 모델은 없다. 외부 프레임워크가 명시적 시간 진행과 API 호출로 시험 시나리오를 구성한다. 단일 제어기의 이륙 중단·복행 기능은 모든 고장의 자동 복구를 의미하지 않는다.

## 검사 상태

[check-automatic-api.py](../scripts/check-automatic-api.py)는 작동기 해석해, 서보 속도 제한·고착, 엔진 추력 상실, 돌풍 파형·재현, 잘못된 프로필의 상태 유지와 시간상수 교체를 확인하도록 작성했다. 원래 파일 바이트를 보존하고 검사 종료 시 복원한다. 실행 방법과 복구 상태는 [automatic-flight.md](automatic-flight.md#실행-하네스)를 따른다.

2026-09-13 최종 Unreal Development 빌드와 Release CTest 7/7, 실제 자동 운항·시스템 하네스 27/27 항목이 통과했다. 366회 API 요청에서 엔진·서보 응답, 고착·출력 상실, 돌풍 끝점·최고점·재현을 확인했다. 음수 질량 파일은 기존 모델·물리 상태를 보존하며 거부됐고, 엔진 시간상수를 .3→.6s로 바꾸었을 때 .3s 후 스풀 출력이 해석해 `1-exp(-.5)`와 일치했다. 원래 프로필·선택 파일과 기존 임시 파일의 바이트 복원도 확인했다. [실행 기록](../artifacts/automatic-api-check.json).

파일 로딩·수치 회귀 검사와 실제 기체 데이터의 정식 검증은 별도다. 다음 데이터 보정·센서·외부 자동조종장치 작업은 [개발 계획](development-plan.md)을 따른다.
