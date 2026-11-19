# 항법·물리 통합 개선 — 2026-11-18

사용자가 승인한 순서인 **기준 진단 → 공중 항법/물리 개선 → 저속·지상 연결 → 실제 지형 통합 마감**을 수행했다. 현재 기본 수납식 연구 기체에 동적 추진과 타이어 모델을 적용했다. 기능 구현·개발 검사 결과이며 실기체 성능 검증은 아니다.

| 단계 | 구현·확인 |
| --- | --- |
| 기준 진단 | 수정 전 실행 파일·프로필·CSV 보존. 좌/우 측방1km 포착 실패와 선회 잔차 재현 |
| 공중 통합 | 마지막 선회 중심의 원주 진입, 중간 항로점 고정 재접근선, 반대 방향 선회 선택 유지, 러더 협조. 로터 관성·축 토크·유도속도를 반영한 추력·자이로 모멘트와 전체 트림 연결 |
| 저속·지상 | 플랩별 트림/기존 착륙 제어 회귀, 바퀴별 지면 조회, 유한 횡미끄럼 타이어·마찰 한계, 반력/간격/미끄럼 공개 |
| 통합 마감 | 코어15/15·실제 API19검사·무풍/작은 바람20km 왕복·실제 표시15검사·기록기8검사와 실제 엔진컷 기록 |

## 항법 변경

중간 항로점은 기존처럼 순서대로 지정 포착 반경·고도15m·TAS2m/s 조건을 만족해야 넘어간다. 통과하지 못하고 지나치면 원래 진입선의 뒤쪽4개 최소 선회반경 거리에서 재접근한다. 그 진입점의1개 선회반경 안에 오면 원래 선을 다시 추종한다. 목표점 직전에 진입선을 계속 바꿔 원을 도는 문제를 제거했다. `navigation.reacquiring`가 이 단계를 공개한다. 경로 최적화나 장애물 회피는 아니다. 재접근 공간도 기존 계산 원점50km·유효 지형 안에 있어야 한다.

**마지막 점은 선회 중심**으로 다룬다. 중심을 관통한 뒤 급히 원을 만드는 대신 원주를 향하는 연속 벡터장을 사용한다. 반경 오차 `max(30m, 반경의10%)`, 코스 오차20° 미만, 고도15m·속도2m/s 조건에서 `loiter`가 된다. 이미 원주와 접선 방향에 있는 초기 상태는 즉시 진입할 수 있다. 이는 중간 항로점을 건너뛰는 처리가 아니다. 자동 이착륙의 직선 참조는 이 마지막 원주 진입 규칙과 분리했다.

러더는 실제 뱅크·속도에서 계산한 정상 선회 요율과 미끄럼각 피드백/적분, 공력 요 모멘트의 보상으로 계산한다. ±20° 한계·러더 고착 중 적분 억제를 적용했고, 실속 회복 중에는 적분을 지운다. 조종면·스로틀만 출력하고 위치를 이동시키지 않는다. 실제 조종계 이득을 식별한 것이 아니다.

## 선회 잔차의 원인·비교

[진단 코드](../tests/navigation_physics_probe.cpp)는 무풍·TAS65m/s·높이2600m·반경1000m의 같은 조건을 사용한다. 아래는 전방 선회 사례의120~300초181개 표본 중앙값이다. 별도로 실제 궤적의 유한 차분 곡률과 힘의 수평 법선 투영을 대조했다. 최대 상대 차이는 수정 전 .169%, 통합 후 .064%였다.1초 표본에서 정한 개발 비교 한계 .2% 안이며 실기체 정확도 허용오차가 아니다.

| 구성 | 미끄럼각 | `R/(V²/(g tanφ))-1` |
| --- | --- | --- |
| 수정 전 고정기어 연구 모델 | +.79377° | +6.1897% |
| 같은 물리·항법만 수정 | -.00034° | -.2331% |
| 새 동적 추진까지 적용 | -.00055° | -.2877% |

기존 선회는 완전 협조 상태가 아니었다. 기존 횡력의 법선 가속도 중앙값은 -.1791m/s²였고, 양력의3차원 투영과 `g tanφ`의 차이도 -.1100m/s²였다. 추력·항력의 법선 성분은 +.0291m/s²였다. 따라서6.2%를 공력계수 오류로 간주해 조정하지 않았다. 잔차는 비협조 횡력·힘 방향·정상 수평 선회 이상식의 가정 차이로 분해했다. 바람이 있으면 지상 궤적과 대기 기준 선회식도 구분해야 한다. [FAA의 협조 선회 조건](https://www.faa.gov/sites/faa.gov/files/regulations_policies/handbooks_manuals/aviation/00-80T-80.pdf)을 참고했다.

수정 전 좌·우 측방1km 사례는300초에도 선회에 들어가지 못했다. 수정 후 고정기어 기준은 각각0초/77초, 통합 구성은0초/75초에 조건을 만족했다.0초 사례는 처음부터 요청 원주 위·접선 방향에 있던 경우다. 중간 측방 항로점은 별도3점 경로에서 재접근을 거쳐 순서대로 포착했다. 실제 API에서도 좌·우·반대·다중 경로가 통과했으며 정착 후90초 관측 구간의 최대 미끄럼각은 개발 기준1° 미만이었다.

## 추진 수식·가정

동적 모델은 `propeller_nominal_rpm>0` 및 `shaft_power_w>0`에서만 켠다. 기존 Thor와 고정기어 연구 파일은 이전 추력 식을 유지한다. 현재 수납식 연구 구성은 **1900RPM 척도·로터 관성8kg·m²·body +X 방향 회전**을 가정한다.1900은 조속기 설정이나 실측 RPM이 아니다.

로터 각속도는 `Aircraft.propeller_omega_rps`의 rad/s 상태이며 quaternion 강체와 같은 명시적 시뮬레이션 시간에 적분한다.

```text
ω0 = nominal_rpm * 2π/60
ρratio = clamp(ρ/1.225, 0, 1)
Qdrive = min(2*Pnom/ω0, throttle_actual * power_fraction * Pnom * ρratio / max(ω, .2*ω0))
Qload = Pnom * ρratio * ω²/ω0³
Jprop * dω/dt = Qdrive - Qload
Pload = Qload * ω
```

`Qload`는 선언한 이차 부하 근사다. 실제 블레이드 CT/CQ·전진비 테이블이나 거버너가 아니다. 엔진/서보 지연은 기존 실제 작동기 상태를 사용한다.

[NASA의 프로펠러 운동량 이론](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/propeller-thrust/)과 [유도동력 관계](https://ntrs.nasa.gov/api/citations/19960047059/downloads/19960047059.pdf)를 사용해 다음 양의 유도속도를 풀었다.

```text
A = π * propeller_radius_m²
T = 2ρA * vi * (V + vi)
η * Pload = T * (V + vi)
T <= mass_kg * maximum_thrust_mps2
```

이 모드의 η는 축동력에서 이상 디스크 동력으로의 개발 손실계수다. 정지 상태의 유용 추진동력 `T*V`는0이고 유도동력은0이 아니다. V는 TAS 크기를 사용하는 축방향 근사다. 비축방향 유입·풍차 구동·후류의 날개/꼬리 영향·P-factor·거버너·리버스·페더링은 미구현이다. 추력 상한에 걸려 남는 부하 동력은 손실로 남으며 추가 추력으로 바꾸지 않는다.

강체에는 축 반작용 `Mx=-spin_direction*Qdrive`, 자이로 반작용 `-Ωbody×Hrotor`, 기존 추력선 모멘트 `My=z*T`를 연결했다. 전체 트림은 추력/피칭 평형과 로터 정상 회전뿐 아니라 미끄럼·에일러론·러더로 횡력/롤·요 모멘트도 함께 맞춘다. 순항 때 무조건 조종면0·스로틀 .5로 두지 않는다.

엔진 출력 상실은 **축 구동 토크/동력0**이다. 동적 모델의 회전 에너지는 즉시 지워지지 않으며 로터 감속·잔류 추력이 생긴다. 기존 모델의 즉시 추력0 계약은 그 모델에서 유지한다. 표시 프로펠러도 동적 모델에서는 계산한 RPM을 따른다.

## 바퀴별 지면·타이어

Unreal 경계에서 CG와3개 바퀴의 지형/활주로를 각각 조회해 코어에 전달한다. CG 법선 이격이 `cg_height_m+10m`보다 높으면 공중 근사로 CG 평면만 사용한다. 지면 누락은 높이0으로 대체하지 않고 명시적 fault/정지로 처리한다. 코어에 Unreal 의존성은 없다.

각 바퀴는 자기 지면의 높이·법선으로 접촉 간격/반력·접선 마찰을 계산한다. 주행 속도1m/s 초과에서 횡마찰 계수 상한은 `min(μlat, Cα*abs(atan2(Vside, abs(Vforward))))`다. 현재 연구값 `Cα=8/rad`이며, 구름·제동과 함께 마찰 원/타원 한계로 제한한다. 저속은 기존 정지 마찰을 유지한다. 개별 바퀴의 반력·간격·최종 횡미끄럼을 공개한다.

완충 추가가 필요한 반복 튐은 이번 선정 조건에서 재현되지 않아 강체 접촉을 유지했다. 스프링·댐퍼·타이어 회전 관성/종방향 슬립/열·안티스키드·개별 동체 점의 서로 다른 지형은 미구현이다. 바퀴 표시는 기존 거리/반경을 사용한다. 표시 지지대를 가상 압축시키거나 반력을 완충력으로 중복 적용하지 않았다.

## 실제 Unreal 연속 왕복

UE5.8.4·대한민국50타일·제주 가상2000×60m 활주로에서 처음 지상 초기화 한 번 뒤 **이륙→수납→20km 임무→귀환→전개→접지→주차**를 이어갔다. 중간 prepare/pose/reset/reload는 없었다.

| 조건 | 실제 귀환 인가 | 주차 시각 | 접지 법선 속도 | 주차 지상속도 |
| --- | --- | --- | --- | --- |
| 무풍 | 20.040km / WGS84 1044.33m | 885.61s | .970m/s | .118m/s |
| 동쪽 .5m/s 바람 | 20.048km / WGS84 1044.05m | 884.07s | 1.014m/s | .107m/s |

두 실행 모두 fault/충돌 없이 전개 잠금·3개 바퀴 접촉·주차로 끝났다. 바퀴 간격 최소값은 약 -1.3e-8m로 수치 오차 규모였고 반력 합은 약37374N이었다. 완충 필요성을 판정할 실측 하중 기준은 없다. 이전 .35m/s 수준의 고정기어 착륙보다 접지 속도가 증가했으므로 이를 실기체 정확도 개선으로 표현하지 않는다. 주차 완료 직전 반력/실제 서비스 브레이크는 마지막 접촉 계산값이며 별도 주차 잠금 명령과 구분한다.

무풍은 이륙 대기·귀환 인가·정렬·플레어·주차 장면을 캡처했다. 바람 실행은 확장 CSV에 기어 전개율·RPM·축동력·러더·바퀴별 반력/미끄럼까지 기록했다. 실제 표시 검사도 정지/저속/고속·공중 회전과 기어/접촉점 일치를 통과했다.

- [수정 전 CSV](../artifacts/navigation-physics/baseline.csv), [항법만 수정](../artifacts/navigation-physics/navigation-final.csv), [통합 구성 CSV](../artifacts/navigation-physics/integrated.csv)
- [비교 요약](../artifacts/navigation-physics/summary.json), [비교 그래프](../artifacts/navigation-physics/comparison.png), [원본 SVG](../artifacts/navigation-physics/comparison.svg)
- [실제 API19검사](../artifacts/navigation-physics/api-check.json), [무풍 왕복](../artifacts/navigation-physics/roundtrip/result.json), [바람 왕복](../artifacts/navigation-physics/roundtrip-wind/result.json)
- [현재 프로필 사본](../artifacts/navigation-physics/integrated-profile.json), [실제 엔진컷 기록301행](../artifacts/navigation-physics/engine-cut/result.json), [표시 검사15개](../artifacts/propeller-visual/result.json)
- [정렬 장면](../artifacts/navigation-physics/roundtrip/return-aligning.png), [주차 장면](../artifacts/navigation-physics/roundtrip/return-complete.png)

## 재실행·검사 한계

```powershell
.\scripts\dev.ps1 check
# 현재 수납식 연구 기체와 Unreal/API 필요. 비행 상태를 초기화하는 검사들이다.
.\.venv\Scripts\python.exe scripts/check-navigation-physics-api.py
.\.venv\Scripts\python.exe scripts/check-return-api.py --output artifacts/integrated-repeat --distance-km 20 --height-m 1000 --waypoint-km 22
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case engine-cut --profile c208b-retractable-research.json --reset-flight --output artifacts/engine-cut-repeat
.\.venv\Scripts\python.exe scripts/report-navigation-physics.py
```

코어15검사는 기존 Thor/고정기어·실속/항법/기어·이착륙 회귀와 새 추진 동력/모멘트 평형·자이로 외적·로터 감속/간격 일관성·근접/다중 항로·러더 고착의 출력 한계/적분 억제·타이어 에너지 소산·바퀴별 요철 하중·누락 거부를 포함한다. 기록기 오프라인8검사는 실패 기록·시간표·수학·파일 복원·서로 다른 열의 비교를 확인했다. UI는 JavaScript 문법/같은 API 상태를 확인하며 브라우저 클릭 자동화는 하지 않는다. [문서 검사](../artifacts/navigation-physics/doc-check.json)에서 공개 Python42메서드·소스의 literal API41경로 누락0, 변경 문서 로컬 링크 누락0을 확인했다.

이번 세션은 Unreal MCP 도구가 노출되지 않은 미연결 상태였으므로 MCP 연결 성공으로 보고하지 않는다. 확인한 `C:\UnrealEngine-release`의 UE5.8.4 명령행 빌드와 제품 API8765·개발 Remote Control30010을 사용했다.

실제 CT/CQ·동일 구성 관성·저속/플랩 공력·타이어/완충 실측 자료와 합격 기준은 여전히 미확보다. 거버너/후류·정밀 완충·전 영역 공력은 이 연구 구현의 완료로 대체하지 않는다. 새 기체/쌍발·센서/FCC·기어 도어/유압·지도 확장은 이번 범위에 넣지 않았다.

사용자 지시에 따라 완료 후 관리형 Unreal과 이번 API 서버를 종료했다. [정리 기록](../artifacts/navigation-physics/cleanup.json)에서 해당 PID 잔존0·8765/30010 수신 포트0을 확인했다. [종료 직전 상태](../artifacts/navigation-physics/final-state.json)와 시험/설정/소스는 보존했다.
