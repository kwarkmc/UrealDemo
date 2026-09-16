# 받음각·실속 모델의 공개 근거

2026-09-13 조사. 아래는 자체 계산식 설계용 문헌 조사이며 실제 기체 검증 결과가 아니다. 외부 비행역학 구현 코드는 가져오거나 읽지 않았다. 현재 코드의 적용 범위와 채택한 값은 `flight-model.md`를 따른다.

## 1. 동압, 부착류와 실속 후 양력 연결

Beard·McLain, *Small Unmanned Aircraft*, 저자 공개 [Chapter 4 강의 PDF](https://drive.google.com/file/d/1BjJuj8QLWV9E1FX6sHVHXIGaIizUaAJ5/view), 슬라이드 17·18·20. 해당 슬라이드를 직접 렌더링해 수식을 확인했다. 비교 근거이며, 현재 구현의 `CL_post=sin(2*alpha)`, `CD_post=2*sin(alpha)^2`가 아래 BYU 식과 같다는 뜻은 아니다.

```text
qbar = rho * Va^2 / 2
L = qbar * S * CL;  D = qbar * S * CD
CL_attached = CL0 + CL_alpha * alpha
CL_flat = 2 * sign(alpha) * sin(alpha)^2 * cos(alpha)
A = exp(-M * (alpha - alpha0))
B = exp( M * (alpha + alpha0))
sigma = (1 + A + B) / ((1 + A) * (1 + B))
CL = (1 - sigma) * CL_attached + sigma * CL_flat
CD_attached = CDp + CL_attached^2 / (pi * e * AR)
AR = b^2 / S
```

`Va`는 공기에 대한 속도(m/s), `rho`는 밀도(kg/m³), `S`는 면적(m²), `alpha`는 rad다. `qbar`와 피치 각속도 `q`는 다른 값이다. 양력·항력의 크기는 공기 상대속도 방향에 수직·평행하게 계산한 뒤 기체축으로 회전한다.

`alpha0`는 전환 위치, `M`은 전환 급격함을 정하는 매개변수다. 슬라이드의 예제 곡선과 M=10~50 비교는 Thor 측정값이 아니다. 실제 양력 최대점도 곧바로 `alpha0`와 같지는 않다. 지수 계산은 허용 받음각과 매개변수 범위에서 overflow가 없도록 구현한다.

이 출처의 항력식은 부착류용 포물선 항력이며 측정된 실속 후 항력 곡선이 아니다. 이를 90°까지 그대로 늘리거나, 떨어진 `CL`만 제곱해 실속 항력을 만드는 것은 별도 근거가 필요하다. 예컨대 `CD_post = CD90 * sin(alpha)^2`와 같은 항력 연결을 채택한다면 그 식과 `CD90` 모두 **개발 가정**으로 명시해야 한다. 제시된 평판 양력과 힘이 판의 법선 방향이라는 추가 가정을 일관되게 적용하면 `CD_flat = 2*abs(sin(alpha))^3`가 나오지만, 이 역시 Thor의 저속 실속 시험값은 아니다.

## 2. Ultra Stick 25e 교육 자료의 수치

UMN AEM5333, [Assignment 3: UAV Steady Airframe Maneuvering Envelope](https://dept.aem.umn.edu/~mettler/Courses/AEM%205333%20%28spring%202013%29/Assignments/A3/A3_AEM5333.pdf), 2013-02-15, 2쪽 Table 1. 원문 표를 확인했다.

| 항목 | 표의 값 |
| --- | --- |
| 질량, 날개폭, 면적, 평균 시위 | 1.959 kg, 1.27 m, 0.31 m², 0.25 m |
| CL_alpha | 4.58; 표의 단위 칸은 `–`이므로 rad 기준으로 쓰는 것은 추가 해석 |
| Cd0, Oswald e | 0.0434, 0.75 |
| Vmin (stall speed), Vmax | 10 m/s, 25 m/s |

이는 정상 기동 계산을 위한 수업 입력 자료다. 19 m/s 시스템 식별 원고의 측정 결과와 같은 지위로 취급하거나 판본별 계수를 섞으면 안 된다. 이 표만으로 Thor의 실속 받음각, 비대칭 양력 곡선, 실속 후 항력·모멘트·조종효과를 확정할 수 없다. `Vstall=10 m/s`로 CLmax를 역산하려면 밀도·하중배수·기체 상태라는 가정이 추가된다.

## 3. Thor 측정 범위의 경계

Dorobantu, [Test Platforms for Model-Based Flight Research](https://dept.aem.umn.edu/~SeilerControl/Thesis/2013/Dorobantu_13PhD_TestPlatformsForModelBasedFlightResearch.pdf), 2013, 인쇄 7쪽(PDF 19쪽) 및 Chapter 3. Ultra Stick 120의 NASA 정적·동적 풍동 기반 비선형 모델과 Ultra Stick 25e의 비행시험·주파수영역 시스템 식별을 구분한다. Mini도 별도 플랫폼이다.

확인한 25e 원고·학위논문·수업 자료에서는 Thor의 측정 CL(alpha) 실속 곡선을 찾지 못했다. 120 또는 Mini의 고받음각 수치를 Thor 측정값으로 대입하지 않는다. `alpha_stall`, 전환 폭, 실속 후 항력, 피칭 모멘트 변화, 조종효과 저하는 새로운 시험 데이터가 없으면 명시적 개발 가정이다. 이 대수적 연결은 동적 실속·히스테리시스·스핀의 검증 모델이 아니다.

## 4. 표준대기 밀도

층 경계와 기하 고도 변환의 원문은 NOAA·NASA·USAF, *U.S. Standard Atmosphere, 1976*, NOAA-S/T 76-1562 / NASA-TM-X-74335다. [NASA 공식 서지](https://ntrs.nasa.gov/citations/19770009539), [NOAA 공식 PDF](https://www.ngdc.noaa.gov/stp/space-weather/online-publications/miscellaneous/us-standard-atmosphere-1976/us-standard-atmosphere_st76-1562_noaa.pdf). 아래 위치는 NOAA PDF를 직접 렌더링해 확인했다.

| 지원 항목 | 원문 위치 |
| --- | --- |
| H=0, 11, 20 km 층 경계; 첫 두 층 온도 기울기 -6.5, 0 K/km | 인쇄 3쪽, Table 4 (PDF 19쪽) |
| 유효 지구 반지름 r0=6,356,766 m | 인쇄 4·8쪽 (PDF 20·24쪽) |
| 기하 고도 Z → 지오포텐셜 고도 H=r0*Z/(r0+Z) | 인쇄 8쪽, 식 (18) (PDF 24쪽); m와 지오포텐셜 m의 수치 변환 |
| R*=8314.32 J/(kmol·K), M0=28.9644 kg/kmol | 인쇄 3·9쪽 (PDF 19·25쪽) |

1976 원문의 비기체상수는 마지막 행에서 `R=R*/M0=287.053072047… J/(kg·K)`로 계산된다. 코어의 `287.05287`은 이 비율과 약 0.7 ppm 차이가 있는 ISA 관용값이며, 1976 원문의 정확한 값이라고 표현하지 않는다. 같은 `287.05287`의 사용례는 [NASA 공개 항공 연구 논문](https://ntrs.nasa.gov/api/citations/20190025458/downloads/20190025458.pdf) 1쪽 기호표에서 확인할 수 있다.

다음 NASA/TP-2007-213475/VOL2 [Appendix A: Standard Atmosphere](https://ntrs.nasa.gov/api/citations/20120003268/downloads/20120003268.pdf?attachment=true), A-1~A-2는 대류권 식을 설명하는 보조 자료다. 여기의 반올림 상수 `R=287.053`을 코어 상수의 정확한 출처로 취급하지 않는다. NASA가 공개한 본문 검색 자료에서 정역학식·이상기체식·상수를 확인했다. 정역학식과 이상기체식을 적분하면 대류권 구간은 다음과 같다.

```text
0 <= H <= 11000 m
T0 = 288.15 K; p0 = 101325 Pa
R = 287.053 J/(kg*K); g = 9.80665 m/s^2; lapse = 0.0065 K/m
T = T0 - lapse * H
p = p0 * (T / T0)^(g / (R * lapse))
rho = p / (R * T)
```

해수면 `rho`는 약 1.225 kg/m³다. `H`는 표준대기의 지오포텐셜 고도이며 API의 WGS84 타원체고와 동일하지 않다. 경계에서 수직 기준을 변환하거나, 타원체고를 대신 쓰는 경우 그 근사를 명시한다. 지역 ENU의 z만으로 절대 대기 고도를 계산하면 비행 원점을 바꿀 때 밀도가 잘못 달라진다. 구간 밖은 별도 식 또는 명시적인 입력 거부가 필요하다. 실제 기상·기압·습도 변화는 이 표준대기와 별개다.

## 5. 밀도와 프로펠러 추력

NASA/TM-20210014010, [Investigation of High Incidence Angle Propeller Aerodynamics for Subscale eVTOL Aircraft](https://ntrs.nasa.gov/api/citations/20210014010/downloads/NASA-TM-20210014010.pdf?attachment=true), 7쪽 식 (1)~(3).

```text
CT = thrust / (rho * n^2 * D^4)
CQ = torque / (rho * n^2 * D^5)
J = Va / (n * D)
CP = 2 * pi * CQ
```

`n`은 rev/s, `D`는 프로펠러 지름(m), `J`는 전진비다. 고정 RPM·고정 형상·거의 같은 CT라는 조건에서만 추력이 밀도에 비례한다고 볼 수 있다. CT는 전진비·레이놀즈수·팁 마하수 등의 영향을 받는다. 이 보고서는 LA-8의 다른 프로펠러 시험이므로 그 계수를 Thor에 이식하지 않는다. 스로틀은 RPM 자체가 아니며 모터·ESC·배터리·해당 프로펠러 데이터가 없으면 스로틀→추력 관계와 밀도 보정은 개발 가정으로 남는다.
