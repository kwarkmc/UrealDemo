# 워게임 발사 연출·수동 판정

[EO/IR 페이지](http://127.0.0.1:8765/eoir)의 워게임 패널에서 STL 모델을 선택해 발사 장면을 재생하고 운영자가 명중·빗나감을 판정한다. 외부 Python도 같은 API를 사용한다. 실행 준비는 [API 매뉴얼](api-reference.md)을 따른다.

발사 연출은 시작할 때 저장한 기체 위치와 표적 지점 사이의 **고정 시간 선형 애니메이션**이다. 표적이 이동해도 저장한 끝점은 바뀌지 않는다. `hit`와 `miss`는 운영자가 지정하는 게임 결과다. 실제 유도·탄도·충돌 기반 자동 명중·명중 확률·살상·피해량을 계산하지 않으며, 기체의 질량·조종·비행역학에도 영향을 주지 않는다.

## 사용 순서

1. EO/IR의 지상 표적 패널에서 가상 표적을 만든다. 워게임 패널에서 모델과 등록 표적을 선택한다.
2. **발사 연출**을 누른다. 정지 중이면 연출도 멈춰 있으므로 영상 위의 **비행·연출 재생** 또는 API `step`으로 시간을 진행한다. **일시정지**는 기체와 연출을 함께 멈춘다.
3. 설정한 시간이 지나 `awaiting_result`가 되면 **명중 판정** 또는 **빗나감 판정**을 누른다. 연출 끝점에 도달한 것만으로 명중하지 않는다.
4. 명중 판정은 표적을 빨간색으로 표시하고 판정 횟수를 누적한다. **판정·연출 기록 초기화**로 연출·판정 기록과 표적 표시를 지운다.

기존 지상 표적의 `kind="missile"`도 정적인 비발사 소품이다. 여기서 선택하는 발사 연출 모델은 별도의 `weapons/catalog.json` 목록이다. EO/IR 표적 추적은 카메라 지향이며 이 애니메이션을 유도하지 않는다.

영상은 1280×960 JPEG로 제공하며 원거리의 작은 도형은 **1° FOV**, **원본 크기**, **전체 화면**으로 확인할 수 있다. 확대는 화면 표시이며 연출 위치·시간·게임 판정을 바꾸지 않는다. 상세 조작은 [EO/IR 안내](eoir.md)를 따른다.

도형 자체를 외부에서 보려면 **발사 연출 → 탄체 따라보기 → 비행·연출 재생**을 사용한다. 따라보기는 Unreal 비행 창의 카메라를 바꾸며 EO/IR 지향은 유지한다. 가장 최근의 진행 연출을 선택하고 그 연출이 끝나거나 취소되면 다른 진행 연출, 모두 끝나면 기체 시점으로 전환한다. **기체 시점**으로 직접 복귀할 수도 있다.

## 모델 파일

[weapons/catalog.json](../weapons/catalog.json)이 모델 목록을 정한다. STL은 `weapons/models` 아래에 두고 정지한 상태에서 **모델 목록 다시 읽기**를 누른다. 진행 중인 `animating` 이벤트가 있으면 재읽기를 거부한다. 성공해도 이전 연출 기록은 보존하고, 실패하면 기존 목록을 유지한다.

최상위 필드는 `schema_version: 1`과 `weapons` 배열 두 개다. UTF-8 JSON 크기는 64KiB 이하, 모델은 1~32개다. 각 항목에는 다음 9개 필드를 정확히 지정한다.

| 필드 | 의미·범위 |
| --- | --- |
| `id` | `[A-Za-z0-9_-]` ASCII 1~48자. 목록 안에서 중복 불가 |
| `label` | 화면 표시 이름, 1~80자. 공백만 있는 이름 불가, 양끝 공백은 보존 |
| `mesh` | weapons 기준 상대 `.stl` 경로, 1~256자. 경로 구분자는 `/` 사용 |
| `scale_to_m` | 원본 좌표를 m로 바꾸는 배율, 0.000001~1000 |
| `forward_axis`, `up_axis` | 각각 `x`, `-x`, `y`, `-y`, `z`, `-z`. 양수 별칭 `+x`, `+y`, `+z`도 허용. 서로 다른 축 |
| `origin` | 원본 파일 단위의 기준점 `[x,y,z]` |
| `color` | 균일 RGB `[r,g,b]`, 각 0~1 |
| `animation_duration_s` | 0.2~30초의 연출 시간. 거리나 실제 비행성능에서 계산하지 않음 |

기본 항목은 `training-marker` / `Game projectile`, 연출 시간 2초다. 축·원점·단위 변환은 [기체 STL 안내](aircraft-model.md)와 같으며 표시 형상에만 적용한다. 원점을 뺀 후 단위를 변환하고 기체 로컬 +X 앞·+Y 오른쪽·+Z 위에 맞춘다. 원본 오른쪽은 `cross(up, forward)`다. 변환 좌표는 원점에서 각 축 100m 이내여야 한다.

ASCII/binary STL을 읽고 파일당 32MiB·200,000삼각형, 전체 목록 합계 400,000삼각형까지 허용한다. 절대 경로·상위 경로 이동·링크를 거부한다. 단일 STL에 재질·텍스처·물리 제원·추진·유도 데이터를 넣지 않는다. 공개 상태에는 표시용 메시 경로·삼각형 수와 연출 시간만 제공한다.

## API

아래 경로는 `/api/v1` 기준이다. JSON 필드를 정확히 모두 보낸다. 성공 응답은 전체 비행 상태이며 `wargame`과 `targets`의 판정 필드를 포함한다.

| 메서드·경로 | 요청 | Python |
| --- | --- | --- |
| `POST /wargame/weapons/reload` | `{}` | `reload_wargame_weapons()` |
| `POST /wargame/fire` | `weapon_id`, `target_id` | `play_wargame_shot(weapon_id, target_id)` |
| `POST /wargame/resolve` | `shot_id`, `outcome`: `hit` 또는 `miss` | `resolve_wargame_shot(shot_id, outcome)` |
| `POST /wargame/reset` | `{}` | `reset_wargame()` |

세 ID는 모두 `[A-Za-z0-9_-]` ASCII 1~48자 문자열이다. `weapon_id`는 현재 목록, `target_id`는 현재 등록 표적, `shot_id`는 응답의 연출 ID를 사용한다. Shot ID는 엔진 실행 동안 증가하는 `shot_N`이며 워게임 초기화로 번호를 재사용하지 않는다.

외부 카메라는 기존 `PUT /view {"mode":"projectile"}` 또는 `set_view("projectile")`로 선택한다. `animating` 연출이 없으면 409다. 복귀는 `set_view("aircraft")`이며 메인 페이지의 **탄체 따라보기**도 같은 API를 사용한다.

형식·타입·누락/추가 필드·ID 문법·결과 열거값 오류는 400이다. 없는 모델/표적/연출, 현재 단계에서 불가능한 판정, 재생 중 또는 연출 중 목록 재읽기는 409이며 `wargame.error`에 설명을 남긴다. Unreal 미연결은 503이다. 오류 응답은 공통 `{"error":{"message":"..."}}` 형식을 따른다.

## 단계·시간·초기화

| `phase` | 의미·전환 |
| --- | --- |
| `animating` | 설정한 시간 동안 저장된 두 점 사이를 표시. 아직 판정할 수 없음 |
| `awaiting_result` | 애니메이션 모델을 제거하고 운영자의 hit/miss 판정을 기다림 |
| `resolved` | 명시적 판정 완료. 같은 연출을 다시 판정할 수 없음 |
| `canceled` | 진행·판정 대기 중 참조 표적이 삭제되어 취소됨 |

동시 `animating`은 최대 16개, 전체 기록은 128개다. 기록이 가득 차면 새 연출을 거부하므로 필요한 기록을 읽은 뒤 명시적으로 초기화한다. `hit`는 표적의 `hit=true`, `hit_count+1`, `last_hit_shot_id`를 적용한다. `miss`는 이전 명중 표시를 지우지 않는다. 표적 파괴·체력·탄약을 모델링하지 않는다.

`wargame.time_s`와 연출 `elapsed_s`는 실제 수락된 비행 시간만큼 진행한다. 정지 시 멈추고 벽시계 재생 배속을 따르며, 명시적 `step(dt_s)`에는 배속을 다시 곱하지 않는다. 비행 초기화·pose 설정은 연출·기록·워게임 시간을 유지하므로 `aircraft.simulation_time_s`와 별도 시계다.

`reset_wargame()`은 연출 Actor·기록·표적 명중 표시와 워게임 시간을 지운다. 모델 목록·기체·등록 표적·카메라는 보존한다. 성공한 지형 재읽기는 워게임과 등록 표적을 비우며 Unreal 재시작도 실행 기록을 초기화한다.

## 상태 필드

| 위치 | 필드·의미 |
| --- | --- |
| `wargame` | `time_s`: 누적 워게임 시간(s), `error`: 연출·목록·판정 오류 |
| `wargame.weapons[]` | `id`, `label`, `mesh`, `animation_duration_s`, `triangle_count`: 현재 표시 모델 목록 |
| `wargame.shots[]` | `id`, `weapon_id`, `target_id`: 연출·모델·표적 ID |
| `wargame.shots[]` | `phase`, `outcome`: 진행 단계와 `pending`/`hit`/`miss`/`canceled` 결과 |
| `wargame.shots[]` | `elapsed_s`, `duration_s`: 진행 시간·해당 연출에 저장된 총 시간(s) |
| `wargame.shots[]` | `resolved_time_s`: 판정·취소 시각, 워게임 시간 기준(s). 그전에는 0 |
| `targets[]` | `hit`, `hit_count`, `last_hit_shot_id`: 명시적 명중 표시·누적 횟수·마지막 명중 연출 ID |

## 외부 Python 예제

실행 중인 기본 대한민국 지도와 기본 모델을 사용하는 예제다. 지상 초기화와 명시적 step은 현재 비행 위치·시간을 바꾸므로 새 시험에서 실행한다. 여기서 선택하는 `hit`는 예제 운영자의 판정이며 계산된 명중 결과가 아니다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.pause()
sim.reset_on_ground()
sim.create_target(id="game_marker", kind="marker",
                  latitude_deg=33.5092, longitude_deg=126.49)
before = {item["id"] for item in sim.get_state()["wargame"]["shots"]}
state = sim.play_wargame_shot("training-marker", "game_marker")
shot_id = next(item["id"] for item in state["wargame"]["shots"]
               if item["id"] not in before)

for _ in range(31):
    shot = next(item for item in state["wargame"]["shots"] if item["id"] == shot_id)
    if shot["phase"] != "animating":
        break
    state = sim.step(min(1, shot["duration_s"] - shot["elapsed_s"]))
    if state["fault"]:
        raise RuntimeError(state["fault"])

shot = next(item for item in state["wargame"]["shots"] if item["id"] == shot_id)
if shot["phase"] != "awaiting_result":
    raise RuntimeError("연출의 현재 단계에서 판정할 수 없습니다.")
state = sim.resolve_wargame_shot(shot_id, "hit")  # 운영자의 명시적 게임 판정
print(state["wargame"]["shots"], state["targets"])
sim.reset_wargame()
sim.remove_target("game_marker")
```

이미 `game_marker`가 등록되어 있으면 다른 ID를 사용한다. 워게임 초기화는 기존 연출 기록과 모든 표적의 명중 표시도 지운다.

## 검사 기록

2026-09-13 외부 따라보기 추가 후 Unreal Development 빌드와 실제 API **20/20**이 통과했다. [실행 기록](../artifacts/wargame-render-api-check.json)은 HTTP 요청 104회·복구 오류 0건이며 기체·EO/IR 설정을 유지하는 외부 시점, 완료 뒤 기체 시점 복귀, 진행 연출이 없을 때 409 거부와 기존 판정 동작을 확인했다. 5초 시험 연출의 [0.5초](../artifacts/projectile-flight-1.png)·[1초](../artifacts/projectile-flight-2.png)·[1.5초](../artifacts/projectile-flight-3.png) 네이티브 캡처에서 도형의 이동과 표시용 궤적을 확인했다. [캡처 상태 기록](../artifacts/projectile-render-check.json)에는 초기화 후 `aircraft` 복귀도 남겼다. 이 도형·궤적은 고정 시간 애니메이션이며 탄도·유도 모델이 아니다.

```powershell
.\.venv\Scripts\python.exe scripts\check-wargame-api.py --capture --output artifacts\wargame-render-api-check.json
```

다음은 외부 따라보기 추가 전 구현 기록이며 하네스의 실행·복구 범위는 아래와 같다.

2026-09-13 Unreal Development 빌드와 실제 워게임 하네스 **18/18**이 통과했다. [실행 기록](../artifacts/wargame-api-check.json)은 HTTP 요청 84회·복구 오류 0건이다. 모델 목록 재읽기·잘못된 파일 거부, 정지/명시적 시간 진행, 자동 명중 없는 판정 대기, hit/miss 표시·중복 판정 거부·표적 삭제 취소·초기화·ID 재사용 방지를 확인했다. 카탈로그 원본 바이트를 복원했다. Python AST·Web UI JavaScript 구문도 통과했으며 변경 없는 계산 코어 검사는 반복하지 않았다.

Native 캡처에서 [정상 표적](../artifacts/wargame-normal.jpg)의 녹색 탱크가 명시적 hit 뒤 [빨간 판정 표시](../artifacts/wargame-hit.jpg)로 바뀌는 것을 직접 확인했다. [STL 연출 화면](../artifacts/wargame-animation.png)에서는 주황색 예제 도형이 기체와 별도로 표시된다. 컴퓨터/UI 자동화와 위 Python 예제의 별도 실행은 하지 않았다. 이 기록은 게임 연출·명시적 판정 기능의 개발 검사이며 무기 성능 검증이 아니다.

```powershell
.\.venv\Scripts\python.exe scripts\check-wargame-api.py --capture
```

재실행에는 실행 중인 API·Unreal·기본 대한민국 지도·예제 카탈로그가 필요하다. 기존 연출 기록이나 명중 표시가 있으면 지우지 않고 실행을 거부한다. 자체 시험 표적을 만들고 마지막에 제거하며, 카탈로그 파일·시작 위치/속도·조종 명령·카메라 설정을 복구하고 정지한다. 내부 동역학·항법·시계까지 되돌리는 전체 스냅샷 복구는 아니다.
