# STL 기체 외형 사용

로컬 STL 파일로 Unreal에 표시하는 기체 외형을 교체한다. 비행역학의 질량·공력·관성은 [기체 프로필](aircraft-profile.md)이 담당하고, 이 기능은 표시 메시만 읽는다. STL에서 공력·무게중심·착륙장치를 자동 추정하지 않는다.

## 파일 배치와 적용

1. STL을 `C:\src\Projects\UAVSim\aircraft\models` 아래에 놓는다. ASCII와 binary STL을 지원한다.
2. `aircraft/visual.json`에서 파일 경로·단위·기수 방향·위쪽 방향·CG 위치를 지정한다.
3. Unreal이 준비되면 비행을 일시정지한 뒤 Web UI의 **기체 모델 다시 읽기**를 사용하거나 `sim.reload_aircraft_visual()`을 호출한다.
4. `state["visual"]`에서 적용한 파일·삼각형 수·크기와 오류를 확인한다.

STL 파일 하나는 최대 **32MiB·삼각형 200000개**다. 기본 제공 파일은 [example.stl](../aircraft/models/example.stl)이며 사용자 파일을 대신하는 형상 예제다. 실제 Thor의 CAD 자료가 아니다. ASCII는 소문자 키워드와 하나의 `solid` 블록을 받으며, 끝의 이름이 있으면 시작 이름과 같아야 한다. binary 파일은 선언한 삼각형 수와 실제 길이가 일치해야 한다. 빈 파일·비유한 숫자·퇴화하거나 변환 후 지나치게 작은 삼각형은 거부한다. 파일 법선 대신 정점 순서에서 표시 법선을 다시 계산한다.

```json
{
  "schema_version": 1,
  "mesh": "models/example.stl",
  "scale_to_m": 1,
  "forward_axis": "x",
  "up_axis": "z",
  "origin": [0, 0, 0],
  "color": [0.7, 0.8, 0.9],
  "show_builtin_gear": true
}
```

위 JSON은 원본 단위가 m이고, +X가 기수·+Z가 위쪽이며 CG가 원점인 파일의 예다. 다른 STL에는 그 파일의 좌표·단위를 지정한다. 기본 외형으로 돌아가려면 `mesh`를 빈 문자열 `""`로 바꾸고 다시 읽는다. 나머지 필드도 모두 유지한다.

설정 파일이 없는 기존 프로젝트는 내장 외형·기어 표시로 시작한다. 내장 외형에서도 `show_builtin_gear=false`를 적용하면 기어 표시를 숨긴다. 내장 외형에는 STL용 축·단위·CG·색 변환을 적용하지 않는다.

## visual.json의 8개 필드

설정은 최대 8KiB이며 누락·추가 필드 없이 다음 8개를 지정한다. 숫자는 유한값이어야 하며 bool은 숫자 대신 `true`/`false`를 쓴다.

| 필드 | 값·의미 |
| --- | --- |
| `schema_version` | 숫자 `1` |
| `mesh` | `aircraft` 기준 상대 `.stl` 경로. 예: `models/my-uav.stl`. 빈 문자열이면 내장 외형 |
| `scale_to_m` | 원본 한 단위를 m로 바꾸는 배율, `0.000001`~`1000` |
| `forward_axis` | 기수 방향: `x`, `-x`, `y`, `-y`, `z`, `-z` 중 하나 |
| `up_axis` | 위쪽 방향: 같은 6개 값 중 하나. forward와 부호를 제외한 축이 달라야 함 |
| `origin` | 원본 파일 단위의 CG 좌표 `[x,y,z]`. 각 성분 −1000000000~1000000000 |
| `color` | STL 표시 단색 RGB `[r,g,b]`, 각 성분 `0`~`1` |
| `show_builtin_gear` | 내장 기어 메시를 함께 표시할지 정하는 bool. 접촉 물리와 기어 API는 유지 |

`mesh`는 `aircraft` 안의 일반 파일이며 경로는 최대 256자다. 하위 경로 구분자는 `/`를 사용한다. 절대 경로·역슬래시·콜론·`.`/`..` 경로 항목·링크/정션은 거부한다. 파일은 `aircraft/models` 아래에 배치하는 방식을 사용한다. 이 API는 파일을 업로드하거나 외부 URL에서 내려받지 않는다.

STL의 수치에 단위를 임의로 추정하지 않는다. 예를 들어 mm로 내보냈다면 `scale_to_m=0.001`, cm라면 `0.01`, m라면 `1`이다. 변환된 좌표는 CG를 기준으로 각 축 −100~100m 안이어야 한다. 이 제한은 표시 데이터 범위이며 해당 크기의 기체 물리가 지원된다는 뜻은 아니다.

## 방향과 CG 맞추기

원본 정점에서 `origin`을 빼고 `scale_to_m`를 곱한 뒤 기수·오른쪽·위쪽 축으로 투영한다. 원본의 오른쪽 축은 `cross(up, forward)`로 정한다. 기본 `forward_axis="x"`, `up_axis="z"`에서는 원본 +Y가 오른쪽이다.

| 원본 모델 | 설정 예 |
| --- | --- |
| +X가 기수, +Z가 위 | `forward_axis: "x"`, `up_axis: "z"` |
| +Y가 기수, +Z가 위 | `forward_axis: "y"`, `up_axis: "z"`; 원본 −X가 오른쪽 |
| −X가 기수, +Z가 위 | `forward_axis: "-x"`, `up_axis: "z"` |
| CG가 원본 mm 좌표 `(120, 0, 35)` | `origin: [120, 0, 35]`, `scale_to_m: 0.001` |

방향이 뒤집혀 있으면 원본 좌표에서 실제 기수·위쪽을 가리키는 축을 다시 지정한다. 위치가 어긋나면 `origin`이 형상 중심이 아니라 사용하려는 CG 위치인지 확인한다. `origin`은 **스케일 적용 전 단위**다. STL과 프로필의 실제 CG 일치 여부를 자동 검증하는 기능은 없다.

## Python·HTTP API

외부 Python 프로젝트에 클라이언트가 설치되어 있고 API·Unreal이 준비된 상태에서 호출한다. 전체 설치·오류 처리 규칙은 [API 매뉴얼](api-reference.md)을 따른다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.pause()
state = sim.reload_aircraft_visual(timeout=30)
print(state["visual"])
```

HTTP는 `POST /api/v1/aircraft/visual/reload`와 `{}`다. 파일 선택은 `visual.json`에서 하므로 요청에 파일명이나 설정을 추가하지 않는다. 재생 중에는 HTTP 409로 거부하며 API가 자동으로 정지시키지 않는다.

성공은 외형만 교체한다. 위치·자세·속도·시뮬레이션 시간·기어·조종 입력·공력 매개변수를 초기화하지 않는다. 실패하면 이전 메시를 유지하고 `visual.error`에 이유를 남긴다. timeout 뒤에는 이미 적용됐을 수 있으므로 자동 재시도하지 않고 `get_state()`로 확인한다. 기체 물리 프로필을 재읽는 `reload_aircraft_profile()`은 공중 트림을 초기화하는 별도 명령이다.

| `visual` 상태 | 의미 |
| --- | --- |
| `source` | `builtin` 또는 `stl` |
| `config` | `aircraft/visual.json` |
| `mesh` | aircraft 기준 상대 STL 경로 또는 내장 외형의 빈 문자열 |
| `triangle_count` | 읽은 STL의 삼각형 수. 내장 외형은 0 |
| `span_m` | 좌표·단위 변환 후 표시 외형의 로컬 Y축 전체 폭(m). 내장 외형은 1.27, 프로필의 공력 날개폭과 별도 |
| `fingerprint` | 설정 UTF-8 텍스트와 STL 원본 바이트의 SHA1 지문. 내장 외형은 설정만 사용. 보안 서명·성능 검증 표시가 아님 |
| `error` | 외형 로드 오류. 실패한 재읽기 후에도 이전 메시 정보 유지 |
| `show_builtin_gear` | 내장 기어 표시 설정 |

## 조종면·기어·접촉의 범위

현재 STL은 **하나의 고정 형상**으로 표시한다. 파일에 날개·조종면·기어가 함께 들어 있어도 부품을 자동 분리하지 않는다. STL 내부의 조종면·기어는 조종 입력이나 기어 API를 따라 각각 움직이지 않는다. STL 자체의 색·텍스처도 읽지 않고 설정한 단색을 사용한다.

`show_builtin_gear=true`이면 기존 위치의 내장 기어를 함께 표시하고 기존 3초 전개·수납 애니메이션을 사용한다. 모델 크기·CG와 맞지 않거나 STL에 기어가 포함되어 있다면 false로 내장 기어 표시를 숨길 수 있다. false여도 실제 기어 상태·항력·바퀴 접촉 계산은 그대로다. STL 안에 그려진 고정 기어는 수납 명령으로 사라지지 않는다.

접촉 물리는 기본 1.27m 개발 기체의 바퀴·동체 대표 점을 계속 사용한다. 큰 STL이나 다른 형상을 읽어도 질량·관성·공력·충돌 형상을 자동 재구성하지 않는다. 표시 외형과 접촉/비행 모델의 일치를 확인하려면 해당 기체의 근거 자료와 별도 모델 작업이 필요하다. 부품별 조종면·기어 애니메이션에는 분리 메시와 힌지 정의가 필요하며 현재 단일 STL 경로에는 포함되지 않는다.

## 검사 기록

2026-09-13 Unreal Development 빌드와 Release CTest **8/8**, 실제 외형 API 하네스 **28/28**이 통과했다. ASCII 예제의 108개 삼각형·폭 1.8m, `solid`로 시작하는 binary STL, mm 단위·축·CG 변환, 잘못된 파일/경로/타입·재생 중 요청 거부와 이전 메시·물리 상태 보존을 확인했다. 내장 기어 표시 전환도 물리 기어 상태를 바꾸지 않았다. [검사 기록](../artifacts/visual-api-check.json).

실제 Unreal 렌더에서 기존 내장 외형을 대체한 파란색 예제 STL을 확인했다. [STL 표시 화면](../artifacts/stl-aircraft-view.png). 시험 후 원래 `visual.json` 바이트를 복원하고 내장 외형·지상 주차·정지·1배속으로 되돌렸다.

```powershell
# API·Unreal이 준비된 상태. 표시 설정과 시험 위치를 변경하고 복원한다.
.\.venv\Scripts\python.exe scripts\check-visual-api.py --capture
```

이 결과는 파일 처리·표시·상태 보존의 개발 검사이며 STL 형상과 실기체 공력/접촉 모델의 일치를 검증한 것은 아니다.
