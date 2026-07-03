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

## 5. 인증 흐름 (Device-style OAuth) — **배포 반영본 (2026-07-03)**

게임 오버레이 안에서 로그인창을 못 띄우므로, 시스템 브라우저 + 홈서버 폴링 방식. **베이스 URL: `https://wonryeol.asuscomm.com/sherbet-auth`** (Phase 0a 배포 완료·live 검증됨). 보안 하드닝으로 **state를 서버가 생성**(`POST /auth/start`)하고 클라는 **폴링**(`GET /auth/poll`)한다 — 옛 `GET /auth/discord?state=<클라nonce>`는 폐기.

```
Sherbet 실행
  → 캐시 토큰({token, hwid, last_verified_unix}) 로드
      · POST /auth/verify {token, hwid} == valid:true          → 통과(잠금해제)
      · 서버 무응답/503 & last_verified 로부터 24h 이내         → 오프라인 그레이스 통과
      · valid:false / 그레이스 초과 / 토큰 없음                 → 잠금(로그인 UI)
  ── 로그인(잠금 패널의 "디스코드로 로그인" 버튼 클릭) ──
  → POST /auth/start {hwid}  → { state, authorize_url }
  → ShellExecute 로 시스템 브라우저에 authorize_url 열기
  → 사용자 디스코드 로그인/승인 → 디스코드 → /auth/callback (서버가 code→토큰 교환, 봇으로 역할 조회, JWT 발급/거부)
  → Sherbet: GET /auth/poll?state=  를 ~2초 간격 폴링(최대 5분)
      · ready + token → 캐시 저장 + last_verified=now → 잠금해제
      · denied(no_buyer_role) → 사유 표시
      · 5분 타임아웃 → 버튼으로 복귀
```

- **토큰 캐시:** config 폴더, 노드락 `sherbet.lic`와 동급 위치. 저장 필드 = JWT 토큰 + hwid + `last_verified_unix`(오프라인 그레이스 기준).
- **HWID:** `sherbet_nodelock.hpp` 재활용(CPUID + C: 볼륨시리얼). `/auth/verify`·`/auth/start` 에 동봉.
- **재확인 주기:** 실행 시마다 `/auth/verify` 로 토큰 유효성 + 역할 스냅샷 재조회(온라인일 때). 오프라인이면 그레이스.
- **로그인 UX(확정):** 버튼식. 잠금 시 오버레이는 로그인 패널만 노출하고 효과·테마·마켓은 잠금. 브라우저는 버튼 클릭 시에만 열림(게임 실행 중 자동 팝업 없음).
- **폴링:** 오버레이 프레임을 막지 않도록 백그라운드 처리(또는 프레임당 1회 비동기 체크).

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

**베이스 경로 `/sherbet-auth` (nginx → 127.0.0.1:8010). Phase 0a = 배포 완료·live 검증.**

| 엔드포인트 | 메서드 | 바디/파라미터 | 응답 | 상태 |
|---|---|---|---|---|
| `/auth/start` | POST | `{hwid}` | `{state, authorize_url}` (state=서버생성) | ✅ 배포 |
| `/auth/callback` | GET(브라우저) | `?code&state` (또는 `?error`) | 안내 HTML(200) | ✅ 배포 |
| `/auth/poll` | GET | `?state` | `{status: pending\|ready\|denied, token?, reason?}` | ✅ 배포 |
| `/auth/verify` | POST | `{token, hwid}` | `{valid, sub?, roles?}` / 503`{valid:null,error}` | ✅ 배포 |
| `/content/me` | GET | Bearer | 권한 콘텐츠 매니페스트 | ⏳ Phase 1+ |
| `/content/file/<id>` | GET | Bearer | 프리셋/fx 바이트 | ⏳ Phase 2 |

- 전 구간 HTTPS(certbot, 기존 `wonryeol.asuscomm.com` 도메인). `/content/*`는 세션토큰 없으면 401(미구현).
- **홈서버 스택: Python + FastAPI** (`~/reshade/server`, systemd `--user` `sherbet-auth.service`, 단일 워커). 봇 토큰은 티켓봇("JeongRyeol Ticket") 공유.
- **HWID 결합:** `/auth/start`·`/auth/verify` 가 hwid를 받고, 토큰 JWT 클레임에 hwid를 넣음. 단 클라측 강제(스펙 §12.3).

---

## 9. Phase 분해 (각 단계 = 독립 작동 결과물)

### Phase 0a — 인증 서버 (✅ 완료·배포·live 검증)
- 홈서버 FastAPI: `/auth/start`·`/auth/callback`·`/auth/poll`·`/auth/verify`, JWT(HS256, HWID·24h), 봇 경유 역할 조회. 보안 하드닝(로그인CSRF·PendingStore TTL·업스트림 실패 처리) 완료. 37 pytest green. `~/reshade/server`, systemd `--user` on 8010, nginx `/sherbet-auth/`.

### Phase 0b — 클라이언트 인증 게이트 (진행 대상)
- 클라(Sherbet C++): 캐시 토큰 로드 + `/auth/verify`, 잠금 시 **버튼식 로그인 패널**, `/auth/start`→`ShellExecute`(브라우저)→`/auth/poll` 폴링, 토큰 캐시({token,hwid,last_verified}), 3개 게이트(생성자 `runtime.cpp:353`/오버레이 `runtime_gui.cpp:1587`/이펙트 `update_effects` `runtime.cpp:3711`).
- **오프라인 그레이스: 24h** (서버 무응답/503 & last_verified 24h 이내 통과).
- WinInet 헬퍼를 POST+JSON+헤더까지 확장(현재 GET 위주, `runtime_update_check.cpp` 재활용). 응답 JSON 파싱.
- 교체: `s_unlocked`+FNV 오프라인 코드 계층 → 이 인증. (단 Phase 0b는 `sherbet-buyer` 게이트만; 테마별 `is_unlocked` 역할조회는 Phase 1.)
- **결과:** "디코 로그인 + 구매자 역할 없으면 효과·오버레이 잠금." Mac 컴파일 불가 → CI 검증.

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
3. **HWID 바인딩: 결합함 (한계 명시).** 세션토큰 클레임에 노드락 HWID(CPUID + C: 볼륨시리얼, `sherbet_nodelock.hpp` 재활용)를 넣고, 정직한 클라가 실제 HWID를 계산해 대조한다. **주의: 이는 클라이언트측 강제이지 서버 통제가 아니다** — HWID 클레임은 서명된 JWT 안에 평문으로 읽히고 `/auth/verify`는 호출자가 보낸 hwid를 신뢰하므로, 변조 클라를 쓰는 공격자는 토큰에 박힌 hwid를 그대로 되보내 우회할 수 있다. 즉 "정직한 클라가 타 PC에서 실행"은 막지만, 결정된 공격자의 토큰 재바인딩은 막지 못한다. (클라 신뢰 모델의 본질적 한계 — 감수)
4. **역할 명명 규칙 (확정):**
   - `sherbet-buyer` — 실행 기본 게이트 (없으면 Sherbet 실행 불가)
   - `sherbet-theme-<id>` — 테마별 (예: `sherbet-theme-strawberry`)
   - `sherbet-preset-<주문번호>` — 프리셋/커스텀별 (`SHERBET_ORDER_NO` 기반, 없으면 owner)

---

## 13. Phase 1 상세 설계 (원격 테마 — 2026-07-04 확정, 범위 1a+1b)

Phase 0(인증) 완료 후, 테마 언락을 오프라인 FNV 코드에서 **디스코드 역할 + 서버 배포**로 전환한다. **재빌드 없이** 서버에 새 테마를 추가할 수 있게 한다.

### 13.1 테마 JSON 스키마 (C++ `theme` 구조체 매핑, `source/sherbet_theme.hpp`)

```json
{
  "id": "aurora",
  "display_name": "Aurora Sky",
  "colors": {
    "bg0":"#0a1f1aff","bg1":"#0f2e24ff","bg2":"#123a2cff",
    "panel":"#122e26b8","panel_alt":"#183a2fd9",
    "chip":"#134233ff","border":"#5df0c040",
    "text":"#eafff6ff","text_dim":"#8fbfaeff",
    "accent":"#5df0c0ff","accent2":"#a7ffe3ff",
    "glow":"#5df0c073"
  },
  "particle": "leaf",
  "hue_cycle": false,
  "role": "sherbet-theme-aurora"
}
```
- 색은 `#rrggbbaa`(8자리 hex, 알파 포함). 12색 = struct 필드 그대로.
- `particle` = `spark|heart|leaf|petal`.
- `role` = 이 테마 언락에 필요한 **디스코드 역할의 숫자 ID**(문자열; `get_member_role_ids`가 숫자 ID를 반환하므로 이름이 아니라 ID로 매칭). `null`/생략이면 무료.

### 13.2 서버 — `GET /sherbet-auth/content/me` (Bearer)

- **인증:** `Authorization: Bearer <JWT>`. 서명·만료 검증(hwid는 검사 안 함 — 콘텐츠는 신원만 필요). `sub` 추출.
- **역할:** `sub`로 **디스코드 역할 라이브 재조회**(`get_member_role_ids`, verify와 동일). 토큰에 박힌 역할 스냅샷이 아니라 현재 역할 기준.
- **소스:** 서버의 `server/content/themes.json`(어드민이 손으로 편집·추가). 배열 형태.
- **필터:** 각 테마의 `role`(숫자 역할 ID)이 `null`이거나 사용자의 역할 ID 목록에 포함되면 매니페스트에 넣는다.
- **응답:** `{ "themes": [ <themeJSON>, ... ] }`. (프리셋/이펙트는 Phase 2에서 추가.)
- **에러:** 토큰 무효/만료 → 401. 디스코드 재조회 실패 → 503(재시도 신호, 클라는 조용히 실패).
- **배포:** 새 테마 = `themes.json`에 항목 추가 + 디코에서 `sherbet-theme-<id>` 역할 부여. 서버 재시작/재빌드 불필요(요청마다 파일 읽거나 mtime 캐시).

### 13.3 클라 — 동적 테마 레지스트리 (`source/sherbet_themes.cpp` 확장)

- **소유 구조:** `struct owned_theme { std::string id, display_name; theme view; }`. `view.id = id.c_str()`, `view.display_name = display_name.c_str()`. 색/particle/hue_cycle은 값.
- **보관:** `std::vector<std::unique_ptr<owned_theme>> s_dynamic;` (unique_ptr → 벡터 성장에도 owned_theme 주소·문자열 포인터 안정).
- **API 확장:**
  - `find_theme(id)` → 정적 `s_themes` 먼저, 없으면 `s_dynamic` 검색.
  - `all_themes` 는 연속 배열을 반환할 수 없게 되므로 **`std::vector<const theme*> themes_snapshot()`** 신규(정적+동적 합친 뷰)로 교체하고 호출부(마켓 그리드 등)를 이 스냅샷 순회로 바꾼다.
  - `add_dynamic_theme(...)` — id 중복 시 무시(정적 우선). 색/필드 세팅 후 `view` 포인터 연결.
- **파싱:** 작은 JSON(테마 소수)을 전용 파서로 처리. `#rrggbbaa` → `IM_COL32`. particle 문자열 → enum. (Phase 0b `sherbet_auth_core.hpp` 파서 스타일 재활용 또는 별도 `sherbet_theme_json`.)

### 13.4 클라 — 역할 스냅샷 + `is_unlocked` 교체

- **역할 노출:** `sherbet::auth::controller` 가 최근 역할 스냅샷을 보관(verify/poll 응답의 `roles`)하고 `bool has_role(const char *role) const` 제공(락 보호).
- **`is_unlocked(id)` 재구현:**
  - 기본(구매) 테마(`SHERBET_DEFAULT_THEME`) → 항상 열림.
  - 내장 테마 → `_sherbet_auth.has_role("sherbet-theme-<id>")` (소프트 게이트, UI만).
  - 동적 테마 → 서버가 권한자에게만 내려주므로 목록에 존재하면 열림(true).
  - `SHERBET_ONLINE_AUTH==0`(개발) → 모두 열림(현행 개발 편의 유지).
- **제거:** `s_unlocked` map + FNV 테마코드 경로(`sherbet_ui.cpp` `unlock_theme`/`load_unlocked_csv`/`unlocked_csv`/`check_theme_code`), 마켓의 SHRB- InputText(`runtime_gui.cpp:3825` 부근)와 config `Unlocked` 저장/로드. **프리셋(PRE-) 경로는 Phase 2까지 유지.**

### 13.5 클라 — "내 전용 불러오기" 버튼

- 위치: 테마 마켓 상단(또는 홈). 인증된 상태에서만 노출.
- 동작: `sherbet::http::get(host, "/sherbet-auth/content/me", resp, bearer)` (Phase 0b `sherbet_http` 재활용) → 파싱 → `add_dynamic_theme` 병합 → 마켓 그리드 갱신. 백그라운드 스레드(오버레이 논블로킹, Phase 0b 컨트롤러 패턴).
- 실패(401/503/전송실패) → 조용히 상태 텍스트만.

### 13.6 위협 모델 (Phase 1)

- **동적 테마 = 하드 게이트:** 서버가 역할 없는 사용자에게 JSON 자체를 주지 않음 → 변조 클라도 미권한 테마 콘텐츠를 얻지 못한다. FNV 코드보다 강함.
- **내장 7테마 = 소프트 게이트:** 이미 바이너리에 존재 → 역할 체크는 UI 표시뿐(노드락 수준). 그러나 **위조 가능한 평문 오프라인 코드는 완전 제거**된다(§13.4). 이 트레이드오프는 §12.3 클라 신뢰 모델 한계로 감수.

### 13.7 Phase 1 분해

- **1a-server:** `/content/me` 엔드포인트 + `themes.json` 로더 + 역할 필터 (FastAPI, host 테스트).
- **1b-client:** 동적 테마 레지스트리 + JSON 파서(host 테스트) + `has_role`/`is_unlocked` 교체 + "내 전용 불러오기" + 마켓 UI 정리(FNV 제거). Mac 컴파일 불가 부분은 CI.
