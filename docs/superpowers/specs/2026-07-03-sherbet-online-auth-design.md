# Sherbet 온라인 인증 + 원격 콘텐츠 배포 시스템 — 설계 문서

**작성일:** 2026-07-03
**대상:** Sherbet (crosire/reshade 포크, 브랜치 `sherbet-base`)
**성격:** 개인용/비수익 (판매 기능 아님 — 사용자들끼리 사용)

---

## 1. 목표 (Goal)

오프라인 FNV 언락코드의 유출 약점을 제거하고, 테마/프리셋/이펙트를 **서버에서 실시간으로 관리·배포**한다. 재빌드 없이 콘텐츠를 추가하고, 디스코드 계정 기반으로 권한을 통제한다.

핵심 사용자 경험:
- Sherbet 실행 → 디스코드 로그인(항상 온라인) → 역할 없으면 실행 차단
- 화면의 **"내 전용 불러오기"** 버튼 → 서버가 그 사람이 권한 가진 **모든** 콘텐츠(테마+프리셋+fx)를 내려줌 → 자동 배치·로드

---

## 2. 확정된 결정 사항

| 항목 | 결정 |
|---|---|
| 인증 모델 | **항상 온라인.** 실행 시마다 디스코드 OAuth + 역할 확인 |
| 권한 단위 | **테마별 디스코드 역할** (예: 딸기 구매 → `sherbet-strawberry` 역할). 기본 "구매자" 역할로 실행 게이트 |
| 파일 보호 | **A (다운로드 게이트).** 받은 .fx/.ini는 평문으로 디스크에 존재. 다운로드 자체를 로그인+역할로 막음. 암호화/메모리로딩(B)은 채택 안 함 |
| "내 전용 불러오기" | **B (권한 전부).** 그 사람이 역할 가진 테마+프리셋+fx 전부 |
| 역할 부여 | 판매자가 수동 또는 티켓봇이 자동 |

---

## 3. 아키텍처

```
[Sherbet 클라이언트 (C++)]  ←HTTPS→  [홈서버 API (Ubuntu)]  ←→  [디스코드 (OAuth + 봇 + 역할)]
```

- **클라이언트:** Sherbet DLL. 인증 상태 관리 + 콘텐츠 다운로드/배치/로드.
- **홈서버:** `wonryeol.asuscomm.com`. OAuth 콜백, 역할 조회(봇 경유), 콘텐츠 API, 콘텐츠 저장소.
- **디스코드:** OAuth 앱(티켓봇 앱 재활용 가능, Client ID `1521966326275641354`), 기존 티켓봇(길드 역할 읽기/부여), 상품별 역할.

---

## 4. 기존 코드 재활용 맵 (중요 — 새로 안 만들고 확장)

분석으로 확인된, **이미 존재하는** 인프라:

### 4.1 HTTP 클라이언트 (재활용)
- `source/runtime_update_check.cpp` — WinInet 기반 HTTPS GET (`InternetOpen`/`InternetOpenUrl`/`InternetReadFile`, RAII `scoped_internet_handle`, `wininet.lib` 링크됨). GitHub API 호출용으로 이미 작동 중.
- → **서버 콘텐츠 다운로드에 이 idiom 그대로 사용.** POST/토큰 헤더가 필요하면 소폭 확장.

### 4.2 "materialize" 패턴 (재활용)
- `source/runtime_gui.cpp:3803` `materialize` 람다 — 내장 리소스 바이트를 `_current_preset_path.parent_path()/"Sherbet-Custom.ini"`로 `std::ofstream` 기록 후 경로 반환.
- → **다운로드된 프리셋/fx도 동일 패턴**: 바이트 받기 → 폴더에 쓰기 → 로드.

### 4.3 프리셋 로드 (재활용)
- `set_current_preset_path(path)` (`runtime_api.cpp:1367`) — `resolve_preset_path`로 자동 검증(.ini + `Techniques` 키). 프리셋 "컬렉션"은 별도 설정 없이 **현재 프리셋과 같은 폴더의 모든 유효 .ini** (`switch_to_next_preset`, `runtime.cpp:1474`).
- → 다운로드한 .ini들을 한 폴더에 쓰면 화살표 네비게이션에 자동 편입.

### 4.4 이펙트(.fx) 로드 — **디스크 필수**
- `.fx`는 `_effect_search_paths`에서 디스크 파일로 발견·컴파일. `load_effect`가 파일 mtime을 캐시키로 쓰고(`runtime.cpp:1625`), `#include`는 디스크에서 해석(`effect_preprocessor.cpp:760`).
- 컴파일러 코어는 메모리 입력(`preprocessor::append_string`) 지원하나, 런타임 경로는 디스크 하드와이어드.
- → **다운로드한 .fx는 `_effect_search_paths` 폴더에 파일로 쓰고 `reload_effects()` 호출.** (A 결정과 일치)

### 4.5 인증 게이트 지점 (3곳)
- **하드 차단:** 생성자 `runtime.cpp:353` 부근 (`load_config` 전후) — 미인증 시 전면 차단.
- **오버레이 차단:** `runtime_gui.cpp:1587` — 노드락 `sherbet::nodelock::is_authorized(dir)`가 **이미 이 패턴**(미인증 시 안내문만 그리고 `ImGui::End()`). 여기에 온라인 인증 상태를 AND/치환.
- **이펙트 컴파일 차단:** `update_effects()` `runtime.cpp:3711` (`_frame_count==0`일 때 첫 로드)를 인증 조건으로 감쌈.

### 4.6 교체되는 오프라인 계층
- `s_unlocked` 맵 + `unlock_theme`/`is_unlocked`/`load_unlocked_csv`/`check_theme_code` (`sherbet_ui.cpp:26-77`).
- FNV 검증 `sherbet_license.hpp` (`verify_theme`/`verify_preset`).
- 마켓의 코드 입력창 2개 (`runtime_gui.cpp:3781`, `3859`).
- config 키 `Unlocked=`/`PresetUnlocked=` (load `374-380`, save `505-507`).
- → **역할 기반 권한 확인으로 대체.** 단, `is_unlocked(id)` 시그니처는 유지하되 내부를 "서버에서 받은 역할/권한 스냅샷 조회"로 바꾸면 마켓 UI 게이트는 그대로 재사용 가능.

### 4.7 유지되는 것 (인증과 직교)
- 테마 데이터 모델·렌더링 (`sherbet_theme.hpp`, `sherbet_themes.cpp`), `set_active_theme`/`active_theme`, `safe_default_id`.
- ⚠️ **권한 강제는 현재 UI 계층에만 존재** (`set_active_theme`엔 잠금 없음). 하드 강제하려면 적용/로드 지점에 체크 추가 필요.

---

## 5. 인증 흐름 (Device-style OAuth)

게임 오버레이 안에서 로그인창을 못 띄우므로, 시스템 브라우저 + 홈서버 콜백 방식(업스케일러의 `login_callback?code=...`와 동일 패턴):

```
Sherbet 실행
  → 캐시 세션토큰 확인
      · 유효 → 통과
      · 없음/만료 → 아래 로그인
  → 시스템 브라우저 열기: https://wonryeol.asuscomm.com/auth/discord?state=<nonce>
  → 사용자 디스코드 로그인/승인
  → 디스코드 → 홈서버 콜백(?code=...)
  → 홈서버: code→토큰 교환 → 봇으로 그 사용자의 길드 역할 조회
  → 홈서버: 세션토큰 발급 (역할 스냅샷 포함)
  → Sherbet: 세션토큰 폴링/수신 후 로컬 캐시
  → 이후 콘텐츠 요청에 세션토큰 사용
```

- 세션토큰 캐시 위치: config 폴더 (노드락 `sherbet.lic`와 동급 위치).
- **재확인 주기:** 실행 시마다 서버에 토큰 유효성 + 역할 재조회.

---

## 6. 권한(Entitlement) 모델

- **디스코드 역할 = 권한.** 길드 내 역할 명명 규칙 예:
  - `sherbet-buyer` — 실행 기본 게이트 (없으면 Sherbet 실행 불가)
  - `sherbet-theme-<id>` — 테마별 (예: `sherbet-theme-strawberry`)
  - `sherbet-preset-<id>` — 프리셋/커스텀별
- 서버가 역할 스냅샷을 세션토큰 or 콘텐츠 응답에 실어 보냄.
- 클라의 `is_unlocked(theme_id)` → "역할 스냅샷에 `sherbet-theme-<id>` 있나" 조회로 재구현.

---

## 7. 콘텐츠 배포 흐름 ("내 전용 불러오기")

```
버튼 클릭
  → GET https://wonryeol.asuscomm.com/content/me   (Authorization: Bearer <세션토큰>)
  → 서버: 역할 기반으로 그 사람 권한 콘텐츠 매니페스트 반환
        { themes:[{id,name,colors,particle}, ...],
          presets:[{id, ini_url}, ...],
          effects:[{filename, fx_url}, ...] }
  → 클라: 각 항목 다운로드
        · 테마 = JSON → 런타임 테마 목록에 병합(동적 테마)
        · 프리셋 = .ini 바이트 → 프리셋 폴더에 파일로 기록 (materialize 패턴)
        · 이펙트 = .fx 바이트 → EffectSearchPaths 폴더에 파일로 기록
  → reload_effects() + 프리셋/테마 목록 갱신
  → "내 전용" 세팅 완성
```

**동적 테마 처리:** 현재 테마는 컴파일 상수 배열(`s_themes[]`). 원격 테마를 받으려면 런타임에 확장 가능한 테마 목록(예: `std::vector<theme>` + 문자열 소유)을 추가하고, `all_themes`/`find_theme`가 정적+동적 둘 다 훑도록 확장한다. (Phase 1 상세)

---

## 8. 서버 API 계약 (초안)

| 엔드포인트 | 메서드 | 인증 | 역할 |
|---|---|---|---|
| `/auth/discord` | GET(브라우저) | — | OAuth 시작, 콜백 처리, 세션토큰 발급 |
| `/auth/verify` | POST | Bearer | 토큰 유효성 + 최신 역할 스냅샷 반환 (실행 시 게이트) |
| `/content/me` | GET | Bearer | 권한 콘텐츠 매니페스트 |
| `/content/file/<id>` | GET | Bearer | 프리셋/fx 바이트 (권한 재확인 후 스트림) |

- 전 구간 HTTPS. `/content/*`는 세션토큰 없으면 401.
- 홈서버 스택: (미정 — Phase 0에서 확정. 후보: Node/Express or Python/FastAPI, 기존 티켓봇과 같은 호스트).

---

## 9. Phase 분해 (각 단계 = 독립 작동 결과물)

### Phase 0 — 인증 게이트 (기반)
- 홈서버: `/auth/discord`, `/auth/verify`, 세션토큰 발급/검증, 봇 경유 역할 조회.
- 클라: 브라우저 OAuth 흐름, 세션토큰 캐시, 3개 게이트 지점(생성자/오버레이/이펙트) 연결.
- **결과:** "디코 로그인 + 구매자 역할 없으면 Sherbet 안 켜짐."
- **안전망(필수):** 서버 무응답 시 캐시토큰 N시간 유예(완전 먹통 방지). 유예 값은 Phase 0에서 확정.

### Phase 1 — 원격 테마 배포
- 동적 테마 목록(정적+동적) 지원, `all_themes`/`find_theme` 확장.
- `/content/me` 테마 부분 + "내 전용 불러오기"의 테마 반영.
- `is_unlocked` → 역할 스냅샷 조회로 교체. 마켓 코드 입력창 제거.
- **결과:** 서버에 테마 JSON 추가 + 역할 부여 → 버튼 누르면 실시간 반영.

### Phase 2 — 프리셋/이펙트 배포
- `/content/file/*` 다운로드, materialize 패턴으로 .ini/.fx 파일 기록, `reload_effects()`.
- PRE- 코드/트라이얼 계층 정리(역할로 대체).
- **결과:** 프리셋+커스텀 fx까지 "내 전용 불러오기" 한 번에.

### Phase 3 — 티켓봇 역할 자동 부여
- 홈서버 티켓봇(`~/discord-ticket-bot`, discord.py)에서 구매/티켓 완료 시 해당 역할 부여.
- 이 리포 밖 시스템 → 스펙에선 인터페이스만. 실제 구현은 홈서버에서 직접.
- **결과:** 판매→역할→콘텐츠 전자동.

---

## 10. 리스크 + 완화

| 리스크 | 완화 |
|---|---|
| 항상-온라인 → 홈서버/인터넷 다운 시 전원 사용 불가 | 캐시토큰 N시간 유예(오프라인 그레이스). Phase 0에서 값 확정 |
| 홈서버 외부 노출 (공격면) | HTTPS 강제, `/content/*` 세션토큰 필수(401), 레이트리밋 |
| 평문 .fx/.ini 복사 유출 (A의 트레이드오프) | 감수하기로 결정. 다운로드 게이트로 캐주얼 공유만 차단 |
| 세션토큰 탈취 | 짧은 만료 + 실행 시 재검증. HWID 바인딩(노드락 재활용) 옵션 |
| 브라우저 OAuth UX (게임 중) | 로그인은 최초 1회 + 토큰 캐시. 이후 무상호작용 |

---

## 11. Out of Scope (현재)

- 암호화 배포 + 메모리 로딩(B) — 채택 안 함.
- 결제 시스템 자동화 — 티켓봇 수동/반자동 유지.
- 다중 서버/이중화 — 홈서버 단일. (그레이스로 완화)
- 판매/수익화 — 개인용 전제.

---

## 12. 확정된 파라미터 (구 열린 질문 — 2026-07-03 확정)

1. **홈서버 API 스택: Python + FastAPI.** 기존 티켓봇(discord.py)과 동일 호스트/언어 → 봇의 길드 역할 조회 로직·토큰 공유가 쉬움.
2. **오프라인 그레이스: 24시간.** 홈서버 무응답 시 캐시 세션토큰을 마지막 검증 후 24h까지 허용(완전 먹통 방지).
3. **HWID 바인딩: 결합함.** 세션토큰을 발급 시 노드락 HWID(CPUID + C: 볼륨시리얼, `sherbet_nodelock.hpp` 재활용)에 묶음 → 토큰 탈취해도 타 PC 무용.
4. **역할 명명 규칙 (확정):**
   - `sherbet-buyer` — 실행 기본 게이트 (없으면 Sherbet 실행 불가)
   - `sherbet-theme-<id>` — 테마별 (예: `sherbet-theme-strawberry`)
   - `sherbet-preset-<주문번호>` — 프리셋/커스텀별 (`SHERBET_ORDER_NO` 기반, 없으면 owner)
