# 저장소 시작·인수인계 안내

2026-09-17에 지금까지의 작업 폴더를 처음 Git으로 정리했다. 저장소 이름은 `UrealDemo`, Unreal 프로젝트와 제품 이름은 `UAVSim`이다. 이번 커밋들은 현재 파일을 기능별로 나눈 것이며 과거 작업 시점의 개별 버전을 복원한 이력은 아니다.

## 새 PC에서 실행

Windows, Windows PowerShell5.1 또는 PowerShell7, Git과 Git LFS, Python 3.12, 호환되는 Unreal Engine 5.8, Visual Studio C++ 도구가 필요하다. 최신 연구 기능의 빌드 기록은 UE5.8.4이며 초기 기록의5.8.2와 구분한다. VS 구성 요소는 [`.vsconfig`](../.vsconfig)를 따르며 현재 Windows 계산 검사는 VS2022에서 통과했다. 기본 Unreal 설치 경로는 `C:\Program Files\Epic Games\UE_5.8`이다.

[클라우드 개발·인계 안내](cloud-development.md)에 클라우드 검사와 Windows 실행의 구분, 현재 엔진 연결 정보와 게시 절차를 기록했다.

```powershell
git lfs install
git clone https://github.com/kwarkmc/UrealDemo.git
cd UrealDemo
git lfs pull
py -3.12 -m venv .venv
.\scripts\dev.ps1 build
.\scripts\dev.ps1 api
```

[컨트롤 데스크](http://127.0.0.1:8765/)에서 **Unreal 실행**을 누른다. [EO/IR 화면](http://127.0.0.1:8765/eoir)은 별도 페이지다. API 서버와 Python 클라이언트는 표준 라이브러리만 사용한다. 빌드·엔진 실행 실패 시 먼저 Unreal 경로와 설치 구성 요소를 확인한다. 다른 경로는 `dev.ps1`의 `-UnrealRoot` 또는 `UAVSIM_UNREAL_ROOT`로 지정할 수 있다.

`Content`의 `.umap`, `.uasset`은 Git LFS로 관리한다. GitHub의 소스 ZIP 대신 위 clone 절차를 사용한다. 파일이 작은 LFS 포인터 텍스트로 남아 있다면 `git lfs pull`을 실행한다. 엔진·컴파일러·Python 환경·컴파일된 실행 파일은 저장소에 포함하지 않는다.

현재 맵과 위성 재질은 포함되어 있어 첫 실행에 `setup-scene`을 다시 수행할 필요가 없다. 이 자산을 재생성할 때는 빌드 후 `scripts/dev.ps1 setup-scene`을 실행한다. 실행 중인 엔진은 먼저 제품 API 또는 UI로 정상 종료한다.

## 코드와 기능 위치

| 영역 | 주요 파일 | 구현 내용 |
| --- | --- | --- |
| 계산 코어 | `Source/FlightCore`, `tests`, `CMakeLists.txt` | WGS84/ENU, 자체 6DOF·공력·실속, 작동기, 지면 접촉, 항법·자동 이착륙, STL·짐벌 계산 |
| 기체 데이터 | `aircraft` | Thor 개발 프로필, 물리 매개변수와 표시 외형 분리 |
| Unreal | `Source/UAVSim`, `Config`, `Content`, `UAVSim.uproject` | 장면·조종면·기어, 지형/영상, 기체 외형, EO/IR와 가상 표적, 게임 연출 |
| 지도 | `maps/active.json`, `maps/korea`, `maps/example` | 실행용 전국 50타일, 제주 상세 영역, 지도 교체 예제 |
| 표시용 탄체 | `weapons/catalog.json`, `weapons/models` | STL 외형, 고정 시간 이동·궤적, 수동 명중/빗나감 판정 |
| 외부 제어 | `scripts/serve-api.py`, `uavsim_client.py`, `web` | HTTP API, 설치 가능한 Python 클라이언트, 비행/EOIR Web UI |
| 개발 하네스 | `scripts/dev.ps1`, `scripts/check-*.py` | 빌드·실행, 실제 엔진 API 검사와 캡처 |
| 문서와 근거 | `docs`, 선택한 `artifacts` | API 사용법, 가정·제한, 개발 검사 결과와 실제 화면 |

첫 공개 이력은 계산 코어 → Unreal·지도·렌더링 → API·UI·하네스 → 문서·검사 기록 순서로 나누었다. 개별 커밋 제목과 본문에 해당 범위를 기록했다. 완전한 실행 기준은 마지막 커밋이다.

## 지도·검사 자료 관리

실행용 `maps` 파일은 포함한다. 지도 다운로드 캐시와 중복된 옛 `data` 타일은 제외하고, `data/jeju/metadata.json`, `data/korea/metadata.json`의 출처 기록은 보존한다. 데이터 이용 조건과 수정·출처 표시는 [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md)에 있다.

지도 재생성이나 Pillow 기반 영상 검사가 필요하면 다음 의존성을 설치한다. 자세한 입력 형식과 재생성 순서는 [지도 안내](maps.md)를 따른다.

```powershell
.\.venv\Scripts\python.exe -m pip install -r requirements-terrain.txt
# 계산 코어만 검사: Unreal 프로세스를 실행하지 않음
.\scripts\dev.ps1 check
```

`prepare-jeju-detail.py`는 기존 제주 원본 캐시를 사용하는 오프라인 도구다. 새 clone에서 다시 생성하려면 `prepare-terrain.py`로 원본 캐시를 준비한다. `prepare-korea.py`로 전국 기본 자료를 다시 생성한 뒤 제주 상세를 추가할 때도 이 순서를 따른다. 포함된 실행용 타일을 그대로 사용할 때는 다운로드가 필요 없다.

`scripts/check-*-api.py`는 실행 중인 시뮬레이터의 위치·입력·표적·실행 상태를 변경할 수 있다. 각 스크립트의 설명과 해당 기능 문서의 복구 범위를 읽고 별도 시험 세션에서 실행한다. 전체 동역학 상태를 되돌리는 스냅샷 기능은 아니다.

`artifacts`는 기본적으로 무시하며, 문서에서 직접 참조하는 결과·캡처만 선택하여 추적한다. 새 검사 근거를 공개할 때는 내용을 확인하고 `git add -f artifacts/<파일>`로 추가한다. PID·실행 경로·초기 상태가 들어 있는 과거 기록은 당시 개발 환경의 결과이며 현재 실행 상태가 아니다. 빌드 로그·임시 파일·원본 백업은 포함하지 않는다.

## 검증 범위와 남은 제한

[README](../README.md), [개발 계획](development-plan.md), [API 매뉴얼](api-reference.md)에 기능별 개발 검사 기록을 남겼다. 이번 저장소 정리 작업에서는 파일·문서 링크·Git LFS·커밋 구성을 확인하며, 변경하지 않은 계산 코어·Unreal 빌드를 반복하지 않는다.

현재 공력·추진·지면 접촉 등은 문서화된 개발 모델이다. 실제 기체 성능 일치나 정식 신뢰성·정적·동적 시험 완료를 주장하지 않는다. EO/IR의 IR은 영상 필터이고, 워게임은 시각 연출과 운영자 판정이며 실제 유도·탄도·살상 성능 계산을 포함하지 않는다. 제주 상세 영상의 원본은 10m이고 다른 전국 기본 영역은 더 낮은 해상도를 사용한다.

앞으로 API를 변경하면 Python 클라이언트·해당 UI·API 매뉴얼을 같은 작업에서 갱신한다. 개발 작업 원칙은 [AGENTS.md](../AGENTS.md)를 따른다.
