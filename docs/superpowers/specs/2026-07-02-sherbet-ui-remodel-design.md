# SHERBET — ReShade 전면 리모델링 설계 문서

- 작성일: 2026-07-02
- 저장소: `Jeong-Ryeol/reshade` (fork of `crosire/reshade`)
- 기준 커밋: `upstream/main` (f191dc03)
- 디자인 시안: `~/reshade-design/preview.html` (v2, 승인됨)

## 1. 목표

crosire ReShade 최신 소스를 기준으로 오버레이 UI를 **"기존 ReShade라고 알아볼 수 없을 정도"로 전면 교체**한 커스텀 제품 **SHERBET**를 만든다. 테마별로 브랜치를 분리해 **테마 1개 = 판매 상품(SKU) 1개**로 배포한다.

- 비주얼(레이아웃·색·폰트·아이콘·애니메이션)은 전면 교체
- 이펙트 컴파일·프리셋·핫키·렌더러 후킹 등 **핵심 기능 로직은 무손상**
- 이전 세션의 cutie-base/theme/* 브랜치는 참고용으로 보존하고, 완전히 새로 작성

## 2. 브랜딩

- 제품명: **Sherbet** (셔벗) — 테마들이 전부 셔벗 맛 라인업이 되는 네이밍
- 워드마크: `Sherbet <테마명>` (Gaegu 폰트), 로고: 그라디언트 라운드 사각형 + 4포인트 스파클(벡터)
- 오버레이·스플래시·OSD·About 어디에도 "ReShade" 브랜드 노출 최소화
  (단, About의 오픈소스 라이선스 고지는 접힌 섹션으로 유지 — BSD 3-Clause 의무)

## 3. 브랜치 구조 (= 판매 구조)

```
upstream/main (crosire 최신)
└─ sherbet-base          ← 스킨 엔진 + 공통 프레임워크 전체
   ├─ sherbet/mint       🌿 Mint Soda
   ├─ sherbet/peach      🧡 Peach Punch
   ├─ sherbet/pink       💗 Pink Crush
   ├─ sherbet/rainbow    🌈 Prism Pop
   ├─ sherbet/lavender   🔮 Lavender Dream
   └─ sherbet/noir       ✦ Noir Gold (프리미엄 SKU)
```

- 각 `sherbet/*` 브랜치는 `source/sherbet_theme.cpp` **단 1개 파일만** base와 다르다.
- base 개선 → 6개 테마 브랜치에 merge로 전파 (충돌 거의 없음).
- 구매자는 자기 테마 브랜치의 빌드 DLL만 받는다. mint 구매자는 pink 빌드를 가질 수 없다.
- 기존 브랜치(`cutie-base`, `theme/*`, `hwid-add`, `gradient`, `custom-*`)는 보존.

## 4. 신규 파일

| 파일 | 역할 | 브랜치별 차이 |
|------|------|----------------|
| `source/sherbet_theme.hpp` | `struct SherbetTheme` 정의 + `g_sherbet` extern | 동일 |
| `source/sherbet_theme.cpp` | 활성 테마 값 정의 | **이 파일만 다름** |
| `source/sherbet_ui.hpp/.cpp` | 스킨 엔진: 스타일 적용, 배경/파티클/글로우, 커스텀 위젯(토글·슬라이더·카드·레일), 레이아웃 헬퍼 | 동일 |
| `res/fonts/Jua-Regular.ttf` | 본문/UI 폰트 (OFL) | 동일 |
| `res/fonts/Gaegu-Bold.ttf` | 제목/워드마크 폰트 (OFL) | 동일 |

### `struct SherbetTheme` 필드 (시안 CSS 변수와 1:1)

- 배경: `bg0, bg1, bg2` (그라디언트 스톱), `panel, panel_alt, chip, border`
- 텍스트: `text, text_dim`
- 강조: `accent, accent2, glow`
- 파티클: `particle_shape`(spark/heart/leaf/petal enum), `particle_count, particle_speed`
- 애니메이션: `bg_drift_speed`, `hue_cycle`(rainbow 전용), `glow_intensity`
- 문구: `theme_display_name`("Mint Soda"), `sku_owner_name`(구매자 표기, 기본 공란), `discord_handle`, `shop_url`, `discord_url`

## 5. 레이아웃 전면 교체 (스킨 엔진)

시안 v2 그대로:

- **좌측 아이콘 레일**: 상단 로고 → 홈/애드온/설정/통계/로그/About 벡터 아이콘 → 하단 버전 칩. 기존 상단 탭바 제거.
- **헤더**: `Sherbet <테마명>` 워드마크 + 프리셋 필(드롭다운) + 원클릭 버튼 3종(스크린샷·성능 모드·리로드)
- **홈 탭**: 검색바(돋보기 아이콘) + 필터 필(전체/켜짐/즐겨찾기) + **카드형 이펙트 리스트**(토글 스위치, 이펙트명, .fx 파일명, ms 뱃지). 카드 확장 시 변수 에디터(그라디언트 슬라이더).
- **하단 상태바**: FPS 뱃지 + 활성 이펙트 수 + 리로드 버튼
- **스플래시**: 로고 마크 + "Sherbet <테마명> 로딩 중…" + 그라디언트 프로그레스 바
- **나머지 탭**(설정/통계/로그/애드온/코드에디터): 테마·라운드·간격·아이콘만 입히고 로직 무손상
- **이펙트/배경**: 오로라 블롭 드리프트(draw list 그라디언트), 벡터 파티클(테마별 도형), 포커스/호버 글로우
- **아이콘**: 이모지 사용 금지. ForkAwesome(내장) + ImDrawList 커스텀 도형만 사용.
- **튜토리얼**: 기존 4단계 로직 유지하되 스킨만 적용

## 6. 테마 6종 (SKU)

| 브랜치 | 이름 | 배경 | 강조색 | 파티클 |
|--------|------|------|--------|--------|
| sherbet/mint | Mint Soda | 딥틸 그린 | `#5df0c0` | 잎사귀 |
| sherbet/peach | Peach Punch | 웜 브라운 | `#ffab7d` | 하트 |
| sherbet/pink | Pink Crush | 다크 플럼 | `#ff7eb6` | 하트 |
| sherbet/rainbow | Prism Pop | 니어블랙 퍼플 | 휴 사이클 | 스파클 |
| sherbet/lavender | Lavender Dream | 다크 인디고 | `#b8a7ff` | 스파클 |
| sherbet/noir | Noir Gold | 블랙 | `#e8c877` | 스파클 |

정확한 팔레트 값은 시안 `preview.html`의 CSS 변수를 그대로 옮긴다.

## 7. 편의 기능 (확정 범위)

1. **OSD(FPS 배지) 완전 커스텀** — 사용자 요청 핵심
   - X/Y 위치 슬라이더 (화면 비율 기준 0~100%), 크기 스케일 (0.5×~2.0×)
   - **마우스 드래그로 이동** (오버레이 열린 상태에서 배지를 잡아 끌기 → 좌표 자동 저장)
   - 표시 항목 토글: FPS / frametime(ms) / 시계 / 프리셋명
   - 저장: ReShade ini `[SHERBET]` 섹션 (`OsdPosX, OsdPosY, OsdScale, OsdItems`)
2. **이펙트 즐겨찾기 + 필터** — 카드에 별 토글, 즐겨찾기 상단 고정, 필터 필(전체/켜짐/즐겨찾기), ini 저장
3. **링크 버튼(브라우저 열기)** — About(및 홈 헤더)에 "디스코드 문의"/"구매 페이지" 버튼 → `ShellExecute`로 기본 브라우저 오픈. 전체화면(독점) 게임에선 알트탭됨을 툴팁으로 안내.
4. **헤더 원클릭 버튼 3종** — 스크린샷·성능 모드·리로드 (모두 기존 기능 호출만)

## 8. 무단 배포 경고 (HWID 대체)

HWID 잠금은 이번 범위에서 제외. 대신:

- About 탭 상단 고지문:
  > 이 빌드는 구매자 전용 커스텀 빌드입니다.
  > 구매한 파일을 무단 배포·공유할 경우 **블랙리스트에 추가되며 파일이 잠깁니다.**
- `sku_owner_name`이 설정된 경우 "등록 소유자: {이름}" 표기 (심리적 억제 + 유출 추적)
- 코드 구조는 이후 `hwid-add` 브랜치를 base에 merge하기 쉽게 유지 (인증 로직과 UI 결합 금지)

## 9. 빌드 / CI

- `.github/workflows/build.yml` 수정: `sherbet-base`, `sherbet/**` push 시 트리거
- 아티팩트 이름에 테마 포함: `Sherbet_MintSoda_x64.dll` 등 (32비트 포함)
- 로컬 검증: Parallels Windows VM + VS2022 또는 GitHub Actions 아티팩트 다운로드 후 게임 테스트

## 10. 제약 & 검증

- ⚠️ Mac에서는 코드 작성만 가능, 컴파일/실행 검증은 CI(Actions) + Windows 환경에서 수행
- ImGui는 ReShade 번들 버전(deps/imgui) API만 사용, 보수적으로 작성해 컴파일 에러 최소화
- 각 단계 커밋 → CI 빌드 통과 확인 → 다음 단계 진행 (빨간 CI 위에 쌓지 않기)
- 기능 검증 체크리스트: 오버레이 토글, 이펙트 켜기/끄기, 변수 조정, 프리셋 저장/로드, 스크린샷, OSD 드래그, 링크 버튼

## 11. 범위 밖 (YAGNI)

- HWID 잠금 (경고문으로 대체, 추후 hwid-add merge)
- 인게임 테마 선택기 (테마 = 컴파일 타임 고정, 판매 모델의 전제)
- 이펙트 컴파일러/렌더러/애드온 API 로직 수정
- 오버레이 내 브라우저 임베드 (불가능 — 링크 버튼으로 대체)
- 셋업 인스톨러(setup/) 리브랜딩 — DLL 직접 배포 방식이므로 후순위

## 12. 오픈 이슈

- `sku_owner_name` / `shop_url` / `discord_url` 실제 값: 브랜치별로 판매 시 기입 (기본 공란)
- Noir Gold 프리미엄 SKU의 차별 요소(전용 파티클 밀도 등)는 구현하며 조정
