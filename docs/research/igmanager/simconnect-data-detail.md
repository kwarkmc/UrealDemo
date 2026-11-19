# SimConnect 데이터·이벤트·객체 상세 인벤토리 — IGManager

> `simconnect-api-surface.md`(API 노출 표면)의 **상세 확장**: "어떤 데이터를 긁어오고, 각 이벤트/객체가 어떤 정보를 담는지"를 struct 필드·SimConnect 변수·enum 수준으로 기록.
> **조사 방법**: `literal text-search + 파일 직접 읽기`. semantic(semantic/clangd) MCP가 프로세스 종료로 실패 → **text-search fallback** (정확성 한계 명시; 일부 필드 수는 truncated read로 카테고리 수준).
> **근거 파일**: `SimControl_P3DSet.cs`(AddToDataDefinition 160~672 · RegisterDataDefineStruct 170~934 · 주기 요청 279~360) · `SimManagerICDStructure.cs`(struct 36개) · `SimControl.cs`(OnRecvSimobjectData switch 2117~2780 · OnRecvEvent 1746 · OnRecvAssignedObjectId 1796 · 주입 SetDataOnSimObject 1998) · `SimManagerDefinition.cs`(enum).
> **기준**: 2026-11-19 · **읽기 전용**(코드 변경 0).
> **2026-11-19 재검증**: struct 수·라인 정정(36개, `:333`/`:373`), `AP_VERTICALSPEED`는 주입 아님으로 정정, 데드코드 항목 보강(§5-6~8).

---

## 0. 데이터 흐름 (수집/주입 공통)

1. 시작 시 `AddToDataDefinition(defID, "SIMCONNECT_VAR", unit, type, default, SIMCONNECT_UNUSED)`로 native 변수→`defID` 정의, `RegisterDataDefineStruct<T>(defID)`로 C# struct `T`를 그 `defID`에 바인딩 (`SimControl_P3DSet.cs:160~934`).
2. `RequestDataOnSimObject(requestID, defID, objectID, SIMCONNECT_PERIOD.VISUAL_FRAME, 0,0, interval, 0)`으로 주기 요청.
3. P3D 패킷 → `WM_USER_SIMCONNECT(0x0402)` → `DefWndProc` → `ReceiveMessage()` → `OnRecvSimobjectData`.
4. `OnRecvSimobjectData`(`SimControl.cs:2117`)가 `data.dwRequestID`를 **switch** → `data.dwData[0]`를 `(structT)`로 **직접 캐스트** → ICD/TM/유도/로그.
5. **주입(TX)**: `SetDataOnSimObject(defID, objectID, SIMCONNECT_DATA_SET_FLAG.DEFAULT, struct)`(값 쓰기) / `TransmitClientEvent(objectID, eEVENT_ID, dwData:uint, group, flags)`(이벤트).

> 모든 struct는 `[StructLayout(LayoutKind.Sequential, Pack=1)]` — SimConnect marshaling용.

---

## 1. 수집(RX) 데이터 — "어떤 데이터를 긁어오는지"

### 1.1 주기 수집 (자기 객체 `SIMCONNECT_OBJECT_ID_USER`, `VISUAL_FRAME`) — `SimControl_P3DSet.cs:279~360`
시작 시 1건씩 등록. **실질 유효 8 + orphan 2(AVIONICS/LG)**.

| eREQUEST_ID | defID | struct | 수집 내용 |
|---|---|---|---|
| `UAV_ENGINE_INFO` | `UAV_ENGINE_INFO` | `UavEngineData` | 엔진(피스톤2+터빈+일반) 전 항목 |
| `UAV_FUEL_INFO` | `UAV_FUEL_INFO` | `UavFuelData` | 연료탱크 11개 × {LEVEL, CAPACITY, QUANTITY} |
| `UAV_LIGHT_INFO` | `UAV_LIGHT_INFO` | `UavLightData` | 조명 16종 on/off |
| `UAV_LATLONALT_INFO` | `UAV_LATLONALT_INFO` | `UavLatLonAltData` | 위치·자세·속도·가속도·회전·기울기 (가장 풍부) |
| `UAV_INSTRUMENTATION_INFO` | `UAV_INSTRUMENTATION_INFO` | `UavFlightInstrumentationData` | 계기(속도/마하/고도계/기압/컴퍼스) |
| `UAV_AVIONICS_INFO` | `UAV_AVIONICS_INFO` | **(없음)** | **orphan** — 요청만, AddToDataDefinition·struct 없음 |
| `UAV_CONTROL_INFO` | `UAV_CONTROL_INFO` | `UavControlData` | 조종면/조종축/기어 상태 |
| `UAV_AP_INFO` | `UAV_AP_INFO` | `UavApData` | 자동비행(AP) 설정·모드 |
| `UAV_LG_INFO` | `UAV_LG_INFO` | **(없음)** | **orphan** — 위와 동일 |
| `UAV_ENVIRONMENT_INFO` | `UAV_ENVIRONMENT_INFO` | `UavEnvironmentData` | 공기밀도·기압·기온·바람 |

### 1.2 객체별 수집 (생성 시, per-object) — `SimControl.cs`
- **5 무인기 위치**: `CreateAirObject0..4` 수신 → `RequestDataOnSimObject(UAV_LATLONALT_CREATE0..4, UAV_LATLONALT_INFO, objID, VISUAL_FRAME)` (1855~1894). RX `2393~2564`에서 `UavLatLonAltData` 캐스트, 이어서 엔진-온 1회 요청.
- **5 무인기 엔진-온**: `UAV_ENGINE_CREATE0..4, UAV_ENGINE_ON, objID, ONCE` (2425~2564) → `UavEngineOnData`(`GeneralEngineCombusion`).
- **표적**: `CREATE_FCS1`(생성) → `RequestDataOnSimObject(TARGET_STATUS, objID, VISUAL_FRAME, interval=5)` (1830) + `TargetInfo` `ONCE` (1898/2778) → `IntruderStatus` 캐스트 (2762).
- **지상 물체**: `CreateGroundObject1..20` (1896~1954) → 지상/레이더/클라우드 객체.

### 1.3 주입(TX) 데이터 — `SetDataOnSimObject` (값을 시뮬에 **쓰는** 쪽)
defID → struct(주입 값):
- **이동**: `MOVE_UAV_POSITION`→`MoveUavPosition` · `MOVE_GROUND_VEHICLE_POSITION`→`GroundVehiclePosition` · `TARGET_WAYPOINTS`→`SIMCONNECT_DATA_WAYPOINT[]`
- **조종**: `THROTTLE`→`Throttle` · `SET_FLIGHT_SURFACE`→`FlightSurface` · `MIXTURE_LEVER`→`MixtureLeverCmd` · `PROPELLER_LEVER`→`PropellerLeverCmd` · `SET_RUDDER`→`UavRudderValue` · `SET_PITCH_DEGREES`→`UavPitch` · `SET_BANK_DEGREES`→`UavBankValue`
- **엔진/기타**: `SET_FUEL_LEVEL`→`UAVFuelLevelEachTank` · `ENGINE_COMBUSION`→`EngineCombusion` · `SET_PRESSURE`→`UavPressure` · `LANDING_GEAR`→`LandingGearPosition` · `VERTICAL_SPEED`→`SetVerticalSpeed` · `AP_TAXING_CONTROL`→`Ap_TaxingControl` · `SET_BRAKE`→`SetBrake` · `HealthPoint`→`HealthPoint` · `PARKINGBRAKES`→`ParkingBrakes`
- **AP**: `AP_GYRO_HEADING`→`GyroHeading` · `AP_MASTER_SET`→`AutopilotMasterSet` · `AP_HEADING_SET`→`AutopilotHeadingVal` · `AP_ALTITUDE_SET`→`AutopilotAltitudeVal` (~~`AP_VERTICALSPEED`~~ — **주입 호출 없음, 정의만 존재** → §5-7)

> **수집 vs 주입 분리**: 같은 struct가 `SetDataOnSimObject`로 쓰이기도(주입)하고 `RequestDataOnSimObject`로 긁어오기도(수집)함. 예: `UavPitch`/`UavBankValue`는 주입, `UavLatLonAltData`는 수집.

---

## 2. 객체(struct)별 정보 — 36개 (카테고리별)

### 2.1 위치·자세·운동 (가장 풍부한 텔레메트리)
**`UavLatLonAltData`** (`UAV_LATLONALT_INFO`) — `SimManagerICDStructure.cs:333~`, 필드:
- 속도: `AirspeedIndicated`, `VelocityGround`, `VelocityTotalWorld`, `VelocityBodyX/Y/Z`, `VelocityWorldX/Y/Z`
- 가속: `AccelerationWorldX/Y/Z`, `AccelerationBodyX/Y/Z`
- 회전: `RotationVelocityBodyX/Y/Z` (rad/s → 유도/Pitch_Rate로 쓰임, `SimManager.cs:12797`)
- 위치: `PlaneLatitude`(deg) · `PlaneLongitude`(deg) · `PlaneAltitude`(ft) · `PlaneAltAboveGround`(m) · `GroundAltitude`(ft)
- 자세: `PlanePitchDegrees` · `PlaneBankDegrees` · `PlaneHeadingDegreesTrue` · `PlaneHeadingDegreesMagnetic` · `Magvar`
- 기타: `SurfaceType`(enum) · `SimOnGround` · `IncidenceAlpha` · `IncidenceBeta` · `WingFlexPctLeft/Right` · `IndicatedAltitude`

**`UavPosition`** (~6): `Latitude` `Longitude` `Altitude` `Pitch` `Bank` `Heading` (+`OnGround`) — 기본 이동용
**`MoveUavPosition`** (~7): 위 6 + `OnGround` — `MOVE_UAV_POSITION` 주입용
**`GroundVehiclePosition`** (4): `x` `y` `heading` `type`
**`UavEnvironmentData`** (~11): `AmbientDensity` `AmbientDensityRatio` `AmbientPressure` `AmbientTemperature` `StandardAtmDensity/Pressure/Temperature` `WindX` `WindY` `WindZ`

### 2.2 엔진
**`UavEngineData`** (`UAV_ENGINE_INFO`, 최대 struct ~55) — `:125~`
- **일반**: `NumberOfEngines` · `GeneralEngineCombustion1/2` · `GeneralEngineStarterActive1/2` · `GeneralEngineFuelUsedSinceStart1/2`
- **피스톤×2** (`:1`/`:2`): `RecipEngineStarter` · `FuelFlow` · `FuelPump×3` · `FuelSelector×3` · `CowlFlap` · `Mixture` · `PropGovernor` · `OilPressure` · `OilTemp` · `OilLevel` · `Rpm` · `FuelTankSelector` · `FuelTanksUsed`
- **터빈**: `TurbEngineN1/N2` · `CorrectedN1/N2` · `CorrectedFF` · `MaxTorquePercent` · `PressureRatio` · `ITT` · `Afterburner` · `JetThrust` · `BleedAir` · `TankSelector` · `TanksUsed` · `NumTanksUsed` · `FuelFlowPPH` · `FuelAvailable` · `ReverseNozzlePercent` · `Vibration` · `PrimaryNozzlePercent` · `IgnitionSwitch` · `IgnitionOn` · `MasterStarterSwitch` · `AfterburnerStageActive` · `AfterburnerPctActive`

**`UavEngineOnData`** (1): `GeneralEngineCombusion`(int) — 엔진 가동 유무
**`EngineCombusion`** (2): `comb1` `comb2` — 엔진 점화 **주입**용

### 2.3 연료
**`UavFuelData`** (`UAV_FUEL_INFO`, 33) — `:247~`
- **탱크 11개**: `CENTER` `CENTER2` `CENTER3` `LEFT MAIN` `LEFT AUX` `LEFT TIP` `RIGHT MAIN` `RIGHT AUX` `RIGHT TIP` `EXTERNAL1` `EXTERNAL2`
- 각 탱크 × **{LEVEL(percent) · CAPACITY(gallons) · QUANTITY(gallons)}** = 33 필드
**`UAVFuelLevelEachTank`** (`SET_FUEL_LEVEL`, 5): `fCenterFWLevel` `fCenterFEEDLevel` `fCenterAFTLevel` `fLeftMainLevel` `fRightMainLevel` — **주입**용

### 2.4 조명
**`UavLightData`** (`UAV_LIGHT_INFO`, 16) — `:299~`
- `HeadLight` `BrakeLight` `TaxiLight` `LogoLight` `RecognitionLight` `WingLight` `StrobeLight` `BeaconLight` `LandingLight` `NavLight` `CabinLight` (native `LIGHT *`) + `Head` `Brake` `Nav` `Beacon` `Landing` (native `LIGHT * ON`)

### 2.5 계기
**`UavFlightInstrumentationData`** (`UAV_INSTRUMENTATION_INFO`) — `:373~`
- `AirspeedTrue` `AirspeedIndicated` `AirspeedGround` `MachNumber` `AltimeterSetting` `CompassDeviation` `DynamicPressure` `StaticPressure` `SuctionPressure` (등)

### 2.6 조종 입력·제어
**`UavControlData`** (`UAV_CONTROL_INFO`) — 조종면/조종축/기어 상태 (`Yoke*` · `Rudder` · `Throttle` · `Brake` · `Gear*` 등)
**`Throttle`** (2): `P3dThrottle` `P3dThrottle2`
**`FlightSurface`** (4): `Aileron` `Elevator` `Throttle` `Rudder`
**`MixtureLeverCmd`** (1): `MixtureLever`
**`PropellerLeverCmd`** (1): `PropellerLever`
**`UavRudderValue`** (1): `RudderValue`
**`SetBrake`** (2): `LeftBrake` `RightBrake`
**`Ap_TaxingControl`** (4): `Rudder` `Throttle` `LeftBrake` `RightBrake` — 활주로 주행 모의
**`LandingGearPosition`** (3): `NLG` `LMG` `RMG`

### 2.7 자동비행(AP)
**`UavApData`** (`UAV_AP_INFO`) — `ApAvailable` `ApMaster` `ApHoldMode` `ApAltitude` `ApHeading` `ApVerticalSpeed` `ApAirspeed` `ApMach` … `SimRate` (등)
**`UavBankValue`** (1): `BankValue` | **`UavPitch`** (1): `PitchValue` | **`UavPressure`** (1): `Pressure`
**`GyroHeading`** (1): `Heading`
**`AutopilotMasterSet`** (1): `ApMaster`
**`AutopilotHeadingVal`** (1): `Heading`
**`AutopilotAltitudeVal`** (1): `Altitude`
**`AutopilotVerticalSpeedSet`** (1): `VerticalSpeed` (대응 defID `AP_VERTICALSPEED`는 데드 — §5-7)

### 2.8 기체 상태
**`HealthPoint`** (1): `Hp` | **`SetVerticalSpeed`** (1): `VerticalSpeed` | **`ParkingBrakes`** (1): `ParkingBrake`

### 2.9 표적
**`IntruderStatus`** (`TARGET_STATUS`, 8) — `:663~` 〔**실사용**, 2번째 등록〕
- `TargetId`(`SURFACE TYPE` 변수에 매핑 — 특수) · `Latitude` · `Longitude` · `Altitude` · `Heading` · `SpeedTas`(`AIRSPEED INDICATED` 매핑) · `SpeedGs`(`GROUND VELOCITY`) · `SpeedVs`(`VERTICAL SPEED`)
**`TargetStatus`** (`TARGET_STATUS`, 7) — `:702~` 〔1번째 등록, `IntruderStatus`에 재정의됨〕
- `fRoll` `fPitch` `fTureHeading` `dLatitude` `dLongitude` `fAltitude` `nSimOnGround`

### 2.10 기상
**`CloudStatus`** — `CloudId` · `Latitude` · `Longitude` · `Altitude` · `CloudName`(string) — `AICreateSimulatedObject`용

---

## 3. 이벤트(TX) — `eEVENT_ID` (클라이언트→Sim)

전달: `TransmitClientEvent(ObjectID, eEVENT_ID, dwData:uint, GroupID:eGROUP_ID, Flags)`.
- **dwData** = 수치 인자(속도/고도/heading/0-1 스위치 값). **GroupID** = `eGROUP_ID`(`OTS` `LG` `LG_HILS` `A5` `B6` `CHAGOON` `MUAV`) — 실제 호출은 **`MUAV`만 사용**(§5-8).
- 매핑: `MapClientEventToSimEvent(eEVENT_ID.X, "SIM_EVENT_NAME")` (`SimControl_P3DSet.cs:942~`). 대표: `PAUSE_ON/OFF`·`VIEW_CAMERA_SELECT_1..6`·`VIEW_UP`→`VIEW_FORWARD`.

### 3.1 이벤트 그룹 (enum region 기준)
- **시스템/일시정지**: `PAUSE_ON/OFF` · `KEY_SIM_RATE_INCR/DECR` · `KEY_RELOAD_PANELS`
- **카메라 뷰**: `VIEW_CAMERA_SELECT_1..6` · `VIEW_FORWARD/BACK/LEFT/RIGHT` · `VIEW_UP`
- **Slew/축**: `KEY_SLEW_ON/OFF/SET/RESET/ALTIT_UP_FAST` · `KEY_AXIS_SLEW_BANK_SET` · `KEY_AXIS_AILERONS/ELEVATOR/RUDDER_SET` · `KEY_THROTTLE_SET`
- **엔진**: `ENGINE_AUTO_START` · `MAGNETO_START/OFF` · `TOGGLE_AFTERBURNER`
- **Freeze(정지)**: `FREEZE_LATITUDE_LONGITUDE_SET` · `FREEZE_ALTITUDE_SET` · `FREEZE_ATTITUDE_SET` · `KEY_POSITION_FREEZE_USER`
- **자동비행(AP)**: `KEY_AUTO_THROTTLE_ARM` · `KEY_AP_PANEL_SPEED_ON/OFF/SET` · `KEY_AP_PANEL_HEADING_SET` · `KEY_AP_PANEL_MACH_ON/OFF/SET` · `KEY_AP_ALT_VAR_SET_ENGLISH` · `KEY_AP_VS_VAR_SET_METRIC` · `KEY_AP_PITCH_REF_INC_UP/DN/SELECT` · `KEY_AP_ATT_HOLD` · `KEY_AP_LOC_HOLD` · `KEY_AP_APR_HOLD` · `KEY_AP_HDG_HOLD` · `KEY_AP_ALT_HOLD` · `KEY_AP_WING_LEVELER` · `KEY_AP_BC_HOLD` · `KEY_AP_NAV1_HOLD` · `KEY_AP_MAX_BANK_INC/DEC` · `KEY_AP_N1_HOLD/REF_INC/DEC/SET` · `KEY_TOGGLE_FLIGHT_DIRECTOR` · `KEY_SYNC_FLIGHT_DIRECTOR_PITCH` · `KEY_AP_NAV_SELECT_SET` · `KEY_AUTOPILOT_AIRSPEED_HOLD_CURRENT` · `KEY_AUTOPILOT_PANEL_AIRSPEED_SET` · `KEY_AUTOPILOT_MACH_HOLD_CURRENT` · `KEY_HEADING/ALTITUDE/VSI/AIRSPEED_BUG_SELECT`
- **FBW**: `KEY_FLY_BY_WIRE_ELAC/FAC/SEC_TOGGLE`
- **액추에이터**: `KEY_FLAPS_SET` · `KEY_SPOILERS_SET` · `SPOILERS_ARM_ON/OFF`
- **패널**: `KEY_PANEL_1..9` · `KEY_PANEL_ID_TOGGLE/OPEN/CLOSE`
- **주행**: `KEY_PARKING_BRAKES` · `KEY_INC_AUTOBRAKE_CONTROL` · `KEY_DEC_AUTOBRAKE_CONTROL`
- **연료**: `KEY_FUEL_DUMP_SWITCH_SET/TOGGLE` · `KEY_FUEL_SELECTOR_ALL/SET` · `KEY_FUEL_SELECTOR_2/3/4_SET` · `KEY_CROSS_FEED_TOGGLE/OPEN/OFF`
- **조명**: `KEY_ALL_LIGHTS_TOGGLE` · `KEY_PANEL_LIGHTS_ON` · `KEY_STROBES_ON` · `KEY_LANDING_LIGHTS_ON` · `KEY_TOGGLE_BEACON/TAXI/LOGO/RECOGNITION/WING/NAV/CABIN_LIGHTS`
- **항법**: `KEY_RADIO_VOR1/2_IDENT_DISABLE/ENABLE` · `KEY_NAV1/2_RADIO_SET` · `KEY_TRUE_AIRSPEED_CAL_SET`
- **표적**: `FCS_ADD_TARGET` · `FCS_REMOVE_TARGET`

### 3.2 수신(RX) 이벤트 — `OnRecv*` 핸들러
- **`OnRecvSimobjectData`** (`SimControl.cs:2117`): **주 텔레메트리**. `dwRequestID` switch로 struct 캐스트 (2121~2780).
- **`OnRecvEvent`** (1746): Sim→client 이벤트 수신 (이벤트는 주로 client→Sim 1방향이라 body는 최소/미활성 — 본문 미확인으로 제한).
- **`OnRecvAssignedObjectId`** (1796): 객체 생성 시 objectID 할당 → **per-object 데이터 요청 트리거**.
- **`OnRecvOpen`** (P3DSet:103) · **`OnRecvQuit`** (1728) · **`OnRecvException`** (1737): 라이프사이클/에러.
- (주석 처리·미활성: `OnRecvObserverData` · `OnRecvWeatherObservation`)

---

## 4. RX 캐스트 맵 (`OnRecvSimobjectData` switch, `SimControl.cs:2117~2780`)

| dwRequestID (eREQUEST_ID) | struct 캐스트 | 용도 |
|---|---|---|
| `UAV_ENGINE_INFO` (2121) | `UavEngineData` | 자기관 엔진 |
| `UAV_FUEL_INFO` (2172) | `UavFuelData` | 자기관 연료 |
| `UAV_LIGHT_INFO` (2179) | `UavLightData` | 자기관 조명 |
| `UAV_LATLONALT_INFO` (2187) | `UavLatLonAltData` | 자기관 위치/자세/운동 |
| `UAV_ENGINE_CREATE0..4` (2328~2380) | `UavEngineOnData` | 5기 엔진-온 (ONCE) |
| `UAV_LATLONALT_CREATE0..4` (2393~2564) | `UavLatLonAltData` | 5기 위치 (VISUAL_FRAME) |
| `UAV_INSTRUMENTATION_INFO` (2714) | `UavFlightInstrumentationData` | 자기관 계기 |
| `UAV_CONTROL_INFO` (2723) | `UavControlData` | 자기관 조종 |
| `UAV_AVIONICS_INFO` (2732) | **(없음)** | orphan |
| `UAV_AP_INFO` (2736) | `UavApData` | 자기관 AP |
| `UAV_LG_INFO` (2745) | **(없음)** | orphan |
| `UAV_ENVIRONMENT_INFO` (2749) | `UavEnvironmentData` | 자기관 환경 |
| `CREATE_FCS1`/`TargetInfo` (2757/2769) | `IntruderStatus` | 표적 |
| `CREATE_CLOUD`/`CREATE_RADAR`/`CreateGroundObject1..20` | (객체 생성) | 지상/레이더/클라우드 |
| `OBSERVER_INFO` (1767) | (재요청 `RequestObserverData` 2169) | 옵저버 — 소비자 없음(§5-6) |

---

## 5. 주의/특이사항

1. **`TARGET_STATUS` 이중 등록**: `TargetStatus`(7) → `IntruderStatus`(8) 재정의(`SimControl_P3DSet.cs:170` vs `:251`). 시뮬이 실제 사용하는 것은 **후자 `IntruderStatus`**. `IntruderStatus.TargetId`는 `SURFACE TYPE` 변수에, `SpeedTas`는 `AIRSPEED INDICATED`에 매핑(필드명과 실제 값이 다름).
2. **orphan 요청 2건**: `UAV_AVIONICS_INFO`, `UAV_LG_INFO` — `RequestDataOnSimObject`(324/351)+RX case(2732/2745)는 있으나 `AddToDataDefinition`·`RegisterDataDefineStruct`가 없어 **실질 데이터 없음**.
3. **5기 고정 인덱스**: `UAV_LATLONALT_CREATE0..4` / `UAV_ENGINE_CREATE0..4` / `CreateAirObject0..4` — 무인기 5대.
4. **주기**: 전부 `VISUAL_FRAME`(프레임 동기). interval은 **표적만 5** 지정(지연 방지, 1830).
5. **수집 vs 주입 방향 분리**: 같은 defID/struct이 쓰기(`SetDataOnSimObject`)와 긁기(`RequestDataOnSimObject`)에 따라 역할이 다름.
6. **`RequestObserverData` 빈 루프(2026-11-19 재검증)**: `case OBSERVER_INFO`(`SimControl.cs:1767`)에서 **활성 호출**(2169)이지만 `OnRecvObserverData` 구독이 주석 처리(`P3DSet.cs:89`) → 수신 데이터 **소비자 없음**.
7. **`AP_VERTICALSPEED` 데드 정의(2026-11-19 재검증)**: `AddToDataDefinition`+`RegisterDataDefineStruct`(921-922)만 존재, `SetDataOnSimObject` 호출 **0건**. 실제 활용 수직속도 주입은 `VERTICAL_SPEED`→`SetVerticalSpeed`(`SimControl.cs:2081`).
8. **`eGROUP_ID` 7종 중 6종 미사용(2026-11-19 재검증)**: `OTS`/`LG`/`LG_HILS`/`A5`/`B6`/`CHAGOON`은 `TransmitClientEvent` 호출 사이트 0 — 실제는 `MUAV`만 사용.

---

## 6. 근거 (파일:라인)

- **AddToDataDefinition**: `SimControl_P3DSet.cs` 160~672 (TARGET_STATUS 242~250, SET_FUEL_LEVEL 198~202, UAV_ENGINE_INFO 490~558, UAV_FUEL_INFO 590~624, UAV_LIGHT_INFO 643~671, UAV_LATLONALT_INFO 677~716)
- **RegisterDataDefineStruct**: `SimControl_P3DSet.cs` 170~934 (33건)
- **struct**: `SimManagerICDStructure.cs` 총 36개 (UavEngineData 125 · UavFuelData 247 · UavLightData 299 · UavLatLonAltData 333 · UavFlightInstrumentationData 373 · IntruderStatus 663 · TargetStatus 702)
- **RX switch**: `SimControl.cs` 2117~2780 · **주기 요청**: `SimControl_P3DSet.cs` 279~360 · **객체별 요청**: `SimControl.cs` 1830, 1855~1894, 1896~1954, 2393~2564
- **enum**: `SimManagerDefinition.cs` (eREQUEST_ID 59종 · eDEFINITION_ID 38종 · eGROUP_ID 7종 · eEVENT_ID · eVIEW_POINT 13 · eCAMERA_MOVE 9 · eZOOM 2 · eENGINE_STATUS)
