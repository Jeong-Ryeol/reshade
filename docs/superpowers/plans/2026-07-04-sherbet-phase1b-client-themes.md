# Sherbet Phase 1b — 클라 동적 테마 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sherbet 클라가 서버 `/content/me` 에서 받은 엔타이틀 테마 목록으로 내장 테마를 언락하고 신규(동적) 테마를 실시간 병합하며, 위조 가능한 오프라인 FNV 테마코드 경로를 제거한다.

**Architecture:** 순수 로직(테마 JSON 파서 = `sherbet_theme_json.hpp`, 엔타이틀 집합 기반 `is_unlocked`)은 host(clang)로 테스트하고, Windows glue(WinInet 콘텐츠 페치, 스레드, ImGui 마켓 UI)는 CI로만 검증한다. 역할→테마 매칭은 **서버가 전담**(themes.json의 숫자 role ID)하고 클라는 서버가 내려준 **엔타이틀 id 집합**만 소비한다. 동적 테마는 `std::vector<std::unique_ptr<owned_theme>>` 로 소유(문자열·주소 안정), 정적 배열과 함께 `themes_snapshot()` 로 노출한다. 레지스트리 변형은 **렌더 스레드에서만**, 워커는 네트워크/파일 IO만 담당한다.

**Tech Stack:** C++17, ImGui(`IM_COL32`/`ImU32`/`particle` enum), WinInet(`sherbet::http`), std::thread/atomic/mutex, GitHub Actions CI(Windows). Host 테스트는 `clang++ -std=c++17 -Ideps/imgui`.

## Global Constraints

- **커밋 메시지에 Claude/AI 저작권·공동저자(Co-Authored-By 등) 문구 절대 금지** (CLAUDE.md). 모든 커밋 본문은 한국어, 무해한 설명만.
- **Mac은 Windows C++ 컴파일 불가.** WinInet/ImGui/스레드에 의존하는 파일(`sherbet_auth.*`, `sherbet_content.*`, `runtime_gui.cpp`, `sherbet_themes.cpp`, `sherbet_ui.cpp`)은 로컬에서 **컴파일/링크 검증 불가** — CI(약 7–8분)가 유일한 검증. 순수 헤더(`sherbet_theme_json.hpp`)와 그 테스트만 로컬 `clang++ -std=c++17 -Ideps/imgui -fsyntax-only`(또는 실행 빌드)로 검증한다.
- **파일 상단 저작권 헤더 유지:** 신규 `.hpp/.cpp` 는 기존 파일과 동일하게 `/* Copyright (C) 2026 정렬 (Jeong-Ryeol) / SPDX-License-Identifier: BSD-3-Clause */` 로 시작.
- **색 바이트 순서는 `IM_COL32` 매크로로만 생성** — 직접 시프트 금지(imconfig 시프트 변경에 안전).
- **`SHERBET_ONLINE_AUTH==0`(개발 빌드) → 모든 테마 열림** 유지. `sherbet::auth::enabled()` 로 판정.
- **프리셋(PRE-) 마켓 경로는 건드리지 않는다** — Phase 2 대상. 이 플랜은 **테마** 마켓(seg==0)과 테마 언락만.
- **서버 계약(1a, 배포 완료):** `GET /sherbet-auth/content/me`, 헤더 `Authorization: Bearer <토큰>`, 응답 `{"themes":[<themeJSON>,...]}`(각 항목에서 `role` 필드 제거됨), 401=토큰무효, 503=디스코드조회실패. themeJSON 스키마는 스펙 §13.1: `id`, `display_name`, `colors`{bg0,bg1,bg2,panel,panel_alt,chip,border,text,text_dim,accent,accent2,glow — 각 `#rrggbbaa`}, `particle`∈{spark,heart,leaf,petal}, `hue_cycle`(bool). 내장 테마 항목은 colors 생략 가능.
- **호스트/경로 상수는 `sherbet_auth.cpp` 것과 일치:** host `L"wonryeol.asuscomm.com"`, content 경로 `L"/sherbet-auth/content/me"`.

---

## File Structure

- **Create `source/sherbet_theme_json.hpp`** — 순수 파서. `parsed_theme` 구조체, `parse_hex_color`, `parse_particle`, `parse_themes_manifest`. imgui.h(`IM_COL32`/`ImU32`/`particle`)만 의존, 스레드/WinInet 없음 → host 테스트 대상.
- **Create `tools/sherbet_theme_json_test.cpp`** — 위 파서의 host 테스트(assert 기반, main 반환코드로 성공/실패). 기존 `tools/sherbet_auth_test.cpp` 스타일.
- **Modify `source/sherbet_theme.hpp`** — `themes_snapshot()` 선언 추가. (`all_themes` 는 잔존시키되 마켓은 snapshot 사용.)
- **Modify `source/sherbet_themes.cpp`** — `owned_theme` + `std::vector<std::unique_ptr<owned_theme>>` 동적 레지스트리, `find_theme` 를 정적→동적 순으로 확장, `themes_snapshot()`, `add_dynamic_theme(const parsed_theme&)`, `clear_dynamic_themes()`.
- **Modify `source/sherbet_ui.hpp`** — FNV 언락 API 제거, 엔타이틀 API 추가(`mark_entitled`/`clear_entitlements`/`is_entitled`), `apply_content(const std::string&)` 선언.
- **Modify `source/sherbet_ui.cpp`** — `s_unlocked` map 제거, `s_entitled` set 도입, `is_unlocked` 재구현, `apply_content` 구현(파서 호출→레지스트리 병합→엔타이틀 등록).
- **Create `source/sherbet_content.hpp` / `.cpp`** — Windows glue. `sherbet::auth::controller` 에 콘텐츠 페치를 붙이는 대신 독립 함수로: `bool fetch_content(const std::string &bearer, const std::string &config_dir, std::string &out_body)`(WinInet GET + 캐시 파일 기록), `bool load_cached_content(const std::string &config_dir, std::string &out_body)`. (컨트롤러는 토큰만 노출; 테마 지식은 넣지 않는다.)
- **Modify `source/sherbet_auth.hpp` / `.cpp`** — 컨트롤러에 `std::string token() const`(락 보호), `begin_fetch_content()`(워커: `fetch_content` 실행 후 바디를 pending 버퍼에 저장), `bool take_content(std::string &out)`(렌더 스레드에서 pending 바디 인출), `init()` 에서 `load_cached_content` 로 pending 초기화.
- **Modify `source/runtime_gui.cpp`** — (a) config `Unlocked` 저장/로드 제거; (b) 프레임 루프에서 `take_content`→`sherbet::apply_content`; (c) 마켓 테마 세그먼트: `themes_snapshot()` 순회, "내 전용 불러오기" 버튼(`begin_fetch_content`), SHRB- InputText/해제 제거.

**의존 방향:** `sherbet_theme_json.hpp`(순수) ← `sherbet_ui.cpp`(apply_content) → `sherbet_themes.cpp`(레지스트리). `runtime_gui.cpp` → `sherbet_auth`(페치/토큰) + `sherbet_ui`(apply/is_unlocked). `sherbet_auth.cpp` → `sherbet_content`(fetch/load). 컨트롤러는 테마를 모른다(바디 문자열만 다룸).

---

## Task 1: 순수 테마 JSON 파서 (`sherbet_theme_json.hpp`) + host 테스트

**Files:**
- Create: `source/sherbet_theme_json.hpp`
- Create: `tools/sherbet_theme_json_test.cpp`

**Interfaces:**
- Consumes: `sherbet::particle` enum, `sherbet::theme` 구조체 필드명(참고용), `IM_COL32`/`ImU32` (`imgui.h`).
- Produces:
  - `struct sherbet::parsed_theme { std::string id, display_name; ImU32 bg0,bg1,bg2,panel,panel_alt,chip,border,text,text_dim,accent,accent2,glow; particle particle_shape; bool hue_cycle; bool ok; };` (기본값: 모든 색 0, `particle_shape=particle::spark`, `hue_cycle=false`, `ok=false`).
  - `bool sherbet::parse_hex_color(const std::string &s, ImU32 &out);` — `#rrggbbaa` (9자, 선행 `#`) → `IM_COL32(r,g,b,a)`. 형식 불량(길이≠9, `#` 없음, 비16진) → false, out 미변경.
  - `sherbet::particle sherbet::parse_particle(const std::string &s);` — "heart"→heart, "leaf"→leaf, "petal"→petal, 그 외/불명 → spark.
  - `std::vector<sherbet::parsed_theme> sherbet::parse_themes_manifest(const std::string &body);` — `{"themes":[ {..}, {..} ]}` 에서 최상위 테마 객체들을 brace-매칭으로 잘라 각각 파싱. `id` 와 12색이 모두 유효할 때만 `ok=true`. `themes` 키/배열 없으면 빈 벡터.

- [ ] **Step 1: Write the failing test**

`tools/sherbet_theme_json_test.cpp`:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Ideps/imgui tools/sherbet_theme_json_test.cpp -o /tmp/tj && /tmp/tj
#include "../source/sherbet_theme_json.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet;

static void test_hex()
{
    ImU32 c = 0;
    assert(parse_hex_color("#5df0c0ff", c));
    assert(c == IM_COL32(0x5d, 0xf0, 0xc0, 0xff));
    assert(parse_hex_color("#00000000", c) && c == IM_COL32(0, 0, 0, 0));
    ImU32 keep = 123;
    assert(!parse_hex_color("5df0c0ff", keep) && keep == 123); // # 없음
    assert(!parse_hex_color("#5df0c0", keep) && keep == 123);  // 길이 부족
    assert(!parse_hex_color("#zzzzzzzz", keep) && keep == 123); // 비16진
}

static void test_particle()
{
    assert(parse_particle("heart") == particle::heart);
    assert(parse_particle("leaf") == particle::leaf);
    assert(parse_particle("petal") == particle::petal);
    assert(parse_particle("spark") == particle::spark);
    assert(parse_particle("nonsense") == particle::spark); // 폴백
}

static const char *kManifest = R"({"themes":[
  {"id":"aurora","display_name":"Aurora","particle":"spark","hue_cycle":true,
   "colors":{"bg0":"#0a1f1aff","bg1":"#0f2e24ff","bg2":"#123a2cff",
     "panel":"#122e26b8","panel_alt":"#183a2fd9","chip":"#134233ff","border":"#5df0c040",
     "text":"#eafff6ff","text_dim":"#8fbfaeff","accent":"#5df0c0ff","accent2":"#a7ffe3ff",
     "glow":"#5df0c073"}},
  {"id":"mint","role":"gone","display_name":"Mint"}
]})";

static void test_manifest()
{
    auto v = parse_themes_manifest(kManifest);
    assert(v.size() == 2);
    assert(v[0].ok);
    assert(v[0].id == "aurora");
    assert(v[0].display_name == "Aurora");
    assert(v[0].particle_shape == particle::spark);
    assert(v[0].hue_cycle == true);
    assert(v[0].accent == IM_COL32(0x5d, 0xf0, 0xc0, 0xff));
    assert(v[0].glow == IM_COL32(0x5d, 0xf0, 0xc0, 0x73));
    // 색 없는 내장 항목 → ok=false(색 불완전), 그러나 id 는 읽힘
    assert(!v[1].ok);
    assert(v[1].id == "mint");
    // themes 없음 → 빈 벡터
    assert(parse_themes_manifest("{}").empty());
    assert(parse_themes_manifest("garbage").empty());
}

int main()
{
    test_hex();
    test_particle();
    test_manifest();
    std::puts("sherbet_theme_json_test: ALL PASS");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails (헤더 없음 → 컴파일 실패)**

Run: `clang++ -std=c++17 -Ideps/imgui tools/sherbet_theme_json_test.cpp -o /tmp/tj`
Expected: FAIL — `'../source/sherbet_theme_json.hpp' file not found`

- [ ] **Step 3: Write the parser header**

`source/sherbet_theme_json.hpp`:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 원격 테마 JSON 파서 — 순수 로직(플랫폼 비의존, WinInet/스레드 없음).
// imgui.h(IM_COL32/ImU32) + sherbet_theme.hpp(particle) 만 의존 → host 테스트 가능.
#pragma once

#include "sherbet_theme.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
	struct parsed_theme
	{
		std::string id, display_name;
		ImU32 bg0 = 0, bg1 = 0, bg2 = 0, panel = 0, panel_alt = 0, chip = 0, border = 0;
		ImU32 text = 0, text_dim = 0, accent = 0, accent2 = 0, glow = 0;
		particle particle_shape = particle::spark;
		bool hue_cycle = false;
		bool ok = false; // id + 12색 모두 유효할 때만 true
	};

	// "#rrggbbaa"(9자) → IM_COL32(r,g,b,a). 형식 불량 → false(out 미변경).
	inline bool parse_hex_color(const std::string &s, ImU32 &out)
	{
		if (s.size() != 9 || s[0] != '#') return false;
		int v[8];
		for (int i = 0; i < 8; ++i)
		{
			const char c = s[i + 1];
			if (c >= '0' && c <= '9') v[i] = c - '0';
			else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
			else return false;
		}
		const int r = v[0] * 16 + v[1], g = v[2] * 16 + v[3];
		const int b = v[4] * 16 + v[5], a = v[6] * 16 + v[7];
		out = IM_COL32(r, g, b, a);
		return true;
	}

	inline particle parse_particle(const std::string &s)
	{
		if (s == "heart") return particle::heart;
		if (s == "leaf") return particle::leaf;
		if (s == "petal") return particle::petal;
		return particle::spark;
	}

	namespace detail
	{
		// obj 안에서 "key":"value" 문자열 값 추출(단순, \" \\ 언이스케이프). 없으면 false.
		inline bool json_str(const std::string &obj, const char *key, std::string &out)
		{
			const std::string needle = std::string("\"") + key + "\"";
			std::size_t k = obj.find(needle);
			if (k == std::string::npos) return false;
			std::size_t colon = obj.find(':', k + needle.size());
			if (colon == std::string::npos) return false;
			std::size_t i = colon + 1;
			while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
			if (i >= obj.size() || obj[i] != '"') return false;
			++i;
			std::string val;
			for (; i < obj.size(); ++i)
			{
				const char c = obj[i];
				if (c == '\\' && i + 1 < obj.size()) { val += obj[++i]; continue; }
				if (c == '"') { out = val; return true; }
				val += c;
			}
			return false;
		}

		// obj 안에서 "key":true/false. 없거나 불리언 아니면 def.
		inline bool json_bool(const std::string &obj, const char *key, bool def)
		{
			const std::string needle = std::string("\"") + key + "\"";
			std::size_t k = obj.find(needle);
			if (k == std::string::npos) return def;
			std::size_t colon = obj.find(':', k + needle.size());
			if (colon == std::string::npos) return def;
			std::size_t i = colon + 1;
			while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
			if (obj.compare(i, 4, "true") == 0) return true;
			if (obj.compare(i, 5, "false") == 0) return false;
			return def;
		}

		// obj 안 "key":"#rrggbbaa" → dst. 실패 시 false.
		inline bool color_field(const std::string &obj, const char *key, ImU32 &dst)
		{
			std::string hex;
			return json_str(obj, key, hex) && parse_hex_color(hex, dst);
		}
	}

	// {"themes":[ {..}, {..} ]} → 각 최상위 테마 객체를 brace 매칭으로 잘라 파싱.
	inline std::vector<parsed_theme> parse_themes_manifest(const std::string &body)
	{
		std::vector<parsed_theme> out;
		std::size_t tk = body.find("\"themes\"");
		if (tk == std::string::npos) return out;
		std::size_t lb = body.find('[', tk);
		if (lb == std::string::npos) return out;

		std::size_t i = lb + 1;
		while (i < body.size())
		{
			// 다음 '{' 또는 배열 종료 ']'
			while (i < body.size() && body[i] != '{' && body[i] != ']') ++i;
			if (i >= body.size() || body[i] == ']') break;
			// brace 매칭(문자열 내 중괄호는 무시)
			const std::size_t start = i;
			int depth = 0;
			bool in_str = false;
			for (; i < body.size(); ++i)
			{
				const char c = body[i];
				if (in_str)
				{
					if (c == '\\') { ++i; continue; }
					if (c == '"') in_str = false;
					continue;
				}
				if (c == '"') in_str = true;
				else if (c == '{') ++depth;
				else if (c == '}') { if (--depth == 0) { ++i; break; } }
			}
			const std::string obj = body.substr(start, i - start);

			parsed_theme pt;
			detail::json_str(obj, "id", pt.id);
			if (!detail::json_str(obj, "display_name", pt.display_name))
				pt.display_name = pt.id;
			std::string ps;
			if (detail::json_str(obj, "particle", ps)) pt.particle_shape = parse_particle(ps);
			pt.hue_cycle = detail::json_bool(obj, "hue_cycle", false);
			const bool colors_ok =
				detail::color_field(obj, "bg0", pt.bg0) & detail::color_field(obj, "bg1", pt.bg1) &
				detail::color_field(obj, "bg2", pt.bg2) & detail::color_field(obj, "panel", pt.panel) &
				detail::color_field(obj, "panel_alt", pt.panel_alt) & detail::color_field(obj, "chip", pt.chip) &
				detail::color_field(obj, "border", pt.border) & detail::color_field(obj, "text", pt.text) &
				detail::color_field(obj, "text_dim", pt.text_dim) & detail::color_field(obj, "accent", pt.accent) &
				detail::color_field(obj, "accent2", pt.accent2) & detail::color_field(obj, "glow", pt.glow);
			pt.ok = !pt.id.empty() && colors_ok;
			out.push_back(std::move(pt));
		}
		return out;
	}
}
```

> 주: `colors_ok` 는 단락평가(&&) 대신 비트 `&` 로 12개 `color_field` 를 **모두 실행**시켜 유효 색은 전부 채운다(부분 불량이어도 채워진 필드는 유지, ok 만 false).

- [ ] **Step 4: Run test to verify it passes**

Run: `clang++ -std=c++17 -Ideps/imgui tools/sherbet_theme_json_test.cpp -o /tmp/tj && /tmp/tj`
Expected: `sherbet_theme_json_test: ALL PASS`, 종료코드 0

- [ ] **Step 5: Commit**

```bash
git add source/sherbet_theme_json.hpp tools/sherbet_theme_json_test.cpp
git commit -m "feat(sherbet): 원격 테마 JSON 파서 + host 테스트

#rrggbbaa→IM_COL32, particle 문자열→enum, {\"themes\":[...]} brace 매칭 파싱.
imgui.h 만 의존해 clang 로 host 테스트 가능."
```

---

## Task 2: 동적 테마 레지스트리 (`sherbet_themes.cpp` + `sherbet_theme.hpp`)

**Files:**
- Modify: `source/sherbet_theme.hpp` (선언 추가)
- Modify: `source/sherbet_themes.cpp` (구현)

**Interfaces:**
- Consumes: `sherbet::parsed_theme`(Task 1), `sherbet::theme`, `find_theme`.
- Produces:
  - `std::vector<const theme *> sherbet::themes_snapshot();` — 정적 7개 + 동적 등록분(등록 순서) 포인터. 포인터는 안정(unique_ptr 소유).
  - `void sherbet::add_dynamic_theme(const parsed_theme &pt);` — `pt.ok==false` 또는 id 가 이미(정적/동적) 존재하면 무시. 아니면 owned_theme 생성·색 복사·`view` 연결 후 등록.
  - `void sherbet::clear_dynamic_themes();` — 동적 목록 비움(재페치 시 중복 방지).
  - `find_theme(id)` 는 정적→동적 순으로 검색(동적 테마도 `set_active_theme`/`active_theme` 에서 조회 가능).

- [ ] **Step 1: 선언 추가 (`sherbet_theme.hpp`)**

`sherbet_theme.hpp` 의 `namespace sherbet` 안, 기존 선언 아래에 추가:

```cpp
	struct parsed_theme; // forward (sherbet_theme_json.hpp)
	std::vector<const theme *> themes_snapshot();
	void add_dynamic_theme(const parsed_theme &pt);
	void clear_dynamic_themes();
```

그리고 파일 상단 include 에 `#include <vector>` 추가(현재 `<cstddef>`, `<imgui.h>` 만 있음).

- [ ] **Step 2: 구현 (`sherbet_themes.cpp`)**

`sherbet_themes.cpp` 상단 include 에 추가:

```cpp
#include "sherbet_theme_json.hpp"
#include <vector>
#include <memory>
#include <string>
```

`s_theme_count` 정의 뒤(익명/`sherbet` 네임스페이스 내부)에 동적 레지스트리를 추가:

```cpp
	// 동적 테마 — 서버(/content/me)에서 받은 신규 테마. unique_ptr 로 소유해 벡터 성장에도
	// owned_theme 주소·내부 문자열 포인터가 안정(view.id/display_name 이 std::string 를 가리킴).
	struct owned_theme { std::string id, display_name; theme view; };
	static std::vector<std::unique_ptr<owned_theme>> s_dynamic;
```

`find_theme` 를 정적→동적 검색으로 교체:

```cpp
	const theme *find_theme(const char *id)
	{
		if (id == nullptr)
			return nullptr;
		for (std::size_t i = 0; i < s_theme_count; ++i)
			if (std::strcmp(s_themes[i].id, id) == 0)
				return &s_themes[i];
		for (const auto &d : s_dynamic)
			if (d->id == id)
				return &d->view;
		return nullptr;
	}
```

`all_themes` 아래에 신규 함수 3개 추가:

```cpp
	std::vector<const theme *> themes_snapshot()
	{
		std::vector<const theme *> out;
		out.reserve(s_theme_count + s_dynamic.size());
		for (std::size_t i = 0; i < s_theme_count; ++i)
			out.push_back(&s_themes[i]);
		for (const auto &d : s_dynamic)
			out.push_back(&d->view);
		return out;
	}

	void add_dynamic_theme(const parsed_theme &pt)
	{
		if (!pt.ok)
			return;
		if (find_theme(pt.id.c_str()) != nullptr) // 정적/동적 중복 → 무시(정적 우선)
			return;
		auto o = std::make_unique<owned_theme>();
		o->id = pt.id;
		o->display_name = pt.display_name.empty() ? pt.id : pt.display_name;
		theme &v = o->view;
		v.id = o->id.c_str();
		v.display_name = o->display_name.c_str();
		v.bg0 = pt.bg0; v.bg1 = pt.bg1; v.bg2 = pt.bg2;
		v.panel = pt.panel; v.panel_alt = pt.panel_alt;
		v.chip = pt.chip; v.border = pt.border;
		v.text = pt.text; v.text_dim = pt.text_dim;
		v.accent = pt.accent; v.accent2 = pt.accent2; v.glow = pt.glow;
		v.particle_shape = pt.particle_shape;
		v.hue_cycle = pt.hue_cycle;
		s_dynamic.push_back(std::move(o));
	}

	void clear_dynamic_themes()
	{
		s_dynamic.clear();
	}
```

> 주: `add_dynamic_theme` 에서 `find_theme` 중복 체크가 새 요소 push 전에 수행되므로, `view.id` 가 `o->id`(방금 move 된 멤버)를 가리키는 self-참조는 push 이후에도 유효(unique_ptr 이 owned_theme 를 힙에 고정, move 는 벡터 슬롯의 포인터만 이동).

- [ ] **Step 3: 로컬 문법 검증(순수 로직 한도 내)**

`sherbet_themes.cpp` 는 `sherbet_owner.h`(빌드 매크로) 의존이라 전체 컴파일은 CI. 단, 추가한 레지스트리 로직의 문법은 아래 최소 스텁으로 확인 가능:

Run:
```bash
clang++ -std=c++17 -Ideps/imgui -fsyntax-only -DSHERBET_DEFAULT_THEME='"mint"' \
  -x c++ - <<'EOF'
#include "source/sherbet_theme_json.hpp"
#include <vector>
#include <memory>
#include <cstring>
namespace sherbet {
  static const theme s_themes[1] = {};
  static const std::size_t s_theme_count = 1;
  struct owned_theme { std::string id, display_name; theme view; };
  static std::vector<std::unique_ptr<owned_theme>> s_dynamic;
  const theme *find_theme(const char*){ return nullptr; }
  std::vector<const theme *> themes_snapshot(){ std::vector<const theme*> o; for(auto&d:s_dynamic) o.push_back(&d->view); return o; }
}
int main(){ return 0; }
EOF
```
Expected: 문법 오류 없이 통과(레지스트리 패턴 검증용 스텁 — 실제 파일 아님). 실패하면 위 코드 수정.

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_theme.hpp source/sherbet_themes.cpp
git commit -m "feat(sherbet): 동적 테마 레지스트리(themes_snapshot/add_dynamic_theme)

서버에서 받은 신규 테마를 unique_ptr<owned_theme> 로 소유(주소·문자열 안정).
find_theme 정적→동적 확장, 정적/동적 중복 id 무시. CI 에서 최종 컴파일 검증."
```

---

## Task 3: 엔타이틀 집합 기반 `is_unlocked` + `apply_content` (`sherbet_ui.*`)

**Files:**
- Modify: `source/sherbet_ui.hpp`
- Modify: `source/sherbet_ui.cpp`

**Interfaces:**
- Consumes: `sherbet::auth::enabled()`(`sherbet_auth.hpp`), `parse_themes_manifest`(Task 1), `add_dynamic_theme`/`clear_dynamic_themes`(Task 2), `set_active_theme`/`find_theme`.
- Produces:
  - `void sherbet::mark_entitled(const char *id);` — 엔타이틀 집합에 id 추가.
  - `void sherbet::clear_entitlements();`
  - `bool sherbet::is_unlocked(const char *id);` (시그니처 유지, 구현 교체) — `!auth::enabled()` 또는 기본테마 또는 엔타이틀 집합 포함 시 true.
  - `void sherbet::apply_content(const std::string &body);` — 매니페스트 파싱 → `clear_dynamic_themes`+`clear_entitlements` 후 각 테마 `mark_entitled`(+신규면 `add_dynamic_theme`) → 삭제된 동적 활성테마면 기본으로 폴백.
- **제거:** `unlock_theme`, `unlocked_csv`, `load_unlocked_csv`, `check_theme_code` (헤더·구현 모두), `s_unlocked` map, `#include "sherbet_license.hpp"` 중 테마 관련 사용부(license::verify_theme). `license::iequals` 는 `is_unlocked` 에서 계속 쓰므로 유지.

- [ ] **Step 1: 헤더 교체 (`sherbet_ui.hpp`)**

`sherbet_ui.hpp` 의 테마 언락 관련 선언(28–33행 부근)을 아래로 교체:

```cpp
	bool is_unlocked(const char *id);
	void mark_entitled(const char *id);   // 서버가 내려준 엔타이틀 테마 id 등록
	void clear_entitlements();            // 재페치 전 초기화
	void apply_content(const std::string &body); // /content/me 응답(JSON) 반영
```

(`unlock_theme`/`unlocked_csv`/`load_unlocked_csv`/`check_theme_code` 4줄 삭제.) `#include <string>` 이 없으면 추가.

- [ ] **Step 2: 구현 교체 (`sherbet_ui.cpp`)**

include 정리: `#include "sherbet_auth.hpp"` 추가(`auth::enabled`), `#include "sherbet_theme_json.hpp"` 추가. `sherbet_license.hpp` 는 프리셋/`iequals` 용으로 유지.

`s_unlocked` map 정의(26행 부근)를 엔타이틀 집합으로 교체:

```cpp
	// 서버(/content/me)가 내려준 엔타이틀 테마 id 집합. 기본(구매) 테마는 별도 처리(항상 열림).
	static std::set<std::string> s_entitled;
```

`is_unlocked`/`unlock_theme`/`unlocked_csv`/`load_unlocked_csv`/`check_theme_code`(35–77행 부근)를 아래로 교체:

```cpp
	bool is_unlocked(const char *id)
	{
		if (!id) return false;
		if (!auth::enabled()) return true; // 개발 빌드: 전부 열림
		if (license::iequals(id, safe_default_id())) return true; // 기본(구매) 테마 항상 열림
		return s_entitled.count(id) > 0;
	}
	void mark_entitled(const char *id)
	{
		if (id && id[0] != '\0') s_entitled.insert(id);
	}
	void clear_entitlements()
	{
		s_entitled.clear();
	}
	// /content/me 응답(JSON)을 반영: 동적 테마 재구성 + 엔타이틀 집합 재구성.
	// 렌더 스레드에서만 호출(레지스트리/엔타이틀은 렌더 루프가 읽음).
	void apply_content(const std::string &body)
	{
		const std::vector<parsed_theme> themes = parse_themes_manifest(body);
		clear_dynamic_themes();
		clear_entitlements();
		for (const parsed_theme &pt : themes)
		{
			if (pt.id.empty()) continue;
			mark_entitled(pt.id.c_str());
			if (find_theme(pt.id.c_str()) == nullptr) // 내장에 없는 신규 → 동적 등록
				add_dynamic_theme(pt);
		}
		// 활성 테마가 사라진 동적 테마였다면 기본으로 폴백
		if (find_theme(active_theme_id()) == nullptr)
			set_active_theme(safe_default_id());
	}
```

> `<set>` 는 이미 include 되어 있음(파일 12행). `parsed_theme`/`parse_themes_manifest` 는 `sherbet_theme_json.hpp`, `clear_dynamic_themes`/`add_dynamic_theme`/`find_theme` 는 `sherbet_theme.hpp`.

- [ ] **Step 3: 로컬 문법 검증(참고, 전체는 CI)**

`sherbet_ui.cpp` 전체는 imgui/auth 링크 의존이라 CI 검증. `apply_content` 로직 문법만 Task 1 파서 위에서 확인:

Run:
```bash
clang++ -std=c++17 -Ideps/imgui -fsyntax-only -x c++ - <<'EOF'
#include "source/sherbet_theme_json.hpp"
#include <set>
#include <string>
namespace sherbet {
  static std::set<std::string> s_entitled;
  const theme *find_theme(const char*);
  void add_dynamic_theme(const parsed_theme&);
  void clear_dynamic_themes();
  const char* active_theme_id();
  void set_active_theme(const char*);
  void mark_entitled(const char* id){ if(id&&id[0]) s_entitled.insert(id); }
  void clear_entitlements(){ s_entitled.clear(); }
  void apply_content(const std::string& body){
    auto ts = parse_themes_manifest(body);
    clear_dynamic_themes(); clear_entitlements();
    for (auto& pt: ts){ if(pt.id.empty()) continue; mark_entitled(pt.id.c_str());
      if(find_theme(pt.id.c_str())==nullptr) add_dynamic_theme(pt); }
    if(find_theme(active_theme_id())==nullptr) set_active_theme("mint");
  }
}
int main(){ return 0; }
EOF
```
Expected: 문법 통과. 실패 시 구현 수정.

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_ui.hpp source/sherbet_ui.cpp
git commit -m "feat(sherbet): is_unlocked 을 서버 엔타이틀 집합 기반으로 교체 + apply_content

FNV 테마코드 경로(unlock_theme/unlocked_csv/load_unlocked_csv/check_theme_code,
s_unlocked map) 제거. /content/me 응답을 파싱해 동적 테마+엔타이틀 재구성.
개발 빌드(auth 비활성)는 전부 열림 유지. CI 에서 컴파일 검증."
```

---

## Task 4: 콘텐츠 페치 glue (`sherbet_content.*`)

**Files:**
- Create: `source/sherbet_content.hpp`
- Create: `source/sherbet_content.cpp`

**Interfaces:**
- Consumes: `sherbet::http::get`(`sherbet_http.hpp`), 표준 파일 IO.
- Produces:
  - `bool sherbet::content::fetch(const std::string &bearer, const std::string &config_dir_utf8, std::string &out_body);` — `GET https://wonryeol.asuscomm.com/sherbet-auth/content/me`, 헤더 Bearer. HTTP 200이고 바디 비어있지 않으면 `out_body` 설정 + `<config_dir>/sherbet.themes` 에 기록 후 true. 그 외 false(캐시 미변경).
  - `bool sherbet::content::load_cached(const std::string &config_dir_utf8, std::string &out_body);` — `sherbet.themes` 읽어 out_body 설정, 성공 true.

- [ ] **Step 1: 헤더**

`source/sherbet_content.hpp`:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <string>

namespace sherbet
{
	namespace content
	{
		// /content/me 를 Bearer 로 GET. 200+비어있지 않은 바디면 out_body 설정 + 캐시 파일 기록 후 true.
		bool fetch(const std::string &bearer, const std::string &config_dir_utf8, std::string &out_body);
		// 캐시 파일(sherbet.themes) 로드. 성공 시 out_body 설정 + true.
		bool load_cached(const std::string &config_dir_utf8, std::string &out_body);
	}
}
```

- [ ] **Step 2: 구현**

`source/sherbet_content.cpp`:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_content.hpp"
#include "sherbet_http.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr wchar_t kContentPath[] = L"/sherbet-auth/content/me";

	std::filesystem::path cache_path(const std::string &config_dir_utf8)
	{
		return std::filesystem::u8path(config_dir_utf8) / L"sherbet.themes";
	}
}

bool sherbet::content::fetch(const std::string &bearer, const std::string &config_dir_utf8, std::string &out_body)
{
	std::string resp;
	const int status = sherbet::http::get(kHost, kContentPath, resp, bearer.empty() ? nullptr : bearer.c_str());
	if (status != 200 || resp.empty())
		return false;
	out_body = resp;
	std::ofstream out(cache_path(config_dir_utf8), std::ios::trunc | std::ios::binary);
	if (out.is_open())
		out << resp;
	return true;
}

bool sherbet::content::load_cached(const std::string &config_dir_utf8, std::string &out_body)
{
	std::ifstream in(cache_path(config_dir_utf8), std::ios::binary);
	if (!in.is_open())
		return false;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (text.empty())
		return false;
	out_body = std::move(text);
	return true;
}
```

- [ ] **Step 3: CMake 등록 확인**

`sherbet_content.cpp` 가 빌드에 포함되도록 소스 목록에 추가한다. `sherbet_auth.cpp` 가 등록된 곳을 찾아 같은 방식으로 추가:

Run: `grep -rn "sherbet_auth.cpp\|sherbet_http.cpp" CMakeLists.txt cmake/ 2>/dev/null`
- 나온 파일에서 `sherbet_auth.cpp` 바로 아래 줄에 `sherbet_content.cpp` 를 동일 들여쓰기로 추가.
- 소스 목록이 glob(`*.cpp`)이면 별도 등록 불필요(그 경우 이 스텝은 확인만).

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_content.hpp source/sherbet_content.cpp CMakeLists.txt
git commit -m "feat(sherbet): 콘텐츠 페치 glue(fetch/load_cached)

/content/me 를 Bearer 로 GET 후 sherbet.themes 캐시에 기록, 오프라인/재시작
복원용 load_cached. WinInet 의존 → CI 컴파일 검증."
```
> `CMakeLists.txt` 변경이 없었으면(glob) `git add` 에서 제외.

---

## Task 5: 컨트롤러에 토큰 노출 + 콘텐츠 페치 연동 (`sherbet_auth.*`)

**Files:**
- Modify: `source/sherbet_auth.hpp`
- Modify: `source/sherbet_auth.cpp`

**Interfaces:**
- Consumes: `sherbet::content::fetch`/`load_cached`(Task 4).
- Produces (controller 멤버 함수):
  - `std::string token() const;` — `_cache.token` 복사(락 보호).
  - `void begin_fetch_content();` — 인증 상태에서 워커 스레드로 `content::fetch(token, _config_dir, body)` 실행 → 성공 시 `_content_body`(뮤텍스) 저장 + `_content_ready=true`. 진행 중 재진입 방지.
  - `bool take_content(std::string &out);` — `_content_ready` 면 `_content_body` 를 out 으로 move, ready 해제, true. 렌더 스레드 전용.
  - `init()` 확장: `content::load_cached` 로 캐시가 있으면 `_content_body`+`_content_ready` 세팅(첫 프레임에 apply).

- [ ] **Step 1: 헤더 확장 (`sherbet_auth.hpp`)**

`controller` public 에 추가:

```cpp
				std::string token() const;
				void begin_fetch_content();
				bool take_content(std::string &out);
```

private 멤버에 추가(기존 `_pending_save` 아래):

```cpp
				std::string _content_body;               // _mtx 보호: 페치/캐시된 /content/me 바디
				std::atomic<bool> _content_ready{ false };// 렌더 스레드가 take_content 로 인출
				std::atomic<bool> _content_active{ false };// 페치 워커 진행 중 재진입 방지
```

- [ ] **Step 2: 구현 (`sherbet_auth.cpp`)**

include 에 `#include "sherbet_content.hpp"` 추가.

`init()` 에서 캐시 로드(개발 빌드 early-return 이후, `enabled()` 인 경로). `_hwid = ...` 아래, 캐시 토큰 로드 부분과 함께 테마 캐시도 로드:

```cpp
	// 테마 캐시 로드 — 첫 프레임 take_content 로 레지스트리에 복원(오프라인/재시작 지속)
	{
		std::string cached;
		if (sherbet::content::load_cached(_config_dir, cached) && !cached.empty()) {
			std::lock_guard<std::mutex> lk(_mtx);
			_content_body = std::move(cached);
			_content_ready = true;
		}
	}
```

(위 블록은 `init()` 내 캐시 파일 로드 직후, verify 워커 시작 전에 넣는다. `enabled()==false` 인 개발 빌드는 `init()` 초입 `return` 으로 도달하지 않음 — 개발 빌드에선 apply 없이도 `is_unlocked` 가 전부 true.)

파일 하단(기존 메서드들 뒤)에 신규 메서드 구현 추가:

```cpp
std::string sherbet::auth::controller::token() const
{
	std::lock_guard<std::mutex> lk(_mtx);
	return _cache.token;
}

bool sherbet::auth::controller::take_content(std::string &out)
{
	if (!_content_ready.load())
		return false;
	std::lock_guard<std::mutex> lk(_mtx);
	if (_content_body.empty()) { _content_ready = false; return false; }
	out = std::move(_content_body);
	_content_body.clear();
	_content_ready = false;
	return true;
}

void sherbet::auth::controller::begin_fetch_content()
{
	if (!enabled()) return;
	if (!_authed.load()) return;                 // 인증된 상태에서만
	if (_content_active.exchange(true)) return;  // 이미 페치 중

	std::string bearer;
	{ std::lock_guard<std::mutex> lk(_mtx); bearer = _cache.token; }
	if (bearer.empty()) { _content_active = false; return; }

	std::thread([this, bearer]() {
		std::string body;
		if (sherbet::content::fetch(bearer, _config_dir, body) && !body.empty()) {
			std::lock_guard<std::mutex> lk(_mtx);
			_content_body = std::move(body);
			_content_ready = true;
		}
		_content_active = false;
	}).detach();
}
```

> **동시성 주의:** 페치 워커는 `_config_dir`(init 이후 불변) 만 읽고 `_content_body`/`_content_ready` 만 쓴다. `_worker` 스레드(verify/login)와 별개의 detached 스레드를 쓰므로 `join_worker()` 와 충돌하지 않는다. detached 스레드가 컨트롤러 파괴 후 접근하는 것을 막기 위해, 소멸자에서 페치 완료를 보장할 수 없다는 점은 감수한다(수명 = 런타임 전체, 오버레이 열림 중에만 트리거). **단순화를 위해 detach 대신 멤버 `_worker` 재사용은 금지**(로그인/버파이와 겹칠 수 있음).

> **재검토 포인트(리뷰어 확인):** detached 스레드 + `this` 캡처는 컨트롤러가 런타임 전체 수명을 갖기에 실무상 안전하나, 엄밀히는 소멸 경쟁이 존재. 런타임 종료 시 오버레이 페치가 드묾을 고려한 트레이드오프. 리뷰에서 더 안전한 패턴(예: `_stop` 확인 후 write) 필요성 판단.

- [ ] **Step 3: Commit**

```bash
git add source/sherbet_auth.hpp source/sherbet_auth.cpp
git commit -m "feat(sherbet): 컨트롤러 token()/begin_fetch_content/take_content

인증 상태에서 /content/me 를 백그라운드로 페치해 pending 버퍼에 저장,
렌더 스레드가 take_content 로 인출. init 은 테마 캐시를 pending 에 실어
첫 프레임 복원. 레지스트리 변형은 렌더 스레드에만. CI 컴파일 검증."
```

---

## Task 6: 마켓 UI 교체 + config 정리 + 프레임 연동 (`runtime_gui.cpp`)

**Files:**
- Modify: `source/runtime_gui.cpp`

**Interfaces:**
- Consumes: `themes_snapshot()`(Task 2), `is_unlocked`/`apply_content`(Task 3), `_sherbet_auth.begin_fetch_content()`/`take_content()`/`is_authed()`(Task 5).
- Produces: 없음(최종 UI/연동 계층).

- [ ] **Step 1: config `Unlocked` 저장/로드 제거**

로드부(374–375행): `load_unlocked_csv` 줄 삭제. `ActiveTheme` 로드는 유지:

```cpp
	{ std::string s; config.get("SHERBET", "ActiveTheme", s); if (!s.empty()) sherbet::set_active_theme(s.c_str());
```
(다음 줄의 `std::string u; ... load_unlocked_csv(u.c_str());` 삭제.)

저장부(506행): `config.set("SHERBET", "Unlocked", sherbet::unlocked_csv());` 줄 삭제. (`ActiveTheme`/`PresetUnlocked` 는 유지.)

- [ ] **Step 2: 프레임 루프에서 콘텐츠 인출→적용**

`_sherbet_auth.tick()` 호출 지점(오버레이 게이트, 약 1585행)에서 tick 직후 추가:

```cpp
		{ std::string _content; if (_sherbet_auth.take_content(_content)) sherbet::apply_content(_content); }
```
(정확한 위치: `_sherbet_auth.tick();` 이 있는 줄 바로 다음. `grep -n "_sherbet_auth.tick()" source/runtime_gui.cpp` 로 확인.)

- [ ] **Step 3: 마켓 테마 세그먼트 교체**

`draw_gui_market()` 의 `if (seg == 0) { ... }` 블록(약 3788–3833행)을 아래로 교체. 안내문·"내 전용 불러오기" 버튼 추가, `themes_snapshot()` 순회, SHRB- InputText/해제 제거:

```cpp
	if (seg == 0)
	{
		ImGui::TextUnformatted("\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4 \xED\x85\x8C\xEB\xA7\x88. \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x9C \xED\x85\x8C\xEB\xA7\x88\xEB\x8A\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\x84\xB8\xEC\x9A\x94."); // "오버레이 테마. 구매한 테마는 디스코드 로그인 후 불러오세요."
		ImGui::Spacing();

		// "내 전용 불러오기" — 인증 상태에서만. /content/me 페치(비동기).
		if (sherbet::auth::enabled() && _sherbet_auth.is_authed())
		{
			if (sherbet::pill_button(ICON_FK_DOWNLOAD "  \xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0", true)) // "내 전용 불러오기"
				_sherbet_auth.begin_fetch_content();
			ImGui::Spacing();
		}

		const std::vector<const sherbet::theme *> snapshot = sherbet::themes_snapshot();
		for (std::size_t i = 0; i < snapshot.size(); ++i)
		{
			const sherbet::theme &th = *snapshot[i];
			const bool unlocked = sherbet::is_unlocked(th.id);
			const bool active = std::strcmp(th.id, sherbet::active_theme_id()) == 0;
			ImGui::PushID((int)i);
			sherbet::begin_card("##theme_card");
			ImDrawList *dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float bw = ImGui::GetContentRegionAvail().x;
			dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + 26.0f), th.bg1, th.accent, th.accent, th.bg1);
			ImGui::Dummy(ImVec2(bw, 30.0f));
			ImGui::Text("%s", th.display_name);
			if (active)
				ImGui::TextDisabled("%s", ICON_FK_OK " \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"); // "사용 중"
			else if (unlocked)
			{
				if (sherbet::pill_button(ICON_FK_OK "  \xEC\xA0\x81\xEC\x9A\xA9", false)) // "적용"
				{ sherbet::set_active_theme(th.id); save_config(); }
			}
			else
			{
				ImGui::TextDisabled("%s", ICON_FK_LOCK " \xEC\x9E\xA0\xEA\xB9\x80"); // "잠김"
			}
			sherbet::end_card();
			ImGui::PopID();
		}
	}
```

> `ICON_FK_DOWNLOAD` 가 폰트 아이콘 세트에 있는지 확인(`grep -n "ICON_FK_DOWNLOAD" source/imgui_widgets.cpp deps/ 2>/dev/null` 또는 forkawesome 헤더). 없으면 `ICON_FK_CLOUD_DOWNLOAD` 또는 아이콘 없이 텍스트만 사용. 마켓에 이미 쓰인 `ICON_FK_KEY`/`ICON_FK_LOCK` 세트 기준으로 존재하는 것 선택.

- [ ] **Step 4: 컴파일 의존 정리 확인**

교체 후 다음이 남아있지 않은지 확인(제거 대상):
- `runtime_gui.cpp` 내 `check_theme_code`, `unlock_theme`, `unlocked_csv`, `load_unlocked_csv`, `code_buf` (테마 마켓 것). `grep -n` 으로 확인:

Run: `grep -n "check_theme_code\|unlock_theme\|unlocked_csv\|load_unlocked_csv\|all_themes" source/runtime_gui.cpp`
Expected: 테마 관련 매치 없음(프리셋 `_sherbet_preset_*` 는 무관하니 남아도 됨). `all_themes` 매치도 없어야 함(snapshot 으로 교체).

- [ ] **Step 5: Commit**

```bash
git add source/runtime_gui.cpp
git commit -m "feat(sherbet): 테마 마켓을 서버 엔타이틀 기반으로 교체

themes_snapshot 순회 + '내 전용 불러오기'(begin_fetch_content) 버튼,
프레임 루프 take_content→apply_content 연동. SHRB- 코드 InputText/해제와
config Unlocked 저장·로드 제거. 프리셋 마켓은 그대로. CI 컴파일 검증."
```

---

## Task 7: CI 통합 빌드 검증 (auth OFF/ON)

**Files:** 없음(빌드/CI 확인만).

**Interfaces:** 전체 링크.

- [ ] **Step 1: 커밋 푸시 후 CI 트리거**

Run: `git push origin sherbet-base`
그리고 GitHub Actions 빌드 워크플로가 도는지 확인. 온라인 인증 OFF(기본)와 ON(`online_auth` dispatch) 두 경로 모두 컴파일/링크되어야 한다.

- [ ] **Step 2: CI 결과 확인**

Run: `gh run list --branch sherbet-base --limit 3`
그리고 `gh run watch <run-id>` 또는 `gh run view <run-id>`.
Expected: 빌드 GREEN. 실패 시 로그의 컴파일 오류를 해당 Task 파일로 되돌아가 수정→재커밋→재푸시.

- [ ] **Step 3: (실패 시) 흔한 오류 체크리스트**
- `themes_snapshot` 미선언 → Task 2 `sherbet_theme.hpp` 선언 확인.
- `parsed_theme` forward 선언과 실제 정의 불일치 → `sherbet_theme_json.hpp` include 순서.
- `sherbet_content.cpp` 링크 누락 → Task 4 CMake 등록.
- `ICON_FK_DOWNLOAD` 미정의 → 존재하는 아이콘으로 교체.
- `auth::enabled` 링크 → `sherbet_ui.cpp`/`runtime_gui.cpp` 가 `sherbet_auth.hpp` include.

---

## Self-Review (작성자 체크)

- **스펙 커버리지:** §13.3(레지스트리)=Task 2, §13.4(엔타이틀/is_unlocked/제거)=Task 3, §13.5(버튼/페치/토큰/캐시/init)=Task 4·5·6, §13.1 파서=Task 1. §13.6 위협모델은 설계 불변(코드 없음). ✅
- **타입 일관성:** `parsed_theme`(Task 1) → `add_dynamic_theme`(Task 2) → `apply_content`(Task 3) 시그니처 일치. `themes_snapshot()` 반환형 `std::vector<const theme *>` Task 2 정의/Task 6 소비 일치. `token()`/`begin_fetch_content()`/`take_content()` Task 5 정의/Task 6 소비 일치. `content::fetch`/`load_cached` Task 4 정의/Task 5 소비 일치. ✅
- **플레이스홀더:** 모든 스텝에 실제 코드/명령. `ICON_FK_DOWNLOAD` 만 "존재 확인 후 대체" 단서 — 코드 폰트 세트 의존이라 확정 불가, 확인 절차 명시. ✅
- **글로벌 제약:** 커밋 문구 무해(Claude 문구 없음), Mac 컴파일 한계 각 Task 명시, 저작권 헤더 포함. ✅
