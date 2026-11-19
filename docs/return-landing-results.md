# 연속 귀환·착륙 구현 결과 — 2026-11-18

M0~M5의 연구 개발 범위를 구현했다. 자동착륙 전환, 실제 Unreal 지형 연속 왕복, 표시 형상·추력선 모멘트·지상 응답을 연결했다. **실제 C208B 성능은 미검증**이며 전국 장애물 회피나 정식 시험 완료를 뜻하지 않는다.

## 원인과 수정

기존 제어기는 원거리 인가 직후 접근 고도/저속을 요구하고, 실제 플랩이 접힌 상태에서 지원하지 않는 저속 트림을 요청했다. 가까운 점을 번갈아 포착하는 접근과 급한 뱅크 전환도 복행/좌우 반전을 만들었다. 최초 평면 진단의 고고도 거절·첫 유도 실패·저고도 반복은 [원래 계획의 진단](return-landing-plan.md)에 보존했다. 사용자 당시 화면과 동일한 현장 조건의 기록은 없어 모든 증상을 같은 원인으로 단정하지 않는다.

착륙 전용 연속 선회/중심선 경로를 추가했다. 현재 형상에 맞는 속도로 귀환·고도 처리한 뒤 감속/실제 플랩 전개·정렬·최종 접근으로 진행한다. 속도 목표는 .5m/s², 뱅크 목표는3°/s로 변한다. 반대 방향 진입의 선회 방향을 유지하며 두 번 복행 후 명시적 대기한다. 수동·자동 모두 같은 자체 비행역학을 통과하며 위치를 직접 옮기지 않는다. 일반 임무 항로점 포착과 러더 협조 제어는 변경하지 않았다.

Unreal 경계는 두 선회 방향의 지형을40m 간격으로 검사한다. 선회/진입 이격40m·최종 접근의 점진적 여유는 개발 기준이다. 누락·계산 원점49km 초과·부족한 현재 높이는 거절한다. 시작 상태는 활주로 기준 높이10~5000m·거리40km 이내이며 실제 승인 여부는 트림·지형·바람도 따른다. 샘플 사이 장애물·선정 경로 밖의 회피를 보장하지 않는다.

UI/API에 단계·별도 거절 이유·목표 코스/뱅크/TAS/고도/상승률·WGS84 참고 경로를 공개했다. 지상 입력은 명령과 실제 출력으로 구분한다. [API 매뉴얼](api-reference.md), [기체 설정](aircraft-profile.md).

## 형상·추진·지상

- 내장 형상은 동체 반치수·날개폭/시위·접촉점·바퀴 반경을 사용한다. 플랩은 실제각, 전륜은 실제 조향각, 바퀴 회전은 접촉 중 거리/반경을 따른다. 꼬리/두께 비율은 연구용이며 CAD가 아니다. STL 자동 부품 분리는 없다.
- C208B 연구 가정: 바퀴 반경 .32m, 표시 프로펠러 반경1.3m, 추력선 CG 아래 .05m. `r×F`의 `My=z*T`와 트림을 연결했다. 기존 정지 추력/효율·출력/밀도 상한을 유지했다. 프로펠러 회전은 스로틀 기반 표시이며 RPM·반작용 토크·후류·거버너·리버스·페더링은 구현하지 않았다.
- 마찰 .025/.7/.8과 충돌 기준4m/s를 기체 파일로 옮겼다. 조향 .25s·30°/s·속도별4m/s² 보호, 제동 .2s 지연은 연구 가정이다. Thor는 기존 즉시 응답 기본값을 유지한다. 주차는 즉시 잠금이다.
- 현재 낮은 충격의 착륙에서 완충 추가가 필요한 튐은 재현되지 않았다. 기존 강체3점 접촉과 기체 아래 단일 지형 평면을 유지했다. 완충·각 바퀴별 지형 조회·타이어 슬립은 추가하지 않았다. 반력은 평균 접촉 충격량/시간이며 최대 기어 하중이 아니다.

## 실제 Unreal 연속 기록

UE5.8.4·대한민국50타일·제주2000×60m **가상 시험 활주로**에서 실행했다. 각 실행은 처음 지상 초기화 **한 번**, 이후 이륙→일반 임무 항로→착륙 인가→귀환→접지→주차다. 비행 중 prepare/pose/reset/reload를 사용하지 않았다. 표의 거리는 귀환 명령 당시 출발점과의 수평 거리, 높이는 WGS84다.

| 실행 | 실제 귀환 거리/높이 | 주차 시간 | 최종 접근 최대 뱅크/횡오차 | 접지 법선 속도 |
| --- | --- | --- | --- | --- |
| 착륙 제어 수정 후·물리 개선 전 | 22.74km / 521.66m | 1012.82s | 1.46° / 3.84m | .367m/s |
| 형상·추진·지상 개선 후 | 22.79km / 521.86m | 1014.64s | 1.45° / 3.83m | .355m/s |
| 동쪽 바람 .5m/s | 22.77km / 521.79m | 1012.59s | 1.52° / 4.18m | .357m/s |
| 높은 임무, 무풍 | 20.01km / 1045.06m | 923.84s | 1.42° / 3.71m | .362m/s |

모두 복행0회·주차 지상속도 .14m/s 미만·충돌/fault 없음이었다. 최종 접근의 **1Hz 표본**에서 ±1°를 넘는 에일러론 명령의 부호 반전0회·24.9° 이상 포화0표본이다. 고주파 진동 부재나 실제 비행 안정성 보증으로 확대하지 않는다. 개선 후 주차 반력 약37374N·CG 법선 이격1.70m다.

첫 비교의10km와 바람 사례의5km는 **최소 귀환 거리 트리거**였다. 먼30km 항로점의 국소 ENU 목표와 WGS84 높이 차이, 높이 조건 대기로 실제 인가는22.7km 부근이었다. 이를5/10km에서 귀환했다고 표시하지 않는다. 추가20km 사례는22km 임무 항로점을 사용해 실제20.01km에서 인가했다. 계획의20km/2600m 실제 지형 시험은 선택하지 않았으며,2600m·반대 기수 등은 별도 코어 회귀 범위다.

원본/변경 프로필·환경·CSV·단계·종료 상태·모듈/코어 SHA256은 실행 폴더에 보존한다. 첫 개선 전 실행은 [프로필](../artifacts/return-flight-baseline/profile.json)과 CSV/결과를 보존했으며 이후 하네스에 자동 SHA256 기록을 추가했다.

- [개선 전 기록](../artifacts/return-flight-baseline/result.json), [개선 후 기록](../artifacts/return-flight-refined/result.json)
- [최종 모듈 왕복 재확인](../artifacts/return-flight-final/result.json): 직접 최종 접근의 불필요한 귀환 고도 하한 거절을 수정한 뒤 같은 조건으로 재실행해1014.64s에 주차했다. 이 실행은 수치 전용이다.
- [최종 재현 확인](../artifacts/return-final-verification.json): 적용 프로필과 보존 파일의 SHA1이 일치하고, 개선 후/최종 실행 CSV의 SHA256이 같았다.
- [바람 기록](../artifacts/return-flight-wind-numeric/result.json), [높은 임무 기록](../artifacts/return-flight-high/result.json)
- [계산 요약](../artifacts/return-flight-report/summary.json), [비교 그래프](../artifacts/return-flight-report/comparison.png)
- [개선 전 주차 장면](../artifacts/return-flight-baseline/return-complete.png), [개선 후 주차 장면](../artifacts/return-flight-refined/return-complete.png)

개선 전후 각각 이륙 대기·귀환 인가·정렬·플레어·주차 장면을 캡처했다. 첫 시도와 바람 시도의 뷰포트 캡처 시간 초과는 [첫 실패](../artifacts/return-flight-first/result.json), [바람 캡처 실패](../artifacts/return-flight-wind/result.json)에 남겼다. 바람/높은 임무 재시험은 `--no-capture`로 비행 수치만 검사했다. 캡처 실패를 비행 성공으로 덮어쓰지 않았다.

## 재실행

진행 중인 비행을 초기화하는 시험이므로 별도 실행 시간을 잡는다. API/관리형 Unreal이 준비된 C208B 연구 프로필에서 프로젝트 루트 PowerShell로 실행한다. 출력 폴더는 새 이름이어야 한다.

```powershell
.\scripts\dev.ps1 api -UnrealRoot C:\UnrealEngine-release
# Web UI의 Unreal 실행, 또는 Python UAVSimClient().start_engine() 후 ready 확인
.\.venv\Scripts\python.exe scripts/check-return-api.py --output artifacts/return-repeat
.\.venv\Scripts\python.exe scripts/check-return-api.py --output artifacts/return-high-repeat --distance-km 20 --height-m 1000 --waypoint-km 22 --no-capture
.\.venv\Scripts\python.exe scripts/check-return-api.py --output artifacts/return-wind-repeat --distance-km 5 --height-m 500 --wind-east 0.5 --no-capture
.\.venv\Scripts\python.exe scripts/report-return-flight.py artifacts/return-repeat artifacts/return-high-repeat --output artifacts/return-comparison
.\.venv\Scripts\python.exe scripts/check-c208b-api.py --output artifacts/return-contract-repeat.json
```

하네스는 종료 시 정지하고 이전 일정 바람/기존 캡처 파일을 복원한다. 성공한 비행은 주차 위치에 남으며 이전 비행 상태로 복원하지 않는다. CCD 설정은 연속 시험에서 바꾸지 않는다. 뷰포트가 렌더되지 않는 상태에서는 캡처가 시간 초과될 수 있고, 수치만 확인하려면 명시적으로 `--no-capture`를 사용한다. 그래프는 Matplotlib이 필요하다.

## 개발 검사와 후속 범위

Release CTest12/12: 기존 Thor·항법·지상/경사/누락·시스템·이착륙 회귀, C208B 트림/이착륙, 고고도/반대 기수/고속 귀환4사례, 추력선 외적·독립 제동 지연 해·추진 동력 상한·정지 반력·조향율·바퀴 거리/반경 검사다. UE5.8.4 Development 빌드 성공, 기록기 오프라인7검사와 Python/JS 문법도 통과했다. [실제 API 계약173검사](../artifacts/return-contract-check.json)는 실패 보존·Thor 재읽기·실제 지상 출력·바퀴 표시 바닥/접촉점 일치·실제 플랩 표시·직접 접근/주차·CCD JPEG와 설정 보존을 확인했다. 첫 직접 접근 거절 실패는 [별도 보존](../artifacts/return-contract-first-failure.json)했다. [문서 검사](../artifacts/return-doc-check.json)에서 공개 Python 메서드42개 누락0·변경 문서의 로컬 링크 누락0이었다. 브라우저 클릭/UI 자동화는 하지 않았다.

일반 근접 측방 항로점 포착·선회식 잔차 분석·러더 협조 제어는 계속 보류한다. 실측 관성/저속 공력/프로펠러·타이어·완충 자료와 합의된 정식 시험, 센서/외부 비행제어컴퓨터 연동도 이번 완료 범위에 넣지 않았다.

사용자의 완료 후 정리 지시에 따라 이번 작업의 관리형 Unreal은 정상 종료하고 API 서버도 종료했다. 기록·프로필·소스는 보존했으며 다음 실행은 위 명령으로 시작한다.
