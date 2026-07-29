# Sherbet 자동 업데이트 — 3단계: 클라 교체 글루 + UI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 구매자 오버레이에 업데이트 알림을 띄우고, 버튼 한 번으로 DLL이 스스로를 교체하게 한다. 실패하면 되돌린다.

**Architecture:** 프로세스 전역 싱글턴 `sherbet::update::controller`가 워커 1개로 매니페스트를 묻고, 사용자가 [업데이트]를 누르면 같은 워커가 다운로드→검증→rename 2회 교체를 수행한다. `DllMain`의 `on_process_attach`가 다음 실행에서 마커를 읽어 복구·롤백을 판정한다. **모든 판정은 1단계 `sherbet_update_core.hpp`의 순수 함수를 호출한다 — 다시 구현하지 않는다.**

**Tech Stack:** WinInet(기존 `sherbet_http`), Win32 파일 API, 네임드 뮤텍스, ImGui 1.92.5

**선행:** 1단계(판정 로직, CI green) · 2단계(서버 매니페스트, CI green)
**설계 스펙:** `docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md` — **§4.4 S0~S15(교체)와 §5.4 R1~R15(롤백)가 절차의 정본이다. 이 계획은 구조·시그니처·함정만 담고, 단계별 절차는 스펙을 따른다.**

## Global Constraints

- **⚠️ 판정을 다시 구현하지 않는다.** `parse_marker` / `serialize_marker` / `classify` / `decide_repair` / `decide_boot` / `should_offer` / `is_mandatory` / `parse_manifest` / `pe_check` / `url_allowed` / `split_https_url` / SHA-256 / 파일명 접미사 / `make_recovery_note` — 전부 `sherbet_update_core.hpp`에 있다. 손으로 다시 짜면 23만 9천 개 단언이 붙지 않은 두 번째 구현이 구매자 DLL을 바꾸게 된다. **스펙 §5.2가 이를 명시적으로 요구한다.**
- **⚠️ `decide_boot`에는 디스크에서 읽은 그대로의 마커를 넘긴다.** §5.4 R6대로 디스크엔 `tries+1`을 먼저 쓰되, 구조체는 증가시키지 않는다. 증가시켜 넘기면 임계값이 2→1로 반토막 나 멀쩡한 설치가 첫 실패에 롤백된다.
- **⚠️ `boot_marker::unknown`은 `parse_marker`만 채운다.** 손으로 채우면 조작된 줄이 알려진 키로 승격된다.
- **⚠️ 파싱 안 되는 마커는 덮어쓰지 않는다.** 다른 writer의 `unknown` 키가 통째로 사라진다.
- **⚠️ `sha256_final_hex`는 멱등하지 않다.** 교체 직후 재해시는 반드시 새 `sha256_init`부터.
- **⚠️ narrow→wide 변환은 `MultiByteToWideChar(CP_UTF8, ...)`.** `std::wstring(s.begin(), s.end())`(기존 `sherbet_content.cpp` 관례)를 복사하지 말 것 — `CP_ACP` best-fit 매핑이 전각 문자를 ASCII로 접어 경로 조작을 되살린다. (`url_allowed`가 바이트 ≥0x7f를 거부하므로 URL은 이미 안전하지만, 파일 경로에도 같은 규칙을 적용한다.)
- **자기 경로는 `g_reshade_dll_path`** (`dll_main.cpp:111`에서 `GetModuleFileNameW`로 설정). `_config_path.parent_path()`는 `INSTALL/BasePath`로 달라질 수 있어 **절대 금지**.
- **모든 임시파일은 DLL과 같은 디렉터리에만.** `%TEMP%` 경유 금지 — 크로스 볼륨 `MoveFile`은 copy+delete로 강등되어 사용 중 파일에서 실패한다.
- **AV·안티치트 표면 금지 목록:** 자식 프로세스 생성 ❌ / `CreateRemoteThread` ❌ / 자기 이미지 `VirtualProtect`·`WriteProcessMemory` ❌ / 새 DLL을 현재 프로세스에 `LoadLibrary` ❌ / **`MOVEFILE_DELAY_UNTIL_REBOOT` ❌**(Defender가 아는 악성 지표) / **UAC 승격 ❌**.
- **개인화 빌드 보호:** `sherbet::has_owner() || SHERBET_NODELOCK` 이면 업데이트를 제안하지 않는다.
- 한글 UI 문자열은 `\x` UTF-8 이스케이프. **`\xNN` 뒤에 16진수 ASCII가 바로 오면 `error C7744`** — 문자열 리터럴을 분리한다(커밋 `c966d30e`에서 겪음).
- ImGui 1.92.5 + `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` — obsolete API 금지.
- 빌드는 MSBuild `.vcxproj`. 새 `.cpp`는 `ReShade.vcxproj` + `.filters`에 등록해야 하고, **누락하면 CI가 7분 뒤 링크 에러로 실패한다.**
- 커밋 메시지에 AI/클로드 저작권 문구 금지.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `source/sherbet_http.hpp/.cpp` (수정) | `get_to_file` — 파일로 스트리밍, 증분 sha256, 32MiB 상한, 다운로드 전용 타임아웃 |
| `source/sherbet_update.hpp` (신규) | 선언 + 프로세스 전역 싱글턴 `controller &instance()` |
| `source/sherbet_update.cpp` (신규) | Win32 글루 전부 — 뮤텍스·마커IO·페치·다운로드·교체·복구 |
| `source/dll_main.cpp` (수정) | `on_process_attach(self)` 호출 1줄 |
| `source/runtime.cpp` (수정) | `on_present`에서 부팅 성공 래치 |
| `source/runtime.hpp` (수정) | 첫 present 시각 멤버 + `draw_sherbet_update_card` 선언 |
| `source/runtime_gui.cpp` (수정) | `tick()` + 배너 3곳 |
| `ReShade.vcxproj` / `.filters` (수정) | 새 파일 등록 |

**컨트롤러는 `runtime` 멤버가 아니라 프로세스 전역 싱글턴이다.** 런타임은 스왑체인당 하나라, 멤버로 두면 워커 둘이 같은 `.part` 파일을 서로 자른다.

---

## Task 1: `http::get_to_file` — 스트리밍 다운로드

**Files:** Modify `source/sherbet_http.hpp`, `source/sherbet_http.cpp`

**Interfaces:**
- Produces:
```cpp
// 응답 바디를 메모리가 아니라 파일로 흘려 쓴다. 진행률 콜백과 취소 플래그 지원.
// 반환 = HTTP 상태코드(기존 규약: 0 = 전송 실패 또는 절단).
// on_chunk(received, total) 는 UI 진행률용. cancel 이 true 가 되면 즉시 중단하고 0 반환.
int get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest,
                unsigned long long expect_size,
                void *ctx, void (*on_chunk)(void *, unsigned long long, unsigned long long),
                const std::atomic<bool> *cancel);
```

**왜 필요한가:** 기존 `request()`는 `out.append()`로 무한정 커진다. 잘못된 URL이 큰 HTML 에러 페이지를 주면 게임 프로세스가 `bad_alloc`으로 죽는다. 또 기존 타임아웃 5초는 수십 KB fx 기준이라 4MB 다운로드엔 부족하다.

- [ ] **Step 1: `request()`를 싱크 콜백 기반으로 리팩터**

문자열 수집과 파일 스트리밍이 **같은 절단 탐지 로직**을 공유하도록 한다. 기존 `post_json`/`get`의 동작과 반환값은 한 바이트도 바뀌면 안 된다(인증·콘텐츠 다운로드가 이미 쓰고 있다).

핵심 규칙(기존 `sherbet_http.cpp:62-81`에서 그대로 가져올 것):
- `InternetReadFile`이 루프 중간에 false를 반환하면 **절단**이므로 EOF로 눙치지 않고 실패 처리
- `Content-Length`가 있으면 수신량과 대조
- 절단이면 `out.clear()` 후 `0` 반환

- [ ] **Step 2: `get_to_file` 추가**

추가 규칙:
- 다운로드 경로만 **수신 타임아웃 20초 + 총 5분 캡**(기존 5초는 fx 기준)
- 루프 안에서 **32MiB 상한** 검사 — 넘으면 즉시 중단, `.part` 삭제, 0 반환
- 청크마다 `cancel` 확인 → 즉시 중단
- **최종 절단 게이트는 `Content-Length`가 아니라 매니페스트의 `size` + `sha256`이다.** GitHub 리다이렉트 종착지가 chunked일 수 있으므로 `Content-Length` 부재를 실패로 보지 않는다.

- [ ] **Step 3: 기존 호출자 회귀 확인**

Run: `cd ~/reshade && grep -rn "http::get\|http::post_json" source/ | grep -v sherbet_http`
Expected: `sherbet_auth.cpp`(start/verify/poll), `sherbet_content.cpp`(fetch/fetch_file). 이들의 동작이 바뀌면 안 된다.

- [ ] **Step 4: 커밋**

```bash
git add source/sherbet_http.hpp source/sherbet_http.cpp
git commit -m "http: 파일 스트리밍 다운로드 추가

기존 request 는 응답을 메모리에 무한정 모은다 — 잘못된 URL 이 큰 HTML 에러
페이지를 주면 게임 프로세스가 bad_alloc 으로 죽는다. 파일로 흘려 쓰고
루프 안에서 32MiB 상한을 검사한다. 다운로드 경로만 타임아웃을 20초로 분리
(기존 5초는 수십 KB fx 기준이라 4MB 에 부족)."
```

---

## Task 2: `sherbet_update` 골격 + 마커 IO + `on_process_attach`

**Files:** Create `source/sherbet_update.hpp`, `source/sherbet_update.cpp`; modify `ReShade.vcxproj`, `.filters`

**Interfaces:**
```cpp
namespace sherbet { namespace update {
    // DllMain 에서 호출. 스펙 §5.4 R1~R11. 원시 Win32 만 쓰되 판정은 코어 함수를 부른다.
    void on_process_attach(const std::wstring &self_path);
    // 부팅 성공 래치(§5.3). 프로세스 전역 static 으로 1회만.
    void mark_boot_ok();
    // 롤백된 (버전,sha) — should_offer 의 블랙리스트 인자
    const std::string &rolled_back_version();
    const std::string &rolled_back_sha();
    bool safe_mode();   // 롤백 직후 세션. update_effects 를 조기 반환시킨다.
}}
```

**⚠️ `DllMain` 제약(스펙 §5.2 개정본):** `LoadLibrary` 금지, 스레드 생성·교차대기 금지, CNG/crypto 금지, 로더 재진입을 유발하는 경로 정규화 금지. **C++ 표준 라이브러리 사용은 금지가 아니다** — `dll_main.cpp:117-201`이 이미 `std::filesystem`·`ini_file`을 쓴다. 그러니 코어 함수를 그대로 부른다.

- [ ] **Step 1: 뮤텍스와 마커 IO**

- 네임드 뮤텍스 `Local\Sherbet.Update.<fnv1a(소문자 DIR)>` — `sherbet_license.hpp`의 `fnv1a` 재사용. 프로세스가 죽으면 OS가 해제하므로 `.lock` 파일은 쓰지 않는다(stale 문제).
- 마커 읽기: 실패하면 **덮어쓰지 않는다**(다른 writer의 `unknown` 소실 방지). `can_write=false`로 기록만 하고 복구는 계속 진행한다 — 시뮬레이터가 그 동작을 전수 검증했다.
- 마커 쓰기: `.part`에 쓰고 `MoveFileExW(REPLACE_EXISTING)`로 원자 교체.

- [ ] **Step 2: `on_process_attach` — 스펙 §5.4 R1~R11 그대로**

호출 위치: `dll_main.cpp`의 **"다른 ReShade 인스턴스 이미 로드됨" 검사(`return FALSE`) 다음, hooks 설치 앞.**

3중 게이트(§5.2) — 셋 다 통과할 때만 `tries+1`:
1. `marker.version == SHERBET_VERSION`
2. `marker.exe == g_target_executable_path.filename()`
3. 네임드 뮤텍스 보유

**게이트 2가 핵심이다.** 구매자는 DLL을 `dxgi.dll`로 넣으므로 `is_dxgi==true` → `dll_main.cpp:127`의 "설정파일 없으면 `return FALSE`" 블록이 통째로 스킵되고, 그 폴더에서 dxgi를 임포트하는 FiveM 런처·게임·NUI 서브프로세스가 전부 로드된다. **exe 게이트가 없으면 멀쩡한 버전이 첫 실행에 `tries` 3을 찍고 무조건 롤백된다.**

복구는 `classify` → `decide_repair`를 그대로 호출한다. `restore_from_bak`은 `bad_ver`/`bad_sha`도 기록해야 한다(§5.4 R10, 시뮬레이터 P8이 의존).

- [ ] **Step 3: vcxproj 등록**

`ReShade.vcxproj`의 `<ClCompile>`에 `source\sherbet_update.cpp`, `<ClInclude>`에 `sherbet_update.hpp`. `.filters`도 다른 sherbet 파일과 같은 필터로. **빠뜨리면 CI가 7분 뒤 링크 에러.**

- [ ] **Step 4: 커밋 + CI green 확인**

```bash
git add source/sherbet_update.hpp source/sherbet_update.cpp source/dll_main.cpp ReShade.vcxproj ReShade.vcxproj.filters
git commit -m "업데이트 부팅 경로 추가 — 마커 IO 와 자동 롤백

DllMain 에서 마커를 읽어 §5.4 R1~R11 을 수행한다. 판정은 전부
sherbet_update_core.hpp 의 순수 함수를 호출한다 — 손으로 다시 짜면
23만9천 단언이 붙지 않은 두 번째 구현이 구매자 DLL 을 바꾸게 된다.
exe 게이트가 없으면 FiveM 서브프로세스들이 각각 카운트해 멀쩡한 버전이 롤백된다."
git push && gh run list --workflow=build.yml --limit 1
```

---

## Task 3: 매니페스트 페치 + 컨트롤러

**Files:** Modify `source/sherbet_update.hpp`, `source/sherbet_update.cpp`

**Interfaces:**
```cpp
class controller {
public:
    void init(const std::wstring &self_path);   // 런타임 생성 시 1회
    void tick();                                // 렌더 스레드에서 매 프레임
    bool has_offer() const;                     // 배너를 띄울까
    bool is_mandatory() const;
    std::string offer_version() const;
    std::string offer_notes() const;
    std::string status_text() const;            // 락 안에서 값 복사
    float progress() const;                     // 0.0~1.0, 다운로드 중
    void begin_update();                        // [업데이트] 클릭
    void dismiss_session();                     // [나중에]
    void clear_blacklist();                     // [그래도 다시 시도] (§5.4 R13)
    bool need_restart() const;
};
controller &instance();
```

**스레드 규약(기존 `sherbet_auth::controller` 패턴 그대로):**
- 조인 가능한 워커 멤버 1개. **`std::thread` 대입 바로 앞에서 반드시 `join()`** — 끝났지만 joinable인 스레드에 재대입하면 `std::terminate`, 즉 게임 즉사.
- `_stop` 아토믹, 소멸자에서 `_stop = true` 후 join.
- `status_text()`는 락 안에서 `std::string` **값 복사** 반환.

- [ ] **Step 1: 페치 워커**

`GET https://wonryeol.asuscomm.com/sherbet-auth/update/manifest?arch=x64&cur=<SHERBET_VERSION>` (미인증, bearer **nullptr**). `#ifdef _WIN64`로 arch 결정. 응답을 `parse_manifest`에 넘기고, `should_offer(SHERBET_VERSION, u, rolled_back_version(), rolled_back_sha())`로 제안 여부를 판정한다.

실패(503/전송실패/파싱실패)는 **조용히 "업데이트 없음"**. 인증의 24h 오프라인 유예에 아무 영향이 없어야 한다.

- [ ] **Step 2: 개인화 빌드 가드**

`sherbet::has_owner() || SHERBET_NODELOCK` 이면 제안하지 않고 "개인 빌드는 디스코드로 문의"만 표시한다. 공용 릴리스가 각인·전용 프리셋을 지우고 노드락을 조용히 끄는 것을 막는다.

- [ ] **Step 3: 커밋 + CI green**

---

## Task 4: 다운로드 · 검증 · 교체 (★ 가장 위험)

**Files:** Modify `source/sherbet_update.cpp`

**절차는 스펙 §4.4 S0~S15를 그대로 따른다.** 각 단계의 실패 시 디스크 상태가 표로 정리돼 있으니 반드시 대조하며 구현할 것.

특히 놓치기 쉬운 것:

| 단계 | 함정 |
|---|---|
| S0 | 워커 재대입 전 `join()` 필수 |
| S1 | 뮤텍스는 `WaitForSingleObject(..., 0)` 즉시 시도. 실패면 "다른 게임 창에서 업데이트 중" |
| S3 | `GetDriveTypeW`가 `DRIVE_REMOTE`면 중단. 쓰기 실측은 `CREATE_NEW\|FILE_FLAG_DELETE_ON_CLOSE`. `size*3+32MiB` 여유 확인. **UAC 승격 절대 금지** |
| S4 | `.sherbet-new`가 있고 sha가 일치하면 S5~S7 건너뛰고 재개 |
| S6 | 검증 순서: 수신크기 == `size` → sha256 == `sha256` → `pe_check`(앞 4KB, `#ifdef _WIN64`로 want_x64) |
| S9 | **복구안내를 먼저 쓴다.** `make_recovery_note(self_name, bak_name)` — 하드코딩 금지, 실제 파일명이 들어가야 고객이 무엇을 되돌릴지 안다 |
| S10 | `MoveFileExW(self → .bak, dwFlags=0)`. **`REPLACE_EXISTING` 금지** |
| S11 | 실패면 즉시 `.bak → self` 원복 5회 재시도 |
| S12 | 사후 재해시 — **새 `sha256_init`부터**(`sha256_final_hex`는 멱등하지 않다) |
| S13 | 마커 `state=pending`, **`tries=0`** — 이 리셋이 없으면 롤백 후 남은 `tries=2`가 물려져 새 빌드가 첫 부팅에 롤백된다 |

- [ ] **Step 1: S0~S7 (다운로드·검증·스테이징)** — 여기까지는 원본 무변화
- [ ] **Step 2: S8~S14 (백업·설치·확정)** — self가 없는 위험 구간은 S10~S11 사이 1ms 미만
- [ ] **Step 3: 커밋 + CI green**

---

## Task 5: 부팅 성공 래치 + 배선

**Files:** Modify `source/runtime.cpp`, `source/runtime.hpp`, `source/dll_main.cpp`

- [ ] **Step 1: `on_present`에 래치**

`runtime.cpp`의 `_frame_count++` 지점(약 816행) 뒤에:
- **프로세스 전역 static 래치 1회** + (`_frame_count >= 300` **또는** 첫 present 이후 20초)
- **`runtime_gui.cpp`의 `tick()` 옆은 절대 안 된다** — 그 블록은 `if (_show_overlay)` 안이라 Home 키를 안 누른 세션이 통째로 "실패"가 되는 100% 오탐이 난다
- `==`가 아니라 `>=` — `_frame_count`는 `runtime.cpp:595`에서 (재)초기화마다 0으로 리셋된다(전체화면 전환·해상도 변경)
- 삭제는 §5.2 게이트 (1)(2)를 통과하고 `state==pending`인 마커만. `rolledback`은 건드리지 않는다(블랙리스트가 날아간다)

- [ ] **Step 2: safe_mode 배선**

롤백 직후 세션은 `update_effects`를 조기 반환시키고, Sherbet UI 대신 "이전 버전으로 되돌렸습니다" 패널만 그린다. **`return FALSE`로 로딩을 중단하면 dxgi 프록시 export가 사라져 게임 자체가 안 켜진다.**

- [ ] **Step 3: 커밋 + CI green**

---

## Task 6: 오버레이 UI

**Files:** Modify `source/runtime_gui.cpp`, `source/runtime.hpp`

- [ ] **Step 1: `tick()` 호출**

`runtime_gui.cpp:1606` 부근 `_sherbet_auth.tick()` 옆에 `sherbet::update::instance().tick()`.

- [ ] **Step 2: 배너 3곳**

**오버레이 안에만 두면 Home 키를 안 누르는 구매자에게 영영 도달하지 않는다.**

1. **홈 탭 최상단** — 버전 + 변경내역 + `[업데이트]` `[나중에]`
2. **인증 게이트 패널 안** — 로그인이 깨진 빌드를 구제하는 유일한 경로
3. **부팅 스플래시 한 줄**

상태별 표시:
- 제안: "새 버전 v1.4.0이 나왔어요" + `notes` 줄바꿈 렌더
- 다운로드 중: 진행률 바 + `[취소]`
- 완료: "업데이트 완료 — 게임을 껐다 켜면 v1.4.0이 적용돼요"
- 롤백됨: "v1.4.0 업데이트가 실패해서 이전 버전으로 되돌렸어요" + `[디스코드 문의]` + **`[그래도 다시 시도]`**
- 필수(`is_mandatory`): 빨간색 + `[디스코드 문의]`. **세션 단위 닫기를 허용하고 오버레이·이펙트를 절대 막지 않는다** — 서버 오타 하나로 전 고객 UI를 잠그면 안 된다

**`[그래도 다시 시도]`는 구현 시 절대 빼지 말 것**(§5.4 R13). 자동 롤백 오탐의 비용을 클릭 1회로 떨어뜨리는 장치다.

- [ ] **Step 3: 한글 문자열 이스케이프 검증**

`\xNN` 뒤에 16진수 ASCII가 오면 `C7744`. 커밋 전에 로컬에서 문자열만 뽑아 확인하거나, CI 실패 시 그 줄을 의심할 것.

- [ ] **Step 4: 커밋 + CI green**

---

## Task 7: 통합 검증

- [ ] **Step 1: 전 스위트 + CI**

세 C++ 스위트 + 서버 85개 + CI 세 잡 모두 green.

- [ ] **Step 2: 오너 인게임 E2E (윈도우, 사용자)**

내가 절차를 단계별로 적어 전달한다. 최소:
1. `SHERBET_ONLINE_AUTH=1` 빌드로 게임 실행 → 오버레이에 배너가 뜨는가
2. `[업데이트]` → 진행률 → "재시작하면 적용" 표시
3. 게임 재시작 → About에 새 버전 → 300프레임 뒤 `sherbet.update` 소멸 → `.sherbet-bak` 1개만
4. **롤백 리허설:** 일부러 깨진 DLL을 릴리스해 2회 실패 → 자동 복원 → `[그래도 다시 시도]` 확인
5. **exe 게이트 실측:** FiveM 켠 상태에서 어떤 프로세스가 dxgi.dll을 로드하는지 확인하고, 정상 업데이트 후 첫 실행에서 `tries`가 정확히 1인지. **이 설계에서 가장 조용히 틀릴 수 있는 지점이다.**

---

## Self-Review

**1. 스펙 커버리지** — §4(자기교체) 전부, §5.2~5.4(부팅마커·롤백) 전부, §6(클라 구조) 전부, §3.4~3.5의 클라측 소비. 다루지 않는 것: §7의 실제 릴리스 발행(운영), §10(첫 배포, 오너 몫).

**2. 플레이스홀더** — 절차 세부는 의도적으로 스펙 §4.4/§5.4를 참조한다. 그 두 절이 단계별 표와 실패 시 디스크 상태를 이미 담고 있고, 여기 복사하면 두 곳이 손으로 동기화되어야 한다. 구현자는 반드시 스펙을 함께 읽을 것.

**3. 타입 일관성** — 모든 판정 함수 시그니처는 `sherbet_update_core.hpp` 그대로. 새로 만드는 것은 `controller`와 `on_process_attach`/`mark_boot_ok`뿐이다.

---

## 이 계획이 끝나면

- 구매자가 오버레이에서 업데이트 알림을 보고 버튼 한 번으로 새 버전을 받는다
- 실패하면 자동으로 되돌아가고, 오탐이면 클릭 한 번으로 재시도한다
- **남은 것은 새 DLL을 빌드해 구매자에게 1회 수동 배포하는 것뿐** — 그 뒤로는 영원히 자동
