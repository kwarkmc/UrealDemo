# Prepar3D SimConnect 주입·관측 기능 조사

조사일: **2026-11-18**. 기준: **Prepar3D v6 공개 SDK**. 대상 DLL 버전·기체·라이선스가 지정되지 않아 공개 표준 계약을 조사했다. 이 문서는 UAVSim에 아래 기능을 구현했다는 뜻이 아니다.

SimConnect는 외부 프로그램과 Prepar3D 내부 서버 사이의 인터페이스다. `LockheedMartin.Prepar3D.SimConnect.dll`은 .NET 래퍼이며, 아래 함수명은 SDK의 네이티브 이름이다. .NET에서는 생성자/Dispose, 핸들 생략, 구조체 마샬링 등의 차이가 있다. 함수 208개를 특정 DLL의 실제 export 또는 .NET 메서드 수로 해석하면 안 된다. [공식 개요](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/simconnect_api_overview.html), [Managed 안내](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/managed_simconnect_projects.html).

## 1. 전체 목록과 집계

아래 CSV는 UTF-8 BOM으로 저장했다. Excel에서 열거나 이름으로 검색할 수 있다. API 식별자·단위·접근 플래그·출처를 추출했으며, 원문 설명 전체를 복제하거나 개별 항목 900개를 한국어로 번역한 자료는 아니다. 분야별 의미는 이 문서에서 설명하고 세부 enum/비트/범위는 각 원문 링크에서 확인한다.

| 파일 | 범위 | 집계 |
| --- | --- | --- |
| [simvars.csv](simconnect-data/simvars.csv) | 주 SimVar 표의 이름·단위·직접 쓰기·읽기·멀티플레이·폐기 표시 | 900행, 이름 기준 897개 |
| [globals.csv](simconnect-data/globals.csv) | 별도 E/P 전역 변수·원문 쓰기 표시·공식 SimConnect 예제 등장 여부 | 42행 |
| [events.csv](simconnect-data/events.csv) | KEY ID·SimConnect 문자열·분야·미지원 표시 | 1,125행 |
| [functions.csv](simconnect-data/functions.csv) | 전용 함수·분야·문서 매개변수명·상세 페이지 | 이름 기준 208개 |
| [system-events.csv](simconnect-data/system-events.csv) | 서버→클라이언트 시스템 알림명 | 40개 |
| [system-states.csv](simconnect-data/system-states.csv) | 별도 시스템 상태 조회명 | 6개 |
| [types.csv](simconnect-data/types.csv) | 색인에 실린 구조체·열거형 | 111개 |
| [inventory.json](simconnect-data/inventory.json) | 분야별 수·수집시각·원문 URL/SHA-256·대조 결과 | 조사 재현 기록 |

주 SimVar 표에서 `Settable`이 Y로 시작하는 행은 **265행**이다. 이 중 `PAYLOAD STATION OBJECT:index`는 원문이 쓰기 전용이라고 명시한다. `AI WAYPOINT LIST`도 연결된 WAYPOINT 구조체 설명상 입력 전용이므로 CSV의 `readable=N`으로 구분했다. 나머지 읽기 표시는 문서상 목록이며 모든 객체의 런타임 지원을 보증하지 않는다. [SimVar 표](https://www.prepar3d.com/SDKv6/sdk/references/variables/simulation_variables.html), [구조체 설명](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/structures_and_enumerations.html#SIMCONNECT_DATA_WAYPOINT).

이벤트는 소제목 3행을 제외한 수다. `Unsupported` 6행과 중복 이름을 보존했다. 문자열 이름은 1,116종이고, `Unsupported`라는 표시를 제외하면 **1,115종**이다. 이름 수는 실제 동작 검증 수가 아니다. 이벤트 적용 여부는 기체·상황·멀티플레이 조건에 달려 있다. [공식 Event IDs](https://www.prepar3d.com/SDKv6/sdk/references/variables/event_ids.html).

## 2. 입력과 관측 경로를 구분하는 법

| 경로 | 외부→시뮬레이터 | 시뮬레이터→외부 |
| --- | --- | --- |
| SimVar | Y 항목을 `SetDataOnSimObject`로 직접 설정 | `RequestDataOnSimObject`로 값 구독 |
| Key Event | 스위치·축·증감·모드 명령 전송 | 구독한 이벤트와 전달 인수 수신 |
| 전용 API | 기상·객체·카메라·시나리오 등의 인수 전달 | 해당 응답 구조체 수신 |
| 시스템 알림 | 구독/해제/알림 활성 제어 | 생성·삭제·정지·충돌 등 발생 통지 |
| Client Data / 사용자 이벤트 | 합의한 사용자 데이터 기록·이벤트 전송 | 다른 클라이언트의 데이터·이벤트 수신 |
| External Sim | 자체 계산 결과를 객체 시뮬레이션에 반영 | 생성·소멸·시뮬레이션 콜백 및 정의한 입력 |

**SimVar의 `Settable=N`은 대응 기능을 조작할 수 없다는 뜻이 아니다.** 대응 Key Event 또는 전용 함수가 있을 수 있다. 반대로 이벤트 수신은 현재 물리 상태의 관측과 다르다. 명령 후 실제 상태는 별도로 조회해야 한다. [직접 데이터 설정](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/simobject_functions.html#SimConnect_SetDataOnSimObject), [이벤트 전송 규약](https://www.prepar3d.com/SDKv6/sdk/references/variables/event_ids.html).

## 3. 주 SimVar의 모든 분야

행 수는 중복을 포함한다. 직접 쓰기는 이벤트를 포함하지 않는다. 개별 접근 여부는 CSV를 따른다.

| 분야 | 전체 행 / 직접 쓰기 행 | 값의 범위 |
| --- | --- | --- |
| 엔진 | 115 / 44 | 피스톤·제트·터보프롭, RPM·연소·추력·토크·유온·유압·시동·레버 |
| 연료 | 48 / 23 | 탱크별 양·용량·선택·이송·펌프·중량·소모 |
| 등화 | 30 / 15 | 항법·착륙·택시·스트로브·객실·패널, 상태 마스크·방향 |
| 위치·운동 | 52 / 34 | 위경고도·자세·지상고·속도·가속도·각운동·받음각·옆미끄럼 |
| 비행계기 | 46 / 26 | IAS/TAS/Mach·고도계·승강률·자이로·경고·계기고장 |
| 항공전자·항법 | 156 / 11 | COM/NAV/ADF/TACAN·DME·ILS·GPS 경로/접근·트랜스폰더 |
| 조종 | 51 / 32 | 조종축·조종면·트림·플랩·스포일러·좌우 브레이크 |
| 자동비행 | 39 / 4 | AP 모드·목표 방위/고도/승강률·FD·자동추력·FBW 상태 |
| 착륙장치 | 54 / 7 | 기어·조향·바퀴·접촉·서스펜션·손상 |
| 기체 주변 대기 | 22 / 0 | 밀도·온도·압력·풍속/풍향·성분·강수·운중·시정 |
| 헬리콥터 | 34 / 5 | 로터·콜렉티브·사이클릭·트림·로터 브레이크 |
| 기타 기체 계통 | 58 / 25 | 전기·배터리·유압·제빙·문·경고·외부하중/호이스트 |
| 일반 값 | 140 / 22 | 질량·CG·관성·공력 제원·결빙·충돌·APU·여압·freeze |
| 문자열 | 12 / 3 | 기종·등록번호·항공사·편명·TITLE·항법 식별자 |
| AI | 16 / 7 | 목표 속도/방위·항로점·운항단계·공항·활주로·주기장·ETA/ETD |
| 항모 운용 | 16 / 2 | 사출기·어레스팅 케이블 등 |
| 레이싱 | 10 / 4 | 경주·랩·진행 상태 |
| 아바타 | 1 / 1 | 아바타 활성 상태 |

분류·단위·접근 플래그 출처: [Prepar3D v6 Simulation Variables](https://www.prepar3d.com/SDKv6/sdk/references/variables/simulation_variables.html). 실제 기체가 없는 계통은 유효한 상태값을 제공하지 않을 수 있다.

### 위치·운동과 조작에서 특히 구분할 값

| 목적 | 대표 식별자 | 해석 |
| --- | --- | --- |
| 지리 위치·자세 | `PLANE LATITUDE`, `PLANE LONGITUDE`, `PLANE ALTITUDE`, `PLANE PITCH DEGREES`, `PLANE BANK DEGREES`, `PLANE HEADING DEGREES TRUE` | 주 표에서는 직접 쓰기 Y |
| 선운동 | `VELOCITY WORLD X/Y/Z`, `VELOCITY BODY X/Y/Z`, `ACCELERATION WORLD X/Y/Z`, `ACCELERATION BODY X/Y/Z` | 축·단위를 변환해 사용 |
| 회전 운동 | `ROTATION VELOCITY BODY X/Y/Z`, `ROTATION ACCELERATION BODY X/Y/Z` | 문서 단위 오류 의심 사항은 아래 참고 |
| 공력 관측 | `INCIDENCE ALPHA`, `INCIDENCE BETA`, `DYNAMIC PRESSURE` | 입력 조종면과 구분 |
| 조종 명령/표시 | `AILERON POSITION`, `ELEVATOR POSITION`, `RUDDER POSITION`, `YOKE X POSITION`, `YOKE Y POSITION` | YOKE 항목은 애니메이션용 표기; 물리 입력과 동일시 금지 |
| 실제 조종면 | `AILERON LEFT DEFLECTION`, `AILERON RIGHT DEFLECTION`, `ELEVATOR DEFLECTION`, `RUDDER DEFLECTION` | 입력 축과 다른 변수 |
| 플랩 | `FLAPS HANDLE INDEX`, `TRAILING EDGE FLAPS LEFT ANGLE`, `TRAILING EDGE FLAPS RIGHT ANGLE` | 핸들 위치·좌우 실제각 구분 |
| AP 목표 | `AUTOPILOT HEADING LOCK DIR`, `AUTOPILOT ALTITUDE LOCK VAR`, `AUTOPILOT VERTICAL HOLD VAR` | AP 활성/모드와 목표값 구분 |
| 총질량·연료/탑재 | `TOTAL WEIGHT`, `FUEL TANK ... QUANTITY`, `PAYLOAD STATION WEIGHT:index` | 총질량 자체는 N; 구성 요소의 접근 플래그 확인 |

위 식별자는 계열 표기이며 슬래시나 `...`를 실제 API 이름에 그대로 넣지 않는다. 전체 정확한 문자열은 [CSV](simconnect-data/simvars.csv)를 따른다. 직접 쓰기 Y도 엔진·AI·자동조종 등 내부 계산이 다음 프레임에 값을 갱신하지 않는다는 보장은 아니다.

## 4. 전역 시간·프로그램·시점 값 42개

주 SimVar 페이지 밖에 [Environment Variables](https://www.prepar3d.com/SDKv6/sdk/references/variables/environment_variables.html)와 [Program Variables](https://www.prepar3d.com/SDKv6/sdk/references/variables/program_variables.html)가 있다. 모두 [globals.csv](simconnect-data/globals.csv)에 별도로 수록했다.

| 분류 | 값 |
| --- | --- |
| E: 시각·달력 | 절대시각, UTC/지역 시각, 요일·일·월·연중일·연도, 시차 |
| E: 조명 전환 | 시간대, 주야·태양/달·노출·발광 보간값 |
| P: 프로그램 | 배속·단위계·풍경 복잡도·그림자·입력장치·녹화 재생·일시정지 |
| P: 활성 시점 | 시점 위경고도·자세, 화면 중심 지면 지점 위경고도·거리 |
| P: 기타 | 아바타 사용 가능·윈드실드 효과·전체/사용자 위치 고정 |

`E:`/`P:`는 SDK 분류·스크립트 접두어다. SimConnect 데이터 정의에 무조건 접두어를 붙이는 규칙으로 해석하지 않는다. 공식 [DataHarvester 예제](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/samples/data_harvester.html)는 `ABSOLUTE TIME`, `ZULU TIME`, UTC 달력 5개와 `TIME ZONE OFFSET`을 접두어 없이 조회한다. 이 8개는 `in_simconnect_sample=True`로 표시했다. `False`는 해당 예제에 없다는 뜻이며 미지원 판정이 아니다. 나머지 항목의 SimConnect 접근·쓰기 동작은 대상 SDK/실행 환경에서 개별 확인해야 한다. P/E 표의 Settable을 주 SimVar의 직접 쓰기 검증 수에 합산하지 않았다.

## 5. 이벤트로 주입할 수 있는 조작

전체 정확한 이름은 [events.csv](simconnect-data/events.csv)에 있다. SimConnect는 `KEY_...` 정수 상수 대신 대응 문자열을 `MapClientEventToSimEvent`에 연결하고 `TransmitClientEvent`의 `dwData`로 인수를 보낸다. 이벤트마다 인수의 의미·범위가 다르다. [공식 Event IDs](https://www.prepar3d.com/SDKv6/sdk/references/variables/event_ids.html).

| 분야 | 문서 행 | 조작 내용 |
| --- | --- | --- |
| 엔진 | 247 | 엔진별 스로틀·리버스·혼합비·프로펠러·시동·마그네토·컨디션 |
| 비행 조종 | 48 | 에일러론·엘리베이터·러더·트림·플랩·스포일러 |
| 자동비행 | 128 | AP/FD·방위·고도·속도·Mach·승강률·접근·자동추력·G1000 |
| 연료 | 49 | 탱크 선택·이송·급유·펌프 |
| 항공전자 | 206 | 주파수·채널·OBS·오디오·송수신 선택·트랜스폰더·GPS |
| 계기 | 32 | 기압·자이로·계기 기준 |
| 등화 | 62 | 개별 등화 ON/OFF/SET/TOGGLE |
| 고장 | 12 | 엔진·전기·유압·브레이크 등 고장 토글 |
| 기타 계통 | 97 | 기어·브레이크·푸시백·도어·제빙·외부하중 등 |
| 헬기 / 선박 | 14 / 1 | 로터·거버너 등 / 선박 관련 조작 |
| Slew | 34 | 위치·자세 이동/회전 및 slew 제어 |
| 시점 | 78 | 뷰·팬·줌·카메라 조작 |
| 일반 | 89 | 일시정지·배속·날짜/UTC·freeze·재설정·종료 등 |
| ATC / 멀티플레이 | 11 / 12 | ATC 메뉴/응답, 세션 관련 입력 |
| 아바타 / DIS | 2 / 3 | 모드 전환, DIS 관련 명령 |

예를 들어 `AXIS_AILERONS_SET`, `AXIS_ELEVATOR_SET`, `AXIS_RUDDER_SET`는 축 입력이고 `GEAR_UP`, `GEAR_DOWN`, `FLAPS_SET`, `PARKING_BRAKES`, `AP_MASTER`, `PAUSE_SET`는 각각 다른 계약의 명령이다. `THROTTLE_SET`의 표기 범위는 0~16383이며 `AXIS_THROTTLE1_SET`의 표기 범위는 -16383~+16383이다. 전체 축을 일률적으로 0~1 또는 같은 정수 범위로 처리하면 안 된다.

`TOGGLE`/증감 명령은 재전송하면 결과가 바뀔 수 있다. 현재 상태를 관측하고 명시적 SET/ON/OFF가 제공되는지 확인하는 것은 호출자 측 설계 사항이다. 이 문서는 이벤트별 재시도·동기화 계층을 구현하지 않는다.

## 6. 변수 표 외의 전용 API — 전체 기능 분야

함수 색인 기준 General53 + SimObject30 + View66 + World25 + Scenario26 + Analysis8 = **208개**다. 전체 함수명과 문서 매개변수명은 [functions.csv](simconnect-data/functions.csv)를 따른다. 매개변수 칸은 상세 문서의 Parameters 목록에서 추출한 것으로 완전한 C++ 선언이나 단위 규약을 대체하지 않는다. [공식 색인](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/simconnect_references.html).

### 객체·외부 비행모델

| 기능 | 주입할 값/명령 | 관측할 값 |
| --- | --- | --- |
| 사용자 기체 변경 | 기체 title, External Sim 지정 | 변경 알림·TITLE·외부 Sim 식별/자료 |
| AI 생성/삭제 | 모델·등록번호·초기 위치·비행계획·소유/시뮬레이션 설정 | 객체 ID·추가/제거 통지·객체 SimVar |
| AI 제어 | AI 해제·항로점 배열·비행계획 파일·지면 고정 | 현재 항로점·운항 상태 |
| 부착/해제 | 부모/자식 ID·부착점·오프셋/회전 | 부착점의 기체/세계 위치·자세 |
| External Sim | GUID·콜백 마스크·데이터 정의·외부 계산 결과 | 생성·소멸·매 시뮬레이션 프레임·위치변경 등의 콜백 |
| Secondary Sim | 보조 시뮬레이션 등록·부착·분리 | 해당 보조 시뮬레이션 콜백 |
| 무장 부착 | 표시/시뮬레이션 객체 title·부착점·수량·제거 | 별도 무장/피해 시스템 알림; 라이선스 조건 확인 |

출처: [SimObject Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/simobject_functions.html). External Sim은 자체 차량 동작을 제공하는 별도 경로다. 단순히 위치를 한 번 쓰거나 조종축을 보내는 것과 구분한다. 해당 콜백은 동기 SimConnect의 해제 규약을 지켜야 한다.

`Initial Position` 구조체는 위도·경도·고도·pitch/bank/heading·지상 여부·대기속도를 받는다. 각도는 deg, 고도는 ft, 속도는 kt다. `AI WAYPOINT LIST`는 각 점의 위경도·고도·flags·목표 kt·스로틀 %를 받는다. `Marker State`는 CG·바퀴·엔진·추력/공력 기준점 등의 개발용 마커 표시다. 이 세 구조체는 입력 전용이다. 사용자 기체를 연속 이동시키기 위해 Initial Position을 반복하면 불필요한 scenery 재로딩을 유발할 수 있다. [입력 구조체 규약](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/structures_and_enumerations.html#SIMCONNECT_DATA_INITPOSITION).

### 기상·지형·시설·효과

| 기능 | 주입할 값/명령 | 관측할 값 |
| --- | --- | --- |
| 기상 | 확장 METAR·기상 모드/테마·변경 시간·동적 변화율 | 지정/최근접 관측소 METAR·지점 보간 기상·영역 구름 밀도 |
| 관측소 | 식별자·이름·위경도·고도·생성/삭제 | 관측소 기상 |
| Thermal | 위치·크기·상승/하강류·난류·영역 비율·삭제 | 생성 ID |
| 지면 조회 | 경계/객체 기준 영역·격자 수·좌표/고도 형식·조회 주기 | 유효성·위경고도·법선·표면 종류/상태·플랫폼/이동 여부 |
| 시설 조회 | 시설 종류·구독/해제 | 캐시 내 공항·VOR·NDB·TACAN·항로점 |
| 환경 설정 | 항공/도로/선박/공항차량 교통 밀도 등 | 교통 설정·그림자 flags·풍경 복잡도 |
| 효과 | 효과명·위치/오프셋·대상 객체·수명·제거 | 효과 객체 ID |

출처: [World Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/world_functions.html), [지면 응답 구조체](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/structures_and_enumerations.html#SIMCONNECT_DATA_GROUND_INFO). 지면 조회를 임의 terrain 메시 수정 기능으로 해석하지 않는다. 시설 목록도 전체 공항/활주로 데이터베이스 덤프가 아닌 캐시 기반 목록이다. 기상은 주변 보간과 지원 구름 형식 때문에 설정 문자열과 재조회 문자열이 같지 않을 수 있다.

### 카메라·영상·관측자

| 기능 | 주입할 값/명령 | 관측할 값 |
| --- | --- | --- |
| 뷰 | 변경·열기·닫기·도킹·해제·창 위치/크기 | 창 위치/크기·뷰 알림 |
| 카메라 | 정의/인스턴스·상대 XYZ·자세·팬·전환 시간·줌·수평/수직 FOV | 상대 6DOF·수평/수직 FOV |
| 센서·후처리 | 센서 모드·후처리 이름·추가/제거 | 센서 모드 |
| 관측자 | 세계 위치·회전·시선 목표·객체 추적/부착·초점·FOV·이동 간격·동작 모드 | 관측자 데이터 |
| 정지 영상 | 저장 경로·파일명·형식·대상 뷰 | 이미지 파일 |
| 영상 스트림 | 목적지/송신 IPv4·포트·해상도·목표 FPS·비트율·시작/종료 | 해당 뷰의 네트워크 영상 |

출처: [View Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/view_functions.html). 센서 enum에는 NONE/IR/IR_BLACK_HOT/GDATA가 있다. 영상은 수치 SimVar와 다른 출력이다. 카메라·관측자·영상 API가 있다는 사실만으로 물리 열상 정확도나 영상인식/가림판정 기능을 단정하지 않는다. 영상 스트림 구조체의 형식은 문서상 H.264다. [센서·영상 구조체](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/structures_and_enumerations.html#SIMCONNECT_DATA_VIDEO_STREAM_INFO).

### 시나리오·기록·UI·통신

| 기능 | 주입할 값/명령 | 관측할 값 |
| --- | --- | --- |
| 시나리오 | FXML 로드/저장·PLN 로드 | 로드/저장/항로 활성 알림 |
| 구조화 시나리오 | GUID 작업 실행/완료·목표 결과 설정 | 목표·하위 목표·mission objective·flight segment·착륙 trigger 기록·이동 scenery |
| 기록/재생 | 기록 시작/저장·파일·북마크·재생 종료시각 | 기록 메타데이터·북마크·재생/녹화 상태 |
| 비행 분석 | 분석 그림 생성 | ILS/고도 분석 그림 결과 |
| UI | 메뉴/하위메뉴·텍스트·지속시간·선택지 | 메뉴/텍스트 응답·생성/제거 |
| 입력 연결 | 키/조이스틱/마우스 매핑·그룹 우선순위·마스킹·예약 키 | 매핑한 이벤트·조이스틱 장치 정보 |
| Client Data | 영역 이름/크기·오프셋/타입 정의·값 기록 | 영역 구독·변경 통지 |
| 통신/동기 | 연결·dispatch·동기 블록·해제·타임아웃 | 연결 버전/라이선스·응답시간·오류·전송 packet ID |

출처: [Scenario Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/scenario_functions.html), [Analysis Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/analysis_functions.html), [General Functions](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/general_functions.html). Client Data는 애드온끼리 정한 바이트 계약이다. 기록했다고 기본 엔진·센서·기체 모델이 자동으로 그 데이터를 사용하는 것은 아니다.

## 7. 수신 주기와 시스템 알림

SimVar는 객체별 단발/주기 요청, 변경 시 전송, datum ID/value 태그, 시작 지연·간격·횟수를 지정할 수 있다. 기본 데이터형은 FLOAT64이므로 정수·문자열·구조체는 명시적 정의가 필요하다. 동기 블록은 해제 또는 타임아웃까지 시뮬레이션 측 처리를 기다리게 하는 기능이며, 독립 계산 코어의 임의 `step(dt)` 또는 실시간 보장과 같지 않다. [데이터 요청](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/simobject_functions.html#SimConnect_RequestDataOnSimObject), [동기 API](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/general_functions.html#SynchronousSimConnect).

시스템 알림 40개는 [전체 CSV](simconnect-data/system-events.csv)에 있다. 시간/프레임, 로드/저장, 일시정지/실행, 충돌, 위치/시점, 객체 추가/삭제, 시나리오/경주, 녹화/재생, 무장 알림으로 나뉜다. `Frame`에는 FPS와 simulation speed가 포함된다. 무장 알림 4개는 공식 표에서 **Professional Plus 전용**으로 명시한다. [시스템 이벤트 표](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/general_functions.html#SimConnect_SubscribeToSystemEvent).

별도 상태 조회명은 `AircraftLoaded`, `DialogMode`, `FlightLoaded`, `FlightPlan`, `FullScreenMode`, `Sim`이다. `RequestSystemState`에서 조회할 수 있다고 모든 이름을 `SetSystemState`로 쓸 수 있는 것은 아니다. [시스템 상태 규약](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/general_functions.html#SimConnect_RequestSystemState).

## 8. 구현 판단에 영향을 주는 제한

1. **목록의 끝은 표준 공개 SDK까지다.** ISimObject가 동적으로 등록한 SimVar, 애드온 전용 이벤트·Client Data 이름/구조는 설치 콘텐츠에 따라 늘어난다. 모든 애드온의 임의 변수를 한 번에 열거한 목록은 아니다. [변수 체계](https://www.prepar3d.com/SDKv6/sdk/references/variables/variables_overview.html).
2. **L/C/S 등의 접근을 섞지 않는다.** L-Var·게이지 내부 변수·시나리오 내부 변수·PDK 인터페이스를 표준 SimVar 목록에 합치지 않았다. 애드온이 별도 브리지를 제공하는 경우는 별도 계약이다. 시나리오 작업 API가 있다고 S: 내부 변수 전체를 직접 읽을 수 있다는 뜻도 아니다. [시나리오 스크립트의 SimConnect 제한](https://www.prepar3d.com/SDKv6/scenario_tools/references/scenario_scripting.html).
3. **단위는 이름으로 추정하지 않는다.** `PLANE PITCH DEGREES` 등은 표의 기본 단위가 rad다. `GROUND ALTITUDE`는 m, `PLANE ALTITUDE`는 ft다. percent/percent over 100/position, BCD/BCO, bool/enum/비트마스크도 구별한다. 단위 환산과 좌표축·회전 부호는 경계 계약으로 기록해야 한다.
4. **높이 기준은 별도로 확정한다.** 조사한 모든 `Altitude` 필드에 WGS84 타원체고 보증이 붙어 있는 것은 아니다. UAVSim의 타원체고 API에 원시 값을 그대로 연결해서는 안 된다. 관련 항목의 MSL/AGL 설명과 대상 버전의 수직 기준을 확인한다.
5. **원문의 의심 표기는 보존했다.** `ROTATION ACCELERATION BODY ...`의 rad/s 표기, 관성 항목의 `Slugs per feet squared`, `STRUCT LATLONALTPBH`의 구조체 설명 등은 차원/설명 확인이 필요하다. CSV에서 임의 수정하지 않았다. `SIM ON GROUND`는 nonzero 쓰기는 지상 배치지만 zero 쓰기는 효과 없다는 원문 조건도 주의한다. [주 변수 표](https://www.prepar3d.com/SDKv6/sdk/references/variables/simulation_variables.html).
6. **멀티플레이와 객체 소유권은 별도다.** 한 기체에서 읽거나 쓸 수 있는 값이 다른 모든 기체에 전파되는 것은 아니다. `Multiplayer` 열, 객체별 접근/생성/삭제 계약, 현실 버블과 시설 캐시 범위를 따른다. [객체 API](https://www.prepar3d.com/SDKv6/sdk/simconnect_api/references/simobject_functions.html).
7. **버전·라이선스·래퍼를 확정해야 실행 계약이 된다.** v6 SDK로 만든 클라이언트와 이전 서버의 호환성을 가정하지 않는다. PDK는 SimConnect와 다른 하위 계층이며 네트워크 인터페이스가 아니다. [공식 SDK 호환성](https://www.prepar3d.com/sdk/), [SDK/PDK 구분](https://www.prepar3d.com/SDKv6/sdk/sdk_overview.html).
8. **직접 상태 쓰기와 물리 입력은 다르다.** 위치·가속도·계기 표시를 쓸 수 있다는 것만으로 임의 힘/모멘트·공력계수 전체를 일관되게 주입하는 API가 있다고 주장하지 않는다. 자체 비행역학과 연결하려면 조종 입력 경로와 External Sim/상태 지정 경로 중 의도한 계약을 정해야 한다.

## 9. 실제 확인과 다음 작업

- 공식 웹 문서 12개를 명령행으로 수집하고 표의 열 수·접근 플래그·필수 항목·비어 있지 않은 결과를 assert로 검사했다. 실패 시 수집기는 0이 아닌 종료 코드를 반환한다.
- HTML의 표 내부 소제목을 제외하고, `ObjectAdded`의 누락된 tr 태그도 처리했다. 중복·미지원·쓰기 전용 표시는 보존했다.
- 함수 색인과 6개 상세 페이지를 대조했다. `CameraSmoothRelative6DOFByName` 대 `CameraSmoothRelative6DofByName`의 대소문자 불일치 1건을 기록했다. CSV는 색인 이름/링크를 보존하고 매개변수는 대소문자 무시 대조로 수집했다. 코드 작성 시 실제 SDK 헤더/래퍼를 확인해야 한다. `GenerateFlightAnalysisDiagrams`의 매개변수 목록은 문서 HTML 형태 때문에 CSV에서 비어 있으며 원문 선언에는 hSimConnect가 있다.
- 최종 로컬 검사: CSV7개 집계·플래그·예외, 출처12개 기록, 로컬 문서 링크15개가 일치했으며 종료 코드0으로 통과했다.
- [재수집 도구](../scripts/collect-simconnect-reference.py): `.\.venv\Scripts\python.exe scripts\collect-simconnect-reference.py`. 웹 내용이 바뀌면 CSV/집계는 갱신되지만 이 설명문은 자동 변경하지 않는다.
- 제품 API·Python 클라이언트·Web UI는 변경하지 않았다. Prepar3D 접속, DLL 로드/리플렉션, 실제 기체 읽기/쓰기, Unreal 실행·빌드·비행시험은 수행하지 않았다.
- 후속 실행 작업은 대상 Prepar3D 버전/라이선스와 DLL을 기준으로 필요한 항목의 읽기·쓰기·이벤트 적용을 확인하는 것이다. 이번 조사는 API 대체 구현이나 전체 기능 동등성 검증의 완료가 아니다.
