# 클라우드 개발·새 PC 인계

2026-11-19 기준 저장소는 `https://github.com/kwarkmc/UrealDemo.git`다. 계산 코어, 기체 프로필, Unreal 코드와 자산, 지도, API/Python 클라이언트/Web UI, 검사 도구, 문서 및 선택한 과거 개발 검사 근거를 함께 관리한다. 최신 선택은 가상 수납식 연구 구성 `aircraft/c208b-retractable-research.json`이며 실제 C208B 사양이나 실기체 성능 검증 모델이 아니다. 다음 구현 후보는 [물리 확장 계획](physics-expansion-plan.md)을 따르며 이번 정리는 새 물리 기능 구현이 아니다.

## Codex 클라우드 환경

Codex 설정의 클라우드 환경에서 `UrealDemo` 환경을 만들고 `kwarkmc/UrealDemo`의 최신 `main`을 연결한다. 이 채팅에서 제공된 도구는 게시 환경 조회만 지원하여 환경 생성·게시를 실행하지 못했다. 사용자가 환경 설정에서 준비·게시한 뒤 그 환경의 새 채팅에서 작업을 이어간다. 기존 `TrashCan` 환경은 변경하지 않았다.

Linux 환경에 C++17 컴파일러, CMake3.16 이상, Python3.11 이상, Git과 Git LFS를 준비한다. 예를 들어 Debian/Ubuntu 계열의 준비 단계는 다음과 같다. 패키지 설치·자산 다운로드에 필요한 네트워크 권한은 환경 설정에서 부여한다.

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake python3 python3-venv git-lfs
```

연결한 저장소 루트에서 실행한다. 엔진 없이 계산 검사·기록 도구 검사를 진행하는 최소 명령이며 `scripts/dev.ps1`은 Windows용이다.

```bash
git lfs install
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 tests/flight_test_tools_checks.py
```

그래프 생성이 필요한 경우에만 별도 Python 환경과 의존성을 준비한다.

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-flight-test.txt
```

`AGENTS.md`와 README를 읽고 작업을 시작한다. API 변경은 endpoint·Python 클라이언트·Web UI·API 매뉴얼을 함께 갱신한다. 클라우드에서 개발한 변경은 커밋·push로 보존하고 Windows PC에서 pull한 뒤 엔진 경계의 변경을 검증한다. 계산 검사 통과만으로 Unreal 빌드·실제 API·렌더링·실기체 성능 검증을 대신하지 않는다. 현재 HTTP API는 같은 PC의 Unreal을 제어하는 구조이므로 클라우드 API가 이 PC의 localhost 엔진에 자동 연결되지 않는다.

## Windows PC에서 Unreal 실행

최초 clone·Python 환경·빌드는 [저장소 시작 안내](repository-guide.md)를 따른다. 이미 clone한 PC는 로컬 변경을 먼저 보존하고 `git pull --ff-only`, `git lfs pull`로 갱신한다. Unreal 자산은 LFS 실파일이어야 하며 소스 ZIP만 받아 실행하지 않는다. 지도 원본 캐시, 엔진, 컴파일러, 가상환경과 빌드 결과는 저장소에 포함하지 않는다. 포함된 실행용 지도를 그대로 쓰면 원본 재다운로드는 필요 없다.

현재 `UAVSim.uproject`의 EngineAssociation은 이 PC의 소스 엔진 등록 GUID다. 새 PC에서는 설치한 호환 엔진으로 프로젝트 연결을 지정하거나 아래처럼 실제 엔진 경로를 명시한다. 로컬 GUID 자체는 다른 PC의 엔진 설치를 뜻하지 않는다. VS 구성 요소 ID도 해당 PC의 Visual Studio 버전에서 설치 가능 여부를 확인한다. 현재 PC의 실제 엔진 경로는 `C:\UnrealEngine-release`이며 기본 설치 경로와 다르다.

```powershell
.\scripts\dev.ps1 build -UnrealRoot 'C:\UnrealEngine-release'
.\scripts\dev.ps1 api -UnrealRoot 'C:\UnrealEngine-release'
```

위 경로는 각 PC의 설치 위치로 바꾼다. 이후 Web UI에서 Unreal 실행을 요청한다. EO/IR·지형 렌더·실제 API 검사는 Unreal 실행이 가능한 Windows 환경에서 수행한다. Unreal MCP는 개발 도구이며 외부 제품 API와 분리한다. 사용자 지시에 따라 UI 자동화는 하지 않는다.

## 이번 정리의 확인 범위

- Windows Release 계산 코어 CTest15/15, Python 기록 도구8검사가 종료 코드0으로 통과했다. 도구 검사의 의도한 통신 실패 사례가 로그에 출력되지만 전체 unittest 결과는 OK다.
- 문서가 참조하는 과거 보고서·그림과 관련 CSV/프로필을 함께 보존한다. 과거 실행의 PID·절대경로·종료 상태는 당시 기록이며 현재 실행 상태가 아니다. 다운로드 캐시·임시 백업·빌드 로그는 무조건 추가하지 않는다.
- 검사 근거의 CSV·보고서는 `.gitattributes`에서 줄바꿈 변환을 끄고 원본 바이트를 보존하여 다른 OS에서도 기록된 파일 해시를 유지한다. [이번 저장소 점검 기록](../artifacts/repository-sync-check.json)을 함께 보존한다.
- 자동 생성 `.slnx`는 `.sln`과 같이 제외하고 기존 파일은 로컬에 보존한다. 비활성 AndroidFileServer의 자동 생성 보안 토큰은 공유 설정에서 제외하고, 누락된 CMake VS 구성 요소는 복구했다.
- 클라우드 Linux 검사, 새 PC 실행, Unreal 재빌드·실제 엔진 API 검사는 이번 저장소 정리에서 수행하지 않았다. 실기체 성능·정식 시험 완료를 주장하지 않는다.
