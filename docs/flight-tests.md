# 비행시험 실행·기록·비교

2026-11-18 통합 개선: 기록기에 `propeller_rpm`, `shaft_drive_power_w`, `shaft_load_power_w`, `shaft_drive_torque_nm`, `propeller_load_torque_nm`와 바퀴별 반력/간격/미끄럼 열을 추가했다. 동적 엔진컷은 즉시 축 구동0과 로터 감속을 구분하며 기존 모델은 즉시 추력0이다. 서로 다른 열의 CSV는 공통 열만 수치 비교하고 `unmatched_columns`로 누락/추가를 표시하며 완전 일치로 판정하지 않는다. [새 선회 진단·무풍/바람 왕복·실제 기록](navigation-physics.md)을 따른다.

2026-11-18: 기록 schema2에 `guidance_desired_course_deg/bank_deg/speed_mps/height_m/climb_mps`와 `ground_steering_deg/brake/applied_steering_deg/applied_brake`를 추가했다. 기존 CSV 보고서는 필요한 열만 읽으므로 과거 schema1도 유지한다. 자동 왕복은 별도 [연속 실행 하네스·결과](return-landing-results.md)를 따른다.

기존 공개 API로 연구 모델을 초기화하고 정해진 입력을 실행한다. 결과는 실제 기체 성능 검증이 아닌 **현재 자체 계산 모델의 개발 시험**이다. 전체 범위와 단계는 [계획](flight-test-plan.md), 물리 가정은 [C208B 연구 모델](c208b-research.md)을 따른다.

## 준비와 첫 실행

프로젝트 루트 PowerShell에서 프로젝트 전용 Python 환경을 사용한다. 이 PC에는 `.venv`와 그래프용 Matplotlib3.10.7을 설치했다. 다른 PC는 Python3.11 이상으로 환경을 만들고 아래 의존성을 설치한다. API/클라이언트/기록 자체는 표준 라이브러리만 사용하며 그래프를 그릴 때만 Matplotlib이 필요하다.

```powershell
# 다른 PC 최초 준비
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-flight-test.txt

# API가 꺼져 있을 때만 실행; 엔진 경로는 해당 PC에 맞춘다.
.\scripts\dev.ps1 api -UnrealRoot 'C:\UnrealEngine-release'
```

[조종 UI](http://127.0.0.1:8765/)의 Unreal 실행 또는 Python의 `start_engine()`으로 실행한 뒤 `get_engine()["ready"]`가 true가 될 때까지 기다린다. 기록기는 엔진을 자동 실행/재시작하지 않는다.

```powershell
# 현재 비행을 시험 조건으로 교체한다. 기존 출력 폴더는 덮어쓰지 않는다.
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case elevator-pulse --reset-flight

# 명시적 이름을 부여하려면 새로운 폴더를 지정
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case elevator-pulse --reset-flight --output artifacts/flight-tests/my-elevator-run
```

기체는 기본 `c208b-research.json`, 위치는33.433°N/126.52°E, 높이와 TAS는 프로필의 초기값이다. 초기 명시적 자세 설정 뒤 reset으로 **실제 계산된 북향 수평 트림**을 사용한다. 무풍·돌풍/고장 해제·기어 전개·플랩0°로 시작한다. 스로틀과 조종면은 트림 결과를 유지하며 자동조종을 사용하지 않는다(선회 시험만 예외).

`--reset-flight` 없이 실행하면 진행 중 비행을 거부한다. 정지 상태에서도 시험은 초기 조건을 교체한다. 기존 워게임 기록이 있으면 연출 시간을 바꾸지 않도록 거부한다. 여러 시험 실행기를 동시에 실행하거나 시험 중 Web UI에서 입력·환경을 바꾸지 않는다. 기체 지문·시간·재생/제어 모드·수동 명령·일정 바람이 예기치 않게 바뀌면 실패 처리한다.

## 시험 종류

| `--case` | 입력 일정 | 시뮬레이션 시간·기록 간격 |
| --- | --- | --- |
| `trim` | 트림 입력 유지 | 30s / .1s |
| `elevator-pulse` | t5~5.5s에 트림 대비 elevator+1° | 30s / .02s |
| `elevator-negative` | 같은 구간 elevator-1° | 30s / .02s |
| `aileron-pulse` | 같은 구간 aileron+1° | 30s / .02s |
| `rudder-pulse` | 같은 구간 rudder+1° | 30s / .02s |
| `loiter` | 초기 위치 전방 북쪽3000m에 항로점을 지정, CW 반경1000m 최종 선회 | 180s / .1s |
| `engine-cut` | t5s 출력 잔존율0, 스로틀 명령 유지 | 30s / .1s |
| `throttle-cut` | t5s 스로틀 명령0, 실제 출력은 스풀 응답 | 30s / .1s |
| `flaps` | t5s 10°, t15s 0° | 30s / .1s |

선회 시험은 조종면·추력을 계산하는 기존 자동항법을 사용한다. 나머지는 트림 명령 기반 수동 응답 시험이다. 전방 직선 접근 뒤 선회하므로 첫 구간을 준정상 선회로 간주하지 않는다. 초기 동쪽1000m 진입 시도는 기존 유도기의 근접 측방 항로점 포착 한계로180초 내 loiter에 진입하지 못했고 실패 기록을 보존했다. 전방 접근은 선회 수식 비교를 위한 시험 조건이며 해당 항로 포착 한계를 수정/검증한 것은 아니다. `flaps`는10°를 지원하지 않는 Thor에서 명시적으로 실패한다.

명시적 `step(dt)`로 실행하므로 벽시계 배속이나 HTTP 요청 속도와 시험 입력 시각은 무관하다. 작은 조종 입력은50Hz로 기록하고 느린 트림/선회/엔진/플랩 시험은10Hz로 기록한다. 내부 최대 .01s 적분은 그대로다. 벽시계 실행 시간이 시뮬레이션 시간과 같다고 보장하지 않는다.

## 파일과 판정

실행 완료 시 출력 폴더와 성공 여부를 표시한다. 종료 코드0은 실행·검사·복구·요청한 보고서 생성 성공이며, 실패는0이 아니다.

| 파일 | 내용 |
| --- | --- |
| `metadata.json` | 형식 버전·축/단위·입력 일정·초기 상태/트림·프로필/소스/Unreal 모듈 SHA256·적용 fingerprint·실제 소요 시간 |
| `profile.json` | 실행한 원본/변형 프로필 바이트와 출처/가정 |
| `samples.csv` | t0 포함 시뮬레이션 시계열. 위치·속도·자세·공기각·각속도·하중배수·분리율·명령/실제 입력·추력·플랩·기어·모드 |
| `result.json` | 성공/실패·검사·최종/마지막 관측 상태·복구 결과 |
| `report.html`, `response.svg`, `response.png` | 단일 실행 보고서와 독립 그래프 |
| `response-metrics.json` | 각 신호의 최소/최대·절댓값 피크/시각·최종 변화량, 선회 비교 지표 |

엘리베이터 시험은 실제각이 움직인 직후의 pitch-rate 부호를 확인한다. 엔진 고장 시험은 고장 적용 후 추력이0인지 확인한다. 선회는 최종 loiter 진입을 확인한다. 모든 시험은 예상 시간 격자·유한값·무고장/무충돌·기체와 제어 모드 격리를 확인한다. 실제 상승률·활공비·감쇠비·원래 고도 복귀를 임의 합격 조건으로 쓰지 않는다.

선회 보고서는 마지막60초 중 낮은 롤 변화율·작은 beta·작은 수직속도·10~30.5° 뱅크의 표본을 골라 위경도 세 점의 기하학적 곡률 반경과 `V²/(g tan|bank|)`를 비교한다. 표본 수와 상대 차이를 공개하며 조건에 맞는 표본이 없으면 미산출이다. 이는 무풍 준정상 개발 비교이며 실기체 선회성능 합격 판정이 아니다.

실패 시에도 메타데이터·마지막 수신 행·원인을 남긴다. HTTP mutation 타임아웃을 자동 재시도하지 않는다. `--no-report`로 기록만 저장하고 그래프 환경에서 나중에 보고서를 만들 수도 있다.

## 재현과 연구 가정 비교

```powershell
# 같은 case/profile/code/초기 조건으로 다시 기록한 뒤 비교
.\.venv\Scripts\python.exe scripts/report-flight-test.py artifacts/flight-tests/run-a artifacts/flight-tests/run-b --require-identical

# 저장 자료만으로 단일 보고서 재생성(API 불필요)
.\.venv\Scripts\python.exe scripts/report-flight-test.py artifacts/flight-tests/run-a

# Iy만 ±10% 바꾼 별도 연구 프로필로 시험
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case elevator-pulse --iy-scale .9 --reset-flight
.\.venv\Scripts\python.exe scripts/run-flight-test.py --case elevator-pulse --iy-scale 1.1 --reset-flight

# 기준과 변형 비교에는 --require-identical을 사용하지 않음
.\.venv\Scripts\python.exe scripts/report-flight-test.py <baseline-run> <variant-run>
```

비교는 같은 case·같은 표본 시각을 요구한다. 몰래 보간하지 않는다. 모든 수치 열의 최대 절대 차이·RMS와 최초 차이 필드를 기록한다. 동일 프로필·코드·초기 조건·입력 일정도 표시한다. `--require-identical`은 이 조건과 모든 표본의 완전 일치를 요구하며 위반 시0이 아닌 코드다. 다른 PC/빌드의 bitwise 일치까지 보장하지 않는다.

비교 산출물은 기준 폴더의 `comparison-<candidate-path-hash>.html/.svg/.png/-metrics.json`이다. 다른 비교 대상을 덮어쓰지 않으며 같은 두 기록의 보고서 재생성은 허용한다. 원본 CSV와 선택한 기준 실행은 자동 덮어쓰지 않는다. 보고서에는 두 실행의 조건·프로필이 함께 표시된다.

`--iy-scale`은 .5~2 입력을 받으며 ±10%가 첫 개발 비교다. 실제 관성 불확도 범위가 아니다. 원본 프로필을 수정하지 않고 고유한 임시 프로필을 만들며 기존 JSON/관성/트림 검증을 그대로 통과해야 한다. 종료 시 임시 파일은 정리한다. 관성 이외의 계수 자동 튜닝 기능은 없다.

## 종료·복구와 한계

선택 파일 바이트와 기존 일정 바람을 복원하고 원래 기체의 **공중 트림·정지 상태**로 끝낸다. 기존 위치·시간·각속도·항로 제어기 메모리·고장/플랩 상태까지 되돌리는 snapshot 기능은 없다. 연결이 끊겨 복구를 확인하지 못하면 오류를 기록하고 성공으로 처리하지 않는다. CCD 설정·가상 표적·카메라 모드를 시험 명령으로 변경하지 않는다.

실제 API 경로는 [매뉴얼](api-reference.md)을 따른다. 기록 도구를 위한 새 endpoint·클라이언트 메서드·Web UI는 추가하지 않았다. 코어의 frozen-state 미계수/관성 비교와 실제 .01/.005/.0025초 적분 간격 검사는 `research_checks`에 남겼다. API의 기록 간격 변경과 적분 수렴 검사를 구분한다.

오프라인 도구 검사:

```powershell
.\.venv\Scripts\python.exe tests/flight_test_tools_checks.py
```

알려진 CSV·시간 오류·NaN·누락 표본·RMS/피크·기하학적 원의 반경·HTML escape·불완전 보고서·타임아웃 무재시도/복구·기존 출력 경로 거부를 확인한다. 관련 변경이 없는 지도·EOIR·워게임 전체 검사를 반복하지 않는다.

## 실제 실행 기록 — 2026-11-17

- 카메라 소스를 UE5.8.4 Development로 빌드하고 C208B 뒤47.55m/위14.265m 배치를 실제 속성으로 확인했다. 코드/문서만 바뀐 부분 때문에 관련 없는 전체 하네스를 반복하지 않았다.
- 오프라인 도구 검사7개와 보강한 `research_checks`가 통과했다. frozen-state 종/횡 모멘트·관성 균형, Iy 역비례, 실제 적분 간격 축소를 포함한다. 피치 차이는 .01↔.005초에서2.43432e-11rad, .005↔.0025초에서1.51269e-12rad로 감소했다. 속도 차이는1.64448e-10→9.79128e-12m/s였다. [코어 기록](../artifacts/flight-tests/research-core-check.txt), [도구 검사](../artifacts/flight-tests/tools-check.txt).
- 스로틀컷 실제 출력과 1차 지연 해석해의 최대 차이는7.77e-16이며, 표본 수와 배정밀도로 정한 누적 반올림 경계2.14e-12 이내였다. 엔진 고장 뒤 추력0, 플랩 최대10°/s도 확인했다. [독립 비교 기록](../artifacts/flight-tests/analytic-api-check.json).
- 30초 트림 기록의 TAS 변화는0이지만 WGS84 타원체고는 약.299061m 변했다. 국소 ENU 접평면의 수평 이동1950m에 대해 `거리²/(2*(자오선 곡률반경+높이))`로 계산한 차이와 약2.9e-7m 이내로 일치했다. 외부 WGS84 높이와 국소 ENU 수직속도를 동일한 값으로 해석하지 않는다. 지구 곡률을 포함한 전지구 비행 모델로 확장한 결과가 아니다.
- 최초 측방1000m 항로점의 선회 시험은180초 내 loiter에 들어가지 못해 **실패**로 보존했다. 전방3000m 접근 조건으로 명시적으로 바꾼 뒤 성공했다. 이 작업은 근접 측방 항로점 포착 유도기를 고친 것이 아니다. [실패 기록](../artifacts/flight-tests/loiter/result.json), [전방 접근 기록](../artifacts/flight-tests/loiter-straight/result.json).
- 전방 접근 선회의 조건부42개 표본에서 기하학적 경로 반경과 무미끄럼 이상식의 상대 차이는 중앙값6.21%, 최대 절댓값6.68%였다. 현재 제어기는 러더 협조를 하지 않고 표본 선택도 beta≤2°를 허용하므로, 완전 협조 선회 조건을 검증했다는 뜻이 아니다. 원인을 모두 식별하거나 계수를 보정한 결과도 아니다. [선회 지표](../artifacts/flight-tests/loiter-straight/response-metrics.json).

벽시계 시간은 시험·복구 시간이며 그래프 생성 시간은 제외한다. 실제 API 기록은 해석 검사나 실측 기체 데이터 자체를 대신하지 않는다. 최종 결과는 아래와 같으며, 첫 실패와 이전 기준 기록도 삭제하지 않았다.

### 최종 마감

[전체 결과와 보고서 목록](../artifacts/flight-tests/index.html), [기계 판독 완료 기록](../artifacts/flight-tests/completion.json).

- 최종13회·총15,013개 표본의 실행·저장·보고서 생성·복구를 완료했다. C208B 9종 시험, 동일 조건 반복1회, Iy 변형2회, Thor 엘리베이터 회귀1회다. 최초 측방 진입 실패는 이 성공 횟수에 포함하지 않고 별도 보존했다.
- 최종 기준은 `elevator-reference-final`, 반복은 `elevator-repeat-final`이다. 같은 프로필·코드·초기 조건·입력 일정에서1501개 표본의 모든 열이 완전히 일치했다. [최종 기준 선택 기록](../artifacts/flight-tests/baseline-final.json). 이전 baseline.json/원시 실행은 자동 덮어쓰지 않았다.
- Iy 기준 대비-10%/기준/+10%의 피치율 절댓값 피크는 각각 약1.753/1.650/1.558°/s였다. 동일 초기 조건·입력/코드와 다른 프로필임을 비교 보고서에 표시했다. 동적 피크를 초기 각가속도의 단순 역비례와 같다고 취급하지 않는다.
- Thor는19m/s 기본 트림에서 같은 짧은 입력 시험을 완료하고 C208B 선택을 복원했다. 고장/돌풍/형상 및 파일 복구 오류는0건이었다.
- 카메라는 C208B 시점 전환/재읽기 뒤47.55m/14.265m, Thor에서6m/1.8m, 복귀 후 다시 C208B 거리임을 확인했다. CCD 모드·FOV·짐벌 모드 설정이 유지됐다. [최종 카메라 검사](../artifacts/flight-tests/camera-final.json).
- 후속 사용자 지시에 따라 Unreal 비행 창을 API로 정상 종료하고 API 서버를 종료했다. Live Coding과 프로젝트 Visual Studio도 정상 종료됐고 하위 MSBuild 노드도 종료됐다. Unity 실행 프로세스는 없었다. 프로젝트 관련 잔여 프로세스0건을 확인했다. 임시 변형 기체 파일은 남지 않았고 종료한 런타임의 오래된 locator를 제거했다. 소스·가상환경·활성 C208B 선택·시험 기록·그래프는 보존했다. [종료 기록](../artifacts/flight-tests/process-cleanup.json).

다음 실행은 `dev.ps1 api -UnrealRoot 'C:\UnrealEngine-release'`로 API를 시작한 뒤 Unreal 실행을 누른다. 원래 비행 시간/동역학 상태를 복원하는 명령이 아니며 새 공중 트림으로 시작한다. 실제 성능 미검증, 근접 측방 항로점 포착 한계, 이상적 협조 선회식과의 잔차는 남은 범위다.
