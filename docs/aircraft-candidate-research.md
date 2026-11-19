# 200kg 초과 단발 고정익 공개 자료 조사

2026-11-17 후속 결정: 사용자가 누락값을 명시적 개발 가정으로 분리한 [C208B 연구 모델](c208b-research.md)을 승인했다. 이 구현은 아래 미확보 자료를 확보한 것으로 처리하지 않으며 실제 성능 미검증이다.

조사일: 2026-11-17. 목표는 실제 운용 총질량 200kg 초과 단발 프로펠러 고정익의 자체 6DOF 모델에 필요한 자료 확보다. 공개 중량 표시는 대부분 최대/설계 총중량이며 특정 비행의 실중량을 보장하지 않는다. 아래 순위는 프로젝트 적용성 판단이며 실제 기체 선정·수치 적용·성능 검증 완료가 아니다. 활성 Thor는 변경하지 않았다.

**후속 사용자 지정(2026-11-17): Cessna 208B Super Cargomaster를 첫 대형 비행역학 기준으로 확정하고 King Air 350을 후속 쌍발 후보로 둔다.** 아래 TigerShark-XP/SIERRA-B 추천과 추가 조사 당시의 미지정 표현은 조사 경과이며 현재 우선순위를 뜻하지 않는다. 기체명 지정과 동일 구성 자료·성능 검증 완료는 다르다. 현재 작업 순서는 [확장 계획](fixed-wing-expansion.md)을 따른다.

## 조사 결론

200kg 초과 실제 기체와 공식 제원·운용 자료는 찾았다. 그러나 동일 판본·질량 구성의 형상/CG/관성, 종·횡방향 정적/동적 공력, 조종효과, 추진/작동기, 독립 비행시험을 모두 갖춘 공개 세트는 이번 조사에서 확보하지 못했다. 검색에서 찾지 못했다는 의미이며 비공개 또는 추가 공개 자료의 부재를 단정하지 않는다.

| 후보/판본 | 확인한 중량·날개폭 | 확보한 근거 | 판단 |
| --- | --- | --- | --- |
| NASC TigerShark-XP | 제조사 Gross Weight 234kg, 폭 6.6m | [제조사 제원](https://www.nasc.com/nasc-tigershark-xp/): 32HP, 최대 80kt, 체공 55~60kt, 바퀴식 이착륙. 옵션/탑재 구성에 따른 성능 차이 명시 | 기존 활주·이착륙 목표의 우선 후보. 최신 XP의 실측 공력·관성·추진표 미확보 |
| NASA SIERRA-B, Handbook Rev B, 2018-12-14 | MGTOW 217.7kg, 공허 154.2kg, 폭 6.1m | [공식 운용 핸드북](https://airbornescience.nasa.gov/sites/default/files/A9UAV-1401-XM001%20-%20SIERRA-B%20Experimenters%20Handbook%20-%20RevB%20-%2012-14-2018.pdf), 본문 4~7쪽: 단발 후방 피스톤/고정피치 프로펠러, 고정 삼륜기어, 성능표·상승/항속 곡선 | 공개 운용 근거가 상세한 후보. 역 V꼬리·플랩 반영 필요. 관성/완전 공력 세트 미확보 |
| NASC/DOE ArcticShark | 최대 총중량 295kg, 폭 6.7m | [DOE 팩트시트](https://armgov.svcs.arm.gov/publications/fact-sheets/docs/doe-sc-arm-25-026.pdf), [DOE 2017 연보 본문 28쪽](https://www.arm.gov/publications/annual-reports/docs/doe-sc-arm-17-038.pdf), [관측 자료 논문](https://essd.copernicus.org/articles/14/3423/2022/index.html) | 연구용 운용·관측 자료 후보. TigerShark와 판본 구분 필수. 관측 자료를 공력 식별 데이터로 취급하지 않음 |
| Textron Shadow V2 / Improved Shadow | 2019 자료 212kg·6.2m; 현재 Improved 표기 243kg·6.2m | [2019 제조사 PDF](https://www.textronsystems.com/sites/default/files/_documents/ShadowV2_Datasheet.pdf), [현재 제조사 페이지](https://www.textronsystems.com/products/shadow-tactical-unmanned-aircraft-systems), [미 육군 2020~2021 핸드북](https://www.army.mil/e2/downloads/rv7/2020-2021_Weapon_Systems_Handbook.pdf) | 질량 조건 충족 판본 있음. 사출·착륙 제동장치와 역 V꼬리가 초기 범위에 추가 부담. 연구용 구형 모델 계수를 혼합하면 안 됨 |

## SIERRA-B에서 실제 쓸 수 있는 것

Rev B 본문 5쪽의 질량/탑재 제약, 6쪽의 접근 50kn·최량 활공 57kn·활공비 10.4:1, 최대중량 이륙거리 335m와 착륙거리 365m, 7쪽 상승률·항속/탑재 곡선을 확인했다. 표의 IAS/TAS 정의와 중량·고도·형상·시험 조건은 수치 사용 전에 확인해야 한다. 이 속도를 곧바로 TAS API에 넣지 않는다.

핸드북은 실험 탑재자를 위한 문서다. 전체 공력계수표나 Ix/Iy/Iz/Ixz 수치 세트가 확보된 문서는 아니다. 본문 9쪽에는 GCS가 자세/각속도·엔진속도·조종면 등의 원격 자료를 기록한다는 설명이 있지만, 해당 원자료의 공개 다운로드와 식별 기동·공기자료·동기화는 확인하지 못했다. 데이터의 존재와 우리가 접근 가능한 식별 자료를 구분한다.

2017 Preliminary의 예측/TBC 성능과 2018 Rev B를 혼합하지 않는다. 구형 SIERRA의 350/400lb 자료도 SIERRA-B와 구분한다. 고정기어 기체이므로 기존 가상 수납기어 기능을 실기체 기능처럼 적용하지 않는다. 역 V꼬리는 엘리베이터/러더 등가 입력에서 실제 러더베이터로의 혼합·포화를 검토해야 한다.

## 공력 자료가 풍부하지만 그대로 채택하면 안 되는 자료

### Pioneer: 풍동 원문과 단위가 있는 유력 비교 자료

Robert M. Bray, *A wind tunnel study of the Pioneer remotely piloted vehicle*, Naval Postgraduate School, 1991, AD-A247028. [NPS 원문](https://calhoun.nps.edu/server/api/core/bitstreams/5720b30f-63c4-4db7-8f12-a789de5aa4d0/content).

검색 색인에서 Table 6.2, 본문 106쪽을 확인했다. 기준은 W=420lbf(질량 약190.5kg), S=30.42ft², b=16.90ft, c=1.80ft, alpha=6°, V=66kt TAS, CG=33% MAC다. 양력 기울기·피칭 안정 미계수·옆미끄럼/방향 안정·엘리베이터/러더 조종효과가 있고 각도 미계수는 rad 기준이다. 풍동 열과 PMARC 예측 열을 구분해야 한다.

직접 PDF 열기는 403으로 실패했으며 본문 전체·동적 미계수·관성 표는 이번에 전부 대조하지 못했다. [UIUC의 원문 참조 목록](https://m-selig.ae.illinois.edu/apasim/Aircraft-uiuc/references.html)도 Bray를 가리킨다. [미 해군 RQ-2A 제원](https://www.navy.mil/Resources/Fact-Files/Display-FactFiles/Article/2166538/rq-2a-pioneer-unmanned-aerial-vehicle-uav/)의 최대 설계 총중량은 188.69kg다. 흔히 보이는 205kg와 이 풍동/해군 구성을 동일하게 취급하지 않는다. 자체 공력 계산의 비교 근거로 가치가 있지만 현재 200kg 초과 실기체 기준의 직접 충족 근거는 아니다.

### KAIST Shadow 연구: 관성 수치가 있어도 측정된 최신 Shadow가 아니다

Han 외, ICAS 2016, *Perch Landing Assisted by Thruster (PLAT): Six D.O.F Trajectory Optimization*. [학회 원문](https://icas.org/icas_archive/ICAS2016/data/papers/2016_0334_paper.pdf).

본문 2쪽은 상세 제원이 없어 CAD·부품 질량 배분으로 추정했다고 명시한다. 3쪽 Table 1은 **140kg, 폭4.3m, 면적2.623m²**, Ix/Iy/Iz=82.59/99.07/174.07kg·m², Ixz=10.63kg·m²의 연구 모델이다. 주익 NACA4415·꼬리 NACA0015 역시 가정이다. 높은 받음각 단면 공력과 조립식 모델링 방법은 참고할 수 있지만, 212/243kg·폭6.2m Shadow의 실측 전기체 계수로 쓸 수 없다. 논문이 추가한 PLAT 추력기는 실제 기본 Shadow 구성이나 이번 구현 목표가 아니다.

### TigerShark의 과거 연구 모델

Karas, 2012, [WVU 공식 학위논문 레코드](https://researchrepository.wvu.edu/etd/487/)를 확인했다. 별도의 연구 출판물에서 과거 TigerShark를 AVL와 Pioneer 보정으로 모델링한 설명이 검색됐다. 과거 318lb급 연구 모델과 현재 515lb XP의 동일성은 입증하지 못했다. 논문 레코드 확인을 최신 XP 공력 데이터 세트 확보로 표시하지 않는다. 후속 작업은 학위논문 원문과 인용 실험 자료를 추적하는 것이다.

### 외부 시뮬레이터·동명이기

- [UIUC Aircraft Dynamics Models](https://m-selig.ae.illinois.edu/apasim/Aircraft-uiuc.html)는 Pioneer 자료와 원출처를 안내하지만 근사 모델·단순 추진·추정 조종 한계를 명시한다. 외부 FDM 구현을 가져오지 않는다. 데이터 파일은 GPL 표시이므로 원실험 논문으로 돌아가 출처·재사용 조건을 따로 확인한다.
- 공개 Aerosonde 제어 논문의 흔한 13.5kg 모델은 크기 조건을 충족하지 않는다. 서로 다른 계수 판본을 섞지 않는다.
- UPC의 *Aerodynamics, stability and control computational analysis for UAVs*에서 등장하는 Shadow는 검색된 원문 설명상 파키스탄 Integrated Dynamics 기체다. AAI RQ-7과 이름이 같다는 이유로 채택하지 않는다.
- Hermes 450 관련 검색에서 제조사 제품과 날개/꼬리 부분 공력 연구는 찾았으나 동일 구성의 완전 6DOF 수치 세트는 확보하지 못해 우선 후보에서 제외했다. 부분 익형 실험을 전기체 자료로 해석하지 않는다.

## 추진·시험 자료의 실제 확보 수준

TigerShark-XP의 32HP는 엔진 출력 제원이지 추력이 아니다. 프로펠러 지름·피치·RPM·전진비별 추력/토크·밀도와 엔진 응답이 있어야 추력 모델에 연결할 수 있다. SIERRA-B에도 추진 구성 설명은 있지만 이번에 해당 전체 수치 세트는 확보하지 못했다. 기존 `maximum_thrust_mps2`를 출력 제원에서 임의로 환산하지 않는다.

Mei 외 2022의 [ESSD 자료 논문](https://essd.copernicus.org/articles/14/3423/2022/index.html), DOI 10.5194/essd-14-3423-2022는 2021년 TigerShark 관측비행 7회와 등록 사용자 대상 공개 자료를 설명한다. ArcticShark가 정비 중이어서 MSU TigerShark에 관측 장비를 탑재한 비행이며 ArcticShark 자체 자료로 바꿔 쓰면 안 된다. 대기·항적 자료와 조종 입력/작동기·각속도·질량·CG가 동기화된 비행역학 식별 자료는 다르다. 실제 파일을 다운로드해 채널/주기/보정 상태까지 확인하지 않았다.

## 선정 판단과 다음 자료 확보

1. **운용 형태 우선 후보: TigerShark-XP.** 200kg 초과 제원과 일반 바퀴식 이착륙이 기존 제품 목표에 맞는다. XP 판본의 질량 구성·CG/관성·공력/추진·작동기·시험 원자료 확보가 선정 조건이다.
2. **공개 운용 근거 우선 후보: SIERRA-B Rev B.** 자료 판본과 성능 비교 지점이 명확하다. 역 V꼬리·플랩·고정기어 처리가 필요하며 공력·관성 자료가 확보되기 전 완성 프로필로 채택하지 않는다.
3. **계산 비교 자료: Pioneer Bray 원문.** 원본 표·시험 조건·부호·단위를 추출해 자체 계산과 비교하되 200kg 초과 최신 기체 데이터로 변형하지 않는다.

후속 확보 목록은 (a) 실제 운용 질량 200kg 초과인 탑재/연료 구성, (b) 기준점/축·형상, (c) CG와 같은 구성의 관성, (d) 종·횡 공력/각속도·조종면 미계수 또는 표와 유효 범위, (e) 프로펠러·엔진 추력/토크/응답, (f) 실제 면 위치·속도 제한/혼합, (g) 기어 접촉/제동, (h) 동기화된 독립 비행시험과 오차 근거다. 제조사/연구기관에 연락하거나 자료를 구매하지 않았다.

실제 자료가 추가 확보되지 않으면 [확장 계획](fixed-wing-expansion.md)의 B는 합성 시험체로 진행할 수 있다. 그 결과를 TigerShark-XP/SIERRA-B 실기체 검증으로 표시하지 않는다. 서로 다른 기체의 제원·관성·공력을 합친 프로필도 만들지 않는다.

## 조사 한계

공식 제조사·NASA·DOE·군/대학/학회 자료를 우선 사용했다. NPS 원문 직접 접근과 DOE 최신 팩트시트 전체 접근은 403이 있었고 해당 항목은 검색 색인/별도 공식 자료로 확인했다. NASA/ICAS PDF는 텍스트를 읽었으나 웹 스크린샷은 cache miss로 실패해 표 이미지 대조는 미수행이다. 유료/제한 자료·제조사 회신·원시 비행 로그·공력표의 전체 전산화는 미확보다. 비행 모델·기체 파일·API·UI 구현과 실기체 적용 시험은 변경/수행하지 않았다.

## 추가 조사: King Air 350과 Cessna 208 계열

### 2026-11-17 실행 단계 A2 자료 대조

Texas A&M의 JAST 공개 원문을 추가 확인했다. 본문 Table 2에는 고도 15000ft, 속도 135kt, 동압 35.219lbf/ft², 중량 8402lb, CG 0.289 MAC, alpha 5.51°가 있다. 이 조건을 최신 EX나 MTOW 조건으로 바꾸지 않는다. 속도 정의·밀도/동압과의 일치, 기준축·형상 상태는 별도로 대조해야 하며 135kt를 곧바로 TAS 초기값으로 사용하지 않았다.

Appendix A에는 clean 횡방향 상태공간 행렬이 공개되어 있다. 이는 선형 응답의 독립 비교 근거 후보이며 전기체 비선형 모델이나 실측 관성·추력표의 대체가 아니다. 본문도 큰 자세/속도 변화에서 선형 모델의 한계를 명시한다. 결합 응답 2008 원문의 색인은 찾았으나 직접 PDF 열기는 접근/리디렉션 오류로 실패했다. 검색 색인의 일부 숫자를 관성 원문 검증 완료로 취급하지 않았다.

| 자료 | 이번 확보 수준 | C1~E 진행에 남은 조건 |
| --- | --- | --- |
| 기체명·공개 연구 계보 | C208B Super Cargomaster, JAST 원문과 결합 응답 서지 | 같은 판본의 엔진/프로펠러·포드·형상 상태 고정 |
| 국소 조건·횡방향 행렬 | JAST Table 2 / Appendix A 확인 | 종방향 계수와 공력 기준점·단위·축을 함께 대조 |
| 운용 구성 CG/관성 | CG 비율 일부 확인, 같은 구성 Ix/Iy/Iz/Ixz 미확보 | 실제 수치/원출처 또는 명시적 별도 연구 가정 필요 |
| 추진·작동기 | 계열 엔진 제원은 있으나 연구 구성의 추력/토크·응답 미확보 | shp를 임의 추력으로 환산하지 않음 |
| 저속/접근·플랩·접촉 | 동일 구성의 충분한 자료 미확보 | D/E의 C208B 운용 완료를 선언하지 않음 |

자료 추적과 별개로 B는 합성 1000kg 시험체로 구현/검사했다. 합성 파일을 C208B로 이름 바꾸거나 Thor 계수를 실기체로 승격하지 않았다. 실자료 기준을 유지할지, 누락값을 명시한 별도 연구 모델을 허용할지의 결정은 실제 자료 확보와 다르며 변경 시 계획에 기록한다.

2026-11-17 사용자 요청으로 유인 터보프롭 두 계열을 추가 조사했다. 단발 무인기라는 기존 대상과 다르지만 자체 계산 코어의 기준 기체 후보로 검토할 수 있다. 이번 검색은 대상 변경 승인이 아니며 활성 기체·확장 단계는 바꾸지 않았다.

### King Air 350: 실측 기반 개발 논문 공개, 전체 모델 데이터는 미확보

[Hui 외, ICAS 2002 원문](https://www.icas.org/icas_archive/ICAS2002/PAPERS/783.PDF), *King Air 350 Flight-Test Data Gathering and Level-D Simulator Aerodynamic Model Development*, 10쪽. NRC Canada와 NLX가 수행한 실기체 시험·모델 식별 연구다. 본문은 PT6A-60A 2발, T꼬리, 최대 램프중량 15,100lb(약6849kg)를 설명한다. 이는 최대이륙중량과 다른 수치다.

약90 비행시간·47 sorties·1300회 이상 기동으로 중량/CG·고도/속도·출력·플랩·기어·지면효과와 단발 정지를 다뤘다. 실속, 좌우 비대칭 추력, 세 플랩 위치, 시험 응답 비교와 전역 모델 작성 과정을 설명한다. 본문 8~9쪽은 110개가 넘는 표로 변환했다고 서술하지만 그 전체 테이블·관성 세트·원시 로그 다운로드는 확보하지 못했다. 논문에서 보고한 Level-D 요건 충족을 우리 구현의 검증/인증으로 표현하지 않는다.

중요한 검색 함정: 제조사의 과거 `king-air-350i` URL이 현재 **King Air 360** 페이지를 반환한다. 그 페이지의 제원·엔진·성능을 2002년 King Air 350 데이터로 섞지 않는다. 실제 기준을 고르면 B300/350, 350i, 350ER, 360 및 장비·프로펠러 구성을 먼저 고정해야 한다.

우리 코드에서는 엔진 2개 각각의 출력/고장·추력 작용점, 비대칭 힘·모멘트, 프로펠러 상태·페더링, 수납기어·플랩과 자동조종 재설계가 필요하다. 단발 1개 스로틀 모델에 총추력만 합치는 것으로 엔진아웃을 지원하지 않는다. 첫 단발 목표보다 후속 쌍발 검증 대상으로 적합하다는 것이 이번 판단이다.

### Cessna 208/208B: 단발 구조에 맞고 공력 연구를 추적할 수 있음

제조사 현재 지상기 제원을 판본별로 분리한다.

| 판본 | MTOW | 엔진 | 날개폭/면적 | 공식 자료 |
| --- | --- | --- | --- | --- |
| Caravan 208 계열 | 3629kg | PT6A-114A, 675shp | 제품 페이지의 해당 지상기 표 사용 | [제조사 Caravan](https://cessna.txtav.com/turboprop/caravan) |
| Grand Caravan EX, 208B 계열 | 3995kg | PT6A-140, 867shp | 약15.9m/25.96m² | [제조사 EX](https://cessna.txtav.com/turboprop/grand-caravan-ex), [2025 제품 카드](https://cessna.txtav.com/-/media/cessna/files/product-cards/turboprop/grand_caravan_ex_product_card.ashx) |

Grand Caravan EX의 포드/수상형 표가 같은 페이지에 있으므로 혼합하지 않는다. 제조사 브로슈어의 중량·성능은 포드 포함 등 조건을 확인한다. 아래 Super Cargomaster 연구 자료를 PT6A-140 EX의 실측값으로 간주하지 않는다.

1. Lampton·Valasek, *Prediction of icing effects on the lateral/directional stability and control of light airplanes*, Aerospace Science and Technology 23 (2012), DOI 10.1016/j.ast.2011.08.005. [저자 소속 Texas A&M 공개 원문](https://vscl.tamu.edu/wp-content/uploads/sites/134/2017/07/JAST_icing.pdf)은 2011 article-in-press 판본이며 최종 서지는 [연구실 목록](https://vscl.tamu.edu/journal/)으로 대조했다. 대상은 **208B Super Cargomaster**. DATCOM/AAA로 계산한 모델, 제한된 비행시험 응답 비교, 트림점·공력 미계수와 착빙 변화 표를 확인했다. 결과 범위는 횡/방향 선형 응답이며 전 영역 실측 공력표가 아니다. 착빙 영향은 NASA Twin Otter 등의 자료를 구성에 맞춰 적용한 연구 근사이며 C208B 전체의 직접 착빙 실측으로 취급하지 않는다.
2. 같은 저자, *Prediction of Icing Effects on the Coupled Dynamic Response of Light Airplanes*, JGCD 31(3), 2008, 656–673, DOI 10.2514/1.31165. [연구실 공식 서지](https://vscl.tamu.edu/journal/)에서 종/횡 결합 응답 후속 자료를 확인했다. 전체 원문·질량/관성 표 전산화는 아직 미완료다.
3. Paris·Alaverdi, AIAA 2016-1666, *Aerodynamic Model Update Using Parameter Identification Supporting a Cessna Grand Caravan Engineering Simulation*, DOI [10.2514/6.2016-1666](https://doi.org/10.2514/6.2016-1666). 검색 레코드와 재게시 색인에서 제목/저자·비행자료 기반 모델 갱신 설명을 찾았으나 출판사 원문 접근은 실패했다. 공개 전체 계수 세트를 확보한 것으로 표시하지 않으며, 원문·자료 제공 조건을 확인할 후속 후보로 둔다.

현 단계 추천은 **C208B Super Cargomaster의 연구 판본 추적을 단발 공중 모델의 우선 조사로 추가**, King Air 350은 쌍발 확장 후보로 유지하는 것이다. 두 기체는 유인기이며 실제 무인기 데이터의 대체라는 의미는 아니다. 어느 쪽도 동일 구성의 관성·프로펠러 지도·전체 공력·독립 로그를 모두 확보하지 못했다. 기체별 속도 범위, 플랩/스포일러·프로펠러·지상 접촉·제어 설정 갱신이 필요하며 현재 Thor 모델에 제원만 넣어 적용하지 않는다.
