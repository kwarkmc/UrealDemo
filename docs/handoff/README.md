# UAVSim 작업 인계 시작점

2026-11-19 작성. 새 채팅의 에이전트가 과거 대화를 기억한다고 가정하지 않고, 작업 결정·구현·실행 방법·검사 근거를 저장소에서 다시 읽도록 정리했다. 저장소는 `kwarkmc/UrealDemo`, TrashCan 안의 디렉터리는 사용자 요청에 따라 **`UnrealDemo`**, Unreal 프로젝트/제품은 **`UAVSim`**이다. 철자를 임의로 통일하거나 프로젝트 파일을 이름 변경하지 않는다.

## 새 작업을 시작할 때

1. `pwd`, `git status --short`, `git remote -v`, `git log -1 --oneline`로 실제 위치·원격·변경·버전을 확인한다. 기존 수정을 보존한다.
2. 프로젝트 루트의 [AGENTS.md](../../AGENTS.md), [README](../../README.md)와 이 문서를 읽는다. 사용자 최신 지시가 우선이며, 자료의 실제 적용 범위를 확인한다.
3. 아래 색인에서 해당 기능의 매뉴얼·수식·코드·검사를 읽고 완료 조건 하나를 정한다. 작은 구현 → 관련 검사 한 번 → 결과/남은 작업 기록 순서다.
4. 코드가 바뀌지 않았고 미해결 우려가 없으면 과거 검사를 무조건 반복하지 않는다. 문서 작업만으로 Unreal 빌드를 돌리지 않는다.
5. API가 바뀌면 endpoint·Python 클라이언트·Web UI·API 매뉴얼·검사 기록을 함께 갱신한다. 단위·필드·공개 목록·로컬 링크를 대조한다.

## 프로젝트 목표와 사용자 결정

- Prepar3D/SimConnect를 대체할 Unreal 기반 고정익 무인기 시뮬레이터다. 구현한 운용 기능은 외부 프로그램도 명시적 제품 API로 사용한다. SimConnect를 그대로 복제한다는 목표는 아니다.
- 비행역학은 자체 C++ 계산 코어다. JSBSim 같은 외부 FDM을 가져오거나 감싸지 않는다. 일반 개발 도구의 오픈소스 사용을 금지한다는 뜻은 아니다.
- 계산 코어는 Unreal 타입·렌더 프레임·벽시계·공유 전역 상태에 의존하지 않는다. 상태·입력·dt를 명시하고 재현 가능하게 한다. Ponytail full로 기존 구조와 표준 라이브러리를 먼저 사용한다.
- 최초 기준은 UMN Ultra Stick25e/Thor다. 이후 첫 대형 개발 기준을 **Cessna208B Super Cargomaster**, 후속 쌍발 후보를 **King Air350**으로 결정했다. 최신 EX 수치와 구형 연구 구성을 섞지 않는다.
- 동일 구성의 관성·추진·저속 공력 자료가 부족하여 사용자가 별도 연구 가정 모델 진행을 승인했다. 현재 선택은 **가상의 수납식 C208B 파생 연구 기체**이며 실제 C208B 사양이 아니다.
- 수동·항로·자동 이착륙은 같은 비행역학을 사용한다. 제어기는 조종면·스로틀만 계산하며 위치를 직접 옮겨 비행시키지 않는다. 수동 입력은 항법을 해제한다.
- IMU/GNSS 항법 센서와 외부 비행제어컴퓨터 연동은 제외 결정이다. EO/IR은 후속 별도 기능으로 추가했다. UI 자동화는 사용자 지시로 중단했으며 명령행·파일·API 작업을 사용한다.
- 변경 결과는 한국어로 간결하게 보고한다. 중요한 결정이 필요하지 않으면 반복 확인 질문으로 진행을 멈추지 않는다. 정기 자동화는 요청하지 않았다.

## 현재 구현과 다음 작업

| 영역 | 현재 상태 | 남은 구분 |
| --- | --- | --- |
| 비행 코어 | WGS84/ENU, 자체6DOF·quaternion RK4, 비선형 공력/박리 지연·ISA·바람·작동기·돌풍·고장 | 실측 전 영역·비대칭 실속·스핀·버핏·연료/CG는 미구현 |
| 기체 설정 | v1 Thor, 합성 v2 검사체, 고정기어/수납식 C208B 연구 구성 | 합성1000kg 검사체를 C208B로 취급하지 않음 |
| 항법/자동 운항 | 근접 최종 선회·중간점 재접근·러더 협조, 귀환·감속·형상·정렬·복행·착륙·주차 | 모든 조건의 성공이나 실제 자동조종장치 성능을 보장하지 않음 |
| 최신 추진/지상 | 수납식 연구 프로필의 동적 로터/RPM·토크·동력·운동량 추력·자이로, 바퀴별 지면·횡미끄럼 | 실제 CT/CQ·거버너·후류·완충·종슬립·안티스키드는 미완료 |
| 외형 | 기체별 내장 크기, STL, 플랩·바퀴·접히는 기어·프로펠러 표시 | 단일 STL 자동 부품 분리, 기어 도어·유압·실측 링크는 없음 |
| 대한민국 지도 | 기본48+독도1+제주상세1, 가상 제주2000×60m 시험 활주로 | 실제 공항/DTED 원본이 아니며 장거리 모델 검증을 뜻하지 않음 |
| EO/IR/워게임 | Unreal JPEG, 짐벌4모드·가상 표적, 고정시간 연출·운영자 hit/miss | 물리 열상·영상 인식·실제 유도/탄도/피해 계산 없음 |
| 외부 도구 | HTTP API·Python 클라이언트·Web UI, CSV/JSON 기록·그래프·비교 | 상태 구독 푸시·외부 FDM 등록·센서 오차 모사는 없음 |

최신 항법·물리 통합은 완료했다. 과거 ‘근접 항로점/러더 보류’, ‘RPM은 표시용’, ‘단일 지면만 사용’ 설명은 해당 당시 기록이며 현재 수납식 연구 구성에는 그대로 적용하지 않는다. 반대로 기존 Thor·고정기어 연구 구성은 새 선택 항목 기본값을 유지한다. 현재의 정확한 수식·가정은 [navigation-physics.md](../navigation-physics.md), 설정 계약은 [aircraft-profile.md](../aircraft-profile.md)를 따른다.

**다음 계획은 P0~P6**다. 자료/요구사항/시험 기준 → 프로펠러/거버너 → 후류 → 완충/타이어 → 비대칭 실속/스핀·연료/CG → 운용 범위 → 공식 시험/인수 순이다. [계획](../physics-expansion-plan.md)은 아직 구현 착수나 완료를 뜻하지 않는다. 구현 완료·프로젝트 공식 시험 합격·실기체 성능 대조 완료를 별도 판정한다. 기존 개발 검사로 정식 시험이나 항공 인증을 대신하지 않는다.

## 좌표·시간·상태를 읽는 기준

- 코어/지형 raw는 ENU(동·북·위) m, 기체축은 FRD(앞·오른쪽·아래)다. Unreal 표시 경계는 동·남·위 cm이며 지역 자세를 전국 렌더 원점으로 회전한다.
- 위치 API 높이는 **WGS84 타원체고 m**다. 정표고 DEM/DTED에는 해당 EGM 지오이드 보정을 적용한다. 공개 Copernicus DSM/Sentinel 영상은 DTED 원본이 아니다. `maps/active.json`이 지도를 선택한다.
- 국소 계산/항로 범위는 재설정한 원점50km 안이다. 전국 렌더링은 장거리 비행 검증이 아니다. 자세·속도/초기조건 명령과 지속 비행 제어 명령을 혼동하지 않는다.
- 렌더 재생의 기본 계산 간격은 .01s, 지면 근처는 최대 .002s다. .25~20배속은 벽시계 재생에만 적용한다. 명시적 `step(dt_s)`에는 배속을 곱하지 않는다.
- `controls`는 명령, `actuators`는 실제 출력이다. TAS와 지상속도, 피치각과 받음각을 구분한다. 상태 JSON은 센서 측정값이 아닌 자체 계산 상태다.
- `ground.height_agl_m`은 CG의 지면 **법선 거리**다. 반력은 충격량/계산 간격이며 최대 착륙장치 하중이 아니다. 바퀴 배열은 전륜·좌주륜·우주륜 순서다. 지면 누락을 높이0으로 추정하지 않는다.
- 기어는 시뮬레이션3초 행정, 완전 전개 잠금만 접촉을 지지한다. 지상 수납과 자동 운항 중 수동 기어/플랩 조작은 거부한다. 준비 명령과 초기화하는 착륙 시험 위치 명령을 구분한다.

## 실행·검사 방법

### Linux 클라우드: 코어·오프라인 도구

TrashCan 루트라면 `UnrealDemo` 서브모듈을 먼저 초기화하고 이동한다. 아래 명령은 프로젝트 루트에서 실행한다. 빌드 도구/패키지 설치와 LFS는 [클라우드 안내](../cloud-development.md)를 따른다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 tests/flight_test_tools_checks.py
```

Unreal 없이 실행하는 검사다. 클라우드 localhost는 Windows PC의 localhost가 아니다. Linux에 이 프로젝트의 Windows 엔진 실행/API 연동이 준비되어 있다고 가정하지 않는다. 그래프 생성에만 `requirements-flight-test.txt`, 지도 재생성에만 `requirements-terrain.txt` 의존성을 추가한다.

### Windows: Unreal·제품 API

기존 PC 루트는 `C:\UnrealDemo\UrealDemo 5.8`, 실제 엔진은 `C:\UnrealEngine-release`의 UE5.8.4다. 다른 PC는 호환 엔진·Visual Studio C++/Windows SDK·CMake·Git LFS·Python을 설치한다. 저장소는 엔진·가상환경·빌드 결과를 포함하지 않는다. `UAVSim.uproject`의 EngineAssociation GUID는 이 PC 등록값이므로 새 PC의 엔진 연결을 지정한다.

```powershell
git lfs install
git lfs pull
py -3.12 -m venv .venv
.\scripts\check-environment.ps1 -UnrealRoot 'C:\UnrealEngine-release'
.\scripts\dev.ps1 check
.\scripts\dev.ps1 build -UnrealRoot 'C:\UnrealEngine-release'
.\scripts\dev.ps1 api -UnrealRoot 'C:\UnrealEngine-release'
```

API가 이미 실행 중이면 다시 시작하지 않는다. `http://127.0.0.1:8765/`에서 Unreal 실행 → 지형/ready 대기 → 재생/조작을 사용한다. EO/IR은 `/eoir`다. 에디터 Play 없이 독립 비행 창을 실행한다. Windows PowerShell5.1/7의 인수 전달을 지원하며 `.vsconfig`의 구성 요소 ID는 해당 Visual Studio 버전에서 설치 가능 여부를 확인한다.

서버는 자신이 시작한 독립 비행 창만 정상 종료한다. 별도로 실행한 에디터/비행 창은 사용자 소유다. `stop_engine()` 후 API 서버는 남으며, 서버 자체 종료는 시작 시 출력한 **확인된 API PID**만 대상으로 한다. 임의의 Unreal·Python·VS 프로세스를 일괄 종료하지 않는다. 과거 PID·Saved locator를 현재 상태로 사용하지 않는다.

## API 사용의 최소 기준

| 경계 | 주소/파일 | 의미 |
| --- | --- | --- |
| 제품 HTTP/웹 | `127.0.0.1:8765`, `/api/v1` | 외부 제어·상태·실행 관리 |
| Unreal Remote Control | `127.0.0.1:30010` | API 서버가 Unreal 함수에 전달하는 개발/엔진 경계 |
| Unreal MCP | `127.0.0.1:8000/mcp` | 에디터 개발 도구. 제품 제어에 필요하지 않음 |

MCP 포트·도구를 직접 확인하기 전에는 연결됐다고 주장하지 않는다. 최신 개발에서는 MCP 도구 미노출 상태를 기록하고 명령행/제품 API/Remote Control을 사용했다. [unreal-mcp.py](../../scripts/unreal-mcp.py)는 MCP 서버가 실제 켜져 있을 때만 사용할 수 있다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
print(sim.get_engine())  # start/stop은 비동기: ready를 별도 조회
state = sim.get_state()  # Unreal 준비 후
print(state['airframe'], state['aircraft'])
```

초기화/조작/시간 진행은 현재 시험 상태를 바꾼다. 필요한 경우 `pause()` → 입력 설정 → `step(.01)`로 실험한다. 연구 기체의 자동 이륙 예제 속도55m/s, 착륙40m/s는 [연구 모델 매뉴얼](../c208b-research.md)의 개발 예제이며 Thor 기본19/18m/s와 섞지 않는다. 실제 입력 범위는 `state.airframe`과 [전체 API 매뉴얼](../api-reference.md)을 따른다. `prepare_landing_approach()`는 시험 위치 초기화이며 자연 귀환 착륙의 대체 명령이 아니다.

비행시험은 [run-flight-test.py](../../scripts/run-flight-test.py)와 [report-flight-test.py](../../scripts/report-flight-test.py)를 사용한다. 기록기는 이전 동역학 snapshot을 복원하지 않으며 종료는 원래 프로필의 공중 트림·정지다. 실패 기록·원시 CSV·프로필·코드 지문을 보존한다. 완료한 시험 디렉터리를 덮어쓰지 않는다.

## 문서·코드·실행 근거 색인

| 주제 | 기준 문서 | 코드/자료 |
| --- | --- | --- |
| 전체 상태·인수인계 | [README](../../README.md), [저장소 안내](../repository-guide.md), [클라우드](../cloud-development.md) | `AGENTS.md`, `.vsconfig`, `scripts/dev.ps1` |
| API | [전체 매뉴얼](../api-reference.md) | `scripts/serve-api.py`, `uavsim_client.py`, `web/`, `Source/UAVSim/FlightScene.h` |
| 비행 수식·출처 | [비행 모델](../flight-model.md), [실속 참고](../stall-references.md) | `Source/FlightCore/FixedWing.h`, `Aerodynamics.h`, `FlightCore.h` |
| 프로필 | [기체 데이터](../aircraft-profile.md), [연구 모델](../c208b-research.md), [수납식](../retractable-research.md) | `aircraft/`, `AircraftParameters.h`, `Propulsion.h` |
| 최신 통합 | [항법·물리](../navigation-physics.md) | `Navigation.h`, `GroundContact.h`, `artifacts/navigation-physics/` |
| 자동 운항·지상 | [자동 이착륙](../automatic-flight.md), [지상 운용](../ground-operations.md) | `FlightOperations.h`, `Source/UAVSim/FlightAutomation.cpp`, `FlightGround.cpp` |
| 이전 귀환 개선 | [계획](../return-landing-plan.md), [결과](../return-landing-results.md) | `scripts/check-return-api.py`, `artifacts/return-flight-*/` |
| 지도·좌표 | [지도](../maps.md), [출처/라이선스](../../THIRD_PARTY_NOTICES.md) | `maps/`, `scripts/prepare-*.py` |
| 외형 | [STL](../aircraft-model.md), [프로펠러](../propeller-visual.md) | `aircraft/visual.json`, `PropellerGeometry.h`, `LandingGearVisual.h`, `Content/` |
| EO/IR·워게임 | [EO/IR](../eoir.md), [워게임](../wargame.md) | `web/eoir.html`, `weapons/`, `Gimbal.h` |
| 비행시험 | [계획](../flight-test-plan.md), [사용·결과](../flight-tests.md) | `tests/`, `scripts/run-flight-test.py`, `artifacts/flight-tests/index.html` |
| 기체 선정·확장 | [후보 조사](../aircraft-candidate-research.md), [확장 설계](../fixed-wing-expansion.md) | C208B/KingAir 연구 근거·판본·누락 자료 |
| 다음 개발 | [개발 계획](../development-plan.md), [P0~P6](../physics-expansion-plan.md) | 계획이며 아직 미착수 |
| SimConnect 조사 | [조사](../simconnect-reference.md) | `docs/simconnect-data/`, `scripts/collect-simconnect-reference.py` |

모든 개발 문서·참조 API 메서드·실행 스크립트의 기계 판독 색인은 [catalog.json](catalog.json)이다. 기준 파일의 SHA256과 제목/절을 포함한다. 전체 API를 이 요약에 복제하지 않고 원래 매뉴얼과 구현을 함께 유지한다.

## 대화 기록과 실행 사실의 범위

[conversation-records.json](conversation-records.json)은 접근 가능한 관련 대화4개의 **40턴**에 대해 사용자 메시지·최종 답변·명령/API 호출 수를 보존한다. 세 과거 개발 대화35턴과 현재 이전 대화5턴이며, 제공된 모든 페이지를 읽었다. 마지막 이전 작업 요청은 진행 중이어서 최종 답변이 아직 없다. 원본 전체 로그의 완전한 내보내기, 첨부 이미지 원본, 모델 내부 추론이나 모델의 모든 일반 지식을 보존한 자료는 아니다. 다른 프로젝트의 대화는 포함하지 않았다.

대화에는 당시의 미구현·보류·임시 실행 상태와 로컬 링크가 남아 있다. 현재 상태 판단에는 최신 사용자 결정·현재 코드와 문서를 우선한다. 과거 실행 실패와 수정 전 기록을 성공 기록으로 덮지 않는다. SimConnect 비교 대화에서의 ‘동일하게 만들 의도는 없다’는 사용자 의도를 유지한다.

2026-11-19 저장소 동기화 커밋 `4c0d769`에서 Windows CTest15/15, 기록기8검사, PowerShell5.1 실행 검사를 직접 통과했고 GitHub main 일치를 확인했다. [당시 점검 기록](../../artifacts/repository-sync-check.json)의 링크424개·근거310개 원본 보존·LFS 검사를 따른다. 이번 추가 인계 문서는 새 Linux/Unreal 실행 성공을 뜻하지 않는다.

과거 통합 검사·왕복·렌더링은 각각의 결과 문서와 artifacts에 있는 당시 기록이다. 현 PC 엔진/API의 최신 종료 사실은 과거 cleanup 기록에서 구분하고, 새 작업 시작 시 실제 상태를 다시 확인한다. 새 PC 실행, 실기체 성능·정식 시험·거버너/후류/완충은 미검증/미완료 범위를 유지한다.
