# Sherbet 발로란트급 조준점 설계

작성일 2026-07-30 · 대상 브랜치 `sherbet-base` · 대상 탭 「에임」(`_sherbet_tab == 5`)

기존 Sherbet 조준점(점/십자/원/이미지 + 크기·두께·간격·투명도·색)을 **발로란트와 픽셀 단위로
같은 조준점**으로 교체한다. **공유 코드 import/export를 반드시 포함한다** — 발로란트 유저는
이미 코드를 주고받고 있고, 그 호환성이 이 기능의 존재 이유다.

---

## 0. 근거와 판정 원칙

### 0.1 소스 등급

| 등급 | 소스 | 무엇의 근거인가 |
|---|---|---|
| **S** | 게임이 저장한 `SavedCrosshairProfileData` JSON 실덤프 (ShooterGame 로그 / RiotUserSettings.ini, 실유저 프로필 17개) | 필드명·타입·기본값 |
| **S** | VCRDB 라이브 번들의 `crosshairMap` 디코더 테이블 (바이트 단위 재추출) | 코드 키별 `[경로, min, max, 정수여부]` |
| **A** | 실제 유통 코드 코퍼스 (208개 / 독립 재검증 174개) | 키 집합·기본값 생략 규칙·출력 순서 |
| **A** | 오픈소스 렌더러 원문 (VCRDB, LilyBergonzat, genesy, crosshaircanvas, valoreye, ruwiss) | 좌표식·홀짝 보정·윤곽선 |
| **B** | Riot 공식 패치노트 | 도입 시점, deadzone, Gun Recovery Time |
| **C** | 블로그·위키·커뮤니티 | 동작의 *의미* 확인용. **숫자 근거로는 쓰지 않았다** |

**원칙: 2차 서술(블로그·위키)과 오픈소스 구현체의 실제 코드가 갈리면 코드를 따른다.**
아래 §0.3의 판정 대부분이 이 원칙으로 갈렸다.

### 0.2 이 문서에 반영한 정정

세 건의 연구 결과를 각각 적대적으로 재검증한 결과 **총 26건의 정정**(파라미터 7 · 공유코드 6 ·
렌더링 13)이 나왔고 **전부 반영**했다. 판정이 갈린 지점은 §0.3에 전부 명시했다.

### 0.3 충돌 판정 (연구 ↔ 검증, 또는 소스 ↔ 소스)

| # | 쟁점 | 채택 | 근거 |
|---|---|---|---|
| 1 | 커스텀 색 인코딩 `RRGGBBAA` vs `AARRGGBB` | **RRGGBBAA** | 실코드 `u;008000FF`(녹색)가 AARRGGBB면 알파 0(완전투명)이 되어 모순. VCRDB가 6자리에 `FF`를 **앞**에 붙이는 건 그 사이트 내부 표현일 뿐 |
| 2 | 바깥선 범위 `1l` 0–10 / `1o` 0–40 vs "재현 불가, nk521은 1–20" | **0–10 / 0–40 (UI 한계)** + **import 시 클램프 금지** | VCRDB 디코더 맵은 *구현체의 실제 테이블*이고 검증에서 바이트 단위 재추출됨. nk521은 **랜덤 조준점 생성기**라 그 범위는 자기 난수 범위지 게임 한계 주장이 아니다. 다만 코퍼스가 상한 근처에 도달 못 한 건 사실이므로 파서는 범위 밖 값을 받아들인다 → §6 실물 확인 |
| 3 | 홀수 두께 −0.5px 시프트 "3개 구현체 일치" | **공식은 채택, 근거 등급은 낮춤** | 3개는 독립이 아니라 VCRDB 테이블의 같은 계보(중국어 클론까지 문자 단위 동일). iNiR QML은 회전 팔 기준이라 다른 규칙 → 보강 근거 아님. 그래도 유통 중인 모든 프리뷰가 이 결과를 내므로 "코드를 붙였을 때 보이던 그림"과 일치한다 → §6 |
| 4 | 윤곽선 = 링(strokeRect) vs 확장 사각형 채우기 | **링** | 링은 픽셀 정확 계보(VCRDB/Lily/crosshaircanvas), 채우기는 valoreye 단독. valoreye는 `+4` 미적용·라인 opacity 미적용·`0v` 무시·홀짝 보정 없음이 확인되어 저충실도 구현이다 |
| 5 | 윤곽선 1-pass(팔마다 fill→outline) vs 2-pass(윤곽선 전부 → fill 전부) | **1-pass** | 모든 레퍼런스 렌더러가 1-pass. "발로란트는 겹쳐도 안 진해진다"는 2-pass 주장은 어느 소스도 뒷받침 못 함 → §6 |
| 6 | 그리기 순서 중앙 점 위치 (inner→dot→outer vs dot→inner→outer) | **inner → dot → outer (선택)** | VCRDB + valoreye 주석 vs genesy + iNiR = 2:2. "확정"이 아니라 **선택**이다. 가로→세로, outer가 inner 위는 4/4 일치라 확정 → §6 |
| 7 | 맨 앞 `0` = 프로필 인덱스? | **아니다. 고정 접두 토큰** | 세이브 JSON은 `{"currentProfile":N,"profiles":[…]}` 로 인덱스를 따로 갖고, 유통 코드는 전부 `0`. 출력은 `0` 고정, 파싱 시 무시 |
| 8 | `m` 키 ↔ `bShowMinError` vs `bFixMinErrorAcrossWeapons` | **`bFixMinErrorAcrossWeapons`** | `m`은 P/A(변형) 레벨 키이고 `bFixMinErrorAcrossWeapons`도 변형 레벨. `bShowMinError`는 라인 레벨이고 코드 키가 아예 없다 |
| 9 | 윤곽선 색 "항상 검정 고정" | **"공유 코드에는 윤곽선 색 키가 없어 import되는 코드는 항상 검정"** | 세이브의 `outlineColor`는 완전한 RGBA 필드이고 실덤프에 `{b:139,g:0,r:0}`(진파랑) 사례가 있다. 외부 툴이 바꾼다. Sherbet은 검정 고정으로 충분하되 "고정 상수"라고 쓰면 틀린다 |
| 10 | Riot JSON의 `color` = 프리셋 인덱스? | **아니다. RGBA 구조체** | 0–8 인덱스는 **공유 코드에만** 존재. 설정 파일에서 "몇 번 프리셋인지"는 복원 불가 |
| 11 | 걷기 +3.0° / 달리기 +6.0° | **폐기. 라이플 walk 2.0° / run 5.0°** | 3.0/6.0은 어떤 패치노트에도 없다. 패치 2.02(run 3.75→5.0) → 패치 3.0(walk 1.3→2.0)로 추적됨. **다만 우리는 속도를 못 읽으므로 상수로 쓰지 않고 walk:run ≈ 1:2.5 비율로만 쓴다** |
| 12 | spread→픽셀 환산표(공중 +10°, 착지 +7°, 밴달 0.25°/1.0°, min error 0.30°=4px) | **전부 폐기** | 인용된 위키가 접근 불가(402)였고 어떤 2차 출처로도 재현되지 않았다. **구현 상수로 쓰지 않는다** |
| 13 | "절대 픽셀" vs "도(degree)를 화면폭에 비례 투영" vs "+4는 스케일 안 함" | **전부 픽셀로 통일** | 세 주장이 서로 모순. `bScaleToResolution:false`가 기본이라는 S급 근거를 살려 **모든 수치를 절대 픽셀**로 두고, 해상도 스케일은 발로란트처럼 **단일 토글**로만 제공 |
| 14 | 오차 배율·투명도 "소수 3자리" | **정밀도 제약 없음** | 파서는 그냥 float. 실세이브에 `0.30000001192092896` 같은 float32 잔차가 그대로 있다. 3자리는 **출력 관례**일 뿐 |
| 15 | 프리셋 2·3번 `#7FFF00`/`#DFFF00` vs `#BBFF00`/`#D6E305` | **#7FFF00 / #DFFF00** | 반대 2건은 스포이드 샘플링 흔적(같은 소스가 Green을 `(3,255,0)`으로 적음). 특히 ValorantCC의 `DefaultColors[]`는 "게임이 프리셋과 정확히 일치하는 값을 덮어쓰는 버그"를 피하려고 만든 배열이라 1비트라도 틀리면 동작하지 않는다 → 결정적 |
| 16 | genesy 구현체 vs 코퍼스 통계 (6개 항목 충돌) | **코퍼스 + 게임 JSON** | genesy는 `b` 키 누락, `f`/`s`를 루트로 모델링, 스나이퍼 기본값 0.8/`FF0000`(실제 0.75/`FFFFFFFF`), 루트 `s`→`p` 순서, 라인 `f/e`→`m/s` 순서, `v`를 `g` 켜졌을 때만 출력 — **6건 모두 genesy가 틀렸다.** 가장 널리 인용되는 레퍼런스라 그대로 베끼면 값을 잃는다 |
| 17 | "코드 키 43개가 전부다" | **공유 코드 기준으로는 맞다. 세이브 스키마 기준으로는 틀리다** | 세이브에는 `focusMode` 레이어 전체, `bUsePrimaryCrosshairForFocusMode`, `bScaleToResolution`, `bHideCrosshair`, `bTouchCrosshairHighlight*`, `outlineColor`가 더 있다. **파서는 미지 키뿐 아니라 미지 섹션 마커에도 안전해야 한다**(§2.6) |
| 18 | `+4`(min error)의 게이트 = `0f`(발사 오차)? | **`0f` 게이트 채택, 단 근사임을 명시** | 커뮤니티 렌더러 전부가 `0f`로 게이팅한다. 게임의 실제 게이트는 라인 레벨 `bShowMinError`일 가능성이 높다(변형 레벨 `bFixMinErrorAcrossWeapons`와 짝) → §6 |
| 19 | `Fade Crosshair With Firing Error` = 위쪽 팔 완전 숨김? | **알파 페이드** | VCRDB의 이진 숨김은 정적 프리뷰의 근사. 커뮤니티 원문은 "top half … **fade**"이고 "사격을 멈추면 돌아온다" → 연속량. 이진은 대안으로만 둔다 → §6 |

### 0.4 발로란트 조준점 = 절대 픽셀

`bScaleToResolution` 기본값이 **false**라는 것이 실덤프로 확인됐다. 즉 발로란트의 조준점
수치는 해상도와 무관한 **순수 스크린 픽셀**이다. 1080p에서 길이 6이면 1440p에서도 6px다.

**Sherbet도 기본은 백버퍼 픽셀 그대로 그린다.** ImGui의 DPI 스케일/`FramebufferScale`을
조준점에 적용하면 안 된다. 해상도 스케일은 발로란트와 동일하게 **끄기 기본인 별도 토글**로만
제공한다.

---

## 1. 파라미터 표

발로란트 메뉴 구조 그대로 묶었다. 현재 인게임 구조는 **4탭**이고, Inner/Outer Lines는
탭이 아니라 Primary 탭 **안의 섹션**이다(블로그들이 흔히 쓰는 "General/Inner/Outer 3섹션"은
구버전이다).

```
Settings → Crosshair
├─ General             프로필 전역 토글
├─ Primary             ┐ Crosshair / Inner Lines / Outer Lines
├─ Aim Down Sights     ┘ 같은 3섹션 구조 반복
└─ Sniper Scope        중앙 점만
```

`Use Advanced Options`가 꺼져 있으면 ADS / Sniper Scope 탭이 잠긴다. 프로필은 최대 15개.

**`min`/`max`는 VCRDB 디코더 맵 원문**(바이트 단위 재추출·검증 완료)**이고, 인게임 슬라이더
한계와 100% 동일한지는 미검증이다**(§6). `기본값`은 게임 세이브 JSON과 일치한다.

### 1.1 General (코드: 섹션 마커 없이 맨 앞)

| UI 라벨 | 코드 키 | Riot JSON 필드 | 타입 | 범위 | step | 기본값 | 설명 |
|---|---|---|---|---|---|---|---|
| Copy Primary Crosshair (ADS) | `p` | `bUsePrimaryCrosshairForADS` | bool | 0/1 | — | **true** | ADS가 Primary를 그대로 씀 |
| Override All Primary Crosshairs… | `c` | `bUseCustomCrosshairOnAllPrimary` | bool | 0/1 | — | false | 무기별 조준점을 내 Primary로 통일 |
| Use Advanced Options | `s` | `bUseAdvancedOptions` | bool | 0/1 | — | false | ADS/Sniper 탭 잠금 해제 |

> ⚠️ **함정:** UI상 General 탭에 있는 `Fade Crosshair With Firing Error`와
> `Show Spectated Player's Crosshair`는 **코드에서는 `P:`/`A:` 섹션 안**에 들어간다(레이어별
> 필드다). 코퍼스에서 `A.f`/`A.s`가 독립적으로 등장해 확인됐다.

### 1.2 Primary / ADS — "Crosshair" 섹션

| UI 라벨 | 코드 키 | Riot JSON 필드 | 타입 | 범위 | step | 기본값 | 설명 |
|---|---|---|---|---|---|---|---|
| Crosshair Color | `c` | (코드 전용 인덱스) | int | 0–8 | 1 | 0 (White) | 8 = 커스텀 스와치 선택 |
| Custom Color | `u` | `colorCustom` | hex RRGGBBAA | — | — | `FFFFFFFF` | 저장된 커스텀 색 |
| (커스텀 색 사용) | `b` | `bUseCustomColor` | bool | 0/1 | — | false | **실제 적용 여부 플래그** |
| Outlines | `h` | `bHasOutline` | bool | 0/1 | — | **true** | |
| Outline Thickness | `t` | `outlineThickness` | int | **1**–6 | 1 | 1 | **min이 0이 아니라 1** |
| Outline Opacity | `o` | `outlineOpacity` | float | 0–1 | 0.01 | **0.5** | |
| Center Dot | `d` | `bDisplayCenterDot` | bool | 0/1 | — | false | |
| Center Dot Thickness | `z` | `centerDotSize` | int | **1**–6 | 1 | 2 | 정사각형 **한 변**의 길이 |
| Center Dot Opacity | `a` | `centerDotOpacity` | float | 0–1 | 0.01 | 1 | |
| Fade Crosshair With Firing Error | `f` | `bFadeCrosshairWithFiringError` | bool | 0/1 | — | **true** | 사격 중 **위쪽 팔** 페이드 |
| Show Spectated Player's Crosshair | `s` | `bShowSpectatedPlayerCrosshair` | bool | 0/1 | — | **true** | Sherbet 무관 |
| Override Firing Error Offset With Crosshair Offset | `m` | `bFixMinErrorAcrossWeapons` | bool | 0/1 | — | false | 켜면 휴지 상태 **+4px 제거** |
| Disable Crosshair | *(코드 키 없음)* | `bHideCrosshair` | bool | — | — | false | 코드로 전달되지 않음 |

**Riot JSON의 `color`/`colorCustom`은 인덱스가 아니라 `{r,g,b,a}` 구조체다.** 0–8 인덱스는
공유 코드 포맷에만 존재한다. 즉 코드 파싱 시 인덱스→RGB 변환은 구현자가 해야 하고,
설정 파일만 보고 "몇 번 프리셋"인지는 복원할 수 없다.

### 1.3 Inner Lines (접두사 `0`) / Outer Lines (접두사 `1`)

**inner와 outer의 max가 다르다.** 이걸 놓치면 발로란트 유저가 바로 알아챈다.

| UI 라벨 | Inner | Outer | Riot JSON 필드 | 타입 | Inner 범위 | Outer 범위 | step | Inner 기본 | Outer 기본 |
|---|---|---|---|---|---|---|---|---|---|
| Show Lines | `0b` | `1b` | `bShowLines` | bool | 0/1 | 0/1 | — | **true** | **true** |
| Line Thickness | `0t` | `1t` | `lineThickness` | int | 0–10 | 0–10 | 1 | 2 | 2 |
| Line Length | `0l` | `1l` | `lineLength` | int | **0–20** | **0–10** ⚠️ | 1 | 6 | 2 |
| Line Length (Vertical) | `0v` | `1v` | `lineLengthVertical` | int | 0–20 | **0–20** ⚠️ | 1 | 6 | 2 |
| Length Is Not Linked | `0g` | `1g` | `bAllowVertScaling` | bool | 0/1 | 0/1 | — | false | false |
| Line Offset | `0o` | `1o` | `lineOffset` | int | **0–20** | **0–40** ⚠️ | 1 | 3 | 10 |
| Line Opacity | `0a` | `1a` | `opacity` | float | 0–1 | 0–1 | 0.01 | **0.8** | **0.35** |
| Movement Error | `0m` | `1m` | `bShowMovementError` | bool | 0/1 | 0/1 | — | **false** | **true** |
| Movement Error Multiplier | `0s` | `1s` | `movementErrorScale` | float | 0–3 | 0–3 | 0.01 | 1 | 1 |
| Firing Error | `0f` | `1f` | `bShowShootingError` | bool | 0/1 | 0/1 | — | **true** | **true** |
| Firing Error Multiplier | `0e` | `1e` | `firingErrorScale` | float | 0–3 | 0–3 | 0.01 | 1 | 1 |
| *(UI 미노출)* | — | — | `bShowMinError` | bool | — | — | — | **true** | **true** |

> **범위 논쟁 (§0.3 #2):** outer는 길이 최대 10인데 오프셋은 최대 40까지 벌어진다. inner는
> 정반대로 길이 20 / 오프셋 20. 검증 측 코퍼스(174개)에서 `1l` 최대 5, `1o` 최대 7이라
> 상한 근처 데이터가 없어 **재현되지 않았다.** 그러나 VCRDB 디코더 맵 원문에 그렇게 박혀
> 있고 다른 코퍼스에서 `1o;40`이 관측됐다. **UI 슬라이더는 10/40으로 두되, import 시에는
> 범위 밖 값도 받아들인다.** outer length 상한은 §6 실물 확인 항목.

> `bShowMinError`는 Riot JSON에만 있고 **공유 코드 키가 없다.** 실유저 프로필 17개 전부
> `true`였다. Sherbet에서는 노출하지 않는다.

### 1.4 Sniper Scope (섹션 `S`)

여기만 **정수가 아니라 float**이다. 실수하기 딱 좋은 부분.

| UI 라벨 | 코드 키 | Riot JSON 필드 | 타입 | 범위 | step | 기본값 |
|---|---|---|---|---|---|---|
| Center Dot | `d` | `bDisplayCenterDot` | bool | 0/1 | — | **true** |
| Center Dot Color | `c` | `centerDotColor` | int(코드 전용) | 0–8 | 1 | **7 (Red)** |
| (커스텀 색 사용) | `b` | `bUseCustomCenterDotColor` | bool | 0/1 | — | false |
| Custom Color | `t` ⚠️ | `centerDotColorCustom` | hex RRGGBBAA | — | — | `FFFFFFFF` |
| Center Dot Thickness | `s` | `centerDotSize` | **float** ⚠️ | **0–4** | 0.01 | 1 |
| Center Dot Opacity | `o` | `centerDotOpacity` | float | 0–1 | 0.01 | **0.75** |

실유저 ini에 `CrosshairSniperCenterDotSize=0.400000`, `=1.0798875`, 실코드에 `S;s;2.204`,
`S;s;0.628` → **확실히 연속 float**이다. 커스텀 색 키가 `u`가 아니라 **`t`**인 것도 주의.

### 1.5 프리셋 색 8종

`colorNames` 배열 원문: `["White","Green","Yellow Green","Green Yellow","Yellow","Cyan","Pink","Red","Custom"]`

| 인덱스 | 이름 | HEX | RGB | ImGui |
|---|---|---|---|---|
| 0 | White | `#FFFFFF` | 255,255,255 | `IM_COL32(255,255,255,a)` |
| 1 | Green | `#00FF00` | 0,255,0 | `IM_COL32(0,255,0,a)` |
| 2 | Yellow Green | `#7FFF00` | 127,255,0 | `IM_COL32(127,255,0,a)` |
| 3 | Green Yellow | `#DFFF00` | 223,255,0 | `IM_COL32(223,255,0,a)` |
| 4 | Yellow | `#FFFF00` | 255,255,0 | `IM_COL32(255,255,0,a)` |
| 5 | Cyan | `#00FFFF` | 0,255,255 | `IM_COL32(0,255,255,a)` |
| 6 | Pink | `#FF00FF` | 255,0,255 | `IM_COL32(255,0,255,a)` |
| 7 | Red | `#FF0000` | 255,0,0 | `IM_COL32(255,0,0,a)` |
| 8 | Custom | 유저 입력 | — | `u` / `S:t` |

인덱스 3은 `#DFFF00`이지 `#ADFF2F`가 **아니다** — 여기서 틀리는 파서가 많다.
윤곽선 색은 **검정 고정**(공유 코드에 윤곽선 색 키가 없으므로 import되는 코드는 항상 검정,
§0.3 #9).

### 1.6 기존 Sherbet 조준점 → 발로란트 사양 매핑

| 기존 Sherbet | 발로란트 대응 |
|---|---|
| 점 | `Center Dot` — **원이 아니라 정사각형** + 크기 1–6 + 별도 투명도 |
| 십자 | Inner Lines + Outer Lines **2계층**으로 분리 |
| 원 | 발로란트에 없음 → 「클래식」 모드로 유지 |
| 이미지 | 발로란트에 없음 → 「클래식」 모드로 유지 |
| 크기 | Length(가로) + Vertical Length(세로) + Thickness 로 분해 |
| 두께 | Line / Outline / Center Dot 3종으로 분리 |
| 간격 | Line Offset (inner 0–20 / outer 0–40) |
| 투명도 | Line / Outline / Center Dot 3종으로 분리 |
| 색 | 프리셋 8종 + Custom HEX |
| — | Outlines, Movement/Firing Error ×2 + 배율 ×2, 세로 길이 링크 해제, 공유 코드 |

기존 `_sherbet_crosshair_*` 멤버는 **삭제하지 말고 「클래식」 모드로 남긴다.** 이미지 조준점을
쓰던 기존 구매자의 설정이 깨지면 안 된다. ini 키(`SHERBET/Crosshair*`)도 그대로 두고,
새 설정은 `SHERBET/Val*` 접두로 따로 저장한다.

---

## 2. 공유 코드 문법

> 이 절만으로 파서와 제너레이터를 **양쪽 다** 구현할 수 있어야 한다. 가장 값어치 있는 절이므로
> 빠짐없이 적었다.

도입: **패치 4.05 (2022-03-22)**, Settings → Crosshair → 우상단 Import/Export Profile Code.

### 2.1 전체 문법

```
code    := VERSION ( ';' rootKV )* ( ';' 'P' ( ';' kv )* )?
                                   ( ';' 'A' ( ';' kv )* )?
                                   ( ';' 'S' ( ';' kv )* )?
VERSION := '0'
kv      := KEY ';' VALUE
```

- 구분자는 **세미콜론 하나**. 공백·개행 일절 없음.
- **토큰 스트림**이다. 앞에서부터 읽다가 토큰이 섹션 마커면 섹션을 바꾸고, 아니면 `(키, 값)`
  2개를 소비한다.
- 섹션 마커는 **1글자 대문자**. 키는 1~2글자 소문자(라인 키는 숫자 접두 + 소문자).
- **맨 앞 `0`은 고정 접두 토큰이다.** 프로필 인덱스가 아니다(§0.3 #7). 세이브 JSON은
  `{"currentProfile":N,"profiles":[…]}`로 인덱스를 따로 갖고, 유통 코드 208개 + 174개
  전부 `0`으로 시작한다. **출력은 `0` 고정, 파싱 시 무시.**
- 게임이 쓰는 정규식 형태: `^0[a-zA-Z0-9;.]*$`
- **완전 기본 프로필의 코드는 `0` 한 글자다.**

### 2.2 섹션 마커

| 마커 | 의미 | 세이브 필드 |
|---|---|---|
| (없음) | 루트 / General | 프로필 레벨 |
| `P` | Primary — 평상시 | `primary` |
| `A` | Aim Down Sights | `aDS` |
| `S` | Sniper Scope | `sniper` |

- `A ⇒ 루트 p;0` 은 성립한다(코퍼스 27/27, 재검증 20/20).
- **역은 성립하지 않는다.** `p;0`이 있는데 `A` 섹션이 없는 코드가 4건 존재한다(§0.3 검증측
  정정). 이 경우 ADS는 **Primary 복사도 아니고 A 값도 아닌 전(全) 기본값**이다.
- `focusMode` 레이어(세이브에 primary/aDS/sniper와 나란히 존재)에 대응하는 마커는
  코퍼스에 **0건**이다. 아직 인코딩되지 않거나, 마커가 있는데 코퍼스가 오래된 것 — 판별 불가.
  **파서는 미지 마커에 안전해야 한다**(§2.6).

### 2.3 키 → 설정 매핑 전표 (총 43개)

키 하나가 섹션마다 완전히 다른 뜻인 경우가 있다. **섹션·접두사 없이 키 문자만 보고 파싱하면
반드시 틀린다.**

| 키 | 루트 | `P` / `A` 섹션 | 라인 그룹(`0`/`1` 접두) | `S` 섹션 |
|---|---|---|---|---|
| `p` | bUsePrimaryCrosshairForADS | — | — | — |
| `c` | bUseCustomCrosshairOnAllPrimary | 색 프리셋 인덱스 | — | 중앙점 색 인덱스 |
| `s` | bUseAdvancedOptions | bShowSpectatedPlayerCrosshair | **movementErrorScale** | **centerDotSize** |
| `t` | — | **outlineThickness** | **lineThickness** | **커스텀 색 HEX** |
| `u` | — | colorCustom (HEX) | — | — |
| `b` | — | bUseCustomColor | **bShowLines** | bUseCustomCenterDotColor |
| `h` | — | bHasOutline | — | — |
| `o` | — | **outlineOpacity** | **lineOffset** | **centerDotOpacity** |
| `d` | — | bDisplayCenterDot | — | bDisplayCenterDot |
| `z` | — | centerDotSize | — | — |
| `a` | — | **centerDotOpacity** | **opacity (라인)** | — |
| `f` | — | bFadeCrosshairWithFiringError | bShowShootingError | — |
| `m` | — | bFixMinErrorAcrossWeapons | bShowMovementError | — |
| `l` | — | — | lineLength | — |
| `v` | — | — | lineLengthVertical | — |
| `g` | — | — | bAllowVertScaling | — |
| `e` | — | — | firingErrorScale | — |

**숫자 접두사: `0` = Inner Lines, `1` = Outer Lines.**
`0l`은 **두 글자 통짜 키**다. `"0"` + `"l"`로 쪼개서 "섹션 0의 키 l"로 읽으면 안쪽선 값이
전부 조용히 사라진다(RazorReaper 소스에 이 버그를 고친 흔적이 남아 있다).

### 2.4 기본값 생략 규칙 — 가장 중요한 규칙

> **값이 기본값과 같은 항목은 코드에서 완전히 생략된다.**

208개 + 174개 두 독립 코퍼스 전수 검사 결과:

```
불리언 키는 전부 "비-기본값 한 종류로만" 등장한다
  ROOT.p (기본 true)  → '0'만 31회      ROOT.c (false) → '1'만 14회
  ROOT.s (false)      → '1'만 119회      P.h    (true)  → '0'만 122회
  P.d    (false)      → '1'만 76회       P.b    (false) → '1'만 23회
  P.f    (true)       → '0'만 134회      P.s    (true)  → '0'만 28회
  P.m    (false)      → '1'만 28회       0b/1b  (true)  → '0'만 38/176회
  0g/1g  (false)      → '1'만 23/11회    0m (false)→'1'만 2회, 1m (true)→'0'만 54회
  0f/1f  (true)       → '0'만 191/51회   S.d (true)→'0'만 5회, S.b (false)→'1'만 5회

숫자 키의 기본값(0l;6, 0v;6, 0o;3, 0a;0.8, 1l;2, 1v;2, 1o;10, 1a;0.35,
0t;2, 1t;2, t;1, o;0.5, z;2, a;1, 0s/1s;1, 0e/1e;1, S.c;7, S.s;1, S.o;0.75)
→ 등장 횟수 0회
```

**단, 파서는 관대하다.** 기본값을 전부 명시한 코드도 실제로 유통되고 게임이 받아들인다.
→ **읽기는 관대하게, 쓰기는 기본값 생략**이 올바른 전략이다.

#### 붕괴(collapse) 규칙

| 조건 | 출력 |
|---|---|
| `bShowLines == false` | `<p>b;0` **하나만**. 나머지 라인 키 일절 출력 안 함 |
| `bHasOutline == false` | `h;0` 만. `t`/`o` 출력 안 함 |
| `bDisplayCenterDot == false` | `d`/`z`/`a` 전부 출력 안 함 |

`0b;0` 뒤에 `0t`/`0l`/`0o`가 붙은 코드는 두 코퍼스 통틀어 **단 하나도 없다**(위반 0건).
`h`/`d` 붕괴는 코퍼스와 모순이 없으나 독립 확인은 못 했다 — 파싱에는 영향이 없고
바이트 동일 재현에만 관계된다.

#### `<p>v` / `<p>g` 규칙 — 여기서 대부분의 파서가 틀린다

- `<p>g;1` = "가로/세로 길이 링크 해제" 토글. 기본 false → **true일 때만 출력.**
- `<p>v`(세로 길이)는 **`g`와 무관하게, 저장값이 기본값과 다르면 항상 출력된다.**
  → `0g` 없이 `0v;5`만 있는 코드가 실제로 존재한다(링크 상태라 렌더링에는 안 쓰이지만
  저장값이 남아 있는 것). 코퍼스 실측: (v있음·g없음) **27건**, (v있음·g있음) 12건,
  (v없음·g있음) 1건.
- **렌더링 시:** `g==1`이면 세로 팔 길이 = `v`, `g==0`이면 세로 팔 길이 = `l`.

genesy와 ruwiss — 가장 널리 인용되는 레퍼런스 2종 — 이 둘 다 `v`를 `g`가 켜졌을 때만
출력해서 값을 잃는다. 이 규칙 하나를 고치자 왕복 재현이 169/209 → 202/209로 뛰었다.

### 2.5 값 포맷

| 타입 | 포맷 | 예 |
|---|---|---|
| bool | `0` / `1` | `h;0` |
| int | 소수점 없이 | `0l;4`, `0t;10` |
| float | **소수점 최대 3자리, 뒤 0 제거** | `1`, `0.5`, `0.802`, `0.053` |
| hex | **대문자 8자리 RRGGBBAA** | `u;A020F0FF` |

`1.0`이나 `0.500`은 게임이 출력하지 않는다. 코퍼스의 `1a;1.0`은 웹사이트가 재포맷한 것이다.
**단 이건 출력 관례일 뿐 파싱 제약이 아니다** — 게임 세이브에는 `0.30000001192092896` 같은
float32 잔차가 그대로 들어 있고 파서는 그냥 `parseFloat`이다(§0.3 #14).

### 2.6 파싱 알고리즘

```
1) trim.
2) [0-9A-Za-z;.] 이외 문자가 하나라도 있으면 → 코드 전체 거부.
3) ';' 로 split. tokens[0] != "0" 이면 거부. tokens[0] 은 값으로 쓰지 않는다.
4) section = ROOT; i = 1
   while i < n:
       tok = tokens[i]
       if tok.size() == 1 && isupper(tok[0]):        // ← 미지 마커 안전장치
           section = (tok=="P") ? PRIMARY :
                     (tok=="A") ? ADS     :
                     (tok=="S") ? SNIPER  : UNKNOWN
           i += 1; continue
       if i + 1 >= n:  → dangling key → 코드 전체 거부
       key = tokens[i]; val = tokens[i+1]; i += 2
       apply(section, key, val)
5) 항상 "모든 값 = 기본값"에서 시작해 코드가 명시한 것만 덮어쓴다.
```

**미지 마커 안전장치가 핵심이다.** "1글자 대문자 = 섹션 마커"로 두면 `focusMode`용 마커(`F`?)나
미래 패치의 새 섹션이 나와도 그 뒤 전체가 밀리지 않는다. 키는 전부 소문자 또는 숫자 접두이고
HEX 값(대문자)은 **키 자리에서 검사되지 않으므로**(키+값을 원자적으로 소비) 충돌하지 않는다.

#### 값 해석

**키의 선언된 타입으로 해석한다.** VCRDB는 "길이 6 또는 8이고 `^[0-9A-F]{6,8}$`면 색, 아니면
float"이라는 값 모양 휴리스틱을 쓰는데, 우리는 §2.3의 전표로 타입을 이미 알고 있으므로 그럴
필요가 없다. 모양 휴리스틱은 **미지 키의 값을 보관할 때만** 폴백으로 쓴다.

| 상황 | 처리 |
|---|---|
| int 필드에 소수 (`0l;4.5`) | **그 키만 무시**(기본값 유지). 코드 전체는 살린다 |
| 범위 밖 값 (`1o;60`) | **클램프하지 않고 그대로 수용.** UI 슬라이더는 §1 범위, 텍스트 입력으로 초과 가능. 안전 상한(200px)에서만 잘라낸다 |
| 6자리 HEX (`u;FFFFFF`) | 뒤에 `FF`를 붙여 `FFFFFFFF`로 (RRGGBB → RRGGBBAA). §6 확인 필요 |
| 미지 키 | 값 소비 후 계속. 경고 카운트만 올린다 |
| float 변환 실패 | 그 키만 기본값 유지 |
| 앞뒤 공백·개행 | trim 후 진행 (게임은 거부하지만 우리는 관대하게) |

**부분 적용은 없다.** 전체 거부 사유(불법 문자 / 잘못된 접두 토큰 / dangling key)에 걸리면
아무것도 바꾸지 않는다. 게임도 동일하다 — 커뮤니티 표준 처방이 "코드가 숫자로 끝날 때까지
백스페이스 누르고 다시 시도"인 이유다(코드는 항상 숫자로 끝난다).

#### 파싱 후처리

```
effectiveADS    = (adsCopiesPrimary || !advancedOptions) ? primary : ads
effectiveSniper = advancedOptions ? sniper : sniperDefaults
```

**파싱된 원본 값은 파괴하지 않는다.** 위 두 줄은 *사용 시점*의 계산이다. `advancedOptions`가
꺼져 있다고 파싱 단계에서 `ads = primary`를 대입해버리면, 유저가 고급 옵션을 다시 켰을 때
원래 ADS 설정이 날아가고 export도 원본과 달라진다.

### 2.7 인코딩 — 정규 출력 순서

```
0
[ p;0 ] [ c;1 ] [ s;1 ]                       ← 루트
P
  c  u  h  t  o  d  b  z  a  f  s  m          ← 스칼라
  0b   |   0t 0l 0v 0g 0o 0a 0m 0f 0s 0e      ← Inner (b는 붕괴 시 단독)
  1b   |   1t 1l 1v 1g 1o 1a 1m 1f 1s 1e      ← Outer
[ A  … P와 동일 … ]
[ S  d  b  c  t  s  o ]
```

**순서 근거의 강도는 항목마다 다르다. 정직하게 구분한다:**

| 구간 | 근거 |
|---|---|
| 루트 `p → c → s` | **확증.** p<c 2건, p<s 20건, c<s 7건, 역방향 0건. (genesy가 `s`를 `p`보다 먼저 내보내는 건 틀렸다) |
| 스칼라 `c<t`, `t<o`, `t<d`, `t<f`, `u<b`, `h<b`, `d<b`, `b<z`, `f<s`, `f<m` | **확증** (각 1~13건, 역방향 0건) |
| 스칼라 `s` vs `m`, `t` vs `u`/`h` | **증거 0건.** 위상정렬이 임의로 배치한 것. 위 순서는 유효해가 여럿인 것 중 하나다 |
| 라인 `1m < 1f` | **확증** 37건 |
| 라인 `0m`/`0f`/`0s`/`0e` 상호 순서 | **사실상 미검증** (`0m` 등장 1회). 인코더 코드를 공유할 가능성이 높아 바깥선 순서를 그대로 쓴다 |
| `S` 섹션 `b<c<t<s<o` | **확증** (b<c 2, c<t 3, t<s 1, s<o 27, 역 0). `d`는 붕괴 단독이라 위치가 논리적 추정 |

순서가 틀려도 **파싱에는 아무 영향이 없다.** 게임 export와 바이트 단위로 같은 문자열을
내려는 경우에만 문제가 된다.

#### 인코딩 규칙

```
1) 기본값과 같은 항목은 출력하지 않는다.
2) 붕괴 규칙(§2.4) 적용: b;0 / h;0 / d 꺼짐.
3) 커스텀 색 사용 중이면  c;8 + u;HEX + b;1  셋을 모두 출력.
   커스텀 색이 저장돼 있으나 미사용이면  u;HEX 만 (기본 FFFFFFFF가 아닐 때).
4) <p>v 는 g 와 무관하게 비-기본값이면 출력.
5) float 는 %.3f 후 trailing zero 제거, "1.000" → "1".
6) HEX 는 대문자 8자리.
7) 아무것도 출력할 게 없으면 결과는 "0".
8) (선택) 파싱 때 보관해 둔 미지 키/미지 섹션을 해당 섹션 끝에 원문 그대로 재출력.
```

8번을 넣으면 미래 패치에서 키가 늘어도 Sherbet을 거쳐간 코드가 값을 잃지 않는다. 비용이
거의 없으므로 권장한다.

**Sherbet은 렌더링하지 않는 `A`/`S` 섹션도 파싱해서 보관하고 export 때 그대로 다시 내보낸다.**
FiveM에는 ADS/스나이퍼 개념이 없어 그리지는 않지만, 유저가 발로란트 코드를 가져와 Primary만
손보고 다시 내보낼 때 ADS/스나이퍼 설정이 사라지면 안 된다.

### 2.8 커스텀 색 — 3개 키가 한 세트

```
c;8            "커스텀 스와치가 선택됨"
u;RRGGBBAA     저장된 커스텀 색 (colorCustom)
b;1            bUseCustomColor — 실제로 커스텀 색을 쓸지 결정
```

**함정: `u`만 있고 `b;1`이 없는 코드가 아주 많다** (코퍼스에서 41건 실측:
`0;s;1;P;c;7;u;FF0000FF;h;0;…`, `0;P;u;A020F0FF;o;0.298;…`). 이건 "커스텀 색은 저장돼
있지만 지금은 프리셋을 쓰는 중"이라는 뜻이다. `colorCustom`도 기본값(`FFFFFFFF`)이 아니면
생략 규칙에 따라 출력되기 때문이다.

**적용 판정:**
```cpp
bool use_custom = (colorIdx == 8) || useCustomColorFlag;   // c;8 또는 b;1
```
코퍼스에서 `b;1`이 있는데 `c;8`이 없는 코드는 0건, `c;8`과 `b;1`은 항상 동반 출현했다.
그래도 **두 경로를 다 처리해야 한다** — 게임 세이브에는 팔레트 인덱스 자체가 없고
`bUseCustomColor` + `colorCustom`(RGBA)만 있으므로, 외부 툴이 만든 코드는 `b;1`만 가질 수 있다.

**알파:** 관측된 모든 `u` 값이 `FF`로 끝난다(게임 컬러픽커가 RGB만 노출). 렌더러들은
`slice(0,6)`으로 알파를 버리지만 **게임이 버린다는 확인은 없다.** Sherbet은 알파를 읽어서
보관하고 export 때 되돌려 주되, **렌더링에는 쓰지 않는다**(발로란트는 `opacity` 키들로
투명도를 제어하므로 여기서 또 곱하면 두 번 어두워진다).

### 2.9 실제 코드 워크드 예제

#### 예제 A — 커스텀 색 + 세로 길이 분리 + 스나이퍼 섹션

```
0;s;1;P;c;8;u;000000FF;o;1;b;1;0t;3;0l;1;0v;0;0g;1;0o;0;0a;1;0f;0;1t;1;1l;4;1g;1;1o;0;1a;1;1m;0;1f;0;S;s;0.664;o;1
```

| 토큰 | 섹션 | 해석 |
|---|---|---|
| `0` | — | 접두 토큰. 무시 |
| `s;1` | ROOT | Use Advanced Options **ON** |
| `P` | → PRIMARY | |
| `c;8` | P | 색 = 커스텀 스와치 |
| `u;000000FF` | P | 커스텀 색 `#000000` 검정, 알파 FF |
| `o;1` | P | **Outline Opacity 1** (`o`는 P 섹션에서 윤곽선 투명도) |
| `b;1` | P | bUseCustomColor ON → 커스텀 색 실제 적용 |
| `0t;3` | P/inner | 안쪽선 두께 3 |
| `0l;1` | P/inner | 안쪽선 가로 길이 1 |
| `0v;0` | P/inner | 안쪽선 세로 길이 0 |
| `0g;1` | P/inner | 링크 해제 ON → 세로 팔 길이 = `0v` = 0 → **세로 팔 없음** |
| `0o;0` | P/inner | 안쪽선 오프셋 0 |
| `0a;1` | P/inner | 안쪽선 불투명도 1 |
| `0f;0` | P/inner | 안쪽선 **발사 오차 OFF** → +4px 없음 |
| `1t;1` | P/outer | 바깥선 두께 1 |
| `1l;4` | P/outer | 바깥선 가로 길이 4 |
| `1g;1` | P/outer | 링크 해제 ON → 세로 길이 = `1v` **미기재 → 기본 2** |
| `1o;0` | P/outer | 바깥선 오프셋 0 |
| `1a;1` | P/outer | 바깥선 불투명도 1 |
| `1m;0` | P/outer | 바깥선 이동 오차 OFF (기본 ON) |
| `1f;0` | P/outer | 바깥선 발사 오차 OFF |
| `S` | → SNIPER | |
| `s;0.664` | S | 스나이퍼 중앙점 크기 0.664 (`s`는 S 섹션에서 크기) |
| `o;1` | S | 스나이퍼 중앙점 불투명도 1 (`o`는 S 섹션에서 투명도) |

**생략되어 기본값인 것:** `h`(윤곽선 ON), `t`(윤곽선 두께 1), `d`(중앙점 OFF),
`f`(fade ON), `s`(관전자 표시 ON), `m`(override OFF), `0m`(안쪽선 이동오차 OFF),
`0s`/`0e`/`1s`/`1e`(배율 1), `1v`(세로 2), `S;c`(빨강 7), `S;d`(점 ON), `S;b`, `S;t`.

**결과:** 오프셋 0에서 시작하는 **검정** 조준점. 안쪽은 두께 3 · 가로 1px 대시 2개(세로 없음),
바깥은 두께 1 · 가로 4 / 세로 2. **윤곽선도 검정**이므로(공유 코드는 윤곽선 색을 담지 않는다)
결과적으로 검정 선 둘레에 검정 테두리가 붙어 그냥 더 두꺼운 검정 조준점이 된다 — 밝은 배경
전용 설정이다. 완전 정적. 스나이퍼에는 빨간 점.

> ⚠️ 이 코드를 "흰 윤곽선 두른 검은 십자"로 읽는 서술이 있는데 **틀렸다.** 윤곽선은 항상
> 검정이고 코드에 색 키가 없다(§0.3 #9).

#### 예제 B — ADS 섹션이 따로 있는 코드

```
0;p;0;s;1;P;c;5;h;0;d;1;z;1;f;0;m;1;0t;1;0l;2;0o;1;0a;1;0e;0.847;1b;0;A;o;1;d;1;z;3;f;0;s;0;0b;0;1b;0;S;c;0;s;0.7;o;0.7
```

| 구간 | 해석 |
|---|---|
| `p;0` | ROOT — **ADS가 Primary를 복사하지 않음** → `A` 섹션 존재 |
| `s;1` | ROOT — 고급 옵션 ON |
| `P` `c;5` | 시안 `#00FFFF` |
| `h;0` | 윤곽선 **OFF** → `t`/`o`는 붕괴 규칙으로 미출력 |
| `d;1` `z;1` | 중앙점 ON, 한 변 1px |
| `f;0` | Fade Crosshair With Firing Error **OFF** |
| `m;1` | **Override Firing Error Offset ON → 휴지 상태 +4px 제거** |
| `0t;1` `0l;2` `0o;1` `0a;1` | 안쪽선 두께1 / 길이2 / 오프셋1 / 불투명1 |
| `0e;0.847` | **안쪽선 발사 오차 배율 0.847** — `0f`가 없다는 건 발사 오차가 기본값 ON이라는 뜻. 이 유저는 오차를 켠 채 벌어짐 폭만 84.7%로 줄였다 |
| `1b;0` | 바깥선 전체 OFF (붕괴 — 뒤에 다른 `1*` 키 없음) |
| `A` `o;1` | ADS 윤곽선 불투명도 1 (`h` 미기재 → 윤곽선은 기본 ON, 두께 1) |
| `A` `d;1` `z;3` | ADS 중앙점 ON, 한 변 3px |
| `A` `f;0` `s;0` | ADS fade OFF, 관전자 표시 OFF |
| `A` `0b;0` `1b;0` | ADS 안쪽·바깥선 **둘 다 OFF** |
| `S` `c;0` `s;0.7` `o;0.7` | 스나이퍼 흰색, 크기 0.7, 불투명 0.7 |

**결과:** Primary는 윤곽선 없는 시안 십자(두께1·길이2·오프셋1) + 1px 중앙점, 발사 시
84.7% 폭으로만 벌어지고 휴지 상태 +4px는 없음. ADS는 색 미기재 → **흰색**, 선 없음 →
검은 윤곽선 두른 흰 3px 사각점 하나. `m;1` + `0e;0.847` 조합이 §4의 오차 모델을
그대로 요구한다.

#### 예제 C — 오차 4종 세트(`m`/`f`/`s`/`e`)가 전부 등장

```
0;P;h;0;0t;1;0l;4;0o;0;0a;1;0m;1;0f;0;0s;0.02;1t;3;1l;1;1o;2;1a;1;1f;0;1s;0.02
```

| 구간 | 해석 |
|---|---|
| `h;0` | 윤곽선 OFF |
| `0t;1` `0l;4` `0o;0` `0a;1` | 안쪽선 두께1 / 길이4 / **오프셋 0** / 불투명1 |
| `0m;1` | 안쪽선 **이동 오차 ON** (기본 OFF — 드문 설정, 코퍼스 2회) |
| `0f;0` | 안쪽선 발사 오차 OFF → **+4px 없음** |
| `0s;0.02` | 안쪽선 이동 오차 배율 **0.02** → 사실상 안 벌어짐 |
| `1t;3` `1l;1` `1o;2` `1a;1` | 바깥선 두께3 / 길이1 / 오프셋2 / 불투명1 |
| `1f;0` | 바깥선 발사 오차 OFF |
| `1s;0.02` | 바깥선 이동 오차 배율 0.02. **`1m` 미기재 → 이동 오차는 기본값 ON 유지** |

**결과:** 안쪽선 오프셋 0 + 발사 오차 OFF → 안쪽 팔 4개가 중앙에서 서로 맞닿는다.
**§0.3 #5(윤곽선 1-pass/2-pass)와 #6(그리기 순서)의 차이가 실제로 보이는 케이스**다 —
다만 이 코드는 `h;0`이라 윤곽선이 없어서 순서만 보인다. 윤곽선 실험용으로는
`0o;0` + `h;1` + `t;6` 조합을 만들어야 한다(§6).

이 코드가 라인 그룹의 `m → f → s → e` 출력 순서를 뒷받침하는 유일한 증거다.

#### 예제 D — 점(dot) 조준점

```
0;P;c;1;o;1;d;1;0b;0;1b;0
```

`c;1` 초록 · `o;1` 윤곽선 불투명도 1(`h` 없으므로 윤곽선은 기본 ON, 두께 1) ·
`d;1` 중앙점 ON(크기·투명도 미기재 → 기본 2 / 1) · `0b;0` `1b;0` 선 전부 OFF.
→ **검은 윤곽선이 1px 둘린 초록 2×2 정사각형 하나.** 선 없음.

#### 예제 E — 전부 기본값

```
0
```

흰색 / 윤곽선 ON(두께1, 불투명0.5) / 중앙점 OFF /
안쪽선 길이6·두께2·오프셋3·불투명0.8·이동오차OFF·발사오차ON /
바깥선 길이2·두께2·오프셋10·불투명0.35·이동오차ON·발사오차ON.
휴지 상태 실제 오프셋은 inner 3+4=**7**, outer 10+4=**14**(§3.4).
= **발로란트 신규 계정 기본 조준점.**

### 2.10 다른 파서들이 틀린 지점

| 흔한 오류 | 실제 | 발견된 구현체 |
|---|---|---|
| `0l`을 섹션 `0` + 키 `l`로 split | 두 글자 통짜 키 | RazorReaper(수정됨) |
| `<p>v`를 `<p>g` 켜졌을 때만 출력 | `g`와 무관하게 비-기본값이면 출력 | **genesy, ruwiss** |
| 프리셋 3을 `#ADFF2F` / `#D6E305`로 | `#DFFF00` | xhair 등 |
| 프리셋 팔레트 전체가 다름 | §1.5 | Be4gu(`4:'#00ffbf'`) |
| 중앙 점을 `arc()`로 원 | **정사각형** | Be4gu |
| Outline Thickness min을 0으로 | min은 **1** | genesy |
| `u`가 있으면 무조건 커스텀 색 적용 | `b;1` 또는 `c;8` 필요 | 다수 |
| 바깥선 기본값을 OFF로 | 기본 **ON** (그래서 `1b;0`이 흔하다) | 다수 |
| `1m` 기본값을 false로 | 바깥선 이동오차 기본 **true** | 다수 |
| P 섹션 `s`를 "고급 옵션"으로 | 루트 `s`만 고급 옵션 | 다수 |
| 스나이퍼 커스텀 색 키를 `u`로 | `S` 섹션은 **`t`** | 다수 |
| 스나이퍼 기본색을 흰색 / 불투명도 0.8로 | **빨강(7) / 0.75** | genesy |
| `f`(fade) 키 미처리 | 필수 | ValoAccountManager |
| offset을 2배로 계산 | 1배 | ValorantCC (WPF Margin 특성) |
| inner/outer 범위를 동일 취급 | §1.3 비대칭 | xhair |
| `0s`/`0e`/`0v`/`0g` 4개 키 누락 | 필수 | 오래된 파서 다수 |

---

## 3. 렌더링 규칙

목표는 **픽셀 단위 재현**이다. 아래 좌표식은 그대로 `ImDrawList` 호출로 옮길 수 있다.

### 3.1 좌표계

```cpp
const ImGuiViewport *vp = ImGui::GetMainViewport();
const int cx = (int)floorf(vp->Pos.x + vp->Size.x * 0.5f);
const int cy = (int)floorf(vp->Pos.y + vp->Size.y * 0.5f);
```

- 모든 좌표는 **정수 픽셀**이다. 서브픽셀 좌표를 쓰면 텍셀 블렌딩이 생겨 발로란트의
  하드 에지가 사라진다.
- **`AddRectFilled(p_min, p_max, col, 0.0f)` 만 쓴다.** rounding 0인 축 정렬 사각형은
  ImGui의 AA 경로를 타지 않으므로 `ImDrawListFlags_AntiAliased*`를 만질 필요가 없다.
  **`AddLine`/`AddCircle`은 절대 쓰지 않는다** — 좌표 중심 기준 + AA라 홀수 두께에서
  반픽셀이 생긴다. 기존 코드가 `AddLine`/`AddCircleFilled`를 쓰는 것이 지금 조준점이
  "비슷한데 다른" 두 번째 이유다.

### 3.2 팔(arm) 4개의 기하

```
t     = L.lineThickness
lenH  = L.lineLength                                    // 좌/우 팔
lenV  = L.bAllowVertScaling ? L.lineLengthVertical : L.lineLength   // 상/하 팔
par   = t & 1                                           // 홀수 두께 보정
crossY = floor(cy - t / 2.0)                            // 좌/우 팔의 y
crossX = floor(cx - t / 2.0)                            // 상/하 팔의 x
off    = §3.4 의 유효 오프셋
```

| 팔 | x | y | w | h |
|---|---|---|---|---|
| 우 | `cx + off` | `crossY` | `lenH` | `t` |
| 좌 | `cx - off - lenH - par` | `crossY` | `lenH` | `t` |
| 하 | `crossX` | `cy + off` | `t` | `lenV` |
| 상 | `crossX` | `cy - off - lenV - par` | `t` | `lenV` |

확정 사항:

1. **오프셋은 "중심에서 선의 *안쪽 끝*까지의 거리"다.** 선 중앙까지가 아니다. 배율도 없다
   (`cx + off` 그대로). offset=3이면 중심에서 3px 떨어진 지점부터 선이 시작한다.
2. **길이는 항상 중심 바깥 방향으로 자란다.** 우 팔은 `cx+off` → `cx+off+lenH`.
3. **두께는 축에 수직으로 중앙 정렬**되되 `floor()`로 스냅된다.
4. **바깥선은 안쪽선 끝 기준 상대 배치가 아니라, 똑같이 중심에서 재는 절대 오프셋**이다.
   inner/outer는 같은 함수에 다른 설정만 넣어 호출된다.
5. **`w <= 0 || h <= 0`이면 그 팔을 아예 그리지 않는다**(윤곽선도 없다). `0l;0` + `0v;5`면
   세로 팔만 있는 조준점이 된다.

### 3.3 홀수 두께 −0.5px 시프트

정수 픽셀 렌더러라 홀수 두께에서는 반드시 어딘가 비대칭이 생긴다. 채택한 규칙:

| 두께 t | 좌/우 팔 y | 상/하 팔 x | 좌·상 팔 추가 보정 | 결과 중심 |
|---|---|---|---|---|
| 짝수 | `cy - t/2` | `cx - t/2` | `par = 0` | 정확히 `(cx, cy)` |
| 홀수 | `cy - (t+1)/2` | `cx - (t+1)/2` | `par = 1` (좌·상만 1px 더) | **`(cx-0.5, cy-0.5)`** |

검산 (t=3, off=0, lenH=6, C=64):
- 우 = x∈[64, 70), 좌 = x∈[64−0−6−1, 64−0−1) = [57, 63) → 두 팔의 중점 = (64+63)/2 = **63.5**
- 상/하 팔의 x = floor(64 − 1.5) = 62, 3px → [62, 65) → 중심 **63.5** ✔ 일치

즉 **홀수 두께면 조준점 전체가 왼쪽 위로 0.5px 스냅되어 픽셀 경계에 딱 맞는다.**

> **근거 등급 주의(§0.3 #3):** 이 규칙은 VCRDB 계보(VCRDB 번들 / LilyBergonzat /
> crosshaircanvas / 중국어 클론)에서만 확인됐고, 넷은 서로 독립이 아니라 같은 테이블의
> 복사본이다. 인게임 스크린샷으로 검증된 바 없다. **그래도 채택하는 이유는** 유통 중인
> 모든 프리뷰가 이 결과를 내므로, 유저가 "코드를 붙였을 때 보던 그림"과 우리 결과가
> 일치하기 때문이다. 방향(좌·상이 밀리는지 우·하가 밀리는지)은 §6 확인 항목.

### 3.4 유효 오프셋 — 휴지 상태 `+4px` (min error)

**기존 조준점과 발로란트가 "비슷한데 다르게" 보이는 1순위 원인이 이것이다.**

```cpp
int off = L.lineOffset;
if (L.bShowShootingError && !layer.bFixMinErrorAcrossWeapons)
    off += 4;                       // ★ min error — 가만히 서 있어도 4px 더 벌어져 있다
off += (int)floorf(errPx + 0.5f);   // §4 의 동적 오차 (정수 반올림)
```

- 조건: **그 라인의 Firing Error가 켜져 있고**, 프로필의 `m`(Override Firing Error Offset
  With Crosshair Offset)이 꺼져 있을 때.
- **inner/outer가 각자 독립적으로 +4를 받는다.** 그래서 기본 조준점은 inner가 3+4=7,
  outer가 10+4=14 위치에서 시작한다.
- **+4는 발사 오차 배율(`0e`/`1e`)로 곱해지지 않는다.** 불리언(`0f`/`1f`)만 본다.
- `m;1`이면 이 4px가 사라져 "설정값 그대로" 딱 붙는다. 커뮤니티 설명과 일치한다.
- `0f;0`이면 +4도, 확장도 없다. 프로들이 정적 조준점을 쓸 때 하는 것.

> **근사임을 명시한다(§0.3 #18).** 게이트를 `0f`로 두는 건 커뮤니티 렌더러 전부의 관행이고,
> 게임의 실제 게이트는 라인 레벨 `bShowMinError`(기본 true, 코드 키 없음)일 가능성이 높다.
> 또 `bFixMinErrorAcrossWeapons`라는 필드명 자체가 **min error가 무기별로 다르다**는 직접
> 증거이므로, 무기 무관 4px 고정은 확실히 근사다. 우리는 무기를 알 수 없으므로 상수 4를 쓴다.
>
> "+4px = 0.30°"라는 도(degree) 논증은 채택하지 않는다 — 근거로 든 spread 표 전체가
> 재현되지 않았다(§0.3 #12). **4는 그냥 4px 상수**로 둔다(§0.4).

### 3.5 중앙 점

```cpp
const int n  = layer.centerDotSize;              // 1..6, 한 변의 길이
const int x0 = (int)floorf(cx - n / 2.0f);
const int y0 = (int)floorf(cy - n / 2.0f);
// AddRectFilled((x0,y0), (x0+n, y0+n), dotCol, 0.0f)
```

- **원이 아니라 정사각형이다.** 발로란트 Primary/ADS 중앙 점은 사각형이고, 크게 키우면
  티가 난다는 게 커뮤니티 정설이다. 기존 Sherbet의 `AddCircleFilled`는 여기서 틀린다.
- 값은 **반지름이 아니라 한 변의 길이**다.
- 홀짝 스냅은 팔과 동일 (`floor(c - n/2)` = `c - ceil(n/2)`, 정수 cx에서 두 식은 동일).
- **오차가 커져도 중앙 점은 움직이지 않는다.**
- 예외: **스나이퍼 중앙 점만 원**이다 — VCRDB `arc(cx, cy, 3 * size)`, ValorantCC의 WPF
  `ellipse.Width = size * 6`(지름) → 두 독립 구현이 **반지름 = 3 × centerDotSize**로 일치.
  범위 0–4 실수. Sherbet은 스나이퍼를 그리지 않으므로 참고만(§5).

### 3.6 윤곽선

캔버스 원문 `strokeRect(x − T/2, y − T/2, w + T, h + T)` + `lineWidth = T`는 정확히
**본체 사각형을 사방 T픽셀 두께로 감싸는 링**을 그린다. ImGui에는 stroke가 없으므로 링을
4조각으로 분해한다. **조각끼리 겹치지 않게 잘라야 한다** — 겹치면 모서리가 진해진다.

```cpp
// (x,y,w,h) = 본체, T = outlineThickness
static void ValOutline(ImDrawList *dl, int x, int y, int w, int h, int T, ImU32 c)
{
    if (w <= 0 || h <= 0 || T <= 0) return;
    Fill(dl, x - T, y - T, w + 2*T, T, c);   // 상 (모서리 포함)
    Fill(dl, x - T, y + h, w + 2*T, T, c);   // 하 (모서리 포함)
    Fill(dl, x - T, y,     T,       h, c);   // 좌
    Fill(dl, x + w, y,     T,       h, c);   // 우
}
```

확정 사항:

1. **본체 아래를 채우지 않는다(링만).** 링만 칠하므로 본체 알파(기본 inner 0.8)가 배경과
   직접 블렌딩된다. 확장 사각형을 통째로 검게 채우고 그 위에 0.8 알파 선을 얹으면 선이
   훨씬 탁해진다 — valoreye가 그렇게 하지만 저충실도 구현이다(§0.3 #4).
2. **선의 끝단도 감싼다.** 링이므로 4변 전부에 붙어 선이 길이 방향으로 2T만큼 길어 보인다.
3. **본체 알파와 링 알파는 곱해지지 않는다.** 각자 독립적으로 합성된다.
   ```cpp
   ImU32 fill    = (rgb & 0x00FFFFFF) | ((ImU32)(L.opacity          * 255.f) << IM_COL32_A_SHIFT);
   ImU32 outline = IM_COL32(0, 0, 0,   (int)(layer.outlineOpacity   * 255.f));
   ```
4. **색은 검정.** 공유 코드에 윤곽선 색 키가 없으므로 import되는 코드는 항상 검정이다(§0.3 #9).
5. **길이 0 또는 두께 0이면 윤곽선도 안 그린다** (`w!==0 && h!==0` 가드).
6. **팔 단위 1-pass** — 팔마다 `본체 → 자기 링` 순으로 그린다. 서로 다른 팔의 링이 겹치면
   그만큼 더 어두워진다. 기본 오프셋(inner 3+4=7)에서는 팔끼리 안 닿아 보이지 않지만,
   `0o;0` + 두꺼운 윤곽선에서는 눈에 띈다. 2-pass(링 전부 → 본체 전부)여야 한다는 주장은
   어느 소스도 뒷받침하지 못했다 → §6.

### 3.7 그리기 순서

```
1) Inner  : 우 → 좌 → 하 → 상      (가로 먼저, 세로 나중)
2) Center dot
3) Outer  : 우 → 좌 → 하 → 상
각 팔은 [본체 → 자기 링] (§3.6 #6)
```

- **가로 먼저 세로 나중, outer가 inner 위** — 4개 소스 전부 일치. **확정.**
- **중앙 점의 위치는 확정이 아니라 선택이다**(§0.3 #6). VCRDB + valoreye는
  inner→dot→outer, genesy + iNiR은 dot을 맨 아래에 둔다(2:2). VCRDB 계보를 따랐다.
- 실제로 보이는 결과:
  - outer가 나중이므로 **outer의 검은 윤곽선이 inner 선의 끝을 덮을 수 있다.** 오프셋이
    가까울 때 실제 게임에서도 나는 아티팩트다.
  - 세로가 나중이므로 오프셋 0 + 두꺼운 선에서 **세로 팔이 가로 팔 위에** 그려진다.

### 3.8 통합 지점

- `source/runtime_gui.cpp` 의 `if (_sherbet_crosshair_on)` 블록(현재 1259행 부근)을
  모드 분기로 바꾼다: **클래식**(기존 코드 그대로) / **발로란트**(신규).
- **오버레이 게이트 바깥**에서 `ImGui::GetForegroundDrawList()`에 그리는 현재 구조를 유지한다.
  렌더 파이프라인을 건드리지 않으므로 안전하다.
- `runtime.cpp`의 "아무것도 안 하면 오버레이를 그리지 않는다" 조건(961행 부근
  `&& !_sherbet_crosshair_on`)에 신규 모드 플래그도 추가해야 한다.
- 설정은 `runtime.hpp` 에 `_sherbet_val_*` 로 추가하고 ini 섹션 `SHERBET`, 키 접두 `Val`로
  저장한다. 값이 많으므로 **개별 키가 아니라 공유 코드 문자열 1개**(`ValCode`)로 저장하고
  로드 시 파싱하는 방식을 권장한다 — ini가 안 붐비고, 유저가 ini를 직접 주고받을 수도 있다.
  단 §1.3 범위 밖 값이나 미지 키 보관까지 원하면 원문 문자열을 그대로 저장해야 하므로
  이 방식이 오히려 더 정확하다.

---

## 4. 오차 애니메이션

### 4.1 발로란트의 실제 동작 (확인된 것)

1. 오차는 **선 그룹별로 독립**이다. inner/outer 각각 `bShowMovementError`,
   `bShowShootingError`와 배율 `movementErrorScale`, `firingErrorScale`(0–3)을 가진다.
2. **오프셋만 늘어난다.** 길이·두께·투명도는 변하지 않고, 4방향이 전부 똑같이 밀린다
   (콘의 반경이므로 등방). **길이를 늘리면 절대 안 된다.**
3. **둘은 덧셈으로 합산된다.** 최댓값이 아니다. 공식 위키가 "penalties will be applied
   **additively**"라고 명시한다.
4. 기본값 조합이 발로란트 특유의 "안쪽은 고정, 바깥쪽만 벌어짐" 느낌을 만든다:
   inner = movement **false** / firing **true**, outer = movement **true** / firing **true**.
5. **Movement Error는 속도의 즉시 함수다 — 시간 상수가 없다.** 사격장에서 멈추면 인디케이터가
   즉시 사라지고, deadzoning(A↔D 전환 중 속도가 0이 되는 한 틱을 노려 쏘는 기법)이 성립하는
   근거가 이 즉시성이다. 감쇠·보간이 있으면 deadzoning이 불가능하다.
   *(근거 등급 주의: 이건 관찰로부터의 추론이고 코드·문서 근거는 없다.)*
6. **Deadzone: 달리기 속도의 27.5% 미만이면 오차 0.** 패치 0.50에서 25%→30%,
   패치 3.0에서 30%→27.5%. **공식 패치노트 원문으로 확인된 값이다.**
7. **Firing Error는 샷마다 계단식으로 누적되고 Gun Recovery Time 동안 회복**한다.
   "Inaccuracy is accrued any time the weapon is re-fired prior to a complete duration of a
   weapon's respective Gun Recovery Time."

| 무기 | Gun Recovery Time | Tap Efficiency |
|---|---|---|
| 밴달 | 0.375 s | 6 |
| 팬텀 | 0.35 s | 4 |
| 불독 | 0.35 s | — |
| 가디언 | 0.35 s | — |

   **패치 0.50(2020) 기준이라 현행 값과 다를 수 있다.** Tap Efficiency의 실제 수식과
   샷당 spread 증가 곡선은 미공개다.
8. **`f`(Fade Crosshair With Firing Error)는 벌어짐이 아니라 위쪽 팔의 페이드**다. 반동으로
   실제 탄착이 위로 올라가므로 "여기는 이미 맞지 않는다"를 알려주는 연출이다. 사격을 멈추면
   돌아온다. 프로들은 대부분 끈다.

### 4.2 확인 못 한 것 — 그리고 왜 상수로 쓰지 않는가

원 조사가 제시했던 spread 환산표(밴달 1발째 0.25°/최대 1.0°, 클래식 0.4°/1.8°,
공중 +10°, 착지 직후 0.225초간 +7°, 걷기 +3.0°, 달리기 +6.0°, min error 0.30°=4px)는
**적대적 재검증에서 전부 재현되지 않았다.** 인용된 위키가 접근 불가(402)였고 어떤 2차
출처로도 확인되지 않았다. 패치노트 체인으로 실제 추적되는 값은 라이플 **walk 2.0° /
run 5.0°** 뿐이다(패치 2.02 run 3.75→5.0, 패치 3.0 walk 1.3→2.0).

**여기서 "이 값이 맞다"고 쓰면 그게 곧 틀린 조준점이 된다.** 그래서:

- **도(degree)→픽셀 투영 전체를 폐기한다.** 모든 수치를 **픽셀**로 직접 다룬다(§0.4, §0.3 #13).
- walk 2.0 / run 5.0은 상수가 아니라 **비율 walk : run ≈ 1 : 2.5** 로만 쓴다.
- Deadzone 27.5%와 Gun Recovery Time만 공식 패치노트 근거가 있으므로 기본값으로 채택한다.
- **나머지는 전부 UI 슬라이더로 노출한다.** 판매 제품 관점에서도 이쪽이 낫다 —
  FiveM은 서버마다 무기 스크립트가 달라 "정답 곡선"이 애초에 존재하지 않는다.

### 4.3 입력만으로 근사하는 모델

읽을 수 있는 것: `_input->raw_mouse_delta_x/y()`(프레임당 1회, 읽으면 리셋),
`_input->is_mouse_button_down(0/1)`, `_input->is_key_down(vk)`. 둘 다 이미 출하되어
동작이 확인된 경로다(스프레이 트레이너가 쓰고 있다).

```cpp
struct ValErrorState
{
    // ── 튜닝 파라미터 (전부 UI 노출) ─────────────────────────────
    // 이동
    int   keyF = 'W', keyB = 'S', keyL = 'A', keyR = 'D';  // 재바인딩 가능
    int   keyWalk = VK_MENU;        // 걷기 수정키 (게임마다 다름)
    float moveAccel   = 12.0f;      // 1/초. 키를 누른 뒤 최고속도까지
    float moveDecel   = 20.0f;      // 1/초. 뗀 뒤 0까지 (Valorant 감속은 빠르다)
    float deadzone    = 0.275f;     // 공식값. 이 미만이면 오차 0
    float walkErrPx   = 6.0f;       // 걷기 최대 확장(px)
    float runErrPx    = 15.0f;      // 달리기 최대 확장(px) — walk:run ≈ 1:2.5
    // 발사
    float firePerShotPx = 2.0f;     // 1발당 확장(px)
    float fireMaxPx     = 14.0f;    // 상한(px)
    float fireRateRpm   = 600.0f;   // 누르고 있을 때 가정하는 연사 속도
    float recoveryTime  = 0.375f;   // 마지막 발 이후 이 시간이 지나면 완전 회복
    float fadeDepth     = 0.85f;    // f 옵션의 최대 페이드량(0..1)

    // ── 상태 ────────────────────────────────────────────────────
    float vel = 0.0f;               // 0..1 정규화 가짜 속도
    float firePx = 0.0f;
    float sinceLastShot = 999.0f;
    float autoAccum = 0.0f;
    bool  lmbPrev = false;
};

void Update(ValErrorState &s, float dt, bool lmb, bool overlayOpen)
{
    // ── 이동: WASD → 가짜 속도. Valorant 처럼 시간 상수 없이 "속도에서 즉시" 계산한다.
    //    속도 자체에만 가감속을 두어 counter-strafe(A↔D) 시 0 을 통과하게 만든다.
    const bool anyMove = !overlayOpen &&
        (Key(s.keyF) || Key(s.keyB) || Key(s.keyL) || Key(s.keyR));
    const bool opposed = (Key(s.keyL) && Key(s.keyR)) || (Key(s.keyF) && Key(s.keyB));
    const float target = (!anyMove || opposed) ? 0.0f : (Key(s.keyWalk) ? 0.4f : 1.0f);
    const float rate   = (target > s.vel) ? s.moveAccel : s.moveDecel;
    s.vel += ImClamp(target - s.vel, -rate * dt, rate * dt);

    // ── 발사: 상승 엣지 = 1발. 누르고 있으면 fireRateRpm 으로 계속 발사한다고 가정.
    const bool edge = lmb && !s.lmbPrev; s.lmbPrev = lmb;
    int shots = edge ? 1 : 0;
    if (lmb && !overlayOpen) {
        s.autoAccum += dt * (s.fireRateRpm / 60.0f);
        while (s.autoAccum >= 1.0f) { s.autoAccum -= 1.0f; shots++; }
    } else s.autoAccum = 0.0f;

    if (shots > 0) { s.firePx = ImMin(s.firePx + s.firePerShotPx * shots, s.fireMaxPx);
                     s.sinceLastShot = 0.0f; }
    else           { s.sinceLastShot += dt; }

    // ── 회복: Gun Recovery Time 을 다 채우면 0 으로. 선형/지수 여부는 불명이라 선형.
    if (s.sinceLastShot > 0.0f && s.firePx > 0.0f)
        s.firePx = ImMax(0.0f, s.firePx - (s.fireMaxPx / s.recoveryTime) * dt);
}

// 이동 오차: 속도의 "즉시" 함수 — 보간하지 않는다.
float MoveErrPx(const ValErrorState &s)
{
    if (s.vel <= s.deadzone) return 0.0f;                       // ★ deadzone
    const float k = (s.vel - s.deadzone) / (1.0f - s.deadzone); // 0..1
    return (k < 0.5f) ? s.walkErrPx * (k / 0.5f)
                      : s.walkErrPx + (s.runErrPx - s.walkErrPx) * ((k - 0.5f) / 0.5f);
}

// 라인별 합산 — 덧셈이다(최댓값 아님).
float LineErrPx(const ValLine &L, const ValErrorState &s)
{
    float e = 0.0f;
    if (L.bShowMovementError) e += MoveErrPx(s) * L.movementErrorScale;  // 0..3
    if (L.bShowShootingError) e += s.firePx     * L.firingErrorScale;    // 0..3
    return e;
}

// f 옵션: 위쪽 팔의 알파 배수 (§0.3 #19 — 이진 숨김이 아니라 페이드)
float TopFade(const ValLayer &P, const ValErrorState &s)
{
    if (!P.bFadeCrosshairWithFiringError || s.fireMaxPx <= 0.0f) return 1.0f;
    return 1.0f - s.fadeDepth * ImClamp(s.firePx / s.fireMaxPx, 0.0f, 1.0f);
}
```

렌더 시:

```cpp
off = L.lineOffset
    + (L.bShowShootingError && !layer.bFixMinErrorAcrossWeapons ? 4 : 0)
    + (int)floorf(LineErrPx(L, s) + 0.5f);
```

**정수 반올림 때문에 확장이 1px 계단으로 움직인다.** 이는 의도한 것이다 — 서브픽셀
좌표를 쓰면 §3.1의 하드 에지가 무너진다. 발로란트도 정수 픽셀 렌더러이므로 같은 계단이
있을 것으로 보이나 확인은 못 했다.

**절대 지켜야 할 구조 3가지** — 이것만 맞으면 발로란트 유저는 "똑같다"고 느낀다:
① 배율을 0–3 범위로 노출, ② inner/outer 독립 토글, ③ **오프셋만 변하고 길이·두께는 고정.**

### 4.4 우리 근사가 눈에 띄게 다른 지점

정직하게 적는다. 아래는 전부 **의도적으로 남긴 차이**다.

| # | 차이 | 왜 생기나 | 얼마나 티가 나나 |
|---|---|---|---|
| 1 | **이동 오차가 "캐릭터 속도"가 아니라 "키 입력"에서 나온다** | 게임 메모리를 안 읽으므로 실제 속도를 모른다 | **가장 크다.** 넉백·슬로우·경사·차량 탑승·물속·앉기 감속이 전부 무시된다. 키를 안 눌러도 밀려나는 상황에서 오차가 0으로 보인다 |
| 2 | **채팅 입력 중 WASD가 이동으로 잡힌다** | 게임의 채팅/메뉴 상태를 알 수 없다 | 채팅 칠 때 조준점이 벌어진다. 오버레이가 열려 있을 때는 막지만(`_show_overlay`), 게임 내 채팅은 감지 불가 → **일시정지 핫키를 제공한다** |
| 3 | **키 바인딩이 다르면 통째로 안 먹는다** | VK 코드를 직접 읽는다 | 전부 재바인딩 가능하게 만든다. "걷기 수정키"는 게임마다 의미가 반대다(발로란트 Shift=걷기, GTA Shift=달리기) → 수정키의 의미도 토글로 |
| 4 | **가감속이 진짜 물리가 아니다** | 우리가 만든 1차 램프 | counter-strafe(A↔D) 시 속도가 0을 통과하도록 만들어 **deadzoning 감각은 재현된다.** 다만 타이밍은 게임과 다르다 |
| 5 | **무기를 모른다** | 메모리 필요 | 밴달/팬텀/셰리프/오퍼레이터가 전부 같은 곡선으로 벌어진다. 휴지 상태 +4px도 무기 무관 고정(§3.4). 발로란트에서는 무기를 바꾸면 휴지 간격 자체가 달라진다 |
| 6 | **탄창·재장전·빈 총을 모른다** | 메모리 필요 | 총알이 없는데 클릭해도 벌어진다. 재장전 중 클릭도 마찬가지 |
| 7 | **연사 판정이 가정이다** | 클릭 상승 엣지만 보인다 | 누르고 있으면 `fireRateRpm`으로 발사한다고 가정한다. 실제 연사 속도·버스트 무기(3점사)·반자동 연타는 어긋난다 |
| 8 | **회복 곡선이 선형이다** | 실제 곡선 미공개 | 발로란트가 지수 감쇠라면 회복 후반부의 체감이 다르다 |
| 9 | **fade 강도의 기준이 다르다** | 실제로 무엇에 비례하는지 미확인 | 우리는 누적 발사 오차에 비례시킨다. 발로란트가 "실제 스프레이 이탈량"을 쓴다면 타이밍이 어긋난다 |
| 10 | **FiveM 서버마다 실제 반동이 다르다** | 서버 스크립트가 무기를 정의한다 | **"정답"이 존재하지 않는다.** 그래서 §4.3의 파라미터를 전부 노출하는 것이 옳다 |

---

## 5. 재현 불가

게임 상태가 필요해서 **입력만으로는 원리적으로 못 하는 것들.** 감추지 않고 UI에도 적는다.

| 항목 | 왜 불가능한가 | 대신 무엇을 하나 |
|---|---|---|
| **캐릭터 이동 속도** | 메모리에만 존재 | WASD 근사(§4.3). **이 기능에서 가장 큰 비재현 항목이다** — 이동 오차는 *캐릭터 속도*의 함수인데 우리가 가진 건 *키 입력*과 *마우스 회전*이라 물리량 자체가 다르다 |
| **무기별 spread / min error** | 메모리 + 미공개 데이터 | 무기 무관 상수(+4px, 단일 곡선). 발로란트는 무기를 바꾸면 휴지 간격이 바뀐다 |
| **실제 탄퍼짐 콘 크기** | 메모리 | 근사 확장량 |
| **탄창·재장전·무기 교체** | 메모리 | 없음 |
| **ADS(정조준) 상태** | 메모리 | RMB(`is_mouse_button_down(1)`) 근사 토글로만 제공. GTA/FiveM은 RMB가 조준이라 대체로 맞지만 서버·차량·무기에 따라 틀린다 |
| **스나이퍼 조준경 상태** | 메모리 | 없음. `S` 섹션은 **파싱·보관·재출력만** 하고 그리지 않는다 |
| **관전 중인 플레이어**(`s` 키) | 관전 개념 없음 | 파싱·보관만 |
| **무기별 조준점 통일**(`c` 키) | 무기별 조준점 개념 없음 | 파싱·보관만 |
| **`focusMode` 레이어** | 콘솔 전용 기능, 코드 섹션 마커 미확인 | 미지 섹션으로 건너뛰고 보관(§2.6) |
| **`bHideCrosshair`** | 코드 키 자체가 없다 | 우리 자체 on/off로 대체 |
| **`bScaleToResolution`** | 인게임 UI 라벨·배율 미확인 | 자체 배율 토글(기본 OFF, §0.4) |
| **`bShowMinError`** | 코드 키 없음 | 항상 true로 취급 |
| **게임의 실제 반동으로 fade 타이밍 맞추기** | 스프레이 이탈량은 메모리 | 누적 발사 오차 비례(§4.4 #9) |
| **게임 내 채팅/메뉴 상태** | 게임 UI 상태 | 일시정지 핫키(§4.4 #2) |

> **한 줄 요약:** 조준점의 *모양*은 픽셀 단위로 재현할 수 있다. 조준점의 *움직임*은
> 근사만 가능하고, 그중에서도 **이동 오차가 가장 크게 어긋난다.**

---

## 6. 실물 확인 필요

발로란트를 한 번 켜거나 스크린샷을 한 번 확대하면 종결되는 것들만 모았다.
**여기 있는 항목을 추측으로 채우면 발로란트 유저가 즉시 알아본다.**

| # | 확인할 것 | 구체적으로 무엇을 보나 |
|---|---|---|
| 1 | **바깥선 길이 상한이 10인가 20인가** | 발로란트 Outer Lines의 Line Length 슬라이더를 끝까지 밀고 숫자를 읽는다. `1o`도 같이(40인지). 코퍼스가 상한 근처에 없어 통계로는 못 정한다(§0.3 #2) |
| 2 | **1 단위 = 화면 1픽셀인가** | 1080p에서 `0;P;h;0;0t;10;0l;20;0o;0;1b;0` 을 넣고 스크린샷을 확대해 팔의 픽셀 수를 센다. 20px·10px이 나와야 한다 |
| 3 | **홀수 두께의 −0.5px 시프트 방향** | `0t;3;0o;0;0l;6` 스크린샷을 확대해 좌 팔이 우 팔보다 1px 긴 쪽으로 밀렸는지, 반대인지 본다. 근거가 VCRDB 단일 계보뿐이다(§0.3 #3) |
| 4 | **윤곽선 겹침이 진해지는가 (1-pass vs 2-pass)** | `0o;0` + `t;6` + `o;0.5` + `0t;2`. 팔 4개의 윤곽선이 중앙에서 만나는 자리에 **더 진한 십자 이음매**가 생기면 1-pass, 균일하면 2-pass(§0.3 #5) |
| 5 | **중앙 점이 안쪽선 위인가 아래인가** | `d;1;z;6;a;0.5` + `0o;0;0t;2`. 반투명 점 아래로 안쪽선이 비쳐 보이면 dot이 위(=inner→dot), 안 보이면 dot이 아래(§0.3 #6) |
| 6 | **발사 오차를 끄면 +4px가 사라지는가** | `0f;1`(기본)과 `0f;0` 두 코드로 휴지 상태 스크린샷을 찍어 중앙~선 안쪽 끝 픽셀을 잰다. 7px vs 3px이면 커뮤니티 게이트가 맞고, 둘 다 7px이면 게이트가 `bShowMinError`다(§0.3 #18) |
| 7 | **+4px가 무기마다 다른가** | 같은 코드로 밴달·클래식·오퍼레이터를 들고 휴지 상태 간격을 잰다. 다르면 우리 상수 4는 확실히 근사다(필드명이 이미 그렇게 시사한다) |
| 8 | **`f`(fade)가 알파 페이드인가 완전 숨김인가, inner/outer 둘 다인가** | `f;1` + 안팎 둘 다 켠 상태로 연사 중 위쪽 팔을 본다. 서서히 흐려지면 페이드, 툭 사라지면 이진. 바깥 위쪽 팔도 같이 사라지는지 확인(§0.3 #19) |
| 9 | **프리셋 2·3번의 정확한 RGB** | `c;2`, `c;3`으로 놓고 인게임 스크린샷을 스포이드로 찍는다. `#7FFF00`/`#DFFF00`인지 `#BBFF00`/`#D6E305`인지 |
| 10 | **6자리 HEX(`u;FFFFFF`)를 게임이 어떻게 받나** | `u;00FF00`(6자리)을 넣어보고 초록이 나오는지, 거부되는지, 알파가 어떻게 되는지 |
| 11 | **범위 밖 값을 게임이 clamp/무시/거부 중 무엇으로 처리하나** | `1o;60`, `0l;4.5`, `0t;-1` 세 코드를 각각 넣어본다. VCRDB는 clamp하지만 게임 동작은 미검증 |
| 12 | **`0` 이외의 접두 토큰이 존재하는가** | 프로필 슬롯 3번쯤에서 export해 코드가 여전히 `0`으로 시작하는지 본다 |
| 13 | **`focusMode` 섹션 마커가 있는가** | PC 클라이언트에서 focusMode 설정을 건드린 뒤 export해 `P`/`A`/`S` 외 마커가 붙는지 본다 |
| 14 | **스나이퍼 중앙 점이 원인가, 크기가 반지름인가 지름인가** | `S;s;4;S;o;1` 스크린샷 확대. 반지름 3× 가설이면 지름 24px이 나와야 한다 |
| 15 | **오차 확장이 1px 계단인가 연속인가** | 천천히 걸으면서 조준점 확장을 녹화해 프레임별로 픽셀을 센다 |
| 16 | **신규 계정의 진짜 공장 기본값** | 특히 `bHasOutline`, `bFadeCrosshairWithFiringError`, `bShowSpectatedPlayerCrosshair`. 신뢰할 factory-default 덤프를 하나도 못 찾았다. 현재는 VCRDB 파서 기본값(전부 true)을 채택 |

---

## 7. 출처

### 게임 자체 데이터 (S급)

- https://raw.githubusercontent.com/ShidqiFaadhil/logs/812b7e2b0fe72fbde94a9b3301bd3e976b947308/ShooterGame-backup-2025.06.06-06.32.11.log — `SavedCrosshairProfileData` 원문
- https://github.com/ayvi-0001/dotfiles/blob/main/games/valorant/RiotUserSettings.ini
- https://raw.githubusercontent.com/JoShMiQueL/VALORANT-CONFIG/c72b2f2f4b8545347d43757e09168061a13e3116/RiotUserSettings.ini
- https://raw.githubusercontent.com/nova-tran/Valorant-Setting-Default/189950342883c64e5ade36b20a14484f881724e3/setting.json — ⚠️ **공장 기본값이 아니라 2022년 유저 덤프**
- https://github.com/nyrpqsqq35/valoreye/blob/master/web/src/pages/sampleprefs.ts — 검정이 아닌 `outlineColor` 사례

### 렌더러 / 파서 원문 (A급)

- https://www.vcrdb.net/builder — `crosshairMap` 디코더 테이블 + `renderCrosshair()`
  (청크 파일명은 배포마다 바뀐다. 재확인하려면 HTML에서 `/_next/static/chunks/*.js`를 다시 긁을 것)
- https://github.com/LilyBergonzat/Crosshair/blob/master/src/util/CrosshairUtil.ts — VCRDB 계보
- https://crosshaircanvas.com/valorant/crosshair-generator — VCRDB 계보
- https://github.com/akmdmc/akmdmc.github.io — VCRDB 계보 클론(독립 아님)
- https://github.com/genesy/crosshair-codes — `codegenerator.ts`, `crosshair.tsx`,
  `samplecrosshairs.ts`(코퍼스 174), `CrosshairDisplay/CrosshairCanvas.tsx`
- https://github.com/nyrpqsqq35/valoreye — `shared/types.ts`, `web/src/pages/XHair.tsx`, `canvasWrapper.ts`
- https://github.com/ruwiss/valorant-tracker/blob/main/src/utils/crosshair.ts
- https://github.com/nk521/ValorantRandomCrosshairGenerator/blob/main/valorant_crosshair.py
- https://github.com/weedeej/ValorantCC — `Processor.cs`(`DefaultColors[]`), `Crosshair_Parser.cs`, `Binder.cs`
- https://github.com/CedrickGD/RazorReaper/blob/main/RazorReaper/Services/Implementations/CrosshairCodeParsers.cs
- https://github.com/YSSF8/crosshair-y/blob/main/public/scripts/crosshair-code-parser.js
- https://github.com/john-riordan/xhair — `constants.js`, `components/Crosshair/utils.js`
- https://github.com/0xbacaba/ValoAccountManager/blob/main/src/valorant/crosshair/ProfileSettings.java
- https://github.com/Be4gu/crosshair/blob/main/lib/crosshair-renderer.ts
- https://github.com/snowarch/iNiR/blob/main/modules/ii/overlay/crosshair/CrosshairContent.qml — 저충실도
- https://github.com/xmichsenx/ValorantRandomizer/blob/main/src/lib/crosshair-generator.ts
- https://github.com/Fizm00/ValoHub/blob/main/src/utils/crosshairParser.ts
- https://github.com/meyomeyo/aimer-lite/blob/main/utils/valorantParser.ts
- https://github.com/mametaro2023/Crosshair/blob/main/crosshair_app/utils.py
- https://github.com/phenibut645/pupupu/blob/main/apps/web/src/overlay/utils/valorantCrosshair.ts
- https://github.com/faheem-s27/ValorantCrosshairParser/blob/main/Decoder.json
- https://github.com/igorwessel/valclient.js — `src/interfaces/valorant.ts`, `docs/Valorant/crossHair.md`
- https://github.com/ardaltunel/omni-tools/blob/master/tools/valorant-crosshair/script.js

### Riot 공식 (B급)

- https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-0-50/ — Gun Recovery Time, deadzone 25→30%
- https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-3-0/ — deadzone 30→27.5%, walk 1.3→2.0
- https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-4-05/ — Import/Export Profile Code 도입 (2022-03-22)
- https://playvalorant.com/en-gb/news/game-updates/valorant-patch-notes-5-04/ — 커스텀 HEX, 가로/세로 독립, `/cc`, 프로필 10→15
- https://playvalorant.com/en-us/news/game-updates/valorant-patch-notes-6-11/
- https://valorant-api.com/v1/weapons — `firstBulletAccuracy`, `fireRate` (spread 곡선은 없음)

### 코드 코퍼스 / 색상 교차 확인 (A~C급)

- https://prosettings.net/blog/best-valorant-crosshair-codes/
- https://www.thespike.gg/valorant/crosshairs/codes
- https://www.pcgamesn.com/valorant/crosshairs-best-codes
- https://www.flank.gg/valorant/crosshairs/colors
- https://crosshaircanvas.com/valorant/crosshair-color-codes
- https://www.vcrdb.net/faq

### 동작 의미 확인용 (C급 — 숫자 근거로 쓰지 않음)

- https://www.flank.gg/valorant/posts/how-to-change-crosshair-in-valorant
- https://dotesports.com/valorant/news/how-to-export-and-import-crosshair-settings-in-valorant
- https://www.theloadout.com/valorant/crosshair-settings
- https://bestgamingtips.com/what-is-firing-error-valorant/
- https://www.gamer.org/valorant-gun-mechanics-explained-accuracy-spray-movement/
- https://www.gamer.org/advanced-valorant-console-guide-aim-curves-focus-mode/
- https://alviran.net/blog/valorant-crosshair-code-not-working-fix-2026/
- https://runitback.gg/article/valorant-patch-3-0-adds-increased-walking-and-running-fire-inaccuracy-changes-to-the-effect-of-tagging-and-weapon-deadzone
- https://www.sportskeeda.com/valorant/valorant-patch-2-02-official-notes-introduces-weapon-updates-changes-competitive-mode
- https://www.oneesports.gg/valorant/how-import-valorant-crosshair-settings/
- https://www.strafe.com/articles/read/how-do-you-strafe-in-valorant/
