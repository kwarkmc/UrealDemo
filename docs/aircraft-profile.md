# 기체 파일·작동기·돌풍·고장

비행역학의 기체 제원·공력계수·추진 및 작동기 응답 값을 JSON으로 읽는다. 현재 파일은 Thor 공개 미계수와 개발 가정을 구분해 기록한 `aircraft/thor.json`이다. 외부 비행역학 구현을 불러오는 기능이 아니며 자체 코어가 이 값으로 힘·모멘트·응답을 계산한다.

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
