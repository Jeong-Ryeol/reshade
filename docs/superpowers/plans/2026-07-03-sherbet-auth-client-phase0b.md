# Sherbet Phase 0b — 클라이언트 인증 게이트 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sherbet(리쉐이드 C++ 포크)가 실행 시 디스코드 OAuth로 로그인하고, `sherbet-buyer` 역할이 없으면 효과·오버레이를 잠그도록 클라이언트 인증 게이트를 붙인다.

**Architecture:** 순수 로직(응답 파싱·토큰 캐시 직렬화·오프라인 그레이스·게이트 결정)을 Windows 비의존 헤더 `sherbet_auth_core.hpp`로 분리해 Mac에서 clang로 실제 단위테스트하고, WinInet/ShellExecute/스레드/런타임 배선 같은 플랫폼 글루는 `sherbet_http.*`/`sherbet_auth.*`에 두고 GitHub Actions CI 빌드로 검증한다. 게이트 3곳(생성자·오버레이·이펙트)이 하나의 `sherbet::auth::controller` 상태를 읽는다.

**Tech Stack:** C++17, WinInet(HTTP), ShellExecuteW(브라우저), ImGui 1.92.5, std::thread/atomic. 서버는 이미 배포된 FastAPI(`https://wonryeol.asuscomm.com/sherbet-auth`).

## Global Constraints

- 브랜치 `sherbet-base`. 작업 리포 `~/reshade`.
- **Mac에선 Windows 의존 파일 컴파일 불가** → 그런 파일의 유일 검증은 **GitHub Actions CI green(약 7분)**. Windows 비의존 헤더/테스트는 로컬 clang로 선검증: `clang++ -std=c++17 -fsyntax-only <file>` (ImGui 포함 파일은 `-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource`).
- 빌드는 **ImGui 1.92.5 + `/D IMGUI_DISABLE_OBSOLETE_FUNCTIONS`** — obsolete API 금지(TabActive→TabSelected 등).
- 새 파일 헤더 주석 = `Copyright (C) 2026 정렬 (Jeong-Ryeol)` / `SPDX-License-Identifier: BSD-3-Clause`.
- **커밋 메시지에 클로드/AI 저작권·공동작성 문구 금지.**
- 베이스 URL = `https://wonryeol.asuscomm.com/sherbet-auth`. 엔드포인트: `POST /auth/start {hwid}`→`{state,authorize_url}` / `GET /auth/poll?state=`→`{status,token?,reason?}` / `POST /auth/verify {token,hwid}`→`{valid,sub?,roles?}` 또는 `503 {valid:null,error}`.
- 오프라인 그레이스 = **24시간(86400초)**. 서버 무응답/503 & 마지막 검증 24h 이내면 통과.
- Phase 0b는 **`sherbet-buyer` 실행 게이트만**. 테마별 `is_unlocked`(역할조회)와 `/content/*`는 Phase 1+. 기존 `s_unlocked`+FNV 계층은 **이번엔 건드리지 않는다**(Phase 1에서 교체).
- 인증은 컴파일 플래그 **`SHERBET_ONLINE_AUTH`** 로 게이트(노드락 `SHERBET_NODELOCK` 패턴 동일). 기본 0(개발 빌드=항상 통과), CI 배포 빌드에서 1. `SHERBET_ONLINE_AUTH==0`이면 `controller`는 no-op이며 항상 `authed`.

---

## File Structure

**신규:**
- `source/sherbet_auth_core.hpp` — Windows 비의존 순수 로직: 응답 구조체, 작은 JSON 파서, 토큰 캐시 직렬화, 그레이스/게이트 결정. **호스트 컴파일·단위테스트 대상.**
- `source/sherbet_http.hpp` / `source/sherbet_http.cpp` — WinInet POST/GET(JSON 바디·헤더·Bearer) → `(status, body)`. Windows-only, CI 검증.
- `source/sherbet_auth.hpp` / `source/sherbet_auth.cpp` — `sherbet::auth::controller`(캐시 로드, 비동기 verify, 로그인 흐름=start→ShellExecute→poll 스레드, 프레임 tick). Windows-only, CI 검증.
- `tools/sherbet_auth_test.cpp` — `sherbet_auth_core.hpp` 단위테스트(프레임워크 없이 assert). Mac clang로 빌드·실행.

**수정:**
- `source/sherbet_nodelock.hpp` — `hwid()`를 `SHERBET_NODELOCK` 여부와 무관하게 `_WIN32`에서 항상 노출.
- `source/sherbet_owner.h` — `SHERBET_ONLINE_AUTH` 기본값 정의(없으면 0).
- `source/runtime.hpp` — `controller` 멤버 + 접근자 선언.
- `source/runtime.cpp` — 생성자에서 auth init, `update_effects()` 이펙트 게이트.
- `source/runtime_gui.cpp` — 오버레이(1587)에서 로그인 패널 + 버튼 + 프레임 poll.
- `ReShade.vcxproj` / `ReShade.vcxproj.filters` — 새 `.cpp`(`sherbet_http.cpp`, `sherbet_auth.cpp`) 등록.

---

## Task 1: `hwid()` 무조건 노출 (nodelock 리팩터)

**Files:**
- Modify: `source/sherbet_nodelock.hpp`

**Interfaces:**
- Produces: `sherbet::nodelock::hwid()` → `std::string` 이 `SHERBET_NODELOCK` 값과 무관하게 `_WIN32` 빌드에서 항상 호출 가능.

현재 `hwid()`는 `#if SHERBET_NODELOCK && defined(_WIN32)` 안에만 있어 기본 빌드(NODELOCK=0)에선 없다. 인증은 NODELOCK과 독립적으로 HWID가 필요하므로 분리한다.

- [ ] **Step 1: `hwid()`/`lic_path()` 정의를 `_WIN32` 전용 블록으로 이동**

`source/sherbet_nodelock.hpp`에서, 파일 상단 `namespace sherbet { namespace nodelock { inline bool enabled()... } }` 바로 아래에 아래 블록을 **새로 추가**한다(기존 `#if SHERBET_NODELOCK` 블록 안의 `hwid()` 정의는 Step 2에서 제거):

```cpp
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <intrin.h>
#include <string>
#include <sstream>

namespace sherbet
{
	namespace nodelock
	{
		// 하드웨어 지문: CPUID(feature) + C: 볼륨 시리얼. NODELOCK 여부와 무관하게 인증(HWID 결합)에서 쓴다.
		inline std::string hwid()
		{
			int cpu[4] = { 0, 0, 0, 0 };
			__cpuid(cpu, 1);

			DWORD volume_serial = 0;
			GetVolumeInformationA("C:\\", nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0);

			std::stringstream ss;
			ss << std::hex << cpu[3] << cpu[0] << "_" << volume_serial;
			return ss.str();
		}
	}
}

#endif // _WIN32
```

- [ ] **Step 2: 기존 `#if SHERBET_NODELOCK` 블록에서 중복된 `hwid()` 정의 제거**

`source/sherbet_nodelock.hpp`의 `#if SHERBET_NODELOCK && defined(_WIN32)` 블록 안에 있던 `inline std::string hwid() { ... }` 정의(56~67행 부근)를 통째로 삭제한다. `check_and_register()`는 그대로 `hwid()`를 호출한다(위에서 이미 정의됨). 같은 블록의 `#include <intrin.h>`/`<sstream>` 중복 include는 그대로 둬도 무방(헤더 가드/`#ifndef`로 안전).

- [ ] **Step 3: 로컬 구문 선검증**

Run: `clang++ -std=c++17 -fsyntax-only -DSHERBET_NODELOCK=0 source/sherbet_nodelock.hpp`
Expected: Mac에선 `Windows.h` 부재로 전체 컴파일은 불가하나, **문법 파싱 단계 에러(중복정의/괄호)** 는 없어야 한다. `Windows.h` not found 류 include 에러만 나오면 통과로 간주. (실제 검증은 Task 9의 CI.)

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_nodelock.hpp
git commit -m "nodelock: hwid()를 NODELOCK 여부와 무관하게 _WIN32에서 항상 노출"
```

---

## Task 2: `sherbet_auth_core.hpp` — 응답 파싱 + 단위테스트 하네스

**Files:**
- Create: `source/sherbet_auth_core.hpp`
- Create: `tools/sherbet_auth_test.cpp`

**Interfaces:**
- Produces:
  - `struct sherbet::auth::start_result { bool ok; std::string state, authorize_url; };`
  - `struct sherbet::auth::poll_result { enum status_t { pending, ready, denied, error } status; std::string token, reason; };`
  - `struct sherbet::auth::verify_result { bool ok; bool upstream_down; std::string sub; std::vector<std::string> roles; };`
  - `start_result parse_start(const std::string &body);`
  - `poll_result parse_poll(const std::string &body);`
  - `verify_result parse_verify(const std::string &body, int http_status);`
  - 저수준: `bool json_string(const std::string &body, const char *key, std::string &out);` / `int json_bool_or_null(const std::string &body, const char *key);`(1=true,0=false,-1=null/없음) / `std::vector<std::string> json_string_array(const std::string &body, const char *key);`

응답이 작고 형식이 고정(서버가 우리 것)이라 전용 미니 파서로 충분하다. 이스케이프는 서버 응답에 사실상 없지만 `\"`/`\\`만 최소 처리한다.

- [ ] **Step 1: 실패 테스트 작성 — `tools/sherbet_auth_test.cpp`**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
#include "sherbet_auth_core.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::auth;

static void test_parse_start() {
	auto r = parse_start(R"({"state":"abc123","authorize_url":"https://discord.com/oauth2/authorize?x=1"})");
	assert(r.ok);
	assert(r.state == "abc123");
	assert(r.authorize_url == "https://discord.com/oauth2/authorize?x=1");
	auto bad = parse_start(R"({"detail":"nope"})");
	assert(!bad.ok);
}

static void test_parse_poll() {
	auto p = parse_poll(R"({"status":"pending"})");
	assert(p.status == poll_result::pending);
	auto r = parse_poll(R"({"status":"ready","token":"JWT.tok.en"})");
	assert(r.status == poll_result::ready && r.token == "JWT.tok.en");
	auto d = parse_poll(R"({"status":"denied","reason":"no_buyer_role"})");
	assert(d.status == poll_result::denied && d.reason == "no_buyer_role");
}

static void test_parse_verify() {
	auto ok = parse_verify(R"({"valid":true,"sub":"123","roles":["sherbet-buyer","900"]})", 200);
	assert(ok.ok && !ok.upstream_down && ok.sub == "123");
	assert(ok.roles.size() == 2 && ok.roles[0] == "sherbet-buyer");
	auto no = parse_verify(R"({"valid":false})", 200);
	assert(!no.ok && !no.upstream_down);
	auto down = parse_verify(R"({"valid":null,"error":"upstream_unavailable"})", 503);
	assert(!down.ok && down.upstream_down);
	auto transport = parse_verify("", 0); // 전송 자체 실패
	assert(!transport.ok && transport.upstream_down);
}

int main() {
	test_parse_start();
	test_parse_poll();
	test_parse_verify();
	std::printf("sherbet_auth_core parsing: ALL PASS\n");
	return 0;
}
```

- [ ] **Step 2: 컴파일 실패 확인(헤더 없음)**

Run: `clang++ -std=c++17 -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test`
Expected: FAIL — `sherbet_auth_core.hpp` 파일이 없어 include 에러.

- [ ] **Step 3: `source/sherbet_auth_core.hpp` 구현**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 온라인 인증 — 순수 로직(플랫폼 비의존). WinInet/스레드/ImGui 등은 여기 넣지 않는다.
#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
	namespace auth
	{
		struct start_result { bool ok = false; std::string state; std::string authorize_url; };
		struct poll_result { enum status_t { pending, ready, denied, error } status = error; std::string token; std::string reason; };
		struct verify_result { bool ok = false; bool upstream_down = false; std::string sub; std::vector<std::string> roles; };

		// "key" 다음의 문자열 값을 찾는다. 매우 단순: `"key"` 뒤 첫 `"`쌍의 내용을 읽되 \" \\ 만 언이스케이프.
		inline bool json_string(const std::string &body, const char *key, std::string &out)
		{
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return false;
			std::size_t colon = body.find(':', k + needle.size());
			if (colon == std::string::npos) return false;
			// 값 시작(공백 스킵)
			std::size_t i = colon + 1;
			while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
			if (i >= body.size() || body[i] != '"') return false; // 문자열 아님
			++i;
			std::string v;
			for (; i < body.size(); ++i) {
				char c = body[i];
				if (c == '\\' && i + 1 < body.size()) { char n = body[++i]; v += (n == 'n' ? '\n' : n); continue; }
				if (c == '"') { out = v; return true; }
				v += c;
			}
			return false;
		}

		// 1=true, 0=false, -1=null 또는 없음/불리언 아님.
		inline int json_bool_or_null(const std::string &body, const char *key)
		{
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return -1;
			std::size_t colon = body.find(':', k + needle.size());
			if (colon == std::string::npos) return -1;
			std::size_t i = colon + 1;
			while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
			if (body.compare(i, 4, "true") == 0) return 1;
			if (body.compare(i, 5, "false") == 0) return 0;
			return -1; // null 포함
		}

		// "key":["a","b"] → {"a","b"}
		inline std::vector<std::string> json_string_array(const std::string &body, const char *key)
		{
			std::vector<std::string> out;
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return out;
			std::size_t lb = body.find('[', k);
			if (lb == std::string::npos) return out;
			std::size_t rb = body.find(']', lb);
			if (rb == std::string::npos) return out;
			for (std::size_t i = lb + 1; i < rb; ) {
				std::size_t q1 = body.find('"', i);
				if (q1 == std::string::npos || q1 >= rb) break;
				std::size_t q2 = body.find('"', q1 + 1);
				if (q2 == std::string::npos || q2 > rb) break;
				out.push_back(body.substr(q1 + 1, q2 - q1 - 1));
				i = q2 + 1;
			}
			return out;
		}

		inline start_result parse_start(const std::string &body)
		{
			start_result r;
			r.ok = json_string(body, "state", r.state) && json_string(body, "authorize_url", r.authorize_url);
			return r;
		}

		inline poll_result parse_poll(const std::string &body)
		{
			poll_result r;
			std::string status;
			if (!json_string(body, "status", status)) { r.status = poll_result::error; return r; }
			if (status == "pending") r.status = poll_result::pending;
			else if (status == "ready") { r.status = poll_result::ready; json_string(body, "token", r.token); }
			else if (status == "denied") { r.status = poll_result::denied; json_string(body, "reason", r.reason); }
			else r.status = poll_result::error;
			return r;
		}

		inline verify_result parse_verify(const std::string &body, int http_status)
		{
			verify_result r;
			if (http_status == 0 || http_status >= 500) { r.upstream_down = true; return r; } // 전송 실패 또는 503/5xx
			const int v = json_bool_or_null(body, "valid");
			if (v == 1) {
				r.ok = true;
				json_string(body, "sub", r.sub);
				r.roles = json_string_array(body, "roles");
			}
			return r; // v==0(구매자 아님) 또는 v==-1 → ok=false, upstream_down=false
		}
	}
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `clang++ -std=c++17 -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test && /tmp/sherbet_auth_test`
Expected: `sherbet_auth_core parsing: ALL PASS` 출력, 종료코드 0.

- [ ] **Step 5: Commit**

```bash
git add source/sherbet_auth_core.hpp tools/sherbet_auth_test.cpp
git commit -m "auth: 순수 로직 헤더 + 응답 파서(start/poll/verify) + 호스트 단위테스트"
```

---

## Task 3: 토큰 캐시 직렬화 + 오프라인 그레이스 + 게이트 결정

**Files:**
- Modify: `source/sherbet_auth_core.hpp`
- Modify: `tools/sherbet_auth_test.cpp`

**Interfaces:**
- Consumes: `verify_result`(Task 2)
- Produces:
  - `struct sherbet::auth::token_cache { std::string token, hwid; long long last_verified_unix = 0; };`
  - `std::string serialize_cache(const token_cache &c);`
  - `bool parse_cache(const std::string &text, token_cache &out);`
  - `bool allow_offline(long long last_verified_unix, long long now_unix, long long grace_secs);`
  - `enum class gate { locked, authed };`
  - `gate decide(const verify_result &vr, const token_cache &cache, long long now_unix, long long grace_secs);`

- [ ] **Step 1: 실패 테스트 추가 — `tools/sherbet_auth_test.cpp`**

`main()` 위에 아래 함수들을 추가하고 `main()`에서 호출한다:

```cpp
static void test_cache_roundtrip() {
	token_cache c; c.token = "JWT.tok.en"; c.hwid = "HWABC_123"; c.last_verified_unix = 1751560000LL;
	std::string s = serialize_cache(c);
	token_cache back;
	assert(parse_cache(s, back));
	assert(back.token == c.token && back.hwid == c.hwid && back.last_verified_unix == c.last_verified_unix);
	token_cache empty;
	assert(!parse_cache("garbage-no-fields", empty)); // 토큰 없으면 실패
}

static void test_grace() {
	// grace=86400. 마지막 검증 t=1000.
	assert(allow_offline(1000, 1000 + 86399, 86400));   // 24h 직전 → 허용
	assert(!allow_offline(1000, 1000 + 86401, 86400));  // 24h 초과 → 거부
	assert(!allow_offline(0, 99999, 86400));            // last_verified 없음(0) → 거부
}

static void test_decide() {
	token_cache c; c.token = "t"; c.last_verified_unix = 1000;
	// 온라인 검증 성공 → authed
	verify_result ok; ok.ok = true;
	assert(decide(ok, c, 5000, 86400) == gate::authed);
	// 구매자 아님(valid:false) → locked (그레이스 무관)
	verify_result no;
	assert(decide(no, c, 1000 + 10, 86400) == gate::locked);
	// 서버 다운 + 그레이스 내 → authed
	verify_result down; down.upstream_down = true;
	assert(decide(down, c, 1000 + 100, 86400) == gate::authed);
	// 서버 다운 + 그레이스 초과 → locked
	assert(decide(down, c, 1000 + 90000, 86400) == gate::locked);
	// 서버 다운 + 캐시 토큰 없음 → locked
	token_cache none;
	assert(decide(down, none, 1000, 86400) == gate::locked);
}
```

`main()`에 추가:
```cpp
	test_cache_roundtrip();
	test_grace();
	test_decide();
```

- [ ] **Step 2: 실패 확인**

Run: `clang++ -std=c++17 -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test`
Expected: FAIL — `token_cache`/`serialize_cache`/`allow_offline`/`decide` 미정의.

- [ ] **Step 3: `sherbet_auth_core.hpp`에 구현 추가**

`namespace auth` 안, `parse_verify` 아래에 추가:

```cpp
		struct token_cache { std::string token; std::string hwid; long long last_verified_unix = 0; };

		inline std::string serialize_cache(const token_cache &c)
		{
			std::string o;
			o += "token=" + c.token + "\n";
			o += "hwid=" + c.hwid + "\n";
			o += "last_verified=" + std::to_string(c.last_verified_unix) + "\n";
			return o;
		}

		inline bool parse_cache(const std::string &text, token_cache &out)
		{
			token_cache tmp;
			std::size_t i = 0;
			while (i < text.size()) {
				std::size_t eol = text.find('\n', i);
				std::string line = text.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
				i = (eol == std::string::npos) ? text.size() : eol + 1;
				std::size_t eq = line.find('=');
				if (eq == std::string::npos) continue;
				std::string key = line.substr(0, eq), val = line.substr(eq + 1);
				while (!val.empty() && (val.back() == '\r' || val.back() == ' ')) val.pop_back();
				if (key == "token") tmp.token = val;
				else if (key == "hwid") tmp.hwid = val;
				else if (key == "last_verified") { try { tmp.last_verified_unix = std::stoll(val); } catch (...) { tmp.last_verified_unix = 0; } }
			}
			if (tmp.token.empty()) return false;
			out = tmp;
			return true;
		}

		inline bool allow_offline(long long last_verified_unix, long long now_unix, long long grace_secs)
		{
			if (last_verified_unix <= 0) return false;
			return (now_unix - last_verified_unix) <= grace_secs;
		}

		enum class gate { locked, authed };

		inline gate decide(const verify_result &vr, const token_cache &cache, long long now_unix, long long grace_secs)
		{
			if (vr.ok) return gate::authed;                                   // 온라인 검증 통과(구매자)
			if (vr.upstream_down && !cache.token.empty() &&                   // 서버 다운 + 캐시 존재 + 그레이스 내
				allow_offline(cache.last_verified_unix, now_unix, grace_secs))
				return gate::authed;
			return gate::locked;                                              // 그 외(구매자 아님/그레이스 초과/토큰 없음)
		}
```

헤더 상단 include에 `#include <stdexcept>`가 필요하면 추가(`std::stoll`). `<string>`은 이미 있음.

- [ ] **Step 4: 테스트 통과 확인**

Run: `clang++ -std=c++17 -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test && /tmp/sherbet_auth_test`
Expected: `ALL PASS`, 종료코드 0.

- [ ] **Step 5: Commit**

```bash
git add source/sherbet_auth_core.hpp tools/sherbet_auth_test.cpp
git commit -m "auth: 토큰 캐시 직렬화 + 24h 오프라인 그레이스 + 게이트 결정 로직(+테스트)"
```

---

## Task 4: WinInet HTTP 헬퍼 (`sherbet_http`)

**Files:**
- Create: `source/sherbet_http.hpp`
- Create: `source/sherbet_http.cpp`
- Modify: `ReShade.vcxproj`, `ReShade.vcxproj.filters`

**Interfaces:**
- Produces:
  - `int sherbet::http::post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer);`
  - `int sherbet::http::get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer);`
  - 반환값 = HTTP status(성공 시), **전송 실패 시 0**. `out` = 응답 바디(UTF-8). `bearer`(nullable) = `Authorization: Bearer <..>`.

`runtime_update_check.cpp`의 `scoped_internet_handle` + 타임아웃 패턴을 재활용하되, GET 전용(`InternetOpenUrl`)이 아니라 `InternetConnect`+`HttpOpenRequest`+`HttpSendRequest`로 POST 바디/헤더/HTTPS를 지원한다.

- [ ] **Step 1: `source/sherbet_http.hpp` 작성**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <string>

namespace sherbet
{
	namespace http
	{
		// HTTPS(443). 반환=HTTP status(성공) 또는 0(전송 실패). out=응답 바디.
		int post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer = nullptr);
		int get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer = nullptr);
	}
}
```

- [ ] **Step 2: `source/sherbet_http.cpp` 작성**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_http.hpp"
#include <Windows.h>
#include <WinInet.h>

namespace
{
	struct scoped_handle
	{
		HINTERNET h;
		scoped_handle(HINTERNET x) : h(x) {}
		~scoped_handle() { if (h) InternetCloseHandle(h); }
		operator HINTERNET() const { return h; }
	};

	int request(const wchar_t *verb, const wchar_t *host, const wchar_t *path,
		const std::string *body, const char *content_type, std::string &out, const char *bearer)
	{
		out.clear();
		const scoped_handle session = InternetOpenW(L"Sherbet", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!session) return 0;

		const scoped_handle conn = InternetConnectW(session, host, INTERNET_DEFAULT_HTTPS_PORT,
			nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
		if (!conn) return 0;

		const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
		const scoped_handle req = HttpOpenRequestW(conn, verb, path, nullptr, nullptr, nullptr, flags, 0);
		if (!req) return 0;

		DWORD timeout = 5000; // 5초
		InternetSetOptionW(req, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionW(req, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionW(req, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

		std::string headers;
		if (content_type) { headers += "Content-Type: "; headers += content_type; headers += "\r\n"; }
		if (bearer) { headers += "Authorization: Bearer "; headers += bearer; headers += "\r\n"; }

		const void *body_ptr = body ? body->data() : nullptr;
		const DWORD body_len = body ? static_cast<DWORD>(body->size()) : 0;

		std::wstring wheaders(headers.begin(), headers.end());
		if (!HttpSendRequestW(req, headers.empty() ? nullptr : wheaders.c_str(),
			static_cast<DWORD>(wheaders.size()), const_cast<void *>(body_ptr), body_len))
			return 0;

		// 상태 코드
		DWORD status = 0, slen = sizeof(status);
		HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, nullptr);

		// 바디 읽기(루프)
		char buf[2048];
		DWORD read = 0;
		while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
			out.append(buf, read);

		return static_cast<int>(status);
	}
}

int sherbet::http::post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer)
{
	return request(L"POST", host, path, &json_body, "application/json", out, bearer);
}

int sherbet::http::get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer)
{
	return request(L"GET", host, path_query, nullptr, nullptr, out, bearer);
}
```

> 주의: `headers`는 ASCII만 담으므로 `std::wstring(headers.begin(), headers.end())` 확장이 안전하다(Bearer 토큰·헤더명 전부 ASCII).

- [ ] **Step 3: vcxproj에 등록 (이 Task에서는 `sherbet_http.cpp`만)**

`ReShade.vcxproj`에서 기존 `<ClCompile Include="source\runtime_update_check.cpp" />` 항목 근처에 추가:
```xml
    <ClCompile Include="source\sherbet_http.cpp" />
```
`ReShade.vcxproj.filters`에도 추가:
```xml
    <ClCompile Include="source\sherbet_http.cpp"><Filter>Source Files</Filter></ClCompile>
```
`source\sherbet_http.hpp`는 `<ClInclude>`로 헤더 섹션에 추가(선택).

> `sherbet_auth.cpp`는 아직 없으므로 지금 등록하면 CI 빌드가 깨진다 — 그 등록은 파일을 만드는 Task 6 Step 3에서 함께 한다.

- [ ] **Step 4: 로컬 구문 선검증(헤더만)**

Run: `clang++ -std=c++17 -fsyntax-only source/sherbet_http.hpp`
Expected: 에러 없음(순수 선언 헤더). `.cpp`는 Windows 의존이라 CI에서 검증.

- [ ] **Step 5: Commit**

```bash
git add source/sherbet_http.hpp source/sherbet_http.cpp ReShade.vcxproj ReShade.vcxproj.filters
git commit -m "auth: WinInet POST/GET JSON 헬퍼(sherbet_http) 추가 + vcxproj 등록"
```

---

## Task 5: `SHERBET_ONLINE_AUTH` 빌드 플래그

**Files:**
- Modify: `source/sherbet_owner.h`

**Interfaces:**
- Produces: 매크로 `SHERBET_ONLINE_AUTH`(0/1). 미정의 시 0.

- [ ] **Step 1: `source/sherbet_owner.h`에 기본값 추가**

파일에서 `SHERBET_NODELOCK` 기본값 정의 부분을 찾아(예: `#ifndef SHERBET_NODELOCK` / `#define SHERBET_NODELOCK 0`) 그 아래에 동일 패턴으로 추가:

```c
#ifndef SHERBET_ONLINE_AUTH
#define SHERBET_ONLINE_AUTH 0
#endif
```

(`SHERBET_NODELOCK` 기본 정의가 이 파일에 없고 vcxproj/CI에서만 온다면, 같은 방식으로 이 파일 최하단에 위 블록을 추가한다. 목적: 정의 안 된 빌드에서 `#if SHERBET_ONLINE_AUTH`가 안전하게 0.)

- [ ] **Step 2: 로컬 구문 선검증**

Run: `clang++ -std=c++17 -fsyntax-only -xc++ source/sherbet_owner.h`
Expected: 에러 없음.

- [ ] **Step 3: Commit**

```bash
git add source/sherbet_owner.h
git commit -m "auth: SHERBET_ONLINE_AUTH 빌드 플래그 기본값(0) 추가"
```

---

## Task 6: `sherbet::auth::controller` (Windows 글루)

**Files:**
- Create: `source/sherbet_auth.hpp`
- Create: `source/sherbet_auth.cpp`
- Modify: `ReShade.vcxproj`, `ReShade.vcxproj.filters`

**Interfaces:**
- Consumes: `sherbet_auth_core.hpp`(파싱·게이트), `sherbet_http`(HTTP), `sherbet::nodelock::hwid()`, `SHERBET_ONLINE_AUTH`.
- Produces:
  - `bool sherbet::auth::enabled();` (`SHERBET_ONLINE_AUTH != 0`)
  - `class sherbet::auth::controller` with:
    - `void init(const std::string &config_dir_utf8);` — 캐시 로드 + 비동기 시작 verify 킥오프.
    - `bool is_authed() const;` — 게이트가 `authed`(또는 auth 비활성 빌드면 항상 true).
    - `void tick();` — 오버레이 프레임마다 호출. 비동기 결과 반영(캐시 저장 등). 논블로킹.
    - `void begin_login();` — start→ShellExecute→poll 스레드 시작. 이미 진행 중이면 무시.
    - `bool login_active() const;` — 로그인 폴링 진행 중.
    - `std::string status_text() const;` — 표시용 상태 문자열(락 안에서 복사한 값 반환)(UTF-8): "" / "로그인 대기중…" / "구매자 역할이 없습니다" / "연결 실패".

동시성: 백그라운드 스레드가 결과를 `std::atomic`/뮤텍스 보호 필드에 쓰고, `tick()`이 메인 스레드에서 읽어 캐시 파일 기록. 상세는 아래.

- [ ] **Step 1: `source/sherbet_auth.hpp`**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "sherbet_auth_core.hpp"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

namespace sherbet
{
	namespace auth
	{
		bool enabled(); // SHERBET_ONLINE_AUTH != 0

		class controller
		{
		public:
			controller();
			~controller();

			void init(const std::string &config_dir_utf8);
			bool is_authed() const;
			void tick();
			void begin_login();
			bool login_active() const;
			std::string status_text() const;

		private:
			void join_worker();
			void save_cache_locked();

			std::string _config_dir;
			std::string _hwid;
			token_cache _cache;
			std::atomic<bool> _authed{ false };
			std::atomic<bool> _login_active{ false };
			std::atomic<bool> _worker_done{ false };
			std::thread _worker;
			mutable std::mutex _mtx;
			std::string _status;          // _mtx 보호
			bool _pending_save = false;   // _mtx 보호: tick()에서 캐시 파일 기록 트리거
			std::string _login_state;     // 진행 중 로그인의 state
		};
	}
}
```

- [ ] **Step 2: `source/sherbet_auth.cpp`**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_auth.hpp"
#include "sherbet_http.hpp"
#include "sherbet_owner.h"
#include "sherbet_nodelock.hpp" // hwid()
#include <Windows.h>
#include <shellapi.h>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace
{
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr wchar_t kStartPath[]  = L"/sherbet-auth/auth/start";
	constexpr wchar_t kVerifyPath[] = L"/sherbet-auth/auth/verify";
	const std::string kPollBase     = "/sherbet-auth/auth/poll?state=";

	long long now_unix()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::string json_escape(const std::string &s)
	{
		std::string o; o.reserve(s.size() + 2);
		for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
		return o;
	}
}

bool sherbet::auth::enabled()
{
	return SHERBET_ONLINE_AUTH != 0;
}

sherbet::auth::controller::controller() {}
sherbet::auth::controller::~controller() { join_worker(); }

void sherbet::auth::controller::join_worker()
{
	if (_worker.joinable()) _worker.join();
}

void sherbet::auth::controller::save_cache_locked()
{
	const std::filesystem::path p = std::filesystem::u8path(_config_dir) / L"sherbet.auth";
	std::ofstream out(p, std::ios::trunc);
	if (out.is_open()) out << serialize_cache(_cache);
}

void sherbet::auth::controller::init(const std::string &config_dir_utf8)
{
	if (!enabled()) { _authed = true; return; } // 개발 빌드: 항상 통과

	_config_dir = config_dir_utf8;
	_hwid = sherbet::nodelock::hwid();

	// 캐시 로드
	const std::filesystem::path p = std::filesystem::u8path(_config_dir) / L"sherbet.auth";
	std::ifstream in(p);
	if (in.is_open()) {
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		parse_cache(text, _cache);
	}

	// 시작 시 비동기 verify (토큰이 있을 때만)
	if (_cache.token.empty()) { _authed = false; return; }

	_worker_done = false;
	_worker = std::thread([this]() {
		const std::string body = std::string("{\"token\":\"") + json_escape(_cache.token) +
			"\",\"hwid\":\"" + json_escape(_hwid) + "\"}";
		std::string resp;
		const int status = sherbet::http::post_json(kHost, kVerifyPath, body, resp, nullptr);
		const verify_result vr = parse_verify(resp, status);
		const gate g = decide(vr, _cache, now_unix(), 86400);
		{
			std::lock_guard<std::mutex> lk(_mtx);
			if (vr.ok) { _cache.last_verified_unix = now_unix(); _pending_save = true; }
		}
		_authed = (g == gate::authed);
		_worker_done = true;
	});
}

bool sherbet::auth::controller::is_authed() const
{
	if (!enabled()) return true;
	return _authed.load();
}

bool sherbet::auth::controller::login_active() const
{
	return _login_active.load();
}

std::string sherbet::auth::controller::status_text() const
{
	std::lock_guard<std::mutex> lk(_mtx);
	return _status; // 락 안에서 값 복사 — 반환 후 워커가 _status를 바꿔도 안전
}

void sherbet::auth::controller::tick()
{
	if (!enabled()) return;

	// 완료된 워커 스레드 정리 + 지연된 캐시 저장 반영
	if (_worker_done.load()) {
		join_worker();
		_worker_done = false;
		std::lock_guard<std::mutex> lk(_mtx);
		if (_pending_save) { save_cache_locked(); _pending_save = false; }
	}
}

void sherbet::auth::controller::begin_login()
{
	if (!enabled()) return;
	if (_login_active.load()) return;      // 이미 진행 중
	if (_worker.joinable() && !_worker_done.load()) return; // 시작 verify 진행 중

	_login_active = true;
	{
		std::lock_guard<std::mutex> lk(_mtx);
		_status = "\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xEC\xA4\x80\xEB\xB9\x84\xEC\xA4\x91\xE2\x80\xA6"; // "로그인 준비중…"
	}

	join_worker();
	_worker_done = false;
	_worker = std::thread([this]() {
		// 1) start
		const std::string sbody = std::string("{\"hwid\":\"") + json_escape(_hwid) + "\"}";
		std::string sresp;
		const int sstatus = sherbet::http::post_json(kHost, kStartPath, sbody, sresp, nullptr);
		const start_result sr = parse_start(sresp);
		if (sstatus == 0 || !sr.ok) {
			{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEC\x97\xB0\xEA\xB2\xB0 \xEC\x8B\xA4\xED\x8C\xA8"; } // "연결 실패"
			_login_active = false; _worker_done = true; return;
		}
		// 2) 브라우저 열기
		{
			std::wstring wurl(sr.authorize_url.begin(), sr.authorize_url.end()); // URL은 ASCII
			ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
		{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xEB\x8C\x80\xEA\xB8\xB0\xEC\xA4\x91\xE2\x80\xA6"; } // "로그인 대기중…"

		// 3) 폴링(최대 5분, 2초 간격)
		const std::string poll_path = kPollBase + sr.state;
		const std::wstring wpoll(poll_path.begin(), poll_path.end());
		for (int i = 0; i < 150; ++i) {
			std::string presp;
			const int pstatus = sherbet::http::get(kHost, wpoll.c_str(), presp, nullptr);
			if (pstatus == 200) {
				const poll_result pr = parse_poll(presp);
				if (pr.status == poll_result::ready) {
					std::lock_guard<std::mutex> lk(_mtx);
					_cache.token = pr.token;
					_cache.hwid = _hwid;
					_cache.last_verified_unix = now_unix();
					_pending_save = true;
					_status.clear();
					_authed = true;
					_login_active = false; _worker_done = true; return;
				}
				if (pr.status == poll_result::denied) {
					std::lock_guard<std::mutex> lk(_mtx);
					_status = "\xEA\xB5\xAC\xEB\xA7\xA4\xEC\x9E\x90 \xEC\x97\xAD\xED\x95\xA0\xEC\x9D\xB4 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"; // "구매자 역할이 없습니다"
					_authed = false;
					_login_active = false; _worker_done = true; return;
				}
			}
			Sleep(2000);
		}
		{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEC\x8B\x9C\xEA\xB0\x84 \xEC\xB4\x88\xEA\xB3\xBC"; } // "시간 초과"
		_login_active = false; _worker_done = true;
	});
}
```

> **동시성 주의:** 워커가 `_cache`/`_status`/`_pending_save`를 `_mtx` 아래에서 쓰고, `tick()`도 `_mtx` 아래에서 `save_cache_locked()`를 호출한다. `_authed`/`_login_active`/`_worker_done`은 atomic. 파괴자에서 `join_worker()`로 스레드 안전 종료.

- [ ] **Step 3: vcxproj에 `sherbet_auth.cpp` 등록**

`ReShade.vcxproj`, `ReShade.vcxproj.filters`에 `<ClCompile Include="source\sherbet_auth.cpp" />`(+filters의 Source Files) 추가. 헤더 `sherbet_auth.hpp`/`sherbet_auth_core.hpp`는 `<ClInclude>` 추가(선택).

- [ ] **Step 4: 로컬 구문 선검증(코어 헤더 재확인)**

Run: `clang++ -std=c++17 -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test && /tmp/sherbet_auth_test`
Expected: 여전히 `ALL PASS`(코어 헤더 회귀 없음). `sherbet_auth.cpp` 자체는 CI(Task 9)에서 검증.

- [ ] **Step 5: Commit**

```bash
git add source/sherbet_auth.hpp source/sherbet_auth.cpp ReShade.vcxproj ReShade.vcxproj.filters
git commit -m "auth: controller(캐시로드·비동기verify·로그인 흐름 start→브라우저→poll) 추가"
```

---

## Task 7: 런타임 배선 — 생성자 init + 이펙트 게이트

**Files:**
- Modify: `source/runtime.hpp`
- Modify: `source/runtime.cpp:353`(생성자), `source/runtime.cpp:3708`(`update_effects`)

**Interfaces:**
- Consumes: `sherbet::auth::controller`(Task 6).
- Produces: `reshade::runtime` 멤버 `sherbet::auth::controller _sherbet_auth;` + 오버레이/이펙트에서 `_sherbet_auth.is_authed()` 사용.

- [ ] **Step 1: `runtime.hpp`에 멤버 추가 + include**

`source/runtime.hpp` 상단 적절한 위치(다른 sherbet include 근처)에:
```cpp
#include "sherbet_auth.hpp"
```
`reshade::runtime` 클래스의 private 멤버 영역(다른 `_sherbet_*` 멤버 근처)에:
```cpp
	sherbet::auth::controller _sherbet_auth;
```

- [ ] **Step 2: 생성자에서 init 호출**

`source/runtime.cpp` 생성자의 `load_config();` **직후**(config path가 확정된 지점, 353행 부근)에 추가:
```cpp
	// SHERBET: 온라인 인증 컨트롤러 초기화(캐시 토큰 로드 + 비동기 시작 검증)
	_sherbet_auth.init(_config_path.parent_path().u8string());
```

- [ ] **Step 3: `update_effects()` 이펙트 게이트**

`source/runtime.cpp`의 `void reshade::runtime::update_effects()`(3708행) **함수 본문 맨 앞**에 추가:
```cpp
	// SHERBET: 인증 안 됐으면 이펙트 컴파일/적용을 건너뛴다(효과 잠금).
	if (!_sherbet_auth.is_authed())
		return;
```

> `update_effects`가 매 프레임 초기에 호출되므로, 인증 전에는 이펙트가 로드/적용되지 않는다. **주의:** `_frame_count`는 `on_present`에서 매 프레임 무조건 증가하므로, 잠금 상태로 프레임이 흐르면 `if (_frame_count == 0)` 최초-로드 원샷을 지나쳐 버린다. 따라서 잠금→인증 **전환 시점에 `reload_effects()`를 한 번 호출해 원샷을 재무장**해야 한다(`_sherbet_auth_was_locked` 엣지 추적). 그렇지 않으면 로그인 성공 후에도 이펙트가 자동 로드되지 않는다. (게임 자체 렌더는 영향 없음.)

- [ ] **Step 4: 로컬 구문 선검증(가능 범위)**

Run: `clang++ -std=c++17 -fsyntax-only -Ideps/imgui -Isource -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS source/sherbet_auth.hpp`
Expected: `sherbet_auth.hpp`가 windows include를 하지 않으므로(코어+표준만) 파싱 통과. 실제 `runtime.cpp` 통합은 CI에서 검증.

- [ ] **Step 5: Commit**

```bash
git add source/runtime.hpp source/runtime.cpp
git commit -m "auth: 런타임 배선 — 생성자 init + update_effects 이펙트 게이트"
```

---

## Task 8: 오버레이 로그인 패널 (버튼식) + 프레임 poll

**Files:**
- Modify: `source/runtime_gui.cpp:1587`(오버레이 게이트)

**Interfaces:**
- Consumes: `_sherbet_auth`(Task 7). `ICON_FK_LOCK`, `ICON_FK_SIGN_IN`(또는 존재하는 아이콘), `SHERBET_DISCORD_URL`.

기존 오버레이 게이트는 `if (!sherbet::nodelock::is_authorized(...)) { 잠금패널; End(); } else { 정상UI }` 구조다. **인증 게이트를 노드락보다 먼저** 평가해, 인증 안 됐으면 로그인 패널을 그린다. 프레임마다 `_sherbet_auth.tick()` 호출.

- [ ] **Step 1: 오버레이 게이트에 인증 분기 추가**

`source/runtime_gui.cpp` 1586행의 `// SHERBET: 노드락` 주석 **직전**에, 프레임 tick + 인증 패널 분기를 삽입한다. 기존 노드락 `if (!...is_authorized...)` 블록은 그대로 두고, 그 **앞에** 다음을 추가:

```cpp
		// SHERBET: 온라인 인증 — 미인증이면 로그인 패널만 노출(효과/오버레이 잠금)
		_sherbet_auth.tick();
		if (sherbet::auth::enabled() && !_sherbet_auth.is_authed())
		{
			ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			ImGui::TextUnformatted("Sherbet");
			ImGui::PopFont();

			const float avail_w = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y * 0.30f));
			auto centered_text = [avail_w](const char *text) {
				const float tw = ImGui::CalcTextSize(text).x;
				ImGui::SetCursorPosX((avail_w - tw) * 0.5f);
				ImGui::TextUnformatted(text);
			};

			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.4f);
			centered_text(ICON_FK_LOCK "  \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8\xEC\x9D\xB4 \xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "로그인이 필요합니다"
			ImGui::PopFont();
			ImGui::Spacing();

			// 로그인 버튼(진행 중이면 비활성 + 스피너 대신 텍스트)
			const bool busy = _sherbet_auth.login_active();
			const char *btn = ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8"; // "디스코드로 로그인"
			const float bw = ImGui::CalcTextSize(btn).x + 40.0f;
			ImGui::SetCursorPosX((avail_w - bw) * 0.5f);
			ImGui::BeginDisabled(busy);
			if (ImGui::Button(btn, ImVec2(bw, 0.0f)))
				_sherbet_auth.begin_login();
			ImGui::EndDisabled();

			// 상태 텍스트(대기중/거부 사유/실패) — status_text()는 락 안에서 복사한 std::string 값 반환
			const std::string st = _sherbet_auth.status_text();
			if (!st.empty()) { ImGui::Spacing(); centered_text(st.c_str()); }

			ImGui::Spacing();
			centered_text("\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 \xEC\x9E\x90\xEB\x8F\x99\xEC\x9C\xBC\xEB\xA1\x9C \xEC\xA7\x84\xED\x96\x89\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "로그인 후 자동으로 진행됩니다"

			ImGui::End();
		}
		else
		// SHERBET: 노드락 — 등록되지 않은 PC면 오버레이 콘텐츠 대신 안내문만 표시
		if (!sherbet::nodelock::is_authorized(_config_path.parent_path().u8string()))
		{
```

즉, 기존 노드락 `if (...)`를 위 `else if` 로 이어붙인다(패널 잠금 → 노드락 잠금 → 정상 UI 의 3분기). 기존 노드락 블록과 그 뒤 `else { 정상 UI }`는 변경 없음.

> `ICON_FK_LOCK`/`ICON_FK_COMMENTS`는 기존 노드락 패널에서 이미 사용 중 → 존재 확인됨. 새 아이콘 추가 불필요.

- [ ] **Step 2: 로컬 구문 선검증(불가 범위 인지)**

`runtime_gui.cpp`는 Windows/ImGui 전체 의존이라 Mac 컴파일 불가. **CI(Task 9)에서 검증.** 이 Step은 육안 리뷰: obsolete ImGui API 미사용(`Button`/`BeginDisabled`/`TextUnformatted`는 1.92.5 정상), `PushFont(font, size)` 2인자 시그니처가 기존 코드와 동일한지 1585행 인근과 대조.

- [ ] **Step 3: Commit**

```bash
git add source/runtime_gui.cpp
git commit -m "auth: 오버레이 버튼식 로그인 패널 + 프레임 tick(미인증 시 효과·UI 잠금)"
```

---

## Task 9: CI 통합 검증 (인증 ON 빌드)

**Files:**
- (필요 시) `.github/workflows/build.yml` — `SHERBET_ONLINE_AUTH` 전달 확인.

**Interfaces:** 없음(통합 검증 단계).

- [ ] **Step 1: 기본(인증 OFF) 빌드가 깨지지 않는지 CI 확인**

브랜치 push 후 GitHub Actions 기본 빌드가 green인지 확인. `SHERBET_ONLINE_AUTH` 미정의(=0)이므로 `controller`는 no-op, 새 파일들이 컴파일·링크만 되면 통과.

Run(로컬): `git push origin sherbet-base`
Expected: Actions 빌드 green(약 7분). 실패 시 로그의 컴파일 에러를 해당 Task로 되돌려 수정.

- [ ] **Step 2: 인증 ON 빌드 경로 확인**

`build.yml`의 `workflow_dispatch`에 `SHERBET_ONLINE_AUTH`를 넘길 방법이 있는지 확인. 없으면 `SHERBET_NODELOCK`을 전달하는 지점과 동일 패턴으로 `/p:` 또는 preprocessor define을 추가한다. (예: MSBuild `AdditionalOptions`/`PreprocessorDefinitions`에 `SHERBET_ONLINE_AUTH=1`.) 이 Step은 **빌드 인자 경로만** 확보하고, 실제 판매 빌드에서 1로 켠다.

- [ ] **Step 3: 인증 ON으로 수동 디스패치 빌드 → green 확인**

`SHERBET_ONLINE_AUTH=1`로 workflow_dispatch 실행 → 빌드 green 확인(컨트롤러 실제 코드가 전부 컴파일되는 경로).
Expected: green. DLL 산출물을 로컬 Windows에서 게임에 넣어 **수동 E2E**(로그인 패널 → 버튼 → 브라우저 → 승인 → 잠금해제/효과 적용)는 사용자 검증으로 남긴다(Mac 자동화 불가).

- [ ] **Step 4: 최종 커밋(문서/원장)**

CI green 확인 후, 남은 문서 갱신이 있으면 커밋. 없으면 Task 8의 커밋이 마지막.

---

## Self-Review 메모(작성자)

- **스펙 커버리지:** §5 흐름(캐시→verify→그레이스/잠금, 버튼 로그인 start/poll)=Task 2·3·6·8. §8 API=Task 4·6. §9 게이트 3곳=Task 7·8. §12 HWID·24h 그레이스=Task 1·3·6. 교체대상 `s_unlocked`는 Phase 0b 범위 밖(명시적 제외) — 이 플랜은 buyer 게이트만.
- **타입 일관성:** `verify_result{ok,upstream_down,sub,roles}`/`token_cache{token,hwid,last_verified_unix}`/`gate{locked,authed}`/`decide(vr,cache,now,grace)`가 Task 2·3·6·7·8에서 동일 시그니처로 사용됨.
- **플랫폼 현실:** 순수 로직(Task 2·3)만 로컬 TDD, 나머지는 CI. 각 Windows Task는 로컬 검증 한계를 명시하고 CI(Task 9)로 수렴.
- **위협모델:** HWID는 클라측 강제(스펙 §12.3) — 플랜은 이를 넘어서는 서버측 통제를 시도하지 않음.
