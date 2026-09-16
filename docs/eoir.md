# EO/IR 영상·짐벌·가상 지상 표적

[EO/IR 페이지](http://127.0.0.1:8765/eoir)에서 기체에 연결된 카메라의 영상을 보고 짐벌·시야각·표적 추적을 제어한다. 메인 비행 화면과 별도 페이지이며 같은 실행 기체와 API를 사용한다. API·Unreal 시작 방법은 [API 매뉴얼](api-reference.md)을 따른다.

CCD 모드는 Unreal 장면을 컬러로 렌더링한다. `ir_white`는 밝기 기반 회색조, `ir_black`은 그 반전이다. **온도·방사율·열전달을 계산하는 열상 모델이 아니다.** 현재 카메라는 이상적인 즉시 각도 제어이며 실제 짐벌 서보 응답·센서 잡음·광학 오차를 모델링하지 않는다. 장착점은 기체 로컬 원점에서 앞 20cm·아래 12cm이며 기체 자체 메시는 센서 영상에서 숨긴다.

영상 종류인 `eoir.mode`와 지향 방식인 `eoir.gimbal_mode`는 별도다. 짐벌은 아래 네 모드 중 하나로 동작한다.

| 짐벌 모드 | 동작 |
| --- | --- |
| `body_fixed` | 지정 방위·고각을 기체 기준으로 유지한다. 기체가 회전하면 카메라의 세계 방향도 함께 바뀐다. |
| `auto` | 선택한 등록 표적 ID의 위치를 계속 향한다. 영상 인식으로 표적을 찾지 않는다. |
| `geo` | 지정한 위도·경도·WGS84 타원체고의 고정점을 계속 향한다. 표적 등록이 필요 없다. |
| `protect` | 방위 180°·고각 +30°로 고정하고 영상을 끈다. 실제 장비의 기계적 보호 성능을 뜻하지 않는 가상 보호 자세다. |

## 페이지에서 사용

1. 메인 페이지에서 Unreal을 실행한 뒤 EO/IR 페이지를 연다. **센서 켜기**, CCD·IR 모드와 FOV를 선택하고 **설정 적용**을 누른다.
2. **운용 모드 → 기체 고정**에서 방위·고각을 정하고 **모드 적용**을 누른다. 방위는 기체 앞 0°에서 오른쪽 양수, 고각은 위쪽 양수다.
3. ID·종류·위경도·방위를 입력하고 **표적 생성**을 누른다. 높이는 지형·가상 활주로에서 계산한다.
4. **자동 · 선택 표적**에서 추적 ID를 고르고 **모드 적용**을 누른다. 지상 표적 패널의 **선택 표적 추적**도 같은 동작이다.
5. **지리좌표 지향**은 위경도와 WGS84 타원체고를 지정해 적용한다. **선택 표적 좌표**는 현재 좌표를 복사하며 이후 표적 이동을 따라가지는 않는다.
6. **보호 → 모드 적용**으로 가상 보호 자세·영상 OFF로 전환한다. 다시 보려면 다른 모드를 적용한 뒤 **센서 켜기 → 설정 적용**을 누른다. **현재 각도로 기체 고정**은 현재 각도를 유지하며 auto/geo/보호를 해제한다.

영상 위의 **넓게 보기**는 페이지 폭을 넓히고 **전체 화면**은 영상 영역을 브라우저 전체 화면으로 표시한다. **원본 크기 · 스크롤**은 1:1 픽셀, **화면에 맞춤**은 현재 영역에 전체 영상을 맞춘다. 센서 설정의 **1°·5°·30°** 버튼은 FOV를 즉시 적용한다. 원거리의 작은 표적은 FOV를 좁혀 화면에서 차지하는 크기를 늘릴 수 있다.

**비행·연출 재생**과 **일시정지**는 기존 비행 시간 API를 호출한다. 카메라 프레임은 비행이 정지해도 갱신되지만 워게임 애니메이션과 기체 이동은 시간 진행이 필요하다. 페이지 위에 재생 상태·시뮬레이션 시간·설정 배속을 함께 표시한다.

워게임의 **탄체 따라보기**는 진행 중 연출을 Unreal 비행 창의 외부 카메라로 보여준다. EO/IR 영상은 현재 짐벌 지향을 유지한다. **기체 시점**으로 되돌릴 수 있으며 진행 연출이 모두 끝나도 기체 시점으로 복귀한다. [연출·판정 안내](wargame.md)를 따른다.

표적은 `tank`, `missile`, `marker`의 단순 도형 소품이다. 탱크·미사일 형식도 정적인 표시물이며 발사·무장·공격·피해 동작은 없다. 외부 API로 위치를 바꾸면 즉시 그 지면 위치로 재배치되며 차량·비행체 동역학으로 움직이는 모델은 아니다.

## API와 범위

모든 경로는 `/api/v1` 기준이다. JSON 요청은 표의 필드를 정확히 모두 지정한다. Python 기본 인수는 클라이언트가 채워 보낸다.

| 메서드·경로 | 요청 필드·범위 | Python |
| --- | --- | --- |
| `PUT /eoir` | `enabled`: bool, `mode`: `ccd`/`ir_white`/`ir_black`, `fov_deg`: 1~90 | `set_eoir(*, enabled, mode="ccd", fov_deg=30)` |
| `PUT /eoir/gimbal-mode` | 아래 모드별 JSON 중 하나 | `set_gimbal_mode(...)` |
| `PUT /eoir/gimbal` | `azimuth_deg`: −180~180, `elevation_deg`: −90~30; `body_fixed` 전환 | `set_gimbal(*, azimuth_deg, elevation_deg)` |
| `POST /eoir/track` | `target_id`: 등록 ID이면 `auto`, 빈 문자열이면 현재 각도의 `body_fixed` | `track_target(target_id="")` |
| `POST /targets` | `id`, `kind`, `latitude_deg`, `longitude_deg`, `heading_deg` | `create_target(*, id, kind, latitude_deg, longitude_deg, heading_deg=0)` |
| `PUT /targets/pose` | `id`, `latitude_deg`, `longitude_deg`, `heading_deg` | `move_target(*, id, latitude_deg, longitude_deg, heading_deg=0)` |
| `POST /targets/remove` | `id` | `remove_target(id)` |
| `GET /eoir/frame.jpg` | 본문 없음 | `get_eoir_frame()` → JPEG bytes |

Python 시그니처는 `set_gimbal_mode(mode, *, azimuth_deg=None, elevation_deg=None, target_id=None, latitude_deg=None, longitude_deg=None, ellipsoid_height_m=None)`다. `None`만 생략하며 해당 모드와 무관한 인수를 보내면 서버가 400으로 거부한다.

`PUT /eoir/gimbal-mode`는 해당 모드의 필드만 정확히 보낸다.

```json
{"mode":"body_fixed","azimuth_deg":0,"elevation_deg":-45}
{"mode":"auto","target_id":"demo_marker"}
{"mode":"geo","latitude_deg":33.510,"longitude_deg":126.49,"ellipsoid_height_m":60}
{"mode":"protect"}
```

각 줄은 별도 요청 본문이다. `body_fixed`는 방위 −180~180°·고각 −90~30°이며 `auto`는 비어 있지 않은 등록 ID가 필요하다. `geo`는 위도 −90~90°·경도 −180~180°·타원체고 −500~20000m와 렌더 원점에서 3차원 직선거리 1000km 이내를 허용한다. 지정 높이를 그대로 사용하며 지형 높이를 자동 적용하지 않는다. 지면 자료가 없는 좌표도 지향할 수 있다.

보호 모드에서 `set_eoir(enabled=True, ...)`는 409로 거부하고 상태를 유지한다. `enabled=False`로 영상 종류·FOV 설정은 바꿀 수 있다. 다른 짐벌 모드나 기존 수동/추적 API를 호출하면 보호를 해제한다. 이때 영상은 자동으로 켜지지 않으므로 별도로 `set_eoir(enabled=True, ...)`를 호출한다.

`id`는 `[A-Za-z0-9_-]`의 ASCII 1~48자이며 최대 64개 표적을 둔다. `kind`는 세 종류 중 하나다. 위도는 −90~90°, 경도는 −180~180°, 방위는 북쪽 0°에서 시계방향 0~360°다. 높이를 요청에 보내지 않는다. 지형 자료가 있고 렌더 원점에서 수평 1000km 이내인 위치만 허용한다. 이는 기체의 국소 비행 범위 50km와 별도다. 반환 표적의 `ellipsoid_height_m`은 표적 원점의 지면 WGS84 타원체고다.

JSON 명령은 적용 후 전체 비행 상태를 반환한다. 형식·타입·범위 오류는 400, 현재 상태에서 수락할 수 없는 명령은 409다. 중복 ID 생성, 없는 ID의 이동·삭제·추적, 표적 수 초과나 지면 없는 위치는 409이며 `eoir.error`에 이유를 남긴다. Unreal 미연결은 503이다. 카메라 비활성 또는 native 캡처 오류의 프레임 요청은 409이며 손상된 영상·메타데이터는 503으로 처리한다. 프레임 오류도 `{"error":{"message":"..."}}` JSON이며 오류 본문을 JPEG로 저장하지 않는다.

## 프레임과 메타데이터

성공한 `GET /eoir/frame.jpg`는 **1280×960 JPEG, 품질 92**를 `image/jpeg`로 반환한다. JSON 상태 응답과 다르다. `Cache-Control: no-store`와 다음 헤더를 제공한다.

| 헤더 | 의미 |
| --- | --- |
| `X-Simulation-Time-S` | 캡처에 대응하는 시뮬레이션 시간(s) |
| `X-Sensor-Sequence` | 프레임 순번 |
| `X-Sensor-Mode` | 실제 프레임의 `ccd`, `ir_white`, `ir_black` 모드 |

`get_eoir_frame()`은 영상 bytes만 반환한다. 메타데이터가 필요한 프레임워크는 HTTP 응답 헤더를 직접 읽는다. 카메라의 프레임 수와 비행 시간은 별도이며 렌더 처리속도·HTTP 폴링 주기가 고정 센서 주기를 보장하지 않는다. 영상 요청 자체로 시뮬레이션 시간을 진행시키지 않는다. 측정 시간을 맞추려면 반환 헤더를 사용한다.

Web UI는 이전 프레임의 수신·디코딩을 마친 뒤 다음 요청을 예약하여 요청을 겹치지 않고 최대 목표 10회/초로 갱신한다. 표시되는 FPS·ms는 최근 성공한 프레임 사이의 실제 수신 간격이며 목표값을 그대로 표시하지 않는다. 현재 픽셀 크기도 수신한 영상에서 읽어 표시한다. 상태 조회는 약 1회/초다.

현재 센서 렌더는 FXAA, 수동 노출(보정 0), 안개·피사계 심도 해제를 사용하며 대기·하늘 표시는 유지한다. 이는 개발용 표시 설정이며 실제 광학·대기·열상 모델의 보정 결과가 아니다. 지형 원본의 해상도 한계와 제주 상세 영역은 [지도 안내](maps.md)를 따른다.

## 추적의 의미

`auto`는 등록한 표적 원점에서 지면 법선 방향 1.5m 위, `geo`는 지정한 위경고도 점 자체를 향하는 **기하학적 지향**이다. 영상에서 물체를 탐지·식별하거나 픽셀 특징을 따라가는 추적기가 아니다. 짐벌 각도 범위에서 지향을 제한하며 이때 `limited`와 실제 방향 오차를 반환한다. `target_in_fov`는 auto/geo의 조준점이 카메라 시야각 안에 있는지를 뜻하며 지형·다른 물체에 의한 가림이 없다는 보장은 아니다.

`enabled=false`에서도 auto/geo의 지향 계산은 계속한다. auto의 표적을 제거하면 ID를 비우고 마지막 각도의 `body_fixed`로 전환하며 `target_missing`으로 표시한다. 다른 표적의 삭제는 geo 지향에 영향을 주지 않는다. 비행 초기화·pose 설정은 보호 모드를 포함한 카메라 설정·짐벌 모드·표적을 유지한다.

지형을 성공적으로 다시 읽으면 등록 표적을 비운다. geo 점이 새 렌더 원점의 허용 범위 안이면 geo를 유지하고, 범위 밖이면 마지막 각도의 `body_fixed`와 `target_missing`으로 전환한다. 실패한 지형 재읽기는 이전 지형·짐벌 모드·표적을 보존하고 비행은 정지한다. Unreal을 재시작하면 카메라와 표적의 실행 상태는 새로 생성한다. 표적 파일 저장·자동 경로·표적 센서 피드백은 없다.

## 상태

`GET /state`와 일반 JSON 명령의 응답에 `eoir` 객체와 `targets` 배열을 포함한다.

| `eoir` 필드 | 의미 |
| --- | --- |
| `enabled`, `mode`, `fov_deg` | 활성 상태, 영상 모드, 수평 시야각(°) |
| `gimbal_mode` | `body_fixed`, `auto`, `geo`, `protect` |
| `azimuth_deg`, `elevation_deg` | 실제 기체 기준 짐벌 각도(°) |
| `target_id` | auto의 등록 표적 ID. 다른 모드와 표적 삭제 후 빈 문자열 |
| `point_latitude_deg`, `point_longitude_deg`, `point_ellipsoid_height_m` | geo의 지정점(°·°·m). 다른 모드에서는 0 |
| `tracking_status` | `manual`, `tracking`, `pointing`, `protected`, `limited`, `target_missing` |
| `limited` | auto/geo의 점 방향을 짐벌 범위 때문에 그대로 적용할 수 없는지 |
| `target_in_fov` | auto/geo의 점이 시야각 안에 있는지. 가림 검사 결과가 아님 |
| `error_deg` | 실제 카메라 방향과 auto/geo 점 방향 사이 각도(°) |
| `frame_sequence` | 프레임 순번 |
| `error` | 카메라·캡처 오류 |

각 `targets` 항목은 `id`, `kind`, `latitude_deg`, `longitude_deg`, `ellipsoid_height_m`, `heading_deg`를 가진다. 입력 방위 360°는 상태에서 0°로 정규화한다. 카메라와 가상 표적을 변경해도 비행 제어 입력·비행역학 매개변수를 바꾸지 않는다. 카메라 추적과 자동항법은 서로 다른 기능이다.

## 외부 Python 예제

API와 Unreal이 준비되고 기본 제주 지형을 사용하는 예제다. 기체 위치를 시험 위치로 초기화하므로 현재 비행을 유지하려면 해당 초기 조건 호출을 생략하고 현재 위치 주변의 표적을 사용한다. `demo_marker`가 이미 있으면 다른 ID를 사용한다.

```python
from pathlib import Path
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.pause()
sim.set_initial_condition(
    latitude_deg=33.509, longitude_deg=126.49, ellipsoid_height_m=300,
    heading_deg=0, pitch_deg=0, roll_deg=0, speed_mps=19)
sim.create_target(id="demo_marker", kind="marker",
                  latitude_deg=33.510, longitude_deg=126.49, heading_deg=0)
sim.set_gimbal_mode("auto", target_id="demo_marker")
state = sim.set_eoir(enabled=True, mode="ccd", fov_deg=30)
print(state["eoir"], state["targets"])
Path("eoir-ccd.jpg").write_bytes(sim.get_eoir_frame())

sim.set_eoir(enabled=True, mode="ir_white", fov_deg=30)
Path("eoir-ir-white.jpg").write_bytes(sim.get_eoir_frame())
sim.set_gimbal_mode("geo", latitude_deg=33.510, longitude_deg=126.49,
                    ellipsoid_height_m=60)  # 지면 높이를 더하지 않는 지정점
print(sim.get_state()["eoir"])
sim.set_gimbal_mode("protect")  # 보호 자세·영상 OFF
sim.set_gimbal_mode("body_fixed", azimuth_deg=0, elevation_deg=-45)
sim.set_eoir(enabled=True, mode="ccd", fov_deg=30)  # 보호 해제 뒤 명시적으로 켠다.
sim.remove_target("demo_marker")
```

이 예제의 IR 파일은 열상 측정 결과가 아니라 같은 렌더 장면의 밝기 필터다. 카메라 영상은 내장 기체와 STL 표시 모두에 사용할 수 있으며 [외형 파일](aircraft-model.md)과 물리 프로필을 합치지 않는다.

## 범위와 검사 기록

EO/IR 표시 기능은 이번 사용자 요청으로 추가했다. IMU·GNSS 같은 항법 센서 오차 모델과 외부 비행제어컴퓨터 연동은 기존 제외 범위를 유지한다. 물리 열상·영상 탐지·가림 판정·짐벌 구동기 시험의 완료를 이 렌더·지향 기능으로 대신하지 않는다.

2026-09-13 영상 품질 변경 후 Unreal Development 빌드와 실제 EO/IR API **28/28**이 통과했다. [실행 기록](../artifacts/eoir-quality-api-check.json)은 HTTP 요청 78회이며 1280×960 JPEG·1° FOV·기존 지향/필터/헤더·초기화 동작을 확인했다. 메인 overview 전환 전후 센서 하늘 영역의 평균 RGB 차이는 0.33 이하였고, 대기 표시를 유지한 별도 센서 렌더를 확인했다. 같은 자세·고도 600m·FOV 30°에서 [변경 전 640×480](../artifacts/eoir-fixture-before.jpg)·[변경 후 1280×960](../artifacts/eoir-fixture-after.jpg)를 비교하고 [1° 영상](../artifacts/eoir-zoom-after.jpg)도 네이티브 캡처로 확인했다.

[순차 JPEG 요청 5회](../artifacts/eoir-quality-latency.json)의 중앙 응답 시간은 약 **27ms**였다. 이는 해당 장면의 API 응답 측정이며 브라우저 10FPS 실측이나 고정 센서 주기 보장이 아니다. [UI 정적 검사](../artifacts/eoir-ui-static-check.json)는 두 페이지의 JavaScript 구문·요소 연결·FOV 범위·재생/시점 경로를 확인했다. 브라우저 직접 조작은 하지 않았다.

제주 영상 경계 처리 후 50타일을 실제 재읽기하여 지형·비행 오류가 없음을 확인했고, [복원 기록](../artifacts/render-quality-restored.json)의 시작 위치·자세·TAS·입력·바람·표적 2개와 `auto/demo_tank`를 복구한 뒤 FOV 1°로 재생을 재개했다. [사용자 위치의 최종 영상](../artifacts/eoir-user-after.jpg)에서 약 8~9km 거리의 두 표적 형상을 확인했다. 엔진 재시작으로 이전 연출 기록은 초기화되었으며 전체 동역학 스냅샷 복구는 아니다.

```powershell
.\.venv\Scripts\python.exe scripts\check-eoir-api.py --output artifacts\eoir-quality-api-check.json
```

이 검사의 위치·표적 변경과 종료 상태는 아래 기존 EO/IR 하네스 설명과 같다. 다음은 이전 구현 단계의 기록이다.

2026-09-13 짐벌 4모드 추가 후 Unreal Development 빌드와 실제 모드 API **17/17**이 통과했다. [실행 기록](../artifacts/gimbal-modes-api-check.json)은 API 요청 57회·복구 오류 0건이다. 기체 회전 중 body_fixed 각도 유지, auto 표적 이동, 독립 WGS84 기준 geo 지향·높이 유지, 지구 반대편을 포함한 범위·필드 오류 거부, 보호 영상 차단·해제·기존 API 호환과 초기화 보존을 확인했다. Python AST·Web UI JavaScript 구문 검사를 통과했다. [페이지 HTTP 검사](../artifacts/gimbal-modes-ui-check.json)에서 200 응답·네 모드·geo 높이 입력·API 경로를 확인했다. 계산 코어 수학은 바꾸지 않아 기존 CTest 9/9를 반복하지 않았다.

```powershell
.\.venv\Scripts\python.exe scripts\check-gimbal-modes-api.py
```

실행 중인 API·Unreal·대한민국 지형에서 정지한 초기조건과 임시 표적을 사용한다. 끝나면 시작 위치·속도·조종 명령·카메라 설정·짐벌 모드·메인 시점을 복구하고 정지한다. 기존 표적은 보존하며 임시 표적은 제거한다. 초기조건 호출로 내부 동역학·항법이 초기화되므로 전체 실행 상태를 되돌리는 스냅샷 복구는 아니다. 브라우저 직접 조작은 하지 않았다.

아래는 4모드 추가 전 EO/IR 검사 기록이다.

2026-09-13 Unreal Development 빌드와 Release CTest **9/9**, 실제 EO/IR 하네스 **27/27**이 통과했다. 당시 프레임은 640×480, FOV 범위는 5~90°였다. [실행 기록](../artifacts/eoir-api-check.json)에 API 요청 76회를 기록했다. 독립 WGS84/기체축 기준 지향, 0.5초 실제 비행 중 추적, 짐벌 제한·시야각, 표적 생성·이동·삭제·입력 거부와 reset 보존을 확인했다. JPEG 크기·헤더·회색조/반전과 메인 overview 전환에도 EO/IR 하늘이 유지되는 것을 검사했다.

렌더 파일은 최신 품질 검사에서 갱신했다: [CCD](../artifacts/eoir-ccd.jpg), [IR 백상](../artifacts/eoir-ir-white.jpg), [IR 흑상](../artifacts/eoir-ir-black.jpg), [미사일 소품](../artifacts/eoir-missile.jpg). HTML 페이지·메인 링크는 HTTP로, 두 페이지의 JavaScript는 구문 검사로 확인했다. 브라우저 직접 조작과 위 Python 예제의 별도 실행은 하지 않았다.

```powershell
.\.venv\Scripts\python.exe scripts\check-eoir-api.py
```

재실행에는 실행 중인 API·Unreal·기본 대한민국 지형이 필요하다. 기체 위치·카메라를 변경하고 시험 표적을 생성하며, 성공하면 시험 탱크를 CCD로 추적하는 정지·1배속 상태를 남긴다. 기존 등록 표적은 보존한다. 실패 시 생성한 표적을 제거하고 카메라 비활성·지상 주차로 복구한다. 이 기록은 개발 검사이며 실기체 센서 성능의 정식 검증이 아니다.
