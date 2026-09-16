# 지도 파일과 좌표를 바꾸는 방법

지도는 `maps/` 안의 폴더로 관리한다. 엔진이 읽는 파일은 `maps/active.json`과 해당 폴더의 `terrain.json`, 메시·영상 파일이다. 다른 프로그램이나 온라인 지도 계정은 필요하지 않다.

```text
maps/
  active.json
  korea/
    terrain.json
    tiles/N33E126/vertices.raw
    tiles/N33E126/satellite.png
    ...
  example/
    map.json
    source/elevation.tif
    source/satellite.png
    source/geoid-egm2008.tif
    terrain.json
    tiles/jeju/vertices.raw
    tiles/jeju/satellite.png
```

현재 선택은 대한민국이다. 기본 48개·독도 상세 1개·제주 상세 1개로 총 50개 타일을 준비한다.

```json
{"manifest": "korea/terrain.json"}
```

`manifest`는 `maps/` 기준 상대 경로다. 폴더 이름에는 영문·숫자·`_`·`-`를 사용한다. 타일 파일 경로도 해당 지도 폴더 안에 둔다.

## 영상 파일만 교체

기존 지역의 영상만 바꾸려면 해당 타일의 `satellite.png`를 교체하고 Web UI의 지도 다시 읽기를 실행한다. Python에서는 다음과 같다.

```python
from uavsim_client import UAVSimClient

sim = UAVSimClient()
sim.reload_terrain()
```

HTTP는 `POST /api/v1/terrain/reload`, 본문은 `{}`다. Unreal이 실행 중이어야 한다.

교체 영상은 기존 타일과 **같은 지리 범위**여야 한다. 위쪽은 북쪽, 왼쪽은 서쪽이며 PNG/JPEG의 전체 사각형이 타일의 `[서쪽 경도, 남쪽 위도, 동쪽 경도, 북쪽 위도]`에 대응한다. 최대 크기는 2048×2048이다. JPEG를 쓰려면 `terrain.json`에서 해당 타일의 `image_file`도 실제 파일명으로 바꾼다. 높이·좌표가 바뀌면 아래 가져오기를 다시 실행해야 한다. `terrain.json`의 좌표 숫자만 바꾸어도 메시가 재생성되는 것은 아니다.

`files` 또는 `sources`에 기록된 SHA-256은 생성 당시 출처 기록이다. 실행 중 영상 교체를 막는 잠금으로 사용하지 않는다. 재현 가능한 납품 자료를 만들 때는 원본을 보관하고 다시 생성하여 출처 기록도 갱신한다.

## 새 지도 또는 새 고도 자료 가져오기

`maps/example`은 실제 제주 자료로 실행해 둔 작은 예제다. 이 폴더를 `maps/my_map`처럼 복사한 뒤 `map.json`의 이름·좌표·파일명을 수정한다. 예제 형식은 다음과 같다.

```json
{
  "id": "example",
  "name": "제주 사용자 지도 예제",
  "attribution": "Contains modified Copernicus Sentinel data 2025-04-06; Copernicus DEM GLO-30; NGA EGM2008 via PROJ.",
  "origin": {
    "latitude_deg": 33.43,
    "longitude_deg": 126.52,
    "ellipsoid_height_m": 0
  },
  "tiles": [
    {
      "id": "jeju",
      "bounds_deg": [126.48, 33.39, 126.56, 33.47],
      "image": "source/satellite.png",
      "dem": "source/elevation.tif",
      "vertical_datum": "EGM2008",
      "geoid_file": "source/geoid-egm2008.tif",
      "grid_size": 129
    }
  ]
}
```

프로젝트 루트 PowerShell에서 실행한다.

```powershell
# 변환·검사만 실행하며 현재 선택 지도는 유지
.\.venv\Scripts\python.exe scripts\prepare-map.py maps\example\map.json

# 변환 성공 후 선택 지도까지 변경
.\.venv\Scripts\python.exe scripts\prepare-map.py maps\my_map\map.json --activate
```

그다음 `reload_terrain()`을 호출하면 선택한 지도를 불러온다. `--activate` 자체는 Unreal을 시작하거나 현재 장면을 즉시 변경하지 않는다. 변환 실패 시 종료 코드는 1이며 선택 파일을 바꾸지 않는다. 예제는 변환과 실제 PNG/JPEG 렌더 검사를 마쳤고, 검사 뒤 현재 선택을 대한민국으로 복구했다.

새 타일은 `tiles` 배열에 같은 형식으로 추가한다. 각 `id`는 영문·숫자·`_`·`-` 1~64자이며 Windows에서 대소문자만 다른 중복 이름은 허용하지 않는다. `name`은 200자, `attribution`은 4096자까지다. 전체 범위는 타일들의 사각형 합집합을 둘러싸는 범위로 계산하며 원점은 그 안에 있어야 한다.

입력 규칙:

- `bounds_deg`: WGS84 경위도 `[west, south, east, north]`. 날짜 변경선을 가로지르는 영역은 지원하지 않는다.
- `image`: 북쪽이 위인 PNG/JPEG 또는 좌표가 들어 있는 8-bit RGB GeoTIFF. GeoTIFF는 타일 좌표로 재투영한다. 일반 영상에는 좌표를 추측해서 붙이지 않고 입력한 사각형을 그대로 적용한다.
- `dem`: 좌표가 있는 GeoTIFF 또는 GDAL이 읽을 수 있는 DTED 등 고도 래스터. 타일 전체를 포함해야 한다. NoData나 누락을 바다로 대신 채우지 않는다.
- `vertical_datum`: 반드시 `WGS84`, `EGM2008`, `EGM96` 중 지정한다. `WGS84`는 타원체고(m), 나머지는 해당 지오이드 기준 정표고(m)다. 변환은 `타원체고 = 정표고 + 지오이드고`다.
- `geoid_file`: EGM2008/EGM96에 대응하는 로컬 지오이드 GeoTIFF. 생략하면 `data/korea/source`에 이미 캐시된 **같은 기준**의 자료만 찾아 사용한다. 기준이 다르거나 영역을 포함하지 않으면 실패한다. 가져오기는 인터넷에서 지오이드를 자동 다운로드하지 않는다.
- `grid_size`: 각 변의 정점 수, 2~513, 기본 129. 최대 64타일이며 타일 중심 기준 각 좌표는 ±200km 이내, 지도 원점 기준 수평 거리는 1000km 이내다. 더 넓은 범위는 현재 로더의 지원 범위를 벗어난다.

가져오기는 모든 입력과 계산을 검사한 뒤 생성 파일을 배치하고 `terrain.json`을 마지막에 갱신한다. 검사 중에는 `.build` 안에 중간 결과가 생길 수 있다. 디스크 쓰기 실패까지 포함하는 전체 디렉터리의 원자적 교체 기능은 아니다. 서로 다른 DEM의 경계 높이 차이는 자동으로 보정하지 않으므로 인접 타일은 같은 기준·이어지는 원본을 사용한다.

## 실행 형식

`terrain.json`은 로더가 읽는 schema 2 파일이다. `id`, `name`, `attribution`, `origin`, `bounds_deg`, `tiles`를 가진다. 각 타일의 `directory`, `vertex_file`, `image_file`, `width`, `height`, `center_enu_m`과 경위도 경계가 명시된다.

`vertices.raw`는 little-endian float32 `E,N,U` 세 값의 반복이며 단위는 m다. 북쪽 행부터 남쪽으로, 각 행은 서쪽에서 동쪽으로 저장한다. 이 값에 타일의 `center_enu_m`을 더하면 지도 원점의 ENU 좌표가 된다. Unreal 표시에서는 동/남/위 cm로 변환한다. 외부 기체 위치 API의 고도는 지도 DEM 기준과 관계없이 WGS84 타원체고(m)다.

대한민국 지도는 기존 `data/korea`의 결과를 삭제하지 않고 `maps/korea`로 복사했다. 전국 자료 재생성 명령은 다음과 같다. 기존 공개 원본 창 캐시는 `data/korea/source`에서 재사용한다.

```powershell
.\.venv\Scripts\python.exe scripts\prepare-korea.py
# 전국 생성 결과에 제주 상세 영역을 다시 적용한다. 기존 제주 캐시를 사용하며 다운로드하지 않는다.
.\.venv\Scripts\python.exe scripts\prepare-jeju-detail.py
```

`prepare-korea.py`를 다시 실행하면 제주 상세 적용도 다시 해야 한다. 두 번째 명령은 기존 `data/jeju` 원본 캐시를 사용하며 준비가 끝나도 실행 중 Unreal의 자료는 즉시 바꾸지 않는다. 이후 지도 재읽기 또는 Unreal 재시작으로 반영한다.

## 제주 상세 영역과 표시 품질

`JejuDetail` 범위는 동경 **126.421875~126.59375°**, 북위 **33.34375~33.515625°**다. 485×485 정점의 메시 간격은 약 33×39m, 영상은 2048×2048이다. 기존 제주 2025-04-06 Sentinel-2와 GLO-30 DSM·EGM2008 캐시에서 만든다. 영상 출력 간격 약 7.8×9.3m/픽셀은 재표본화된 격자이며 원본 Sentinel-2의 실제 정보 해상도는 10m다.

상세 타일이 대체하는 `N33E126`의 1,936개 거친 셀은 `skip_cells=[108,124,152,168]`로 제외한다. 바깥 한 개 기본 셀 폭에서 기존 메시로 높이를 이어 붙이며 렌더와 지면 계산은 같은 정점을 사용한다. [준비 검사 기록](../artifacts/jeju-detail-preparation.json)의 최대 경계 맞춤 오차는 약 0.54mm다. 이는 두 메시의 수치적 연결 오차이며 지형 측량 정확도를 뜻하지 않는다.

같은 외곽 띠에서 RGB도 기본 `N33E126` 영상을 위경도 UV로 bilinear 샘플링하여 섞는다. 경계 픽셀의 반올림 오차는 채널당 0.5/255 이하이며 내부 상세 영상 바이트와 지형 정점은 유지했다. 이 처리는 촬영 시기·색감이 다른 자료의 화면 경계를 완화하며 영상의 실제 정보 해상도를 높이지 않는다. 적용 근거는 준비 기록과 manifest의 `imagery_feather`에 남긴다.

상세 영역은 정점·영상 메모리와 렌더 부하를 늘리며 전국 기본 영상의 해상도를 바꾸지는 않는다. 현재 화면은 100% 렌더 해상도·TSR, 모션 블러·피사계 심도 해제, 지형 영상의 경계 clamp·trilinear 필터를 사용한다. 센서 화면은 별도 렌더 설정과 1280×960 JPEG를 사용하며 실제 갱신률은 장면과 실행 환경에 따라 달라진다. [EO/IR 보기 설정](eoir.md)을 따른다.

## 현재 대한민국 자료의 출처

| 자료 | 현재 사용 |
| --- | --- |
| Copernicus GLO-30 DSM | 육지 고도, EGM2008 정표고. 건물·식생을 포함할 수 있으며 정부/군 DTED 원본이 아니다. |
| EOX Sentinel-2 cloudless 2016 | 전국 기본 RGB. 2016/2017 원자료를 가공한 CC BY 4.0 영상이다. 일부 구름·흰 영역과 촬영 경계가 남는다. |
| Sentinel-2 L2A, 2025 | 동남부 `N35E128`, `N35E129`, `N36E128`, `N36E129`, 울릉도 `N37E130` 육지 영상 보완. 독도에는 2025-06-04 자료를 사용한다. 장면별 날짜·URL은 `terrain.json`에 기록한다. |
| NGA EGM2008 / PROJ | DEM의 정표고를 WGS84 타원체고로 바꾸는 지오이드고. |

Copernicus 타일 목록과 해양 타일 정책은 [제공자 설명](https://copernicus-dem-30m.s3.amazonaws.com/readme.html)에 있다. 실제 타일 URL 예시는 [제주 N33E126 DSM](https://copernicus-dem-30m.s3.amazonaws.com/Copernicus_DSM_COG_10_N33_00_E126_00_DEM/Copernicus_DSM_COG_10_N33_00_E126_00_DEM.tif)이다. 누락된 해양 타일은 제공자의 정책에 따라 정표고 0으로 만들며, 다운로드 실패한 육지를 0으로 대체하지 않는다.

EOX는 [공식 이용 조건](https://cloudless.eox.at/license-non-commercial)의 2016 CC BY 4.0판을 사용한다. 2018~2025 무료판의 비상업 조건을 회사용 자료에 대신 적용하지 않았다. 실제 영상은 [WMS 요청 예시](https://tiles.maps.eox.at/wms?service=WMS&version=1.1.1&request=GetMap&layers=s2cloudless&styles=&srs=EPSG%3A4326&bbox=126%2C33%2C127%2C34&width=1024&height=1024&format=image%2Fjpeg)처럼 1° 범위를 요청해 PNG로 보관한다. 출처 표시는 `attribution`에 유지한다.

독도 보완의 원본은 [Sentinel-2 2025-06-04 TCI](https://sentinel-cogs.s3.us-west-2.amazonaws.com/sentinel-s2-l2a-cogs/53/S/KB/2025/6/S2C_53SKB_20250604_0_L2A/TCI.tif)이며, 지오이드는 [PROJ 배포 NGA EGM2008](https://cdn.proj.org/us_nga_egm08_25.tif)이다. 원본 전체를 내려받는 대신 필요한 창과 축소본을 캐시하고 URL·SHA-256을 기록했다.

기본 메시 간격은 약 340~435m, 영상은 약 84~109m/픽셀이다. 독도와 위 제주 영역은 별도 상세 타일을 사용한다. 지형·영상이 존재한다는 검사와 좌표 변환 검사는 실제 지형 측량 정확도나 정식 신뢰성 시험을 대신하지 않는다.
