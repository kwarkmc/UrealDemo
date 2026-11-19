# 비선형 비행역학 v2 — 근거와 적용 범위

2026-11-18: 현재 수납식 연구 구성에는 [동적 추진·협조 제어·바퀴별 지면/타이어](navigation-physics.md)를 추가했다. 로터 각속도·축 토크/동력·운동량 추력·자이로 반작용은 실제 성능 미검증의 개발 모델이다. 아래 Thor/기존 고정기어 연구 모델의 추력 식과 구분한다. 계산한 RPM을 표시에도 사용하며 동적 엔진컷은 회전 에너지 감속을 포함한다.

2026-11-17: [C208B 연구 모델](c208b-research.md)을 별도로 추가했다. 실제 성능 미검증이며 아래 기존 Thor 수치와 구분한다. 현재 기체별 범위·형상·기어·항로 속도는 API `airframe`을 따른다. 가상 제주 시험 활주로는 현재2000×60m다.

2026-11-17: 기존 Thor 수식·적분을 유지하면서 [프로필 v2의 초기화/공중 범위 검사](aircraft-profile.md)를 추가했다. 프로필 schema v2와 이 문서 제목의 비선형 모델 v2는 다른 버전 개념이다. 합성 1000kg/65m/s 시험체는 수치 검사 전용이며 C208B 구현은 아니다. Release CTest 9/9·Unreal 빌드·실제 초기화 API 30/30 통과는 아래 Thor의 실측 정확도나 C208B 성능 검증을 확장하지 않는다.

`Source/FlightCore/FixedWing.h`, `Aerodynamics.h`, `AircraftParameters.h`는 자체 작성한 6자유도 개발 모델이다. 실제 조종면 위치와 엔진 응답이 힘·모멘트를 만들고, 중력·관성·속도·자세·위치를 함께 적분한다. 외부 비행역학 구현을 가져오지 않았다. 계산식 식별자는 `thor_nonlinear_6dof_v2`, 기본 기체 설정 ID는 `thor-development`다. 실제 Thor의 전 비행영역 모델이나 상용 시뮬레이터와 동등한 정확도가 검증된 모델이라는 뜻은 아니다.

## 수치 근거

기준은 Dorobantu 외, *System Identification for Small, Low-Cost, Fixed-Wing Unmanned Aircraft*, DOI 10.2514/1.C032065의 [UMN 공개 32쪽 원고](https://dept.aem.umn.edu/~mettler/Courses/AEM%205333%20%28spring%202013%29/AEM5333%20CourseDropbox/Week%207%20Identification/Ultrastick%20Identification/2012_AIAA_JA_SYSID.pdf)다.

- Table 1: 질량 1.959 kg, 면적 .31 m², 날개폭 1.27 m, 평균 시위 .25 m. `Ix=.089, Iy=.144, Iz=.162, Ixz=.014 kg·m²`; 관성 행렬 xz 성분은 `-Ixz`다.
- Eq. (25), (26)의 약 19 m/s 부근 힘·모멘트 미계수를 무차원 공력계수로 재구성했다. 종방향 장주기 항과 `Z_delta_e`는 원고에서도 초기 추정값을 유지한다. 원고의 기준 스로틀 70%를 현재 추진 모델에 이식하지 않는다.
- 재구성 기준은 `V0=19`, `rho0=1.225`, `alpha0=atan2(.21,9.80)`이다. alpha0는 중력 항으로부터 역산한 개발 기준이며 측정 실속각이 아니다. 운동학이 섞인 `Xq=-.36+w0`, `Zq=16.74-u0`, `Yp=.46-w0`, `Yr=-18.21+u0`를 먼저 분리했다.
- 힘계수는 질량과 기준 `qbar*S`, 모멘트계수는 관성과 `qbar*S*b` 또는 `qbar*S*c`로 환산한다. `d/dalpha=-w0*d/du+u0*d/dw`, 각속도는 `pb/(2V), qc/(2V), rb/(2V)`를 쓴다. 환산값과 개발 가정은 `FlightModelParameters`에 모았다. 이 재구성은 원고 전체 A/B 행렬을 동일하게 유지하는 작업은 아니다.

실속·표준대기·추진 관련 1차 문헌과 채택하지 않은 교육용 수치는 [stall-references.md](stall-references.md)에 구분했다. Thor의 측정 실속 곡선은 확인한 자료에서 찾지 못했다.

## 기체 설정

`aircraft/active.json`의 `{"profile":"thor.json"}`이 같은 폴더의 기체 파일을 선택한다. `thor.json`은 `schema_version:1`, `id`, `parameters`, `provenance`를 포함한다. `AircraftParameters.h`의 `parameter_fields`에 등록된 50개 숫자를 모두 지정하며, 누락·알 수 없는 키·비유한 값·수치 범위 오류를 거부한다. 질량·날개 면적/폭/시위·관성·공력계수·추진·응답 시간과 조종면 속도 제한이 이 설정에 들어간다. 관성은 양의 정부호이고 주축 관성의 삼각조건도 만족해야 한다. 각 범위는 입력 오류를 막는 개발용 경계이며 실기체 운용 한계가 아니다.

Unreal은 정지 상태의 `POST /api/v1/aircraft/reload`에서 파일을 읽고 2600m/19m/s 초기 트림이 존재하는지 확인한 후 적용한다. 성공하면 그 공중 트림으로 초기화하고 자동조종·고장·돌풍을 지우며 일정 바람은 유지한다. `airframe`에 설정 ID·파일·지문·오류를 공개한다. 코어는 파일을 직접 읽지 않고 `Environment.model`의 읽기 전용 매개변수 객체를 참조한다. 서로 다른 기체 상태는 서로 다른 객체를 사용할 수 있고 공유 기본값을 수정하지 않는다. 완전 전개 기어 접촉점·동체 충돌점은 `GroundContact.h`의 1.27m 개발 프록시로 유지한다. 내장 표시 외형도 같은 기준이며 [STL 설정](aircraft-model.md)으로 외형만 교체할 수 있다. 공력 span 변경과 STL 교체는 서로 독립이며 어느 쪽도 접촉 모델을 자동 재구성하지 않는다.

## 공력과 실속

기체축은 전방/오른쪽/아래(FRD), 위치·바람은 동/북/위(ENU)다. `Aircraft.body_velocity_mps`는 **지상 속도를 기체축으로 표현한 값**이다. 바람을 기체축으로 변환하여 빼고 공기 상대속도를 구한다.

```text
Va_body = Vground_body - R_body_from_ENU * wind_ENU
Va = length(Va_body)
alpha = atan2(w_air, u_air); beta = asin(v_air / Va)
qbar = rho * Va² / 2
L = qbar*S*CL; D = qbar*S*CD; Y = qbar*S*CY
```

양력·측력은 상대속도와 수직이고 항력은 그 반대 방향이다. `dot(F_aero, Va_body)=-D*Va`, `D>=0`를 검사한다. 속도가 0이면 공력과 공력 모멘트는 0이 되고 중력과 추력에 의한 운동은 계속된다. 각속도 무차원화의 분모에는 1 m/s 하한을 둔다.

종방향 평면 속도 `hypot(u_air,w_air)`가 1mm/s 이하이면 alpha를 0으로 정의한다. 순수 횡풍과 주기 접촉의 수치 잔차에서 의미 없는 180° 받음각을 만들지 않기 위한 정의다. beta는 전체 공기속도가 1mm/s 이하일 때 0이다. 이 문턱은 실속 속도 기준이 아니다.

아래는 **측정값이 없는 개발 가정**이다. 받음각 절댓값 12°부터 20°까지 `t=clamp((abs(alpha)-12°)/8°,0,1)` 및 `s_eq=3t²-2t³`로 정상 박리량을 정한다. 실제 상태 `s`는 `ds/dt=(s_eq-s)/tau`로 적분한다. 박리가 증가할 때 tau=.12초, 재부착은 .60초다. 이 단일 지연 상태는 정식 동적 실속 모델의 검증이나 스핀 재현을 의미하지 않는다.

```text
CL_attached = CL_ref + CL_alpha*(alpha_attached-alpha0)
              + CL_elevator*delta_e + CL_pitch_rate*qc/(2V)
CL = (1-s)*CL_attached + s*sin(2*alpha)
CD = CD0 + (1-s)*k*CL_attached² + 2*s*sin(alpha)² + .60*sin(beta)²
alpha_attached = clamp(alpha, -20°, 20°)
```

`CL_ref=.2778231`, `CL_alpha=5.6618537/rad`는 기존 미계수와 기준 힘에서 재구성했다. `CD0=.1125421`, `k=.0226848`은 이전 모델의 암묵적인 순항 항력과 국소 기울기를 유지하도록 만든 개발 항력 극선이다. 기준 항력이 크고 실측 보정 전이다. `sin(2alpha)` 양력과 `2sin²(alpha)` 항력 연결은 현재 채택한 평판 근사이며, 참고한 BYU 문헌의 다른 평판 식과 동일하다고 주장하지 않는다.

박리가 커지면 조종 모멘트 효과는 기존의 15%, 각속도 감쇠는 40%까지 줄어든다. 실속 후 피칭 모멘트의 `-.35*sin(2alpha)`도 개발 가정이다. 좌우 날개 개별 모델·프로펠러 후류·비대칭 날개 실속·버핏·스핀·Reynolds/Mach 보정은 구현하지 않았다.

`stall_warning`은 |alpha|≥9.6° 또는 박리량>.15, `stalled`는 박리량≥.5다. 단순한 고정 저속 임계값으로 실속을 판정하지 않는다. `load_factor`는 `-F_aero_body.z/(m*g)`인 기체 수직 공력 하중이다. 중력을 더한 관성가속도가 아니며 경로 법선 하중과도 항상 같지는 않다. 지면 접촉 반력은 이 값에 포함하지 않고 `ground.normal_force_n`으로 별도 공개한다.

## 대기·추진·수치 적분

표준중력은 9.80665 m/s²다. U.S. Standard Atmosphere의 기하고도→지오포텐셜 변환 후 0~11 km 감률층과 11~20 km 등온층을 사용한다. 대기고도 입력은 `environment.origin_altitude_m + position_enu_m.z`다. Unreal은 국소 계산 원점을 초기 위경도·타원체고 0m로 둔다. 현재 대기 계산에서는 **이 절대 ENU 높이를 기하고도의 근사로 사용**하며 WGS84 타원체고와 정표고의 지오이드 차이는 적용하지 않는다. 지형 DEM의 지오이드 보정과는 별개다. 실기상·습도·확률 난류·풍속 고도 분포는 없으며 아래의 결정적 돌풍만 지원한다.

기본 추력은 `T=m*8*actual_throttle*engine_power_fraction*min(1,30/max(Va,1))` N이다. 30m/s 초과에서 유효 추진 출력이 일정해지는 근사이며 RPM·토크·프로펠러 효율·밀도 보정의 실측 모델은 아니다. `actual_throttle`은 명령을 시간상수 .30초로 따르는 0~1 엔진 상태다. 상태별 수평 트림에서 엘리베이터와 스로틀을 함께 풀기 때문에 모든 고도에서 스로틀 .5가 트림은 아니다. 예를 들어 무풍 2600m/19m/s의 기본 설정은 피치 약 2.053°, 엘리베이터 -.597°, 스로틀 .391이다.

각 비행 조종면은 `d(actual)/dt=clamp((command-actual)/tau, ±rate)`를 따른다. 기본 `servo_tau_s=.08`, `servo_rate_deg_s=120`이며 면 위치는 ±25°다. 속도 제한 구간의 선형 응답과 이어지는 지수 응답을 해석적으로 계산하여 초과 이동을 막고 RK4의 각 중간 시각에 적용한다. [NASA GTM 시뮬레이션 문헌](https://ntrs.nasa.gov/api/citations/20110014509/downloads/20110014509.pdf)은 1차 지연과 위치·속도 제한이라는 구조의 참고다. 현재 엔진·서보 시간상수와 제한값은 Thor에서 측정되지 않은 개발 가정이다. 트림/지상 초기화는 실제 입력도 초기화하므로 트림에서 불필요한 시동 과도응답을 만들지 않는다. 직접 만든 `Aircraft`가 `actuator_initialized=false`이면 호환을 위해 첫 명령으로 실제 입력을 한 번 초기화한다. 명시적 응답 시험은 `actual_controls`와 `actuator_initialized=true`를 지정한다.

강체 방정식은 `Vdot=F/m+gravity-omega×V`, `I*omega_dot=M-omega×Iomega`다. 자세는 RK4 내부에서 정규화 quaternion으로 적분하고 API용 Euler 각은 결과에서 계산한다. 이전의 전방 속도 양수·피치 ±85° 정지 조건을 제거했다. 수직 자세를 지나면서 Euler 롤·방위가 다른 등가 표현으로 바뀔 수 있지만 물리적 자세는 연속이다.

최대 .01초의 명시적 RK4 간격을 사용하고, 지면 근처에서는 최대 .002초로 분할하여 강체 적분 후 접촉 충격량을 적용한다. 접촉 중 박리 지연은 임시 자유낙하 속도를 사용하는 RK4 내부에서 중복 적분하지 않고, 접촉 해결 후 실제 공기각으로 지수 적분한다. 비유한 값이나 수치 영역 밖 단계는 원자적으로 거부하고 이전 유효 상태를 유지한다. 물리적인 충돌은 해당 접촉 시점의 상태와 실제 진행 시간을 반환하고 Unreal이 정지·fault를 기록한다. 0.25~20배속은 벽시계 재생만 바꾸며 명시적 `step(dt)`에는 곱하지 않는다. 좌표·항로의 계산 원점 50 km, 높이 -500~20000m 제한은 장면/수치 경계이며 측정된 기체 운용 한계가 아니다.

## 돌풍과 고장

`wind_at(environment,time_s)`는 일정 ENU 바람에 지정한 한 번의 펄스를 더한다. 시작 `t0`, 지속 `D`, 벡터 피크 `G`에 대해 `t0<t<t0+D`에서 `wind=base+G*(1-cos(2*pi*(t-t0)/D))/2`, 그 밖에서는 base다. 시작/끝은 0, 중간은 G이며 지상속도에서 이 바람을 빼 TAS와 공력을 계산한다. [FAA AC 25.341-1 Figure 2](https://www.faa.gov/documentLibrary/media/Advisory_Circular/AC_25_341-1.pdf)의 1-cosine 형상을 명시적 시간 구간으로 사용한다. 공간 돌풍 구배·EAS 설계 속도·인증 하중 조건을 구현했다는 뜻은 아니다. `set_gust(east_mps=..., north_mps=..., up_mps=..., delay_s=..., duration_s=...)`는 현재 시뮬레이션 시간을 기준으로 예약한다. API 피크는 축별 ±15m/s, 지연 0~600초, 지속 .1~120초다. 정지하면 돌풍의 시간도 정지하며 피크를 모두 0으로 설정하면 제거된다.

`set_failures(engine_power_fraction=..., elevator_jammed=..., aileron_jammed=..., rudder_jammed=...)`는 출력 비율 0~1과 세 면의 잼을 지정한다. 엔진 출력 비율은 발생 추력에 즉시 곱하며 0은 엔진컷이다. 잼은 요청 시 실제 면 위치를 저장하고 이후 명령과 관계없이 유지한다. 잼을 해제하면 그 위치에서 다시 서보 응답을 시작한다. `controls`는 명령, `actuators`는 실제 위치·엔진 상태·추력이며 공력과 화면은 실제 값을 사용한다. 상태 `disturbance`에는 고장 설정과 현재 유효 바람을 공개한다. 고장 중 수평 트림이 없으면 코어 `level_trim`은 실패를 반환한다. 항로 제어기는 정상 기체의 feedforward와 실제 공기 상태의 피드백을 사용하며, 모든 고장 상태에서 경로 유지가 가능하다는 보장은 없다.

## 지면 접촉

`GroundContact.h`의 완전 전개 시 3개 바퀴 접촉점과 `FixedWing.h`의 자체 순차 충격량 계산을 사용한다. 지면 법선의 비관통 조건과 접선 방향 마찰을 풀고 앞바퀴 조향, 구름저항, 브레이크·주차 브레이크를 적용한다. 힘·모멘트에 따른 활주·이륙·착지가 같은 강체 상태에서 이어지며 위치를 지정 궤적으로 옮기지 않는다. 동체·날개 접촉 또는 바퀴 접촉의 법선 접근속도 4m/s 초과는 충돌 상태를 유지하고 초기화를 요구한다.

기어는 `Aircraft.gear`의 명령과 실제 전개율을 분리하여 3초에 전체 행정을 이동한다. 적분 중간 단계에서도 같은 시간의 전개율을 사용한다. 완전 전개 잠금에서만 바퀴 지지·마찰을 적용하며 수납·이동 중에도 동체 충돌 검사는 유지한다. 기존 전개 상태의 공력 기준을 보존하도록 `Cd = max(0, 기존 Cd + .012*(전개율-1))`로 수납 시 항력을 줄인다. 3초와 .012는 `GroundContact.h`의 개발 상수이며 기체 JSON의 실측 계수가 아니다. 표시 기구는 90° 접힘과 완전 수납 시 숨김으로 표현하며 개별 기어 고장·유압·도어·수납 중 접촉 형상은 없다. [기어 API와 상태](api-reference.md#7-지상-조종과-랜딩기어)를 따른다.

바퀴 위치·마찰계수·충돌 기준은 실측 자료가 없는 개발 가정이다. 내장 외형과 접촉 모델은 1.27m 날개폭 기준이며 완전 전개 접촉점은 기체 무게중심 아래 .18m에 놓인다. 다른 STL을 표시해도 이 접촉점은 유지한다. 완충장치 변형·타이어 회전/슬립·파손·수면 동역학·지면효과는 구현하지 않았다. Unreal은 표시 지형 삼각형 또는 가상 활주로에서 국소 평면을 구해 코어로 전달한다. 자료가 없는 곳은 지면 없음으로 공개한다. CG의 법선 거리, 반력 정의, 가상 활주로 설정과 수동 조작은 [ground-operations.md](ground-operations.md)를 따른다.

## API와 재현

기존 초기조건 7필드는 유지한다. 초기 TAS는 5~45 m/s, 피치 ±89°, 롤 ±180°다. `angle_of_attack_deg`와 `sideslip_deg`를 **함께** 추가하면 공기 상대속도 방향도 지정한다. 초기 박리량은 해당 alpha의 정상 박리값으로 설정한다. 생략 시 해당 고도/속도의 계산 트림 alpha를 사용하며, 수평 트림이 없으면 기존 기준 alpha를 사용한다. 요청한 피치·롤은 그대로 적용하므로 임의 자세 초기조건이 수평비행을 보장하지 않는다. 실제 수평 트림이 있으면 그 조종 입력을, 없으면 기준 중립/스로틀 .5를 적용한다. 초기 조건 설정은 항법 해제·각속도 0·거리 0·정지이며 시뮬레이션 시간은 유지한다.

```python
from uavsim_client import UAVSimClient
sim = UAVSimClient()
sim.pause()
sim.set_wind(wind_east_mps=0, wind_north_mps=0, wind_up_mps=0)
sim.set_initial_condition(
    latitude_deg=33.43, longitude_deg=126.52, ellipsoid_height_m=2600,
    heading_deg=0, pitch_deg=18, roll_deg=0, speed_mps=9,
    angle_of_attack_deg=18, sideslip_deg=0)
sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=0)
print(sim.step(.2)["aircraft"])  # 실속, 양력 부족, 침하
sim.set_controls(elevator_deg=3, aileron_deg=0, rudder_deg=0, throttle=1)
print(sim.step(.6)["aircraft"])  # 받음각 감소·재부착 진행; 기수 하강 입력을 계속 유지하지 않는다.
sim.reset()  # 현재 원점 2600m/19m/s의 계산 수평 트림, 시간 0, 정지
```

`PUT /api/v1/environment`는 `wind_east_mps`, `wind_north_mps`, `wind_up_mps` 세 성분(각 ±30m/s)을 한 번에 받는다. 양수는 그 방향으로 **부는** 속도다. 바람을 바꿔도 관성 위치·속도를 순간 변경하지 않는다. `reset()`도 바람을 유지한다. `speed_mps`는 TAS, `ground_speed_mps`는 수평 지상속도다. 상태에 alpha/beta, 밀도, 동압, CL/CD, 박리량, 실속 경고, 실속, 하중계수를 추가했다. 강한 수직 바람에서 19m/s 수평 트림이 존재하지 않으면 reset은 fault를 반환한다.

항로 목표 TAS는 16~24m/s다. 고도별 트림을 기준으로 조종하고 지상 경로로 항로·최종 선회를 추종한다. 실속 또는 |alpha|가 설정의 실속 시작각(기본 12°) 이상이면 고도 제어 적분을 비우고 날개 수평·받음각 4° 지향·최대 추력의 회복 제어를 우선한다. 재부착·받음각·속도 조건을 만족하면 항법으로 돌아간다. `navigation.stall_recovery`가 이를 표시한다. 이는 개발 제어기이며 모든 바람·고도·진입 자세에서 회복을 보장하지 않는다. 정상 설정에서 계산 가능한 트림이 없으면 명령을 거부하고 fault로 정지한다. 공중 항로용 제어기로 지상 접촉 중 항로 설정을 거부한다. 자동 이착륙은 `FlightOperations.h`의 별도 개발 제어기가 같은 조종면·추력·지면 접촉을 사용한다. 지형 회피는 없다.

## 실행 검사

`tests/fixed_wing_checks.cpp`는 해수면/2600m 60초 수평 평형의 해석해, 바람 이동량, 동압 비례, 공력의 에너지 방향, 실속 양력 저하·항력 증가·조종 효과 감소·분리 지연·회복, 기준 모멘트와 관성, 속도 0과 수직 자세 통과, RK4 수렴·결정성을 확인한다. `tests/navigation_checks.cpp`는 기존 단일/다중/CW·CCW/짧은 상승·24m/s 선회와 2600m·횡풍·실속 회복을 검사한다. 허용오차는 개발 회귀와 수치 계산용이며 실기체 성능 합격 기준이 아니다.

`tests/ground_checks.cpp`에는 정지 반력과 중량의 평형, 제동, 조향의 자전거 기하, 경사면 주차, 이륙·착지·충돌·재현 검사를 남겼다. `tests/terrain_surface_checks.cpp`는 삼각형 보간, 경계·겹침·구멍·자료 누락을 확인한다. 착륙장치 시험 자료와 비교한 성능 검증은 아니다.

`tests/systems_checks.cpp`는 50개 매개변수의 유한값 검증, 결합 관성의 물리 조건, 기체별 공력·엔진·질량 격리, 엔진/서보 과도응답 해석해와 속도 제한, 면 잼·엔진컷, 1-cosine 펄스의 끝점·중간값, 시간 재현을 검사한다. 횡풍 지상 가속과 받음각 ±89/±120/±170°의 유한 적분 및 비추진 항력도 포함한다. 큰 받음각에서 수치가 유한하다는 것은 스핀이나 역방향 비행성능을 실측 검증했다는 뜻이 아니다. 정적 공력 미계수 검사는 명령을 즉시 적용하는 대신 실제 조종면 위치를 직접 지정하여 기존 수치 기준을 유지한다.

```powershell
.\scripts\dev.ps1 check
.\.venv\Scripts\python.exe scripts\check-stall-api.py
.\.venv\Scripts\python.exe scripts\check-navigation-api.py
```

실제 API 검사는 Unreal이 켜진 상태에서 수행하며 입력/시간/바람을 변경한다. 실속 검사는 종료 시 무풍·수평 트림·1배속·정지로 복구한다. 항법 검사는 무풍에서 진행하고 수평 트림·정지로 복구한다. 실행 결과는 `artifacts/stall-api-check.json`, `artifacts/navigation-api-check.json`에 기록한다. 실제 기체의 공력·추진 시험 자료를 반영하는 작업과 정식 신뢰성·정적·동적 시험은 별도다.
