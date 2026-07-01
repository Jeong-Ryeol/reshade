# 큐티 커스텀 ReShade UI 전면 재작성 — 설계 문서

- 작성일: 2026-07-02
- 대상 저장소: `Jeong-Ryeol/reshade` (fork of `crosire/reshade`)
- 기준 커밋: `upstream/main` (f191dc03 시점)

## 1. 목표

ReShade 오버레이 UI를 "귀엽고 화려한(cute + flashy)" 감성으로 전면 재작성한다.
파스텔 색감 + 반짝임/글로우/애니메이션을 얹어 선물용으로 배포 가능한 커스텀 빌드를 만든다.

각 테마는 **별도 git 브랜치**로 관리하여, 받는 사람마다 자기 테마 빌드(.dll)를 따로 줄 수 있게 한다.

## 2. 접근법 (Approach A: 테마 엔진 + 전면 비주얼 리스킨)

전체 화면을 시각적으로 완전히 바꾸되, **검증 불가능한 핵심 기능 로직은 재작성하지 않는다.**

- 스타일/색/폰트/애니메이션/장식은 전면 교체
- 스플래시 / 홈 / About / 네비게이션은 레이아웃까지 완전 재디자인
- 이펙트 목록 / 통계 / 설정 / 각종 에디터는 테마·라운드·간격만 입히고 **로직은 무손상**

### 채택하지 않은 접근

- **순수 테마 레이어만**: 임팩트가 약해서 제외.
- **모든 탭 로직까지 재작성**: Mac에서 빌드/검증 불가 + 기능 파손 위험 최대 + 작업량 폭발. 제외.

## 3. 저장소 & 브랜치 구조

```
main (fork 미러)
upstream/main (crosire 최신)
└─ cutie-base   ← 공통 큐티 프레임워크 (upstream/main 기준으로 새로 생성)
   ├─ theme/peach    (복숭아 파스텔)
   ├─ theme/pink     (핑크 하트)
   ├─ theme/mint     (민트 소다)
   └─ theme/rainbow  (무지개 RGB)
```

- `cutie-base`: 모든 공통 프레임워크. 여기서 개선하면 각 theme 브랜치에 merge/rebase로 내려준다.
- `theme/*`: 각자 팔레트 하나로 고정. **인게임 테마 선택기 없음 (단일 테마 고정).**
- 기존 브랜치(`gradient`, `custom-*`, `hwid-add`)는 건드리지 않고 보존(참고용).

## 4. 파일 구성 (신규)

| 파일 | 역할 | 브랜치별 차이 |
|------|------|----------------|
| `source/cutie_theme.hpp` | `struct CutieTheme` 정의 + 활성 테마 extern 선언 | 동일 |
| `source/cutie_theme.cpp` | `g_theme` = 활성 테마 값 정의 | **이 파일만 브랜치마다 다름** |
| `source/cutie_ui.hpp` | 공통 렌더 헬퍼 선언 | 동일 |
| `source/cutie_ui.cpp` | 공통 렌더 헬퍼 구현 | 동일 |
| `res/fonts/Jua-Regular.ttf` | 본문/UI 폰트 (OFL) | 동일 |
| `res/fonts/Gaegu-Bold.ttf` | 제목/스플래시 폰트 (OFL) | 동일 |

`cutie_theme.cpp` 하나만 브랜치별로 갈리므로, base → theme 브랜치 merge 시 충돌이 거의 없다.

### `struct CutieTheme` 필드 (초안)

- 팔레트: `bg_gradient_stops[]`, `panel`, `panel_alt`, `text`, `text_dim`, `border`
- 강조: `accent`, `accent_hover`, `accent_active`, `glow_color`
- 파티클: `particle_glyph`(예: "✨"), `particle_color`, `particle_count`, `particle_speed`
- 애니메이션: `bg_anim_speed`, `bg_saturation`, `bg_brightness`, `glow_intensity`
- 폰트: `body_font`, `title_font` (기본 Jua/Gaegu)
- About: `maker_name`(기본 "정렬"), `recipient_name`(기본 "OOO"), `about_message`, `discord_handle`

## 5. 공통 비주얼 프레임워크 (`cutie-base`)

- **스타일 적용**: 매 프레임 `apply_style(ImGuiStyle&, CutieTheme)` — 색·라운드·패딩·간격 전부 테마화
- **애니메이션 그라디언트 배경**: `draw_animated_background()` — 팔레트 스톱을 부드럽게 순환/이동
- **반짝이 파티클 레이어**: `draw_sparkles()` — foreground draw list에 테마색 파티클(✨/🌸 등) 떠다님
- **글로우**: `draw_glow(rect)` — 포커스/호버 요소 뒤 부드러운 빛번짐(additive)
- **큐티 폰트**: `build_font_atlas`에서 Jua(본문)+Gaegu(제목)을 한글 글리프 범위 포함해 로드
- **레이아웃 재디자인**:
  - 스플래시: 애니메이션 인트로 + 테마명 + 받는 사람
  - 홈: 히어로 헤더(커스텀 타이틀+마스코트/이모지) + 필형 네비 + 카드 레이아웃
  - About: 개인화 문구("이 리쉐이드는 {maker}가 {recipient}를 위해 제작한 커스텀 리쉐이드입니다") + 디스코드 + 오픈소스 라이선스 접기
  - 네비게이션: 둥근 필/아이콘 탭
- **기능 패널 리스킨**: `draw_gui_home/settings/statistics/log/addons`, `draw_variable_editor`, `draw_technique_editor`, `draw_code_editor` = 테마+라운드+간격만 적용, 로직 무손상

## 6. 브랜치별 개인화 (theme/*)

각 `theme/*`의 `cutie_theme.cpp`에서 `g_theme` 값만 교체:

| 브랜치 | 팔레트 | 파티클 | 비고 |
|--------|--------|--------|------|
| theme/peach | 핑크→퍼플→민트→하늘 파스텔 | 🌸 | 무지개 파스텔 |
| theme/pink | 핫핑크 + 로즈골드 | 💗 | 하트/리본 강조 |
| theme/mint | 민트 + 크림 + 소프트그린 | 🍃 | 상큼·편안 |
| theme/rainbow | 풀 RGB 사이클 | ✨ | 채도·애니 MAX |

받는 사람 이름은 브랜치별 `recipient_name`으로 채운다 (초기값 `OOO`).

## 7. 폰트

전부 Google Fonts, **OFL 라이선스**(무료·재배포·임베드 허용 — 배포용 DLL에 안전).

- 본문/UI: **Jua** (둥글고 통통, 작은 글씨 가독성 좋음)
- 제목/스플래시: **Gaegu** (손글씨 감성 포인트, 큰 글씨 전용)
- 테마별 대안: Dongle / Poor Story / Gamja Flower (브랜치에서 폰트만 교체 가능)

## 8. 저장/설정

인게임 선택기가 없으므로 테마는 컴파일 타임 고정. ReShade 기본 config 스키마는 건드리지 않는다.

## 9. 제약 & 검증

- ⚠️ **Mac = 코드 작성만 가능. 빌드/실행 검증 불가.**
- 검증: Windows + Visual Studio 2022(“C++ 데스크톱 개발” 워크로드) + Python(gl3w)에서
  `git clone --recurse-submodules` → 브랜치 checkout → `ReShade.sln` 빌드 → 스크린샷 → 반복.
- 코드는 ReShade가 번들한 ImGui 버전 API에 맞춰 보수적으로 작성해 컴파일 에러를 최소화한다.

## 10. 범위 밖 (YAGNI)

- 인게임 테마 선택기 ❌
- 이펙트/테크닉/코드 에디터 **로직** 재작성 ❌
- HWID 등 신규 기능 ❌
- 불필요한 리팩터링 ❌

## 11. 오픈 이슈

- 받는 사람 이름: 현재 `OOO` 플레이스홀더. 브랜치별로 나중에 채움.
- 실제 ImGui 버전/폰트 임베드 방식(리소스 vs 파일 로드)은 구현 계획 단계에서 코드 확인 후 확정.
