# SimConnect.dll API 노출(Through) 표면 — IGManager 인벤토리

> **목적**: IGManager가 Prepar3D(v5)와 통신하기 위해 `SimConnect.dll`의 어떤 **기능·명령·클래스·메소드**를 실제로 "뚫고 사용(punch-through)"하는지를 고정(고정점)으로 기록.
> **조사 방법**: 소스 정적 분석. C#에 semantic/clangd가 불완전해 **literal text-search + 파일 직접 읽기**로 교차 검증. (text-search fallback — 정확성 한계 명시)
> **기준 시점**: 2026-11-19 · **변경 없음**: 본 인벤토리는 읽기 전용 조사 결과.
> **2026-11-19 재검증**: 실제 호출부와 대조해 오류 정정(using 파일 수, `eGROUP_ID` 구성, `RequestObserverData`, `eREQUEST_ID` 구성·개수, struct 수, 라인) + 데드코드 신규 발견 보강(§12).

---

## 1. 접근 경로 (중요 전제)

- IGManager(C# WinForms, **.NET 4.8, x64**)는 native `SimConnect.dll` export를 **직접 `DllImport`하지 않는다.**
- 대신 managed wrapper **`LockheedMartin.Prepar3D.SimConnect` v4.4.0.0**을 참조 — `IGManager.csproj:118-120`(HintPath `bin\x64\Debug\LockheedMartin.Prepar3D.SimConnect.dll`). 이 managed 조립체가 내부에서 native `SimConnect.dll` export를 호출한다.
- 따라서 "punch-through 표면" = **프로젝트가 호출하는 managed `SimConnect` 클래스 메소드 / 이벤트 / 구조체 / 상수** + 이들이 내부로 매핑하는 **native export**.
- `using LockheedMartin.Prepar3D.SimConnect;`는 **6개 파일**에 존재: `Program.cs:11` · `SimControl.cs:11` · `SimControl_P3DSet.cs:11` · `SimManager.cs:12` · `SimManagerICDStructure.cs:10` · `UAV/VideoManager/CameraManager.cs:11`.
- 호출 경로: ① `SimControl`의 private 필드 `_simConnect`(대부분) ② `SimControl`의 노출된 **`SimConnect` 프로퍼티** (= `_simConnect`) — `SimManager.cs:3482,3496`(항공기 생성/삭제)·`CameraManager.cs:799,835`(지상 디미)에서 사용. → **SimManager/CameraManager → SimControl → SimConnect 직접 뚫림** 존재(vendor split 시 제거 대상).

---

## 2. managed `SimConnect` 실제 호출 표면 → native export 매핑

### 2.1 라이프사이클 / 메시지 펌프
| managed 호출 | native export | 코드 위치 |
|---|---|---|
| `new SimConnect("Managed Client Events", this.Handle, WM_USER_SIMCONNECT, null, 0)` | `SimConnect_Open` | `SimControl.cs:4017` |
| `_simConnect.ReceiveMessage()` (WndProc) | `SimConnect_Recv` | `SimControl_P3DSet.cs:47-52` |
| `_simConnect.Dispose()` | `SimConnect_Close` | `SimControl.cs:2058`(OnFormClosed) · `4166` — 2사이트 |

- **메시지 펌프**: WinForms `WndProc`가 `WM_USER_SIMCONNECT = 0x0402`(`SimControl_P3DSet.cs:29`)를 받으면 `ReceiveMessage()` 호출 → RX 이벤트 파이프라인. **메인 스레드에서만 처리**(`_simMutex`로 보호).

### 2.2 데이터 정의 (RX/TX 공통, 시작 시 1회)
| managed 호출 | native export |
|---|---|
| `AddToDataDefinition(defID, dataType, unit, SIMCONNECT_DATATYPE, def, arrayIdx)` | `SimConnect_AddToDataDefinition` |
| `RegisterDataDefineStruct<T>(defID)` | (managed marshaling — 직접 export 없음, 위 정의 버퍼에 struct 매핑) |

### 2.3 데이터 요청 (보고값 = RX)
| managed 호출 | native export | 비고 |
|---|---|---|
| `RequestDataOnSimObject(reqID, defID, objID, SIMCONNECT_PERIOD.VISUAL_FRAME, flags, interval, simObjID, evtID)` | `SimConnect_RequestDataOnSimObject` | **유일하게 쓰는 Request 계열**. 주기 전부 `VISUAL_FRAME`. |

### 2.4 데이터 전송 (값 주입 = TX)
| managed 호출 | native export |
|---|---|
| `SetDataOnSimObject(defID, objID, SIMCONNECT_DATA_SET_FLAG.DEFAULT, pDataSet)` | `SimConnect_SetDataOnSimObject` |

### 2.5 클라이언트 이벤트
| managed 호출 | native export |
|---|---|
| `MapClientEventToSimEvent(eEVENT_ID.*, "SIM_EVENT_NAME")` | `SimConnect_MapClientEventToSimEvent` |
| `TransmitClientEvent(objID, eEVENT_ID.*, dwData, eGROUP_ID.*, SIMCONNECT_EVENT_FLAG.GROUPID_IS_PRIORITY)` | `SimConnect_TransmitClientEvent` |

### 2.6 AI 객체 생성/삭제
| managed 호출 | native export | 위치 |
|---|---|---|
| `AICreateNonATCAircraft("KT-1"/"TEST_UAV", tail, SIMCONNECT_DATA_INITPOSITION, reqID)` | `SimConnect_CreateAirObject` | `SimManager.cs:3482,3496`(SimControl.SimConnect 경유) |
| `AICreateSimulatedObject(name, SIMCONNECT_DATA_INITPOSITION, reqID)` | `SimConnect_CreateSimulatedObject` | `SimControl.cs:4443,4456`(구름/디미) · `7951`(표적) · `CameraManager.cs:799,835`(지상 디미) |
| `AIRemoveObject(objID, reqID)` | `SimConnect_AIRemoveObject` | `SimControl.cs:4148,7888` |

> **지상물체**는 `CreateGroundVehicleObject` 대신 **`AICreateSimulatedObject`**로 생성하고, `eDEFINITION_ID.MOVE_GROUND_VEHICLE_POSITION`("Ground Vehicle Position X/Y/Z")을 `SetDataOnSimObject`로 설정.

### 2.7 옵저버 / 카메라
| managed 호출 | native export |
|---|---|
| `SetMainCameraFov(hFov, vFov)` | `SimConnect_SetMainCameraFov` |
| `SetObserverRotation(name, SIMCONNECT_DATA_PBH)` | `SimConnect_SetObserverRotation` |
| `SetObserverTrackEntityOn(name, objID)` | `SimConnect_SetObserverTrackEntityOn` |
| `SetObserverTrackEntityOff(name)` | `SimConnect_SetObserverTrackEntityOff` |
| `SetObserverAttachToEntityOn(name, objID, SIMCONNECT_DATA_XYZ)` | `SimConnect_SetObserverAttachToEntityOn` |
| `SetObserverAttachToEntityOff(name)` | `SimConnect_SetObserverAttachToEntityOff` |
| `SetObserverLookAt(name, SIMCONNECT_DATA_LATLONALT)` | `SimConnect_SetObserverLookAt` |

(비활성/주석: `SetObserverFieldOfView`, `SetObserverLookAtEx`)

### 2.8 기상
| managed 호출 | native export | 비고 |
|---|---|---|
| `WeatherSetModeGlobal()` | `SimConnect_WeatherSetModeGlobal` | `SimControl_P3DSet.cs:137` |
| `SetWeatherObservation(strMetar)` | (managed 기상 관측값 전송 래퍼 — `SimConnect_WeatherAddIcaoObservation` 계열, 추정) | 프로젝트 메서드 `SimControl.cs:4514` |

> `WeatherAddCloud` / `RequestWeatherObservation` 등 직접 사용 **없음**. 구름은 `AICreateSimulatedObject`(cloud)로 표현.

---

## 3. 수신(RX) 이벤트 구독 (managed event → SIMCONNECT_RECV 블록)

구독 위치: `SimControl_P3DSet.cs:79-89`

| managed 이벤트 | 핸들러 | native recv 블록 |
|---|---|---|
| `OnRecvOpen` | `SimConnect_OnRecvOpen` (`P3DSet.cs:103`) | `SIMCONNECT_RECV_OPEN` |
| `OnRecvQuit` | `SimConnect_OnRecvQuit` (`SimControl.cs:1728`) | `SIMCONNECT_RECV` |
| `OnRecvException` | `SimConnect_OnRecvException` (`1737`) | `SIMCONNECT_RECV_EXCEPTION` |
| `OnRecvSimobjectData` | `SimConnect_OnRecvSimobjectData` (`2117`, **public**) | `SIMCONNECT_RECV_SIMOBJECT_DATA` |
| `OnRecvEvent` | `SimConnect_OnRecvEvent` (`1746`) | `SIMCONNECT_RECV_EVENT` |
| `OnRecvAssignedObjectId` | `SimConnect_OnRecvAssignedObjectId` (`1796`) | `SIMCONNECT_RECV_ASSIGNED_OBJECT_ID` |
| ~~`OnRecvObserverData`~~ | 주석 처리 (`1763`) | `SIMCONNECT_RECV_OBSERVER_DATA` |
| ~~`OnRecvWeatherObservation`~~ | 주석 처리 | (기상 관측) |

- `OnRecvExternalSimCreateEvent`(`1751`)는 외부 Sim 객체 생성 알림.
- **핵심 RX**: `OnRecvSimobjectData`가 `dwData`를 `ICD.*` struct(`UavEngineData` 등)로 **직접 캐스트** + `SimManagerICDStructure.cs`(36개 struct) 소형 codec.
- **`OBSERVER_INFO`**: `OnRecvSimobjectData`의 `case OBSERVER_INFO`(`SimControl.cs:1767`)가 **`RequestObserverData`를 활성 호출**(`2169`) — 단 `OnRecvObserverData` 구독이 주석 처리돼 수신 데이터 **소비자 없음**(§12-1).

---

## 4. 요청 ID `eREQUEST_ID` (전체, `SimManagerDefinition.cs` — 59종)

- **주기 데이터(자기관, 11)**: `UAV_ENGINE_INFO`, `UAV_FUEL_INFO`, `UAV_LIGHT_INFO`, `UAV_LATLONALT_INFO`, `UAV_INSTRUMENTATION_INFO`, `UAV_AVIONICS_INFO`, `UAV_CONTROL_INFO`, `UAV_AP_INFO`, `UAV_LG_INFO`, `UAV_ENVIRONMENT_INFO`, `UAV_STATUS_INFO`
- **5기별 (10)**: `UAV_ENGINE_CREATE0..4`, `UAV_LATLONALT_CREATE0..4`
- **생성 응답 (32)**: `CREATE_FCS1`(=0), `CREATE_FCS__2`, `CREATE_FCS__3`(더블 언더스코어 주의), `CreateAirObject0..4`, `CreateGroundObject1..20`, `CREATE_CLOUD`, `CREATE_RADAR`, `CREATE_CUSTOM_SURFACE`, `CREATE_AI_OBJECT`
- **표적 (4)**: `FIX_TARGET`, `REMOVE_TARGET`, `RemoveTargetUav`, `TargetInfo`
- **기타 (2)**: `OBSERVER_INFO`, `AI_REMOVE_OBJECT`

> 전부 **`SIMCONNECT_PERIOD.VISUAL_FRAME`**으로 요청(표적 일부 `ONCE`). `TARGET_STATUS`/`MOVE_UAV_POSITION` 등은 **`eDEFINITION_ID`**이며 `eREQUEST_ID`가 아님.

## 5. 이벤트 그룹 `eGROUP_ID` (전체 — 7종)

`OTS`, `LG`, `LG_HILS`, `A5`, `B6`, `CHAGOON`, `MUAV`

> **2026-11-19 재검증**: `TransmitClientEvent` 실제 호출에서 **`MUAV`만 사용**. 나머지 6종은 미사용 enum member(§12-2).

## 6. 데이터 정의 `eDEFINITION_ID` (38종, enum `SimManagerDefinition.cs` / 매핑 `SimControl_P3DSet.cs:160~934`) — SimConnect 변수 매핑

`AddToDataDefinition` + `RegisterDataDefineStruct<T>`로 시작 시 등록. 카테고리(대표 `Initialize*` 함수):

- **UAV 상태/이동**: `UAV_LATLONALT_INFO`(위경고도+PBH+속도), `MOVE_UAV_POSITION`(Position/Pitch Bank Heading), `UAV_LG_INFO`, `UAV_ENVIRONMENT_INFO`, `UAV_AVIONICS_INFO`, `UAV_CONTROL_INFO`, `UAV_AP_INFO`
- **연료/엔진**: `UAV_FUEL_INFO`(FUEL TANK * LEVEL, percent), `ENGINE_COMBUSION`, `SET_FUEL_LEVEL`
- **조종 입력(TX)**: `THROTTLE`, `MIXTURE_LEVER`, `PROPELLER_LEVER`, `SET_PRESSURE`, `LANDING_GEAR`, `SET_PITCH_DEGREES`, `AP_TAXING_CONTROL`, `VERTICAL_SPEED`
- **기체 상태**: `HealthPoint`
- **표적**: `TARGET_STATUS`(PLANE BANK/PITCH/HEADING, Lat/Lon, …), `TARGET_WAYPOINTS`(`SIMCONNECT_DATA_WAYPOINT[]`)
- **지상**: `MOVE_GROUND_VEHICLE_POSITION`
- **기타**: `SET_FLIGHT_SURFACE`, `SET_RUDDER`, `SET_BANK_DEGREES`, `SET_BRAKE`, `PARKINGBRAKES`, `UAV_ENGINE_ON`, `UAV_INFORMATION`, `WAYPOINT_DATA`, `AP_VERTICALSPEED`(데드 — §12-3)

- 매핑된 native 변수 예시: `PLANE BANK DEGREES`, `PLANE PITCH DEGREES`, `PLANE HEADING DEGREES TRUE`, `Plane Latitude/Longitude`, `TURB ENG N1:1/2`, `FUEL TANK CENTER LEVEL` 등.

## 7. 클라이언트 이벤트 `eEVENT_ID` (`SimManagerDefinition.cs`, 대형 enum)

- `MapClientEventToSimEvent`로 SimConnect 이벤트 문자열에 매핑(`SimControl_P3DSet.cs:942`~). 예: `PAUSE_ON/OFF`·`VIEW_CAMERA_SELECT_1..6`·`VIEW_UP`→`VIEW_FORWARD`.
- `TransmitClientEvent`로 실제 전송(그룹 + `GROUPID_IS_PRIORITY`). 대표: `ENGINE_AUTO_START`, `FREEZE_LATITUDE_LONGITUDE_SET`, `FREEZE_ALTITUDE_SET`, `FREEZE_ATTITUDE_SET`, `KEY_AUTO_THROTTLE_ARM`, `KEY_AP_PANEL_SPEED_ON`, `KEY_AP_SPD_VAR_SET`.
- 관련 enum: `eVIEW_POINT`(카메라 뷰 13종), `eCAMERA_MOVE`(9방향), `eZOOM`(IN/OUT), `eENGINE_STATUS`(ON=3/OFF=12).

## 8. 사용된 SimConnect 데이터 구조 (`SIMCONNECT_DATA_*`)

`SIMCONNECT_DATA_INITPOSITION`(생성 초기 위치) · `SIMCONNECT_DATA_WAYPOINT`(이동 경로) · `SIMCONNECT_DATA_OBSERVER` · `SIMCONNECT_DATA_XYZ`(offset) · `SIMCONNECT_DATA_PBH`(회전) · `SIMCONNECT_DATA_LATLONALT`(look-at)

## 9. 사용된 SimConnect 열거형 / 상수

- `SIMCONNECT_PERIOD.VISUAL_FRAME`
- `SIMCONNECT_DATATYPE.FLOAT32 / FLOAT64 / INT32 …`
- `SIMCONNECT_EVENT_FLAG.GROUPID_IS_PRIORITY`
- `SIMCONNECT_DATA_SET_FLAG.DEFAULT`
- `SimConnect.SIMCONNECT_UNUSED`(배열 인덱스)
- `SimConnect.SIMCONNECT_OBJECT_ID_USER`(자기/유저 객체 ID)

## 10. 사용 **않는** 주요 export (미노출 — text-search 0건)

`RegisterEvent` · `PauseDataControls`/`SetPauseDataControls` · `SetFrameRate`/`SetFrameOffset` · `GetVersion`/`GetSuggestedFrameTime` · `RequestDataOnTimer`/`OnPeriodicTimer`/`OnStateChange`/`OnSystemClock`/`OnClient` · `RequestSimObjectListData` · `RequestSimVarsSystem`/`SimObject` · `RequestWeatherObservation` · `CreateAirframeObject` · `CreateGroundVehicleObject` · `CreateCustomSurfaceObject` · `AttachSimObjectToSimObject`/`Detach…`

> **2026-11-19 재검증**: `RequestObserverData`는 위 목록에서 **제외** — `SimControl.cs:2169`에서 활성 호출(단, 소비자 없는 문제 있음: §12-1).

---

## 11. 아키텍처 함의 (vendor split)

- 이 표면 전체가 **100% 업체(Prepar3D 통합)로 이동** 대상(`architecture-vendor-split.md`). `SimControl`/`SimControl_P3DSet`의 SimConnect 부분이 제거되고, 본 프로젝트는 `icd-sim-api-v1.md`(SIM_* 메시지)/`vendor-requirements.md`(R-ACC-*)만 소비.
- 즉 업체는 위 **2절의 기능**(데이터 정의·주기 요청·값 주입·이벤트·객체 생성/삭제·옵저버·기상)을 **내부적으로 `SimConnect.dll`로 구현**하고, 대외에는 ICD 메시지만 노출해야 함.

---

## 12. 데드코드 / 의미 없는 호출 (2026-11-19 재검증)

1. **`RequestObserverData` 빈 루프**: `OnRecvSimobjectData`의 `case OBSERVER_INFO`(`SimControl.cs:1767`)가 `RequestObserverData`(`2169`)를 활성 호출하지만, `OnRecvObserverData` 구독이 주석 처리(`P3DSet.cs:89`) → 옵저버 데이터를 받아도 **소비하는 코드 없음**.
2. **`eGROUP_ID` 7종 중 6종 미사용**: `TransmitClientEvent` 호출은 전부 **`MUAV`만** 사용(`SimControl.cs` 전역). `OTS`/`LG`/`LG_HILS`/`A5`/`B6`/`CHAGOON`은 호출 사이트 0(기존 ICD 그룹 enum 잔재).
3. **`AP_VERTICALSPEED` 데드 정의**: `AddToDataDefinition`+`RegisterDataDefineStruct<AutopilotVerticalSpeedSet>`만 존재(`P3DSet.cs:921-922`), **`SetDataOnSimObject` 호출 0건**. 실제 활용 수직속도 주입은 `VERTICAL_SPEED`→`SetVerticalSpeed`(`SimControl.cs:2081`). 상세: `simconnect-data-detail.md` §5-7.
4. **주석 처리 핸들러**: `OnRecvObserverData`(`SimControl.cs:1763`) · `OnRecvWeatherObservation` — 데드 핸들러(§3에 정확히 기재).

---

## 부록: 근거 파일/라인

- `IGManager.csproj:118-120` — wrapper 참조
- `SimControl.cs`(partial, 9635줄) — `_simConnect` 생성 `4017` · RX 핸들러 `1728-1796`, `2117` · `TransmitClientEvent` `1970` · `SetDataOnSimObject` `1998` · `Dispose` `2058`/`4166` · observer `2873-2934` · `AIRemoveObject` `4148`/`7888` · `AICreateSimulatedObject`(표적) `7951` · camera `1710`/`3085` · 기상 `4514`
- `SimControl_P3DSet.cs`(partial, 1227줄) — WndProc/ReceiveMessage `47-52` · 이벤트 구독 `79-89` · `AddToDataDefinition`/`RegisterDataDefineStruct` `160-934` · `MapClientEventToSimEvent` `942`~ · `WeatherSetModeGlobal` `137`
- `SimManagerDefinition.cs` — `eREQUEST_ID`(59) · `eDEFINITION_ID`(38) · `eGROUP_ID`(7) · `eEVENT_ID` · `eVIEW_POINT`(13) · `eCAMERA_MOVE`(9) · `eZOOM`(2) · `eENGINE_STATUS`
- `SimManager.cs:3454-3496` — `AICreateNonATCAircraft`(SimControl.SimConnect 경유)
