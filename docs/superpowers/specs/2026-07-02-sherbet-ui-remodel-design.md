# SHERBET — ReShade 전면 리모델링 설계 문서

- 작성일: 2026-07-02
- 저장소: `Jeong-Ryeol/reshade` (fork of `crosire/reshade`)
- 기준 커밋: `upstream/main` (f191dc03)
- 디자인 시안: `~/reshade-design/preview.html` (v2, 승인됨)

## 1. 목표

crosire ReShade 최신 소스를 기준으로 오버레이 UI를 **"기존 ReShade라고 알아볼 수 없을 정도"로 전면 교체**한 커스텀 제품 **SHERBET**를 만든다.

**핵심 컨셉: "정렬이 한 사람을 위해 직접 만든 개인 커스텀 리쉐이드."**
- 축 1 — **테마**: 브랜치로 분리 (테마 1개 = 판매 상품/SKU 1개)
- 축 2 — **구매자**: 컴파일 시 닉네임 1개만 주입 → 파일 곳곳에 그 사람 이름을 박아 "당신만을 위한" 감성 + 유출 추적

- 비주얼(레이아웃·색·폰트·아이콘·애니메이션)은 전면 교체
- 이펙트 컴파일·프리셋·핫키·렌더러 후킹 등 **핵심 기능 로직은 무손상**
- 이전 세션의 cutie-base/theme/* 브랜치는 참고용으로 보존하고, 완전히 새로 작성

## 2. 브랜딩 & 크레딧

- 제품명: **Sherbet** (셔벗) — 테마들이 전부 셔벗 맛 라인업이 되는 네이밍
- 워드마크: `Sherbet <테마명>` (Gaegu 폰트), 로고: 그라디언트 라운드 사각형 + 4포인트 스파클(벡터)
- 제작자 크레딧: **"정렬이 만든 커스텀 리쉐이드"** — About 탭 상단 + 스플래시에 `by 정렬` 표기
- 디스코드: About/마켓/홈 헤더에 "디스코드 참여" 버튼 → `ShellExecute`로 https://discord.gg/5NGR7XVFta 열기
- 오버레이·스플래시·OSD 어디에도 "ReShade" 브랜드 노출 최소화
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
| `source/sherbet_market.hpp/.cpp` | 프리셋 마켓 탭: 카드 그리드, 10초 체험 타이머, 언락 상태 관리 | 동일 |
| `source/sherbet_license.hpp/.cpp` | 노드 락 + 언락코드 서명 검증(오프라인). UI와 분리 | 동일 |
| `source/sherbet_owner.h` | `SHERBET_OWNER`(구매자 닉), `SHERBET_ORDER_NO` 정의. **빌드 시 CI가 덮어씀** | 주문마다 다름(빌드 인자) |
| `res/fonts/Jua-Regular.ttf` | 본문/UI 폰트 (OFL) | 동일 |
| `res/fonts/Gaegu-Bold.ttf` | 제목/워드마크 폰트 (OFL) | 동일 |

### `struct SherbetTheme` 필드 (시안 CSS 변수와 1:1)

- 배경: `bg0, bg1, bg2` (그라디언트 스톱), `panel, panel_alt, chip, border`
- 텍스트: `text, text_dim`
- 강조: `accent, accent2, glow`
- 파티클: `particle_shape`(spark/heart/leaf/petal enum), `particle_count, particle_speed`
- 애니메이션: `bg_drift_speed`, `hue_cycle`(rainbow 전용), `glow_intensity`
- 문구: `theme_display_name`("Mint Soda"), `discord_url`(고정 https://discord.gg/5NGR7XVFta)
- 구매자 개인화 값은 테마가 아니라 `sherbet_owner.h`(§8.2)에 분리 — `SHERBET_OWNER`, `SHERBET_ORDER_NO`

## 5. 레이아웃 전면 교체 (스킨 엔진)

시안 v2 기반. **탭은 4개로 축소**:

### 탭 구성 (핵심 변경)

| 표시 | 아이콘 | 내용 |
|------|--------|------|
| 홈 | home | 이펙트 카드 리스트 (핵심 화면) |
| 마켓 | store | **프리셋 마켓** (신규 탭, §8.3 참조) |
| 설정 | sliders | 키/화면/OSD 설정 + 맨 아래 "고급" 접힘 안에 **로그** 이식 |
| 정보 | heart | About: 제작자 크레딧·디스코드·무단배포 경고·라이선스 |

- **숨김 처리**: 기존 **애드온 탭 완전 제거**, **통계 탭 제거**, **로그 탭 제거**(로그 뷰어 코드는 살려 설정>고급 접힘 섹션으로 이식 — A/S용). 통계 수치(FPS/frametime)는 OSD로 대체하므로 별도 탭 불필요.
- **좌측 아이콘 레일**: 상단 로고 → 홈/마켓/설정/정보 벡터 아이콘 → 하단 버전 칩. 기존 상단 탭바 제거.
- **헤더**: `Sherbet <테마명>` 워드마크 + 프리셋 필(드롭다운) + 원클릭 버튼 3종(스크린샷·성능 모드·리로드)
- **홈 탭**: 검색바(돋보기 아이콘) + 필터 필(전체/켜짐/즐겨찾기) + **카드형 이펙트 리스트**(토글 스위치, 이펙트명, .fx 파일명, ms 뱃지). 카드 확장 시 변수 에디터(그라디언트 슬라이더).
- **하단 상태바**: FPS 뱃지 + 활성 이펙트 수 + 리로드 버튼
- **스플래시**: 로고 마크 + "Sherbet <테마명> 로딩 중… by 정렬" + 그라디언트 프로그레스 바
- **이펙트/배경**: 오로라 블롭 드리프트(draw list 그라디언트), 벡터 파티클(테마별 도형), 포커스/호버 글로우
- **아이콘**: 이모지 사용 금지. ReShade 내장 **ForkAwesome 폰트**(res/fonts/forkawesome.h, 335 글리프, ImGui 버튼에서 이미 사용 중) + ImDrawList 커스텀 도형. 필요한 글리프 전수 확인 완료:
  - 홈 `ICON_FK_HOME` · 마켓 `ICON_FK_SHOPPING_CART` · 설정 `ICON_FK_SLIDERS` · 정보 `ICON_FK_INFO_CIRCLE`
  - 검색 `ICON_FK_SEARCH` · 즐겨찾기 `ICON_FK_STAR`/`STAR_EMPTY` · 스크린샷 `ICON_FK_CAMERA` · 성능 `ICON_FK_BOLT` · 리로드 `ICON_FK_REFRESH`
  - 로고/파티클 `ICON_FK_MAGIC`/`HEART` · 잠금 `ICON_FK_LOCK`/`UNLOCK` · 체험 `ICON_FK_PLAY` · 언락 `ICON_FK_KEY`
- **커스텀 위젯 구현 근거**: 토글·그라디언트 슬라이더·카드·파티클·글로우·오로라 배경은 모두 `ImDrawList`(AddRectFilled/PathArcTo/AddConvexPolyFilled)로 그린다 — ReShade 코드에 이미 동일 API 사용처 존재(imgui_widgets.cpp). 외부 의존성 추가 없음. 애니메이션은 매 프레임 시간 기반 계산.
- **튜토리얼**: 기존 4단계 로직은 부담 → 첫 실행 웰컴 카드 1장(테마 인사말+단축키 안내)으로 대체

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
3. **원클릭 프리셋 전환** — 헤더 프리셋 필 드롭다운 + 이전/다음 프리셋 단축키(낮/밤/영화 감성 즉시 전환). 기존 프리셋 전환 API 재사용.
4. **비교 슬라이더** — 화면을 세로선으로 갈라 적용 전/후 실시간 비교(핸들 드래그). 구매 전 손님을 홀리는 데모용. draw list로 클립.
5. **링크 버튼(브라우저 열기)** — About/마켓/헤더에 "디스코드 참여" 버튼 → `ShellExecute`로 기본 브라우저 오픈. 전체화면(독점) 게임에선 알트탭됨을 툴팁으로 안내.
6. **헤더 원클릭 버튼 3종** — 스크린샷·성능 모드·리로드 (모두 기존 기능 호출만)

## 8. 사업성 기능 (제품 차별화)

Sherbet를 "예쁜 스킨"이 아니라 **판매·보호·부가매출까지 되는 제품**으로 만드는 부분.

### 8.1 노드 락 (처음 1번 실행 → PC 고정)

- 처음 실행 시 그 PC 하드웨어 지문(메인보드 UUID + CPU 등 조합, 기존 `hwid-add` 로직 참고)을 계산해 DLL 옆 `sherbet.lic`에 기록·서명 → 이후 지문 불일치 PC에서는 오버레이 대신 "이 빌드는 다른 PC에 등록되어 있습니다" 안내만 표시.
- **서버 불필요·오프라인·게임 중 부하 0** (사용자 요청 핵심 근거).
- 재발급(PC 교체/포맷): 봇 `/재발급` 안내 → 새 지문으로 lic 재서명(수동).
- ⚠️ 한계: 순수 오프라인 노드 락은 "먼저 켠 사람" 우선일 뿐 완벽 차단은 아니다. → **워터마크(8.2)로 유출 추적 보완**. 목표는 "일반 게이머의 파일 재배포 차단"이며 이 조합으로 충족.

### 8.2 구매자 개인화 각인 ("당신만을 위한" — 핵심 판매 포인트)

단순 워터마크가 아니라 **제품 컨셉 자체**. 컴파일 시 닉네임 1개만 주입하면 UI 곳곳에 그 사람 이름이 박힌다.

**주입 방식 (둘 다 지원)**
- A(추천): 빌드 파라미터 `SHERBET_OWNER=홍길동` → CI(Actions)에서 이름 칸만 채우고 빌드 → `Sherbet_Mint_홍길동_x64.dll` 산출. 님은 이름만 타이핑.
- B: `source/sherbet_owner.h`의 `#define SHERBET_OWNER "홍길동"` 한 줄 수정 후 빌드.
- 구현: `sherbet_owner.h`에 `SHERBET_OWNER`(기본 공란)·`SHERBET_ORDER_NO` 정의, CI가 이 값을 덮어씀. 테마는 브랜치(축1), 사람은 이 상수(축2)로 완전 분리 → 조합 폭발 없음.

**각인 위치**
| 위치 | 예시 |
|------|------|
| 스플래시 | "{owner}님을 위한 Sherbet · by 정렬" |
| 첫 실행 웰컴 카드 | "{owner}님 어서오세요 — 당신만을 위해 만들어졌어요" |
| 홈 헤더 | `Sherbet · for {owner}` |
| 정보 탭 | "등록 소유자: {owner} · 주문 #{order}" |
| OSD 배지(옵션) | 배지 옆 닉네임 표시 토글 |
| DLL 파일 메타/리소스 | 파일 속성·내부 문자열에 각인 (추적) |

- owner가 공란이면 개인화 문구는 자동 숨김(데모/스크린샷용 빌드 대응).
- 유출 파일 발견 시 owner로 즉시 식별 → 블랙리스트 근거(8.1 노드락과 시너지).

### 8.3 프리셋 마켓 (신규 탭 · 지속 매출 모델)

게임 내 상점처럼 **프리셋을 잠긴 채 진열** → 님 확인 후 언락코드로 개별 해제.

- **탭 UI**: 프리셋 카드 그리드(썸네일 자리 + 이름 + 잠금 배지 + "10초 체험"/"언락코드 입력" 버튼).
- **체험(맛보기)**: 잠긴 프리셋도 **10초간 실제 적용** 후 자동 원복 + "구매하면 계속 사용" 토스트. (타이머는 프레임 시간 기준)
- **언락**: 손님이 마켓 탭에 **언락코드 입력**(예: `MINT-ROSE-8842`) → 서명 검증 통과 시 해당 프리셋 영구 잠금 해제. 서버 불필요(오프라인 서명).
- 급할 땐 님이 프리셋 파일 직접 전송도 가능(보조 경로).

### 8.4 테마 업그레이드권

- 민트 구매자가 핑크로 갈아탈 때: 언락코드로 **다른 테마 팔레트를 추가 해제**하는 구조.
- 구현상 마켓/언락 시스템(8.3)과 동일한 코드 검증 인프라를 공유. About/마켓에 "업그레이드 문의" 안내.

### 8.5 공통: 오프라인 언락코드 시스템

- 노드 락·프리셋 언락·테마 업그레이드가 **하나의 코드 검증 모듈**을 공유.
- 방식: 님이 보유한 비밀키로 코드 생성(별도 소형 생성기 — CLI 또는 봇 명령), DLL은 내장 검증키로 서명 확인. **인터넷 불필요**.
- 코드에 대상(HWID / preset-id / theme-id)과 서명이 인코딩되어 위조·재사용 방지.
- 인증/검증 로직은 UI와 분리(`source/sherbet_license.*`)해 유지보수·테마 브랜치 merge 용이.

### 8.6 무단 배포 경고 (About)

- 고지문:
  > 이 빌드는 **정렬**이 제작한 구매자 전용 커스텀 빌드입니다.
  > 무단 배포·공유 시 **블랙리스트 추가 및 파일 잠금** 조치됩니다.
- "등록 소유자: {이름}" + 디스코드 문의 버튼 병기.

## 9. 빌드 / CI

- `.github/workflows/build.yml` 수정: `sherbet-base`, `sherbet/**` push 시 트리거
- **주문용 빌드 워크플로**(`workflow_dispatch`): 입력 칸 `owner`(구매자 닉), `order_no` → `sherbet_owner.h` 덮어쓰고 빌드 → 아티팩트 `Sherbet_<테마>_<owner>_x64.dll`. 님은 이름만 입력 후 Run 클릭.
- 기본 push 빌드는 owner 공란(데모 빌드) 산출. 32/64비트 모두.
- 로컬 검증: Parallels Windows VM + VS2022 또는 GitHub Actions 아티팩트 다운로드 후 게임 테스트

## 10. 제약 & 검증

- ⚠️ Mac에서는 코드 작성만 가능, 컴파일/실행 검증은 CI(Actions) + Windows 환경에서 수행
- ImGui는 ReShade 번들 버전(deps/imgui) API만 사용, 보수적으로 작성해 컴파일 에러 최소화
- 각 단계 커밋 → CI 빌드 통과 확인 → 다음 단계 진행 (빨간 CI 위에 쌓지 않기)
- 기능 검증 체크리스트: 오버레이 토글, 이펙트 켜기/끄기, 변수 조정, 프리셋 저장/로드, 스크린샷, OSD 드래그, 링크 버튼

## 11. 범위 밖 (YAGNI)

- 인게임 테마 선택기 (테마 = 컴파일 타임 고정, 판매 모델의 전제)
- 자동 판매 봇/자동 발송 (판매는 수동 — 티켓 문의 시 님이 직접 처리)
- 인생샷 모드(UI 숨김+로고 각인 스샷) — 2차 버전으로 보류
- 애드온 탭/통계 탭 (제거), 튜토리얼 4단계 (웰컴 카드로 대체)
- 이펙트 컴파일러/렌더러/애드온 API 로직 수정
- 오버레이 내 브라우저 임베드 (불가능 — 링크 버튼으로 대체)
- 온라인 실시간 인증 서버 (오프라인 서명 방식 채택)
- 셋업 인스톨러(setup/) 리브랜딩 — DLL 직접 배포 방식이므로 후순위

## 12. 오픈 이슈

- `sku_owner_name` / `discord_url` 실제 값: 브랜치·주문별로 판매 시 기입 (디스코드는 https://discord.gg/5NGR7XVFta 고정)
- 언락코드 서명 알고리즘·키 관리 구체화(생성기 형태: CLI vs 디스코드 봇)는 구현 단계에서 확정
- 프리셋 마켓 초기 상품 목록(어떤 감성 프리셋을 진열할지)은 판매 준비 시 채움
- Noir Gold 프리미엄 SKU의 차별 요소(전용 파티클 밀도 등)는 구현하며 조정
