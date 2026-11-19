# UAVSim

Prepar3D/SimConnect를 대체할 Unreal 기반 고정익 무인기 시뮬레이터. 자체 C++ 계산 코어, 외부 제어 API, 대한민국 지형·위성영상 표시를 작은 실행 단위로 개발한다. 외부 비행역학 구현은 사용하지 않는다.

외부 프레임워크 연동의 기준 문서는 [전체 API 사용 매뉴얼](docs/api-reference.md)이다. 모든 HTTP 경로·Python 메서드, 필드·단위·상태·오류, 실행 예제를 모았으며 API 변경과 함께 갱신한다.

2026-11-19 [추진·지상·비행 영역 확장 및 공식 시험 후속 계획](docs/physics-expansion-plan.md)을 작성했다. 거버너/후류·완충/타이어·비대칭 실속/스핀·연료/CG와 운용/공식 시험의 단계·완료 기준을 정리한 계획이며 아직 구현 착수/완료를 뜻하지 않는다.

2026-11-18 [Prepar3D SimConnect 주입·관측 기능 조사](docs/simconnect-reference.md): v6 공개 SDK의 SimVar900행·별도 전역변수42개·이벤트1125행·함수208개를 CSV와 한국어 분야별 안내로 정리했다. 직접 쓰기·명령 이벤트·전용 API를 구분한 문서 조사이며, DLL/실제 기체의 실행 검증이나 UAVSim 구현 완료를 뜻하지 않는다.

**처음 내려받았다면 [저장소 시작·인수인계 안내](docs/repository-guide.md)를 먼저 읽는다.** Git LFS 자산 받기, 새 PC 실행 순서, 기능별 코드 위치와 이번 커밋 구성을 정리했다. 아래 검사 결과는 개발 당시 기록이며 새 PC에서 다시 실행한 결과를 뜻하지 않는다.

2026-11-19 저장소 동기화: 최신 연구 모델·항법/물리·외형·API/UI·비행시험 도구와 문서에서 참조하는 결과 자료를 함께 관리한다. Windows Release CTest15/15·기록 도구8검사를 이번 정리에서 확인했다. [클라우드 개발·새 PC 인계](docs/cloud-development.md)에 Linux 계산 검사 명령과 Windows Unreal 검증의 경계, 환경 게시 절차를 기록했다. 클라우드 실행·환경 게시와 Unreal 재빌드는 이번 정리에서 수행하지 않았다.

**2026-11-18 항법·물리 통합 개선:** 보류했던 근접 항로점/재접근·선회 잔차 분석·러더 협조를 구현했다. 수납식 연구 구성에 로터 관성/RPM·축동력/토크·운동량 추력·자이로 반작용·바퀴별 지면/횡미끄럼을 연결했다. 코어15/15·실제 API19검사와 무풍/작은 바람20km 임무 왕복을 통과했다. [기준 대비 결과·수식·가정·남은 범위](docs/navigation-physics.md). 실제 거버너/후류·완충·실기체 성능 검증은 별도다.

**이전 M0~M5 연구 범위 구현:** 원거리 자동착륙의 귀환·고도 처리·감속/형상·접근 정렬, 경로/진단 UI·API, 실제 지형의 중간 초기화 없는 왕복, 플랩·기어/바퀴/프로펠러 표시, 추력선 모멘트·기체별 지상 응답을 연결했다. 당시 Release CTest12/12·UE5.8.4 빌드·실제 관련 API173검사가 통과했다. [당시 결과·화면·사용법](docs/return-landing-results.md), [원래 계획과 진단](docs/return-landing-plan.md). 이 단계에서 보류했던 항법 작업은 위 통합 개선에서 진행했다. 실제 성능은 미검증이다.

## 현재 상태 — 2026-09-13

2026-11-18 기본 기체 후속 변경: 사용자 요청으로 **가상의 수납식 기어 연구 구성**을 선택했다. 동체 힌지부터 접히는 기어를3초 동안 표시하며 자동 이륙 후 수납·착륙 전 전개를 연결한다. 기존 C208B 고정기어 프로필은 별도로 유지한다. [조작법·수납 화면·검사 결과](docs/retractable-research.md).

2026-11-18 외형 후속 수정: 동체에 연결되는 주기어 지지대, 곡면3엽 프로펠러·스피너와 고속 회전 잔상 표시를 적용했다. [실제 화면·표시 검사·사용법](docs/propeller-visual.md).

**2026-11-17 추가 구현:** 사용자 승인한 **별도 C208B 연구 모델**로 C~F 연구 범위를 구현했다. 약3811kg·초기TAS65m/s·고정기어·단순 출력 제한 추진·플랩·기체별 접촉/항로/자동 운항을 연결했다. 관성·추진·저속 계수 등은 개발 가정이며 **실제 비행성능 미검증**이다. Release CTest **10/10**·UE **5.8.4 빌드**·[기존 API30개](artifacts/airframe-api-check.json)·[연구 API 검사](artifacts/c208b-api-check.json)가 통과했다. [사용법·가정](docs/c208b-research.md), [단계별 상태](docs/fixed-wing-expansion.md). 아래 표는 이전 Thor 개발 결과이며 현재 기본 선택과 구분한다.

| 항목 | 실제 상태 |
| --- | --- |
| 개발 환경 | VS2022에 MSVC 14.44, CMake, Windows SDK 10.0.26100 설치 완료 |
| 계산 코어·파일 처리 | WGS84↔ENU, 자체 비선형 6DOF·실속·ISA·바람·작동기·지상 접촉·자동 이착륙·STL 파싱·짐벌 기하. Release CTest 9/9 통과 |
| 실제 지형 | 대한민국 기본 48개 + 독도 상세 1개 + 제주 상세 1개로 50타일 준비. 제주 메시 약33×39m·영상2048². 기존 7개 지역 좌표/자세 이동 검사 통과. [상세 범위·출처](docs/maps.md) |
| Unreal 5.8.2 | C:\Program Files\Epic Games\UE_5.8에 설치 완료 |
| Unreal 프로젝트 | UAVSimEditor Win64 Development 빌드 성공. 제주 맵·위성 재질 생성 및 저장 완료 |
| MCP·외부 API | 내장 MCP 연동 확인. localhost:8765의 `/api/v1` 및 Python 클라이언트. 기어 추가 전 자동 이착륙·시스템 API 27/27, 실속 API 16개 검사 통과 |
| 조종면·Web UI | 수동·자동 이착륙, 명령/실제 작동기 출력, 돌풍·고장·기체 파일 재읽기 추가. 브라우저 직접 조작은 미수행 |
| Unreal 실행·종료 | Web UI·Python API에서 독립 비행 창 시작·정상 종료 구현. 변경 후 Development 빌드 성공, 실행 하네스 9개 검사 통과 |
| 비행 화면 | 기존 전국 49타일 Game 렌더 및 제주 상세 화면 보존. 최신 제주 상세 영역·영상 경계 처리는 [지도 안내](docs/maps.md) 참고 |
| 비행 모델 | Thor 공개 미계수에서 재구성한 비선형 6자유도. 실속 전환·조종효과 감소·재부착 지연, 추진/실속 매개변수는 개발 가정 |
| 자동항법 | 1~64점 순차 추종·최종 CW/CCW 선회·실속 회복 우선 제어. 작동기 추가 후 실제 Unreal API 14개 검사 통과 |
| 자동 이착륙·기체 설정 | 같은 기체의 이륙→귀환 착륙·이륙 중단, JSON 50개 매개변수·작동기·돌풍·고장. 기어 추가 전 실제 Unreal API 27/27, 366회 요청 및 원본 파일 복원 확인 |
| 지도 교체·배속 | maps 폴더와 좌표 JSON, PNG/JPEG·DEM 가져오기, 실행 중 지도 다시 읽기. 0.25~20배속과 이동 거리 표시, 실제 API 검사 통과 |
| 지상 운용 | 3개 바퀴 접촉·조향·브레이크·충돌 정지, 가상 시험 활주로 및 수동 활주·이착륙. 작동기 추가 후 실제 API 15개 검사 통과, 활주로 렌더 캡처 확인. 사용법·가정은 docs/ground-operations.md |
| 랜딩기어·API 매뉴얼 | 3초 전개·수납·잠금과 자동 이착륙 연계, API 21/21 통과. 20배속 자동 왕복에서 수납→전개→착륙 확인. 전체 API 매뉴얼·동반 갱신 규칙 추가 |
| STL 외형 | aircraft/visual.json으로 표시 형상 교체·정지 중 다시 읽기. 실제 API 28/28·STL 렌더 확인, 물리 상태 보존. [사용법](docs/aircraft-model.md) |
| EO/IR 영상·표적 | 별도 영상 페이지, CCD·IR 밝기 필터·수동 짐벌·선택 ID 지향과 가상 지상 표적 API. 실제 API 27/27·영상 렌더·메인 카메라와 독립 확인. [사용법·결과](docs/eoir.md) |
| 영상 가독성 | EO/IR 1280×960·JPEG92·FOV1~90°, 확대/전체화면/원본 크기·실제 수신 FPS·재생 제어. 실제 API 28/28·UI 정적 검사 통과. JPEG 요청 5회 중앙 약27ms |
| 짐벌 4모드 | 기체 고정·등록 표적 자동·위경고도 지향·가상 보호 모드. 실제 API 17/17 통과, 보호 영상 차단·명시적 재활성화와 기존 API 호환 확인. [사용법·결과](docs/eoir.md) |
| 워게임 연출·판정 | STL 표시 모델·고정 시간 애니메이션·운영자 명중/빗나감 판정·외부 탄체 따라보기. 시점 추가 후 실제 API 20/20 통과. 실제 유도·자동 명중·피해 계산은 없음. [사용법·결과](docs/wargame.md) |
| 첫 기체 레퍼런스 | UMN Ultra Stick 25e(Thor), 약 19m/s 부근 모델. 적용 범위는 docs/flight-model.md |

사용자 지시에 따라 컴퓨터 사용/UI 자동화를 중단했다. 명령행·파일·API 작업은 계속하고, 필요한 화면 작업만 사용자에게 안내한다.

메인 추적 카메라 크기 대응 수정: [C208B 카메라 적용·빌드 상태](docs/c208b-research.md)를 따른다.

[비행시험 기록·비교 사용법](docs/flight-tests.md): 기존 API 기반 명령행 시험·CSV/JSON·정적 그래프·두 실행 비교를 제공한다. 최종13회15,013개 표본·완전 재현·관성 비교·Thor 회귀를 완료했고, 사용자 요청으로 Unreal/API를 종료했다. 개발 검사와 실제 성능 검증을 구분하며, 구현 순서는 [작업 계획](docs/flight-test-plan.md)을 따른다.

## 실행

Windows PowerShell 5.1 또는 PowerShell 7에서 프로젝트 루트를 기준으로 실행한다. C++ 구성은 .vsconfig, 데이터 도구 버전은 requirements-terrain.txt에 기록했다.

평소에는 아래 명령으로 API를 시작하고 [조종 UI](http://127.0.0.1:8765/)의 **Unreal 실행**을 누른다. API가 이미 실행 중이면 브라우저만 열면 된다. 전국 지형을 읽는 비행 창이 열리고 지형이 준비되면 조종 입력이 활성화된다. 에디터에서 Play를 누를 필요는 없다. **Unreal 종료**를 눌러도 API와 Web UI는 유지되어 다시 실행할 수 있다.

```powershell
.\scripts\dev.ps1 api
```

슬라이더로 조종면을 움직이고 Unreal 적용값을 확인한다. 비행 일시정지·0.1초 진행·초기화는 비행 시간 제어이며 Unreal 프로세스 종료와 별개다. API 서버는 백그라운드로 실행되며 시작 명령이 PID를 출력한다. API 서버 자체를 종료할 때는 Unreal을 먼저 종료하고 해당 API PID에 `Stop-Process -Id <PID>`를 실행한다.

카메라는 메인 페이지의 EO/IR 링크 또는 [별도 영상 페이지](http://127.0.0.1:8765/eoir)에서 제어한다. CCD/IR 영상 종류와 기체 고정·표적 자동·지리좌표·보호의 짐벌 운용 모드를 따로 선택한다. 보호는 후방 180°·위 30°의 가상 자세와 영상 OFF이며, 다른 모드로 해제한 뒤 센서를 명시적으로 켠다. IR은 밝기 필터, 추적은 알려진 좌표 지향이다. [EO/IR 사용 안내](docs/eoir.md).

영상 위의 **넓게 보기·전체 화면·원본 크기**로 확대하고, 센서 설정의 **1°·5°·30°** 버튼으로 시야각을 즉시 바꾼다. **비행·연출 재생**과 **일시정지**도 같은 페이지에서 사용할 수 있다. 영상은 1280×960이며 수신 완료 후 다음 프레임을 요청하는 최대 목표 10Hz 방식으로 실제 수신 FPS·간격을 표시한다.

같은 페이지의 워게임 패널에서 표시 모델과 표적을 선택해 **발사 연출**을 실행한다. 재생 시간이 끝나면 운영자가 **명중 판정** 또는 **빗나감 판정**을 선택한다. 모델 파일과 상태·초기화·Python 예제는 [워게임 안내](docs/wargame.md)를 따른다.

빌드·데이터 준비와 에디터 개발 작업은 다음 명령을 사용한다.

```powershell
# 에디터 없이 계산 코어·파일 처리 빌드 + 검사 9개
.\scripts\dev.ps1 check
# 도구 파일 경로 확인
.\scripts\check-environment.ps1
# 전국 데이터 재생성 (source 캐시가 있으면 재사용)
.\.venv\Scripts\python.exe scripts\prepare-korea.py
# 생성한 전국 지도에 제주 상세 영역 재적용 (기존 제주 캐시 사용)
.\.venv\Scripts\python.exe scripts\prepare-jeju-detail.py
# 기존 제주 상세 데이터 재생성
.\.venv\Scripts\python.exe scripts\prepare-terrain.py
# Unreal 프로젝트 빌드 (실행 확인 완료)
.\scripts\dev.ps1 build
# 최초 장면 생성 (실행 확인 완료, 완료 후 에디터 종료)
.\scripts\dev.ps1 setup-scene
# MCP·에디터 개발 작업 (에디터의 Play로 비행 시작)
.\scripts\dev.ps1 editor
```

setup-scene은 위성 재질과 /Game/Maps/JejuDemo를 만들고 FlightScene을 배치하는 일회성 작업이다. 명령은 에디터를 시작한 뒤 먼저 반환하며, Python 작업이 끝나면 에디터가 정상 종료된다. Saved/Logs/UAVSim.log의 `UAVSim scene setup complete`와 artifacts/scene-setup.json으로 완료를 확인한다. 현재 장면은 이미 생성되어 있다.

별도로 연 에디터나 비행 창은 `external`로 표시하며 해당 창에서 직접 종료한다. API 서버를 재시작해 이전 비행 프로세스의 소유 정보를 잃은 경우도 같다. Web UI 종료는 이 API 서버가 시작하고 초기화가 끝난 독립 비행 창에만 적용한다. 지형 로드에 실패한 창도 정상 종료를 요청할 수 있으며 강제 종료하지 않는다. 프로세스가 살아 있는 오류 상태에서는 먼저 종료한 뒤 다시 실행한다.

2026-11-18 실행 호환 수정: Windows PowerShell5.1에 없는 `ProcessStartInfo.ArgumentList` 대신 해당 환경에서는 Windows 인수 인용 처리를 사용한다. 공백·따옴표·빈 인수·한글·끝 역슬래시 전달과 종료 코드를5.1/7에서 검사했고,5.1의 실제 API 시작·웹/상태 HTTP200을 확인했다. 이 PC의 시작 명령은 `.\scripts\dev.ps1 api -UnrealRoot 'C:\UnrealEngine-release'`다. 재현 검사는 `powershell.exe -NoProfile -File tests/dev_process_checks.ps1`로 실행한다. Unreal 빌드/비행시험은 이 실행 스크립트 수정 때문에 반복하지 않았다.

다른 엔진 경로는 `dev.ps1 api -UnrealRoot 'C:\경로\UE_5.8'` 또는 `UAVSIM_UNREAL_ROOT`로 지정한다. 서버 직접 실행 시에는 `serve-api.py --unreal-root`를 사용한다. MSBuild의 PATH/Path 중복 문제는 하네스의 자식 프로세스 환경에서만 정리한다.

## 계산과 표시의 경계

외부 프로그램 → 명시적 제어 함수 → 계산/좌표 코어 → Unreal 표시. 개발용 Unreal MCP와 제품 제어 경로는 분리한다.

- Source/FlightCore/FlightCore.h: C++17 표준 라이브러리만 사용. 전역 상태·Unreal 타입·벽시계 의존성 없음. 잘못된 입력은 std::nullopt.
- 위치는 위도/경도(deg), **WGS84 타원체고(m)**. 계산·지형 raw는 ENU 동/북/위(m)이며, Unreal 표시 경계에서 동/남/위(cm)로 변환해 영상 좌우 반전을 방지한다.
- Aerodynamics.h의 계수·대기와 FixedWing.h의 비선형 공력·강체 방정식을 quaternion RK4로 적분한다. 기초 검사에 남아 있는 일정 합력 step과 실제 비행 모델은 별개다.
- 내장 기체 외형은 기본 Thor 설정의 날개폭 1.27m에 맞춘 개발 형상이며 STL로 교체할 수 있다. 표시 외형·공력 프로필·접촉 모델은 분리한다. STL이나 프로필의 날개폭을 바꾸어도 1.27m 개발 기체의 바퀴·동체 접촉점은 자동 재구성되지 않는다. 내장 외형·기어 기구는 실제 Thor의 CAD 자료가 아니다. 위치·자세는 자체 비행역학 결과를 표시한다. 초기 상태는 제주 원점 위 타원체고 2600m, 19m/s, 북쪽 수평비행 트림이다.
- Navigation.h는 조종면·스로틀만 계산하며 위치·자세를 직접 바꾸지 않는다. 수동 조종도 동일한 비행역학을 통과한다.
- 화면 비행은 .01초 고정 간격으로 진행하고 지면 근처에서는 최대 .002초로 분할하여 접촉을 계산한다. 벽시계 진행량에 0.25~20배속을 적용하며 한 프레임의 벽시계 지연은 최대 .25초만 따라간다. 실제 진행 배속은 최근 약 .5초의 시뮬레이션 시간/벽시계 시간으로 표시한다. 성능 부족 시 목표 배속보다 낮아질 수 있으며 실시간 주기 보장 기능은 아니다. 수동 step의 시간은 배속을 곱하지 않는다.

## 대한민국 지형·영상

현재는 **동경 124~132°, 북위 33~39°**에 1° 기본 타일 48개·독도 보완 1개·제주 상세 1개를 사용한다. 본토·제주·울릉도·독도를 포함하며 국경에 맞춘 잘라내기는 하지 않아 주변 바다와 인접 육지도 보인다. 타일은 시작할 때 로컬 파일에서 모두 읽는다. 거리별 스트리밍은 아직 없다. 제주 상세 영상의 원본 정보 해상도는 10m이며 [출력 격자·경계 연결·재생성](docs/maps.md)을 구분해 기록했다.

| 자료 | 기준/처리 |
| --- | --- |
| Copernicus DSM GLO-30 | EGM2008 정표고. 건물·식생을 포함하는 표면 자료이며 DTED 원본이 아님 |
| 전국 기본 영상 | EOX Sentinel-2 cloudless 2016, CC BY 4.0. 실제 합성 자료 연도는 2016–2017 |
| 보완 영상 | 구름이 심한 동남부·울릉도 및 독도에 실제 Sentinel-2 2025 영상을 보완 |
| NGA EGM2008 | 정표고에 지오이드고를 더해 WGS84 타원체고로 변환 |
| 기본 메시·영상 | 타일당 257² 정점·1024² 영상. 지형 간격 약 340~435m, 영상 약 84~109m/pixel |
| 독도 메시·영상 | 약 4×5km 영역의 257² 정점·1024² 영상. 고도 정확도는 원본 DEM의 한계가 있음 |

렌더 원점은 36°, 128°, 타원체고 0m다. 타일은 ECEF→ENU로 지구 곡률을 반영하며 상대 정점과 배치 위치를 분리했다. `maps/korea/terrain.json`에 범위·출처·라이선스·해시·검사를 기록한다. 기본 메시의 인접 경계 최대 차이는 float32 저장 후 약 3.9mm였다. 작은 해상도 모델의 수치 연속성 검사이며 실측 지형 정확도를 의미하지 않는다.

[조종 UI](http://127.0.0.1:8765/)에서 **전국 보기**, **기체 추적**을 전환하고 제주·서울·부산·대구·강릉·울릉도·독도로 이동할 수 있다. 지역 이동은 2600m/19m/s로 초기화하고 정지한다. 비행 재생을 눌러 출발한다. 초기화할 때 그 위치에 비행 계산 원점을 새로 잡고, 기체 자세도 전국 렌더 좌표계로 변환한다. 자체 비행 모델의 항로·비행 범위는 그 원점에서 50km로 유지한다.

기존 제주 20×20km 상세 데이터(약 39m 메시·10m 영상)는 `data/jeju`에 보존되어 있다. 전국 기본 지형에서는 이 상세 데이터가 중첩 표시되지 않는다. 평소에는 `maps/active.json`이 지정한 지도를 읽는다. 선택 파일이 없을 때만 이전 `data/korea`, `data/jeju` 구조를 호환 로드한다. 재읽기가 실패하면 이전 지형과 기체 위치를 유지하고 일시정지하며 오류를 표시한다.

`scripts/check-korea-api.py`는 실제 Unreal의 기체 컴포넌트 위치·방향을 독립 pyproj/ECEF 계산과 비교하고 7개 지역 초기화·시간 진행, 카메라 전환, 잘못된 요청의 상태 유지, 좌우 조종면 표시를 검사한다. 모두 종료 코드 0으로 통과했다. 당시 Development 빌드 후 49타일을 로드한 [전국 화면](artifacts/korea-unreal-overview.png)과 [서울 추적 화면](artifacts/korea-seoul-flight.png)도 확인했다. [검사 기록](artifacts/korea-api-check.json), [당시 실행 상태](artifacts/korea-runtime.json), [지형 준비 기록](artifacts/korea-preparation.json), [자료 출처](THIRD_PARTY_NOTICES.md). 현재 제주 상세 준비·경계 처리 결과는 [지도 안내](docs/maps.md#제주-상세-영역과-표시-품질)를 따른다.

생성 파일 약 79MB와 원본 창 캐시 약 58MB를 사용한다. 초기판에는 일부 구름·눈과 촬영 시기별 색상 경계가 남아 있다. 전국 가까운 비행에서 제주 상세 영역과 같은 화질을 내려면 지역별 상세 타일과 거리별 로딩을 추가해야 한다.

```powershell
.\.venv\Scripts\python.exe scripts\check-korea-api.py
# 기존 제주 범위에 대한 보유 DTED 입력 경로 (실제 DTED 파일로는 아직 미시험)
.\.venv\Scripts\python.exe scripts\prepare-terrain.py --dem 'C:\Data\e126\n33.dt2' --vertical-datum EGM96 --output data\jeju-dted
```

## 지도 파일 교체·추가

[지도 사용 안내](docs/maps.md)에 출처 URL, 폴더 구조, 좌표 JSON 전체 예제를 기록했다. 현재 활성 파일은 `maps/active.json`의 `{"manifest":"korea/terrain.json"}`이다.

- 같은 영역의 영상만 교체: `maps/korea/tiles/<타일ID>/satellite.png`를 교체하고 Web UI **지도 다시 읽기**를 누른다. JPEG·다른 파일명은 `maps/korea/terrain.json`의 해당 `image_file`을 수정한다. 영상은 같은 위경도 범위·북쪽 위 방향이어야 한다.
- 다른 지역·DEM·타일 추가: `maps/example`을 `maps/my_map`으로 복사하고 `source/`에 PNG/JPEG 또는 RGB GeoTIFF와 고도 GeoTIFF/DTED를 넣는다. `map.json`의 `tiles` 배열에 파일명과 `[서경도, 남위도, 동경도, 북위도]`, 고도 기준을 지정한다.
- `terrain.json`의 경계 숫자만 바꾸면 메시가 다시 계산되는 것은 아니다. 고도나 범위가 바뀌면 아래 준비 스크립트로 재생성한다. Unreal 재빌드는 필요 없다.

```powershell
.\.venv\Scripts\python.exe scripts\prepare-map.py maps\my_map\map.json --activate
```

이후 Web UI **지도 다시 읽기** 또는 Python `sim.reload_terrain()`을 호출한다. 기본 제공 `maps/example/map.json`은 로컬 실제 제주 자료로 변환까지 완료했다. [교체 JPEG의 실제 렌더](artifacts/custom-map-jpeg-render.png), [가져오기 검사](maps/example/verification.json), [지도·배속 API 검사](artifacts/map-playback-api-check.json)를 남겼다. 실제 DTED 파일 입력은 아직 미시험이다.

## 외부 Python API와 Web UI

STL 기체 파일은 `aircraft/models` 아래에 놓고 `aircraft/visual.json`에서 단위·방향·CG를 지정한다. 정지 후 `sim.reload_aircraft_visual()` 또는 Web UI의 **기체 모델 다시 읽기**로 적용한다. 위치·시간·기어·조종 입력은 유지한다. 단일 STL 내부의 조종면·기어를 자동 분리하지 않으며 내장 기어 표시를 선택할 수 있다. [외형 파일 사용 안내](docs/aircraft-model.md).

정확한 전체 계약과 외부 Python·curl 실행 예는 [API 사용 매뉴얼](docs/api-reference.md)을 따른다. 아래는 주요 기능과 기존 검사 기록이다.

외부 프레임워크와 Web UI는 같은 [로컬 HTTP API](http://127.0.0.1:8765/api/v1/state)를 사용한다. Python 표준 라이브러리 서버가 Unreal을 실행하고 제어 요청을 Remote Control에 전달한다. 실행 중인 게임 Actor 경로와 프로세스 ID는 BeginPlay가 작성하는 `Saved/uavsim-runtime.json`의 `object_path`, `process_id`에서 읽으므로 외부 프로그램이 PIE 이름을 지정하거나 MCP에 연결할 필요가 없다. 현재는 동일 PC의 활성 기체 한 대를 다루는 초기 API이며, 고정 지연이나 실시간 주기를 보장하지 않는다.

외부 Python 프로젝트의 가상환경에서 클라이언트를 설치한다. 런타임 의존성은 없으며 Python 3.9 이상을 사용한다.

```powershell
python -m pip install -e C:\src\Projects\UAVSim
```

Web UI 또는 `sim.start_engine()`으로 실행한 뒤 `sim.get_engine()`의 `ready`가 `true`이면 아래 조종 예제를 사용할 수 있다. 시작·종료 호출은 완료를 기다리지 않으며 `get_engine()`으로 상태를 조회한다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient("http://127.0.0.1:8765")
sim.pause()
state = sim.set_controls(
    elevator_deg=10, aileron_deg=5, rudder_deg=0, throttle=0.3
)
print(state["controls"])
sim.step(0.1)
print(sim.get_state()["aircraft"])
```

조종 입력은 네 필드를 한 요청으로 모두 보낸다. 각도는 **식별 기준 트림 대비 -25~25°**, 스로틀은 **0~1**이다. 실제 초기 입력은 고도·속도별 계산 트림이며 2600m/19m/s 무풍에서는 엘리베이터 약 -.597°·스로틀 .391이다. 실제 Thor의 기계적 한계나 모터 보정값은 아니다. 양수 엘리베이터는 양쪽 후연 아래, 양수 에일러론은 오른쪽 후연 위·왼쪽 후연 아래, 양수 러더는 후연 오른쪽이다. 수동 입력은 자동항법을 해제한다. Web UI와 Python이 동시에 제어하면 나중에 적용된 요청이 상태를 덮어쓴다.

| HTTP 경로 (`/api/v1` 기준) | JSON 요청 / Python 메서드 |
| --- | --- |
| `GET /engine` | 본문 없음 / `get_engine()`; Unreal 프로세스 상태 |
| `POST /engine/start` | `{}` / `start_engine()`; 고정된 UAVSim 프로젝트를 `UnrealEditor -game`으로 실행 |
| `POST /engine/stop` | `{}` / `stop_engine()`; 이 서버가 시작한 독립 비행 창 정상 종료 |
| `GET /state` | 본문 없음 / `get_state()` |
| `PUT /time-scale` | `{"factor": 5}` / `set_time_scale(factor)`; 0.25~20배속 |
| `POST /terrain/reload` | `{}` / `reload_terrain(timeout=60)`; 선택 지도 재읽기, 초기화·정지 |
| `PUT /view` | `mode`: `overview`/`aircraft`/`projectile` / `set_view(mode)`; 탄체 시점은 진행 연출이 있을 때 선택 |
| `PUT /eoir` | `enabled`, `mode`(ccd/ir_white/ir_black), `fov_deg` 1~90° / `set_eoir(...)` |
| `PUT /eoir/gimbal-mode` | `mode`와 모드별 각도·표적 ID·위경고도 / `set_gimbal_mode(...)`; body_fixed/auto/geo/protect |
| `PUT /eoir/gimbal` | `azimuth_deg` −180~180°, `elevation_deg` −90~30° / `set_gimbal(...)`; 수동 지향·추적 해제 |
| `POST /eoir/track` | `target_id` / `track_target(target_id="")`; 선택 ID 지향, 빈 문자열은 해제 |
| `POST /targets` | `id`, `kind`(tank/missile/marker), `latitude_deg`, `longitude_deg`, `heading_deg` / `create_target(...)` |
| `PUT /targets/pose` | `id`, `latitude_deg`, `longitude_deg`, `heading_deg` / `move_target(...)`; 지면 위 재배치 |
| `POST /targets/remove` | `id` / `remove_target(id)` |
| `GET /eoir/frame.jpg` | 본문 없음 / `get_eoir_frame()`; 1280×960 JPEG bytes와 시간·순번·모드 헤더 |
| `POST /wargame/weapons/reload` | `{}` / `reload_wargame_weapons()`; 정지·진행 연출 없음 상태에서 표시 목록만 교체 |
| `POST /wargame/fire` | `weapon_id`, `target_id` / `play_wargame_shot(...)`; 저장된 두 점 사이의 시간 지정 연출 |
| `POST /wargame/resolve` | `shot_id`, `outcome`(hit/miss) / `resolve_wargame_shot(...)`; 완료한 연출의 운영자 판정 |
| `POST /wargame/reset` | `{}` / `reset_wargame()`; 연출·기록·명중 표시·워게임 시간 초기화 |
| `PUT /controls` | `elevator_deg`, `aileron_deg`, `rudder_deg`, `throttle` / `set_controls(...)` |
| `PUT /ground-controls` | `steering_deg` ±30°, `brake` 0~1, `parking_brake` bool / `set_ground_controls(...)`; 세 필드 모두 지정, 수동 조작으로 항법 해제 |
| `PUT /gear` | `down` bool / `set_landing_gear(down=True)`; 시뮬레이션 3초 전개·수납, 지상 수납·활성 자동 운항 중 수동 명령 거부 |
| `POST /ground/reset` | `{}` / `reset_on_ground()`; 시험 활주로에 속도·추력 0, 주차 브레이크, 정지 |
| `PUT /ground/initial-condition` | `latitude_deg`, `longitude_deg`, `heading_deg`, `ground_speed_mps` 0~15m/s / `set_ground_initial_condition(...)`; 지면 높이·자세 자동 계산, 정지 |
| `PUT /pose` | `latitude_deg`, `longitude_deg`, `ellipsoid_height_m`, `heading_deg`, `pitch_deg`, `roll_deg` / `set_pose(...)`; 적용 후 정지 |
| `PUT /initial-condition` | pose의 6필드 + TAS `speed_mps`; 선택적으로 `angle_of_attack_deg`, `sideslip_deg` 함께 지정 / `set_initial_condition(...)`; 항법 해제·각속도 0·정지 |
| `PUT /environment` | `wind_east_mps`, `wind_north_mps`, `wind_up_mps` 각 ±30m/s / `set_wind(...)` |
| `PUT /gust` | `east_mps`, `north_mps`, `up_mps` 각 ±15m/s, `delay_s` 0~600s, `duration_s` .1~120s / `set_gust(...)` |
| `PUT /failures` | `engine_power_fraction` 0~1, `elevator_jammed`, `aileron_jammed`, `rudder_jammed` bool / `set_failures(...)` |
| `POST /aircraft/reload` | `{}` / `reload_aircraft_profile()`; 정지 중 aircraft/active.json 재읽기·공중 트림 초기화 |
| `POST /aircraft/visual/reload` | `{}` / `reload_aircraft_visual(timeout=30)`; 정지 중 외형만 다시 읽기, 물리 상태 유지 |
| `POST /automation/takeoff` | `runway_index` 0~15 정수, `climb_height_m` 20~150m, `speed_mps` 16~24m/s / `start_automatic_takeoff(...)`; 재생/정지 유지 |
| `POST /automation/land` | `runway_index` 0~15 정수, `approach_speed_mps` 16~22m/s / `start_automatic_landing(...)`; 재생/정지 유지 |
| `POST /automation/prepare-landing` | `runway_index` 0~15 정수 / `prepare_landing_approach(...)`; 착륙 시험 위치로 초기화·정지 |
| `POST /automation/cancel` | `{}` / `cancel_flight_operation()`; 자동 이착륙 해제 |
| `PUT /route` | `waypoints`, `loiter_radius_m`, `clockwise` / `set_route(...)`; 항법 활성화, 기존 재생/정지 유지 |
| `POST /navigation/cancel` | `{}` / `cancel_navigation()`; 현재 조종 입력을 유지하며 수동 모드로 전환 |
| `PUT /simulation` | `{"running": true}` 또는 `false` / `set_running(...)`, `pause()` |
| `POST /reset` | `{}` / `reset()`; 19m/s 계산 수평 트림, 시간 0, 바람 유지, 정지 |
| `POST /step` | `{"dt_s": 0.1}` / `step(...)`; 정지 중 `0 < dt_s <= 1`초 진행 |

엔진 경로의 응답은 `status`, `pid`, `managed`, `ready`, `message`다. `status`는 `stopped`, `starting`, `running`, `stopping`, `error`, `external` 중 하나이며 `managed`는 이 API 서버가 시작한 프로세스인지 나타낸다. `ready`는 기체 API와 지형 준비가 끝났음을 뜻한다. 엔진이 꺼져 있어도 엔진 상태와 Web UI를 사용할 수 있다.

JPEG 프레임을 제외한 나머지 JSON 경로의 성공 응답은 `aircraft`, `controls`, `actuators`, `gear`, `ground`, `navigation`, `automation`, `airframe`, `visual`, `disturbance`, `object_path`, `model`, `fault`, `view_mode`, `terrain`, `environment`, `eoir`, `wargame`, `targets`를 포함한다. 워게임 연출·판정과 표적 명중 표시는 [워게임 상태](docs/wargame.md#상태-필드)를 따른다. 카메라·표적 필드는 [EO/IR 상태](docs/eoir.md#상태)를 따른다. `terrain`은 `dataset`, `tile_count`, `manifest`, `attribution`, `error`다. `model`은 `thor_nonlinear_6dof_v2`다. `aircraft`에는 위치·자세, `speed_mps`, ENU 수직속도 `vertical_speed_mps`, 기체축 p/q/r의 `roll_rate_deg_s`, `pitch_rate_deg_s`, `yaw_rate_deg_s`, 시간과 실행 상태, `time_scale`, `effective_time_scale`, `distance_travelled_m`이 있다. 거리는 마지막 초기 조건·초기화·지도 재읽기 이후 국소 3D 경로 길이이며 m다. 정지하면 실제 배속은 0이고, 설정 배속은 유지한다. `navigation`은 `mode`(manual/waypoint/loiter), 0부터 시작하는 `active_waypoint_index`(수동 -1), `waypoint_count`, 수평 거리 `distance_to_waypoint_m`, `loiter_radius_m`다. `ground`의 접촉·반력·AGL·충돌 상태는 [지상 상태 표](docs/ground-operations.md#지상-상태), 자동 운항은 [운항 상태](docs/automatic-flight.md#운항-상태), 작동기·프로필·고장은 [기체 데이터 안내](docs/aircraft-profile.md)를 따른다.

잘못된 입력은 HTTP 400, 현재 상태에서 거부된 명령은 409, Unreal 미연결은 503과 `{"error":{"message":"..."}}`를 반환한다. 잘못된 항로는 전체를 거부하고 기존 항로를 유지한다. 적분이 수치 계산 범위를 벗어나면 마지막 유효 상태에서 정지하고 `fault`를 기록한다. `reset()` 또는 새 초기 조건으로 복구한다. 쓰기 요청은 자동 재시도하지 않으며 타임아웃 후에는 적용 여부를 상태로 확인한다.

이번 Unreal Development 빌드와 클라이언트 editable 설치가 성공했다. 프로젝트 밖 작업 디렉터리의 격리된 Python에서 설치한 클라이언트를 import하고 조종 입력을 실제 적용했다. 아래 하네스의 17개 검사(왕복, 잘못된 입력 거부·상태 유지, 위치 설정, 수동 시간 진행·재현)가 종료 코드 0으로 통과했다. [결과 기록](artifacts/control-api-check.json)에 실제 경로와 반환 입력이 있다. 네 입력을 적용한 뒤 MCP로 다섯 힌지의 회전을 읽어 좌우 에일러론의 반대 방향을 확인했고([힌지 기록](artifacts/control-hinges.json)), [실제 게임 화면](artifacts/control-surfaces.png)에서도 조종면 표시를 확인했다. Web UI JavaScript 구문 검사도 통과했으며 사용자 지시에 따라 브라우저 UI 자동 조작은 수행하지 않았다.

```powershell
.\.venv\Scripts\python.exe scripts\check-control-api.py
```

실행·종료 변경 후 Unreal Development 빌드가 성공했다. 아래 하네스의 **9개 검사가 종료 코드 0으로 통과**했다. 실제 시작→지형·API 준비→조종 입력→정상 종료→재실행, 중복 요청 처리와 종료 후 Web UI 응답을 확인했다. 지형 오류 상태에서 중복 실행을 거부하는 분기도 확인했다. [결과 기록](artifacts/engine-api-check.json)에 두 실행 PID를 남겼다. 하네스는 API가 켜지고 Unreal이 꺼진 상태에서 실행하며, 완료 후 Unreal을 꺼 둔다.

```powershell
.\.venv\Scripts\python.exe scripts\check-engine-api.py
```

## 재생과 시험용 배속

재생 중 기체는 추적 카메라의 중앙에 남아 있고 지형이 지나간다. 기존 보고 당시 3.03초 동안 위치가 약 60m 바뀌었으므로 비행 정지는 아니었다. 전국 약 100m/pixel 영상과 2600m 고도에서는 움직임이 작게 보인다. Web UI 재생 버튼은 기체 추적 화면으로 전환하며 속도·이동 거리·시뮬레이션 시간·실제 진행 배속을 표시한다.

```python
sim.set_time_scale(5)
sim.set_running(True)
# 정지 후 .5초만 정확히 진행: 설정 배속은 곱하지 않는다.
sim.pause()
sim.step(.5)
```

실제 Unreal 검사에서 요청 .25/1/5/20배속은 벽시계 측정 약 .241/.993/4.929/19.607배였다. 호출 지연을 포함한 짧은 개발 검사 결과다. 두 배속(.25·20)에서 수동 .5초 진행의 위치·시간·거리가 같았고 잘못된 배속·지도 경로·이미지를 거부하는 것도 확인했다. 이 검사는 `scripts/check-map-playback-api.py`로 재현하며 종료 후 원래 지도·1배속·일시정지로 복구한다.

## 실속과 비선형 비행역학

Web UI를 새로고침한 뒤 **실속 시험 준비 → 비행 재생**을 누르면 현재 위치에서 9m/s·받음각 18°·무추력 상태를 재현한다. 받음각 증가에 따른 양력 감소·항력 증가·조종 효과 감소와 재부착 지연이 같은 자체 물리 계산에 연결된다. 기수 하강 입력과 추력으로 받음각을 줄이고, 줄어든 뒤 하강 입력을 풀어 자세를 회복한다. 자동항법은 실속 시 회복을 우선하며 상태에 `navigation.stall_recovery`를 표시한다.

API `aircraft`에는 `angle_of_attack_deg`, `sideslip_deg`, `ground_speed_mps`, `air_density_kg_m3`, `dynamic_pressure_pa`, `lift_coefficient`, `drag_coefficient`, `flow_separation`, `stall_warning`, `stalled`, `load_factor`가 추가됐다. `environment`에는 ENU 바람 세 성분이 있다. 기존 `speed_mps`는 TAS, 새 `ground_speed_mps`는 수평 지상속도다. 초기 속도 5~45m/s와 임의 받음각은 시험 초기조건 허용 범위이며 지속 가능한 수평비행 범위가 아니다.

[식·계수·개발 가정·Python 예제](docs/flight-model.md)를 기록했다. 실측 실속 곡선을 확보하지 못해 실속 전환각 12~20°와 추진 등은 명시적 개발 가정이다. 현재 구현을 상용 시뮬레이터와 동등한 정확도 또는 정식 시험 완료 모델로 보지는 않는다.

Unreal 최종 빌드, 독립 코어 검사, 실제 [실속 API 16개 검사](artifacts/stall-api-check.json)와 [기존 항법 API 14개 검사](artifacts/navigation-api-check.json)가 통과했다. 무풍 2600m/19m/s 수평 트림, 저속 실속·수동 회복·자동 회복, 바람에 의한 지상 이동, .25/20배속 간 명시적 시간의 동일성을 확인했다. 독립 항법 검사에는 2600m·횡풍 3m/s 및 실속 진입도 포함한다. 실제 비선형 항법의 20배속 벽시계 측정은 약 19.62배였다([기록](artifacts/nonlinear-playback.json)). [Unreal 실속 표시 화면](artifacts/stall-unreal.png)도 저장했다.

```powershell
.\.venv\Scripts\python.exe scripts\check-stall-api.py
```

## 지상 활주·수동 이착륙

Web UI의 **지상 시작**은 첫 가상 시험 활주로 시작점 안쪽 30m에 기체를 정지 상태로 놓는다. 현재 기본값은 제주 33.509°N, 126.49°E 중심의 방위 60°, 1000×40m 시험 활주로다. 실제 공항 시설을 재현한 위치가 아니다. 무풍 개발 예제에서는 주차·일반 브레이크 해제 → 스로틀 100%·비행 재생 → TAS 11m/s 이상에서 엘리베이터 −6° → 이륙 후 당김 완화 순서로 조작한다. 저속에서 앞바퀴로 조향하고 착지 후 브레이크를 적용한다. 이 수치는 개발 모델의 조작 예시이며 실제 기체의 이륙 속도·운용 절차가 아니다.

동체·날개 접촉이나 지면 법선 접근속도 4m/s 초과의 바퀴 접촉은 `ground.crashed`와 `fault`를 기록하고 정지한다. 다시 **지상 시작** 또는 초기 조건 설정으로 복구한다. 지형 자료가 없으면 `ground.available=false`이며 가상의 높이 0 지면을 만들지 않는다. 지상에서 공중 항로 설정은 거부하며 자동 이착륙은 별도 운항 명령을 사용한다.

[지상 운용 안내](docs/ground-operations.md)에 Python 예제, `runways.json` 설정, CG의 지면 법선 거리인 AGL, 반력의 정의와 접촉 모델 한계를 기록했다.

## 자동 이착륙·기체 파일·고장 시험

Web UI에서 **지상 시작 → 자동 이륙**으로 출발하고, 공중에서 **자동 착륙**으로 같은 활주로에 돌아온다. 착륙만 시험할 때는 **착륙 시험 위치 → 자동 착륙**을 사용한다. 시험 위치 버튼은 위치를 초기화하며 자동 운항 명령 자체는 기체를 순간 이동시키지 않는다. Python에서는 운항 준비와 명시적 시간 진행을 나누어 호출할 수 있다. [자동 이착륙 안내](docs/automatic-flight.md).

`aircraft/active.json`으로 선택한 파일에서 기체 매개변수 50개를 읽는다. **기체 파일 다시 읽기**는 정지 중 공중 트림으로 초기화하고 돌풍·고장·자동조종을 지우며 일정 바람을 유지한다. Web UI·API는 조종 명령과 실제 작동기 출력, 단일 돌풍, 엔진 출력 상실·조종면 고착을 제공한다. [파일 구조·단위·작동기·고장 안내](docs/aircraft-profile.md).

기어 전개·수납 추가 전 Unreal Development 빌드, Release CTest **7/7**, 실제 자동 운항·시스템 API **27/27**이 통과했다. 366회 API 요청으로 작동기 해석해·돌풍 재현·잘못된 프로필의 상태 유지와 파일 복원을 확인했다. 기본 무풍 시험에서 이륙 후 고도 유지까지 33초, 같은 기체의 귀환 착륙·주차까지 이륙 시작부터 261.25초였다. 명시적 착륙 시험 위치에서는 42.32초, 초기 엔진컷 이륙 중단은 출발부터 2.03초에 정지했다. 모두 시뮬레이션 시간이며 실기체 성능 자료가 아니다. [실행 기록](artifacts/automatic-api-check.json).

```powershell
.\.venv\Scripts\python.exe scripts\check-automatic-api.py
```

## 항로점 자동항법

같은 API에 한 점 또는 여러 점을 배열로 보낸다. 각 점은 위도·경도(deg), WGS84 타원체고(m), 대기속도(m/s)다. 속도 목표는 바람에 대한 진대기속도이며 지상 경로는 바람에 따라 달라진다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.reset()  # 초기 상태로 복귀하고 정지
waypoints = [
    {"latitude_deg": 33.433, "longitude_deg": 126.520,
     "ellipsoid_height_m": 2600, "speed_mps": 19},
    {"latitude_deg": 33.433, "longitude_deg": 126.524,
     "ellipsoid_height_m": 2620, "speed_mps": 20},
]
sim.set_route(waypoints, loiter_radius_m=150, clockwise=True)
sim.set_running(True)
print(sim.get_state()["navigation"])

# 단일점은 같은 호출에 waypoints[:1]을 전달한다.
# 수동 조종으로 전환: 현재 위치·속도는 유지하고 항로만 해제된다.
# sim.set_controls(elevator_deg=0, aileron_deg=0, rudder_deg=0, throttle=.5)
```

- 최대 64점, 목표 속도 16~24m/s. 최종 선회 반경은 `max(100, V_last²/(9.80665*tan30°))` 이상, 5000m 이하다. 24m/s·100m는 거부되고 110m는 허용된다.
- 각 점의 수평 거리 30m·높이 차 15m·속도 차 2m/s 이내에서 다음 점으로 진행한다. 마지막 점을 도달하면 지정한 방향과 반경으로 계속 선회한다. 도달 직후부터 목표 원에 정착하는 시간이 필요하다.
- 고도·속도는 각 항로점의 목표값으로 제어한다. 급한 상승이나 짧은 구간 때문에 통과 조건을 놓치면 일정 거리 직진 후 다시 접근한다. 위치를 순간 이동시키거나 항로점을 건너뛰지 않는다.
- 항로 전체와 마지막 선회 원은 마지막 초기화 위치의 계산 원점 50km 안에 있어야 한다. 공중 항로용 제어기이며 지상 접촉 상태에서 항로 설정은 거부한다. 자동 이착륙은 별도 제어기를 사용하고 충돌 회피·지형 회피는 없다. 현재 지형 표시는 전국 기본 타일을 사용한다.
- 항로 적용은 현재 재생/정지를 유지한다. `cancel_navigation()`은 적용 중인 조종 입력을 유지하며 수동으로 전환한다. 초기 조건은 `set_initial_condition(...)`의 위치·자세 6필드와 `speed_mps`로 지정하며 정지, 항로 해제, 각속도 0, 입력 트림을 적용한다. 초기 속도는 5~45m/s, 자세는 피치 ±89°·롤 ±180° 이내다. 받음각·옆미끄럼각도 함께 지정할 수 있다.

`scripts/check-navigation-api.py`가 실제 Unreal에서 수동 조종, 잘못된 항로의 원자적 거부, 단일점·다중점 추종과 CW/CCW 최종 선회를 검사한다. 작동기·기체 파일 변경 후에도 14개 항목이 통과했고 [실행 기록](artifacts/navigation-api-check.json)에 결과를 남겼다. 두 점 항로에서는 목표 속도 18→20m/s 변경도 확인했다. 이는 같은 개발 모델 안의 수치 시험 결과이며 실제 기체 성능 검증값이 아니다. 독립 C++ 검사에는 3점 경로, 짧은 상승·선회 구간, 24m/s·110m 선회도 포함한다.

```powershell
.\scripts\dev.ps1 check
.\.venv\Scripts\python.exe scripts\check-navigation-api.py
```

## Unreal 어댑터와 개발용 MCP

Unreal의 기본 Remote Control HTTP는 http://127.0.0.1:30010에서 실행한다. UE5.8 설정은 Config/DefaultRemoteControl.ini의 `/Script/RemoteControlCommon.RemoteControlSettings`에 두며, 아래 FlightScene 함수를 명시적으로 허용한다. 일반 연동은 위 `/api/v1`을 사용한다. [Epic HTTP API](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-control-api-http-reference-for-unreal-engine)

| 공개 함수 | 동작 |
| --- | --- |
| LoadTerrain() | 로컬 메시·영상 읽기, 현재 계산 원점 위에서 초기화·정지 |
| SetTimeScale(TimeScale) | 벽시계 재생 배속 0.25~20. 고정 물리 간격과 수동 step 시간은 유지 |
| SetViewMode(ViewMode) | aircraft 기체·overview 전국·projectile 최근 진행 연출 따라보기. EO/IR 지향 유지 |
| SetSensorSettings(Enabled, Mode, FovDeg) | EO/IR 활성·영상 모드·수평 시야각 |
| SetGimbal(AzimuthDeg, ElevationDeg) | 기체 기준 즉시 수동 지향·선택 추적 해제 |
| TrackSensorTarget(TargetId) | 가상 표적 ID의 기하학적 지향; 빈 ID로 해제 |
| PointSensorGeodetic(LatitudeDeg, LongitudeDeg, EllipsoidHeightM) | WGS84 지정점의 geo 지향. 지면 높이를 자동 적용하지 않음 |
| ProtectSensor() | 가상 보호 자세 180°/+30°와 영상 OFF |
| CaptureSensorFrame() | 실제 장면 JPEG의 base64·시간·순번·모드 반환 |
| CreateGroundTarget(Id, Kind, LatitudeDeg, LongitudeDeg, HeadingDeg) | 지면 높이에 정적인 가상 표적 생성 |
| MoveGroundTarget(Id, LatitudeDeg, LongitudeDeg, HeadingDeg) | 표적의 지면 위치·방위 변경 |
| RemoveGroundTarget(Id) | 표적 삭제; 추적 중이었다면 마지막 각도 유지 |
| ReloadWargameWeapons() | 정지·진행 연출 없음 상태에서 STL 연출 모델 목록만 다시 읽기 |
| PlayWargameShot(WeaponId, TargetId) | 저장된 시작점·끝점 사이의 고정 시간 애니메이션 생성 |
| ResolveWargameShot(ShotId, Outcome) | 완료한 연출에 운영자의 hit/miss 게임 판정 적용 |
| ResetWargame() | 연출·기록·워게임 시간과 표적 명중 표시 초기화 |
| CaptureView() | 다음 Unreal 렌더 프레임을 artifacts/terrain-view.png로 저장 요청. 데스크탑 캡처 아님 |
| GetAircraftState() | 위경고도·자세·조종 입력·시간·실행 상태·ProcessId 반환 |
| SetAircraftGeodetic(...) | 위경고도·방위각·피치·롤 적용 후 데모 정지 |
| SetControls(...) | 조종면 각도 세 개와 스로틀을 한 번에 적용. 잘못된 입력이면 모두 유지 |
| SetGroundControls(SteeringDeg, Brake, ParkingBrake) | 앞바퀴 조향·브레이크·주차 브레이크 적용, 항법 해제 |
| SetLandingGear(Down) | 기어 전개·수납 목표 설정. 실제 비율·잠금은 상태로 반환 |
| SetGroundInitialCondition(LatitudeDeg, LongitudeDeg, HeadingDeg, GroundSpeedMps) | 지상속도 0~15m/s, 지면 높이·자세 자동 계산, 시간·거리 0, 항법 해제·정지 |
| ResetOnGround() | 첫 시험 활주로 출발점으로 복귀. 활주로가 없으면 현재 계산 원점의 지면 사용 |
| SetInitialCondition(...) | 위치·자세·TAS 초기화, 항로 해제, 정지 |
| SetAerodynamicInitialCondition(...) | 위 초기조건과 함께 받음각·옆미끄럼각 지정 |
| SetWind(WindEastMps, WindNorthMps, WindUpMps) | 일정 ENU 바람 3성분 적용; 지상 관성 속도는 유지 |
| SetGust(GustEastMps, GustNorthMps, GustUpMps, DelayS, DurationS) | 시뮬레이션 시간 기준 단일 ENU 돌풍 설정 |
| SetFailures(EnginePowerFraction, ElevatorJammed, AileronJammed, RudderJammed) | 엔진 출력 비율과 조종면 고착 적용 |
| ReloadAircraftProfile() | 정지 중 기체 파일 재읽기·공중 트림 초기화 |
| ReloadAircraftVisual() | 정지 중 STL/내장 표시 외형 재읽기. 위치·시간·입력·물리 프로필 유지 |
| StartAutomaticTakeoff(RunwayIndex, ClimbHeightM, SpeedMps) | 자동 이륙 준비, 현재 재생/정지 유지 |
| StartAutomaticLanding(RunwayIndex, ApproachSpeedMps) | 자동 착륙 준비, 현재 재생/정지 유지 |
| PrepareLandingApproach(RunwayIndex) | 명시적 착륙 시험 위치로 초기화·정지 |
| CancelFlightOperation() | 자동 이착륙 해제, 현재 재생/정지 유지 |
| SetRoute(Waypoints, LoiterRadiusM, Clockwise) | 한 점 또는 여러 점의 항로 전체를 검증하고 자동항법 적용 |
| CancelNavigation() | 조종 입력과 비행 상태를 유지하며 수동 모드로 복귀 |
| SetRunning(bool) | 현재 상태에서 비행 진행/정지. fault 상태의 재생은 거부 |
| ResetFlight() | 19m/s 계산 수평 트림·시간 0으로 복귀하고 정지; 바람 유지 |
| AdvanceSimulation(dt) | 정지 중 0 < dt <= 1초 수동 진행 |
| RequestSimulationExit(ExpectedProcessId) | PID가 일치하는 독립 Game 실행만 다음 틱에 정상 종료. 에디터·PIE·PID 불일치는 거부 |

방위각은 북쪽 0°에서 시계방향, 피치는 기수 상승 양수, 롤은 오른쪽 날개 하강 양수다. 초기 위치는 국가 manifest의 위경도 범위·타원체고 -500~20000m 안에서 지정한다. 비행 중에는 그 위치의 국소 계산 원점에서 50km로 제한한다. `reset()`은 현재 계산 원점의 2600m/19m/s로 돌아간다.

장면 생성 후 에디터 Actor 경로를 artifacts/scene-setup.json에 기록한다. PIE의 실제 게임 Actor 경로는 다르며, 직접 Remote Control을 호출할 때는 실행 중인 `Saved/uavsim-runtime.json`에서 읽는다. 요청의 parameters는 이름→값 객체이며, 함수 반환값은 응답의 ReturnValue에 들어간다. 파라미터 이름은 헤더의 C++ 함수 인수명과 같다.

```powershell
$flightObjectPath = (Get-Content Saved/uavsim-runtime.json -Raw | ConvertFrom-Json).object_path
$requestBody = @{
    objectPath = $flightObjectPath
    functionName = 'GetAircraftState'
    parameters = @{}
} | ConvertTo-Json
$response = Invoke-RestMethod -Method Put -Uri 'http://127.0.0.1:30010/remote/object/call' -ContentType 'application/json' -Body $requestBody
$response.ReturnValue

# 실행 중인 PIE를 대상으로 위치·자세 왕복, 입력 거부, 시간 진행·재현 검사
.\.venv\Scripts\python.exe scripts\check-flight-api.py --object-path $flightObjectPath
```

하네스는 Python 표준 라이브러리만 사용한다. 이번 실행에서 10개 검사가 통과하고 종료 코드 0을 반환했다. [결과 기록](artifacts/flight-api-check.json)에 실제 경로와 반환 상태를 남겼다. 검사는 기체 위치와 데모 시간을 바꾼 뒤 데모 실행을 재개한다. 실패 시 0이 아닌 종료 코드를 반환한다.

MCP는 UE5.8 내장 ModelContextProtocol/EditorToolset을 에디터 대상에만 켠다. 주소는 http://127.0.0.1:8000/mcp이며 장면 초기화에서 .codex/config.toml이 생성됐다. 서버는 에디터가 실행 중일 때 연결할 수 있다. list_toolsets, describe_toolset, StartPIE, Actor 조회를 실제 호출했고, PIE 카메라 대상이 FlightScene인 것과 [게임 화면](artifacts/jeju-pie.png)을 확인했다. [Epic MCP 문서](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor)

## 기준 기체와 다음 작업

2026-11-17 최종 결정: 첫 대형기 기준은 Cessna208B Super Cargomaster, 후속 쌍발 후보는 King Air350이다. 사용자 승인으로 별도 C208B 연구 가정 모델을 만들고 C~F 연구 범위까지 연결했다. 현재 기본 선택은 `aircraft/c208b-research.json`이며 Thor는 `thor.json`으로 복귀할 수 있다. 실기체 동일 구성 자료와 성능 검증은 미완료다. [연구 모델 사용법](docs/c208b-research.md), [원래 자료 기반 계획과 현재 진행 구분](docs/fixed-wing-expansion.md)을 따른다.

DTED 다운로드·연결은 사용자 지시로 다음에 진행한다. 현재 Copernicus DSM·위성영상의 전국 기본 장면을 사용한다.

첫 기준 기체는 **미네소타대 Ultra Stick 25e(Thor)**다. 측정 근거와 비행시험을 통한 모델 추정 과정이 공개된 소형 전동 고정익이므로, 계산 코드와 근거를 연결할 초기 대상으로 선택했다. 회사 실제 기체 모델과는 구분한다.

기준 문헌은 Dorobantu 외, *System Identification for Small, Low-Cost, Fixed-Wing Unmanned Aircraft*, Journal of Aircraft 50(4), 1117–1130, 2013, DOI 10.2514/1.C032065다. [대학 공식 서지](https://experts.umn.edu/en/publications/system-identification-for-small-low-cost-fixed-wing-unmanned-airc), [대학 공개 원고 PDF](https://dept.aem.umn.edu/~mettler/Courses/AEM%205333%20%28spring%202013%29/AEM5333%20CourseDropbox/Week%207%20Identification/Ultrastick%20Identification/2012_AIAA_JA_SYSID.pdf).

수치 기준은 위 **32쪽 공개 원고(파일명 2012_AIAA_JA_SYSID.pdf)**로 고정한다. 다른 연도·시험기 구성의 수치를 섞지 않는다. Table 1(원고 7쪽)의 기본값은 다음과 같다.

| 항목 | 기준값 |
| --- | --- |
| 질량 | 1.959 kg |
| 날개폭 | 1.27 m |
| 날개 면적 | 0.31 m² |
| 평균 공력 시위 | 0.25 m |
| 관성 Ix, Iy, Iz | 0.089, 0.144, 0.162 kg·m² |
| 관성곱 Ixz | 0.014 kg·m²; 행렬 적용 부호는 원고 운동방정식과 대조 |

현재는 이 문헌의 Eq. (25)·(26)의 국소 미계수를 무차원화하고 비선형 공력·실속 전환·강체 방정식에 연결했다. 트림 재구성·부호·추진 개발 가정·검사 근거는 [flight-model.md](docs/flight-model.md)에 기록했다. 2600m 초기 높이는 제주 지형 표시를 위한 설정이며 고고도 성능을 재현한 조건이 아니다.

다음 단계는 실제 추진·공력·실속·착륙장치 시험 데이터로 현재 개발 가정을 보정하는 작업이다. 기체 파일, 작동기 지연·속도 제한, 단일 돌풍·엔진 출력 상실·조종면 고착, 자동 이착륙을 구현했다. IMU·GNSS 항법 센서·외부 비행제어컴퓨터 연동은 제외 범위이며 EO/IR 표시는 후속 요청으로 추가했다. 검증된 타이어·완충장치 동역학, 프로펠러 후류·비대칭 실속·스핀, 정식 신뢰성·정적/동적 시험과 패키지 빌드는 남아 있다. 우선순위와 완료 조건은 [개발 계획](docs/development-plan.md)을 따른다.
