# 지도·지형 자료 출처

이 프로젝트의 비행역학 구현과 별개로, `data/korea`와 `data/jeju`의 실제 지형·영상 준비에 아래 공개 자료를 사용한다. 원본 전체 대신 필요한 창·COG 축소본·WMS 응답을 `source`에 보존한다. URL, 창 범위, CRS, 파일 SHA-256은 `metadata.json`과 `source/*.json`에 기록한다. 전국 자료는 축소·재표본화·해안 영상 합성 및 지오이드 보정을 적용한 수정 자료다.

## EOxCloudless 2016 전국 기본 영상

- EOX Sentinel-2 cloudless 2016 (`s2cloudless` WMS layer), 2016–2017년 합성 영상.
- 이 판본은 **CC BY 4.0**이다. 비상업 조건이 붙은 2018년 이후 무료 판본을 사용하지 않는다.
- 1° 단위 WMS 1.1.1 / EPSG:4326 / 1024² 요청을 로컬 PNG로 저장한다. 구름·눈·색상 경계가 남아 있으며 실시간 또는 완전 무구름 영상이 아니다.
- [EOX 이용 조건](https://cloudless.eox.at/license-non-commercial), [서비스 안내](https://cloudless.eox.at/documentation/usage), [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)

필수 출처 표시는 Web UI와 실제 Unreal 전국 화면에도 표시한다:

> EOxCloudless https://cloudless.eox.at by EOX IT Services GmbH (Contains modified Copernicus Sentinel data 2016 & 2017)

## Copernicus DEM GLO-30

- 제공 데이터: Copernicus DSM GLO-30, 동경 124~132°·북위 33~39° 및 제주 상세 영역, AWS 공개 COG 판본.
- 수평 기준: WGS84. 수직 기준: EGM2008 정표고(m). 건물·식생을 포함한 표면 모델이다.
- 이 파일은 정부 또는 군의 DTED 원본이 아니다. AWS COG 원본에서 작은 창을 추출하고 재표본화한 자료다.
- [제공자 데이터 설명](https://copernicus-dem-30m.s3.amazonaws.com/readme.html)
- [Copernicus 라이선스](https://documentation.dataspace.copernicus.eu/APIs/SentinelHub/Data/DEM/resources/license/License-COPDEM-30.pdf)

라이선스 Article 6의 수정 자료 출처 표시:

> produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved.

> The organisations in charge of the Copernicus programme by law or by delegation do not incur any liability for any use of the Copernicus WorldDEM-30.

자료를 재배포할 때 이 출처·면책 문구와 라이선스 의무를 후속 사용자에게 함께 전달해야 한다. 제공자가 UAVSim을 보증하거나 승인한다는 의미가 아니다.

## Copernicus Sentinel-2 영상

- 전국 보완: 구름이 심한 동남부 기본 타일 4개와 울릉도 1개에 2025 Sentinel-2 TCI 창을 적용한다. 재표본화한 DEM 육지 마스크 안의 유효 RGB만 덮고 EOX 바다는 유지한다. 장면별 촬영일·운량·URL은 국가 manifest 각 타일의 `sources`에 기록한다.
- 독도: `S2C_53SKB_20250604_0_L2A`, 2025-06-04. 해당 작은 창을 별도 메시 위에 표시하며 DEM 정표고 0.1m 이하에는 EOX 바다 색을 사용한다. 해안·작은 섬의 모양은 원본 해상도의 한계를 갖는다.
- 이하 제주 장면은 기존 `data/jeju` 상세 자료의 출처다.
- 장면: `S2B_52SBC_20250406_0_L2A`, 2025-04-06 02:28:14.518 UTC.
- Earth Search / Element 84가 제공하는 10m true-color COG를 자르고 재표본화했다.
- 장면 전체 운량 메타데이터: 0.211705%. 장면별 운량은 모든 지점의 무구름을 보장하지 않는다.
- [공개 데이터 설명](https://registry.opendata.aws/sentinel-2-l2a-cogs/)
- [장면 메타데이터](https://earth-search.aws.element84.com/v1/collections/sentinel-2-l2a/items/S2B_52SBC_20250406_0_L2A)
- [Sentinel 자료 이용 공지](https://sentinels.copernicus.eu/documents/247904/690755/Sentinel_Data_Legal_Notice)

> Contains modified Copernicus Sentinel data 2025

## NGA EGM2008 / EGM96 지오이드

- PROJ CDN의 `us_nga_egm08_25.tif`(2.5 arc-minute) 또는 DTED EGM96 입력용 `us_nga_egm96_15.tif`(15 arc-minute).
- NGA 원자료를 변환한 Public Domain 자료. 대한민국 범위의 격자 창만 보존한다.
- 정표고 H와 지오이드고 N을 더해 WGS84 타원체고 h를 구한다. 격자 보간에 따른 근사이며 실측 측량고를 의미하지 않는다.
- [제공자 설명 및 라이선스](https://cdn.proj.org/us_nga_README.txt)

## 개발 데이터 도구

Python, NumPy, Rasterio/GDAL, pyproj/PROJ, Pillow는 오프라인 데이터 준비 도구이며 외부 비행역학 구현을 사용하지 않는다. 설치 버전은 `requirements-terrain.txt`, 기존 제주 처리 버전은 `data/jeju/metadata.json`에 남긴다. 각 도구를 제품에 재배포할 경우 해당 배포물의 라이선스와 공지를 함께 제공해야 한다.

사용자 지정 DEM/DTED를 입력하면 그 자료의 배포 권한과 출처는 사용자가 제공한 원자료의 조건을 따른다. 위 Copernicus DEM 출처는 기본 예제 DEM에만 적용한다.
