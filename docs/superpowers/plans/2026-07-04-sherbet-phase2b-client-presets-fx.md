# Sherbet Phase 2b — 클라 프리셋·fx 배치 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** "내 전용 불러오기"가 테마뿐 아니라 서버가 배달한 **프리셋(.ini)·이펙트(.fx)** 파일까지 다운로드해 디스크에 배치하고, `Sherbet-Fx` 를 이펙트 검색경로에 등록해 `reload_effects()` 하며, 마켓 프리셋 세그먼트를 서버 목록 기반으로 교체한다(PRE- 코드/체험/내장 프리셋 제거).

**Architecture:** Phase 2a 서버(`/content/me` 확장 + `/content/file/<id>`, 배포됨) 소비. 순수 로직(매니페스트의 presets/effects 배열 파싱 + 다운로드 대상 경로 계산)은 host(clang)로 테스트. 네트워크(파일 다운로드)·파일 IO 는 Phase 1b 콘텐츠 워커를 확장해 백그라운드에서. 이펙트 검색경로 등록·`reload_effects`·마켓 UI 는 렌더 스레드(runtime). 다운로드 완료 후에만 매니페스트를 렌더 스레드에 넘겨(take_content) 파일이 디스크에 있는 상태에서 reload 하도록 순서를 보장한다.

**Tech Stack:** C++17, ImGui, WinInet(`sherbet::http`/`sherbet::content`), std::thread/atomic/mutex, ReShade `reload_effects`/`set_current_preset_path`/`_effect_search_paths`. Host 테스트 `clang++ -std=c++17 -Ideps/imgui`.

## Global Constraints

- **커밋 메시지 Claude/AI·공동저자 문구 절대 금지**(CLAUDE.md). 한국어·무해한 설명만.
- **Mac은 Windows C++ 컴파일 불가.** `sherbet_content.*`/`sherbet_auth.*`/`runtime_gui.cpp`/`sherbet_ui.cpp` 는 CI(약 7–8분)가 유일 검증. 순수 헤더(`sherbet_content_json.hpp`)+테스트만 로컬 `clang++ -std=c++17 -Ideps/imgui` 로 검증.
- **신규 `.hpp` 상단 저작권 헤더:** `/* Copyright (C) 2026 정렬 (Jeong-Ryeol) / SPDX-License-Identifier: BSD-3-Clause */` 2줄 형식.
- **레지스트리/런타임 상태 변형은 렌더 스레드에서만.** 워커는 네트워크+파일 IO만. (Phase 1b 규칙 유지 — 콘텐츠 페치 스레드는 조인 가능한 `_content_worker`.)
- **서버 계약(2a, 배포됨):** `GET /content/me` → `{themes, presets, effects}`, 각 preset/effect 항목 `{id, filename, display_name}`(role 제거됨). `GET /content/file/<id>` Bearer → 파일 바이트(200), 401/403/404/503. host=`L"wonryeol.asuscomm.com"`, 경로 `L"/sherbet-auth/content/file/"` + id.
- **파일명 안전:** 서버 화이트리스트지만 클라도 `filename` 에서 `/`·`\`·`..` 제거하고 basename 만 사용.
- **배치 폴더(config 디렉터리 = `_config_path.parent_path()` 기준):** 프리셋 → `Sherbet-Presets/<filename>`, 이펙트/텍스처 → `Sherbet-Fx/<filename>`. `Sherbet-Fx` 절대경로를 `_effect_search_paths`+`_texture_search_paths` 에 없으면 추가(config 저장) 후 `reload_effects()`.
- **PRE-(프리셋 오프라인 코드)·10초 체험·내장 `IDR_SHERBET_PRESET_PERSONAL` 제거**(테마 SHRB- 제거와 동형). `license::verify_preset` 호출 제거. `sherbet_license.hpp` 자체(노드락 등)는 유지.
- **`SHERBET_ONLINE_AUTH==0`(개발 빌드):** 콘텐츠 페치 no-op(Phase 1b `begin_fetch_content` 가 이미 `enabled()` 가드). 마켓 프리셋 세그먼트는 빈 목록/안내만.

---

## File Structure

- **Create `source/sherbet_content_json.hpp`** — 순수. `content_item{id,filename,display_name}`, `parse_content_items(body,key)`, `safe_basename(filename)`, `download_target{id,dest_path}`, `content_download_targets(body, presets_dir, effects_dir)`. `sherbet_theme_json.hpp`의 `detail::json_str` + 신규 `detail::split_top_level_objects` 재사용.
- **Create `tools/sherbet_content_json_test.cpp`** — 위 순수 로직 host 테스트.
- **Modify `source/sherbet_theme_json.hpp`** — 객체 분할 로직을 `detail::split_top_level_objects(body,key)` 로 추출(파일 파서·콘텐츠 파서 공용). `parse_themes_manifest` 를 이 헬퍼 사용으로 리팩터(동작 불변 — 기존 host 테스트로 회귀 확인).
- **Modify `source/sherbet_content.hpp`/`.cpp`** — `fetch_file(bearer, id, dest_path_utf8)` 추가(GET `/content/file/<id>` Bearer → 부모 디렉터리 생성 후 바이트 기록).
- **Modify `source/sherbet_auth.hpp`/`.cpp`** — 콘텐츠 워커 확장: `/content/me` 바디 수신 후 `content_download_targets` 로 파일 목록 산출→각 `content::fetch_file` 다운로드→완료 시 `_content_ready`(바디)+`_content_files_changed` 세팅. 렌더 스레드용 `bool take_files_changed()`. init(캐시) 경로는 `_content_files_changed` 세팅 안 함(재시작 시 불필요 reload 방지).
- **Modify `source/sherbet_ui.hpp`/`.cpp`** — `apply_content` 에 preset 아이템 파싱→`s_content_presets` 저장 추가(마켓 UI용). `const std::vector<content_item> &content_presets()` 노출.
- **Modify `source/runtime_gui.cpp`** — 프레임 루프: apply_content 뒤 `take_files_changed()` 시 `Sherbet-Fx` 검색경로 등록+`reload_effects`. 마켓 프리셋 세그먼트(seg==1)를 서버 목록 기반 카드로 교체(적용→`set_current_preset_path(config_dir/Sherbet-Presets/<filename>)`). PRE-/체험/내장 제거.

**의존 방향:** `sherbet_content_json.hpp`(순수) ← `sherbet_auth.cpp`(워커: download_targets) + `sherbet_ui.cpp`(apply_content: preset 파싱). `runtime_gui.cpp` → `sherbet_auth`(take_files_changed) + `sherbet_ui`(content_presets/apply_content) + runtime(_effect_search_paths/reload_effects). 컨트롤러는 여전히 테마 레지스트리를 모름 — download_targets(순수)로 경로만 계산해 다운로드.

---

## Task 1: 순수 콘텐츠 매니페스트 파서 (`sherbet_content_json.hpp`) + host 테스트

**Files:**
- Modify: `source/sherbet_theme_json.hpp` (객체 분할 헬퍼 추출)
- Create: `source/sherbet_content_json.hpp`
- Create: `tools/sherbet_content_json_test.cpp`

**Interfaces:**
- Consumes: `sherbet::detail::json_str`(기존), `sherbet::detail::split_top_level_objects`(Task 1 신규).
- Produces:
  - `struct sherbet::content_item { std::string id, filename, display_name; };`
  - `std::vector<content_item> sherbet::parse_content_items(const std::string &body, const char *key);` — `body` 의 `"key"`(=`"presets"` 또는 `"effects"`) 배열에서 각 객체의 `id`/`filename`/`display_name` 추출. `id` 비었으면 스킵. `display_name` 없으면 `filename`.
  - `std::string sherbet::safe_basename(const std::string &filename);` — `/`·`\` 기준 마지막 요소만, `..` 는 빈 문자열로 취급(거부).
  - `struct sherbet::download_target { std::string id, dest_path; };`
  - `std::vector<download_target> sherbet::content_download_targets(const std::string &body, const std::string &presets_dir, const std::string &effects_dir);` — presets→`presets_dir + "/" + safe_basename(filename)`, effects→`effects_dir + "/" + safe_basename(filename)`. id 또는 basename 비면 스킵.

- [ ] **Step 1: 객체 분할 헬퍼 추출 (`sherbet_theme_json.hpp`)**

`namespace detail` 안(`json_str`/`json_bool`/`color_field` 옆)에 추가:

```cpp
		// body 의 "key":[ {..}, {..} ] 배열에서 최상위 객체 문자열들을 brace 매칭으로 잘라 반환.
		inline std::vector<std::string> split_top_level_objects(const std::string &body, const char *key)
		{
			std::vector<std::string> out;
			std::string needle = std::string("\"") + key + "\"";
			std::size_t tk = body.find(needle);
			if (tk == std::string::npos) return out;
			std::size_t lb = body.find('[', tk);
			if (lb == std::string::npos) return out;
			std::size_t i = lb + 1;
			while (i < body.size())
			{
				while (i < body.size() && body[i] != '{' && body[i] != ']') ++i;
				if (i >= body.size() || body[i] == ']') break;
				const std::size_t start = i;
				int depth = 0;
				bool in_str = false;
				for (; i < body.size(); ++i)
				{
					const char c = body[i];
					if (in_str) { if (c == '\\') { ++i; continue; } if (c == '"') in_str = false; continue; }
					if (c == '"') in_str = true;
					else if (c == '{') ++depth;
					else if (c == '}') { if (--depth == 0) { ++i; break; } }
				}
				out.push_back(body.substr(start, i - start));
			}
			return out;
		}
```

그리고 `parse_themes_manifest` 의 본문에서 직접 brace 매칭하던 루프를 이 헬퍼 사용으로 교체(동작 불변):

```cpp
	inline std::vector<parsed_theme> parse_themes_manifest(const std::string &body)
	{
		std::vector<parsed_theme> out;
		for (const std::string &obj : detail::split_top_level_objects(body, "themes"))
		{
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
```

- [ ] **Step 2: 회귀 확인 — 기존 테마 파서 host 테스트**

Run: `clang++ -std=c++17 -Ideps/imgui tools/sherbet_theme_json_test.cpp -o /tmp/tj && /tmp/tj`
Expected: `sherbet_theme_json_test: ALL PASS` (리팩터로 동작 안 바뀜 확인).

- [ ] **Step 3: 콘텐츠 파서 테스트 작성 (`tools/sherbet_content_json_test.cpp`)**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Ideps/imgui tools/sherbet_content_json_test.cpp -o /tmp/tc && /tmp/tc
#include "../source/sherbet_content_json.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet;

static const char *kBody = R"({"themes":[{"id":"t1"}],
"presets":[
  {"id":"p-1","filename":"Strawberry.ini","display_name":"딸기"},
  {"id":"p-2","filename":"noname.ini"}
],
"effects":[
  {"id":"e-1","filename":"Grade.fx","display_name":"Grade"}
]})";

static void test_parse_items()
{
    auto ps = parse_content_items(kBody, "presets");
    assert(ps.size() == 2);
    assert(ps[0].id == "p-1" && ps[0].filename == "Strawberry.ini" && ps[0].display_name == "딸기");
    assert(ps[1].display_name == "noname.ini"); // display_name 없으면 filename
    auto es = parse_content_items(kBody, "effects");
    assert(es.size() == 1 && es[0].id == "e-1" && es[0].filename == "Grade.fx");
    assert(parse_content_items("{}", "presets").empty());
    assert(parse_content_items(kBody, "nope").empty());
}

static void test_safe_basename()
{
    assert(safe_basename("Strawberry.ini") == "Strawberry.ini");
    assert(safe_basename("a/b/c.fx") == "c.fx");
    assert(safe_basename("a\\b\\c.fx") == "c.fx");
    assert(safe_basename("../evil.ini").empty());     // .. 거부
    assert(safe_basename("dir/../evil").empty());      // 경로 내 .. 거부
    assert(safe_basename("").empty());
}

static void test_download_targets()
{
    auto t = content_download_targets(kBody, "C:/g/Sherbet-Presets", "C:/g/Sherbet-Fx");
    // presets 2 + effects 1 = 3
    assert(t.size() == 3);
    assert(t[0].id == "p-1" && t[0].dest_path == "C:/g/Sherbet-Presets/Strawberry.ini");
    assert(t[1].id == "p-2" && t[1].dest_path == "C:/g/Sherbet-Presets/noname.ini");
    assert(t[2].id == "e-1" && t[2].dest_path == "C:/g/Sherbet-Fx/Grade.fx");
}

int main()
{
    test_parse_items();
    test_safe_basename();
    test_download_targets();
    std::puts("sherbet_content_json_test: ALL PASS");
    return 0;
}
```

- [ ] **Step 4: 테스트 실패 확인**

Run: `clang++ -std=c++17 -Ideps/imgui tools/sherbet_content_json_test.cpp -o /tmp/tc`
Expected: FAIL — `'../source/sherbet_content_json.hpp' file not found`.

- [ ] **Step 5: 콘텐츠 파서 헤더 작성 (`source/sherbet_content_json.hpp`)**

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 원격 콘텐츠(프리셋/이펙트) 매니페스트 파서 — 순수 로직.
// sherbet_theme_json.hpp 의 JSON 헬퍼 재사용. WinInet/스레드 없음 → host 테스트 가능.
#pragma once

#include "sherbet_theme_json.hpp"
#include <string>
#include <vector>

namespace sherbet
{
	struct content_item { std::string id, filename, display_name; };

	inline std::vector<content_item> parse_content_items(const std::string &body, const char *key)
	{
		std::vector<content_item> out;
		for (const std::string &obj : detail::split_top_level_objects(body, key))
		{
			content_item it;
			detail::json_str(obj, "id", it.id);
			detail::json_str(obj, "filename", it.filename);
			if (!detail::json_str(obj, "display_name", it.display_name))
				it.display_name = it.filename;
			if (it.id.empty()) continue;
			out.push_back(std::move(it));
		}
		return out;
	}

	// 경로 구분자 뒤 마지막 요소만. ".." 포함 요소는 거부(빈 문자열).
	inline std::string safe_basename(const std::string &filename)
	{
		std::size_t slash = filename.find_last_of("/\\");
		std::string base = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
		if (base.empty() || base == "." || base == "..") return "";
		if (filename.find("..") != std::string::npos) return ""; // 경로 어디에든 .. 있으면 거부
		return base;
	}

	struct download_target { std::string id, dest_path; };

	inline std::vector<download_target> content_download_targets(
		const std::string &body, const std::string &presets_dir, const std::string &effects_dir)
	{
		std::vector<download_target> out;
		auto add = [&out](const std::vector<content_item> &items, const std::string &dir) {
			for (const content_item &it : items)
			{
				const std::string base = safe_basename(it.filename);
				if (it.id.empty() || base.empty()) continue;
				out.push_back({ it.id, dir + "/" + base });
			}
		};
		add(parse_content_items(body, "presets"), presets_dir);
		add(parse_content_items(body, "effects"), effects_dir);
		return out;
	}
}
```

- [ ] **Step 6: 콘텐츠 파서 테스트 통과 확인**

Run: `clang++ -std=c++17 -Ideps/imgui tools/sherbet_content_json_test.cpp -o /tmp/tc && /tmp/tc`
Expected: `sherbet_content_json_test: ALL PASS`.

- [ ] **Step 7: Commit**

```bash
git add source/sherbet_theme_json.hpp source/sherbet_content_json.hpp tools/sherbet_content_json_test.cpp
git commit -m "feat(sherbet): 콘텐츠 매니페스트 파서(presets/effects) + 다운로드 대상 계산

객체 분할 로직을 detail::split_top_level_objects 로 추출해 테마·콘텐츠 파서 공용.
content_item/parse_content_items/safe_basename/content_download_targets, host 테스트."
```

---

## Task 2: `content::fetch_file` — 단일 파일 다운로드 glue (`sherbet_content.*`)

**Files:**
- Modify: `source/sherbet_content.hpp`
- Modify: `source/sherbet_content.cpp`

**Interfaces:**
- Consumes: `sherbet::http::get`(host/path/out/bearer), `<filesystem>`/`<fstream>`.
- Produces: `bool sherbet::content::fetch_file(const std::string &bearer, const std::string &item_id, const std::string &dest_path_utf8);` — `GET /sherbet-auth/content/file/<item_id>` Bearer. HTTP 200 이고 바디 비어있지 않으면 dest 부모 디렉터리 생성 후 바이트 기록, true. 그 외 false(기존 파일 미변경).

- [ ] **Step 1: 헤더 선언 추가 (`sherbet_content.hpp`)**

`namespace content` 안에 추가:
```cpp
		// /content/file/<id> 를 Bearer 로 GET. 200+비어있지 않으면 dest_path 에 바이트 기록 후 true.
		bool fetch_file(const std::string &bearer, const std::string &item_id, const std::string &dest_path_utf8);
```

- [ ] **Step 2: 구현 (`sherbet_content.cpp`)**

익명 네임스페이스에 경로 상수 추가(기존 `kHost`/`kContentPath` 옆):
```cpp
	const std::string kFilePathPrefix = "/sherbet-auth/content/file/";
```
`<filesystem>` 는 이미 include 됨. 파일 하단에 구현 추가:
```cpp
bool sherbet::content::fetch_file(const std::string &bearer, const std::string &item_id, const std::string &dest_path_utf8)
{
	const std::string path = kFilePathPrefix + item_id;      // item_id 는 서버 매니페스트의 불투명 키(ASCII)
	const std::wstring wpath(path.begin(), path.end());
	std::string resp;
	const int status = sherbet::http::get(kHost, wpath.c_str(), resp, bearer.empty() ? nullptr : bearer.c_str());
	if (status != 200 || resp.empty())
		return false;
	const std::filesystem::path dest = std::filesystem::u8path(dest_path_utf8);
	std::error_code ec;
	std::filesystem::create_directories(dest.parent_path(), ec); // 실패해도 아래 open 에서 재판정
	std::ofstream out(dest, std::ios::trunc | std::ios::binary);
	if (!out.is_open())
		return false;
	out.write(resp.data(), static_cast<std::streamsize>(resp.size()));
	return true;
}
```

- [ ] **Step 3: 로컬 참고 확인(전체는 CI)**

`sherbet_content.cpp` 는 WinInet 의존이라 CI 컴파일. 시그니처/헤더 일치만 육안 확인.

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_content.hpp source/sherbet_content.cpp
git commit -m "feat(sherbet): content::fetch_file — /content/file/<id> 단일 파일 다운로드

Bearer GET 후 200+비어있지 않으면 dest 부모 디렉터리 생성하고 바이트 기록.
실패 시 기존 파일 미변경. CI 컴파일 검증."
```

---

## Task 3: 콘텐츠 워커에 파일 다운로드 연동 (`sherbet_auth.*`)

**Files:**
- Modify: `source/sherbet_auth.hpp`
- Modify: `source/sherbet_auth.cpp`

**Interfaces:**
- Consumes: `content::fetch_file`(Task 2), `content_download_targets`(Task 1).
- Produces:
  - 콘텐츠 워커(`begin_fetch_content`)가 `/content/me` 바디 수신(200) 후, `content_download_targets(body, _config_dir+"/Sherbet-Presets", _config_dir+"/Sherbet-Fx")` 로 대상 산출 → 각 `content::fetch_file(bearer, id, dest)` 다운로드 → `_content_body`(바디)+`_content_ready`+`_content_files_changed` 세팅.
  - `bool take_files_changed();` — `_content_files_changed` 면 false 로 클리어하고 true 반환(렌더 스레드가 검색경로 등록+reload 트리거).
  - 신규 멤버 `std::atomic<bool> _content_files_changed{false}`.
  - `init()` 캐시 프리로드 경로는 `_content_files_changed` 를 세팅하지 않음(재시작 시 파일 이미 디스크에 있고 검색경로는 config 로 복원 → 불필요 reload 방지).

- [ ] **Step 1: 헤더 (`sherbet_auth.hpp`)**

public 에 추가:
```cpp
				bool take_files_changed();
```
private 멤버에 추가(`_content_active` 옆):
```cpp
				std::atomic<bool> _content_files_changed{ false }; // 파일 새로 받음 → 렌더 스레드가 reload
```
include 에 추가(파일 상단):
```cpp
#include "sherbet_content_json.hpp" // content_download_targets
```

- [ ] **Step 2: 구현 (`sherbet_auth.cpp`)**

`begin_fetch_content` 의 워커 람다에서, `content::fetch` 성공 후 파일 다운로드를 추가한다. 기존:
```cpp
	_content_worker = std::thread([this, bearer]() {
		std::string body;
		if (sherbet::content::fetch(bearer, _config_dir, body) && !body.empty()) {
			std::lock_guard<std::mutex> lk(_mtx);
			_content_body = std::move(body);
			_content_ready = true;
		}
		_content_active = false;
	});
```
를 아래로 교체:
```cpp
	_content_worker = std::thread([this, bearer]() {
		std::string body;
		if (sherbet::content::fetch(bearer, _config_dir, body) && !body.empty()) {
			// 프리셋/이펙트 파일 다운로드(순수 계산으로 대상 산출 → 각 다운로드)
			const auto targets = sherbet::content_download_targets(
				body, _config_dir + "/Sherbet-Presets", _config_dir + "/Sherbet-Fx");
			for (const auto &t : targets) {
				if (_stop.load()) break;
				sherbet::content::fetch_file(bearer, t.id, t.dest_path); // 실패는 조용히 스킵
			}
			{
				std::lock_guard<std::mutex> lk(_mtx);
				_content_body = std::move(body);
				_content_ready = true;
			}
			_content_files_changed = true; // 렌더 스레드가 검색경로+reload
		}
		_content_active = false;
	});
```

파일 하단에 `take_files_changed` 구현 추가:
```cpp
bool sherbet::auth::controller::take_files_changed()
{
	return _content_files_changed.exchange(false);
}
```

(주: `init()` 의 캐시 프리로드는 `_content_ready` 만 세팅하는 기존 코드 그대로 — `_content_files_changed` 는 건드리지 않는다.)

- [ ] **Step 3: Commit**

```bash
git add source/sherbet_auth.hpp source/sherbet_auth.cpp
git commit -m "feat(sherbet): 콘텐츠 워커가 프리셋/이펙트 파일까지 다운로드

/content/me 바디 수신 후 content_download_targets 로 대상 산출→각 fetch_file
(Sherbet-Presets/Sherbet-Fx). 완료 시 _content_files_changed 세팅, 렌더 스레드가
take_files_changed 로 검색경로 등록+reload. 캐시 프리로드는 트리거 안 함. CI 검증."
```

---

## Task 4: `apply_content` 프리셋 목록 파싱 (`sherbet_ui.*`)

**Files:**
- Modify: `source/sherbet_ui.hpp`
- Modify: `source/sherbet_ui.cpp`

**Interfaces:**
- Consumes: `parse_content_items`(Task 1), `content_item`(Task 1).
- Produces:
  - `apply_content(body)` 가 기존 테마 처리에 더해 `s_content_presets = parse_content_items(body, "presets")` 저장.
  - `const std::vector<content_item> &sherbet::content_presets();` — 마켓 프리셋 세그먼트가 순회.

- [ ] **Step 1: 헤더 (`sherbet_ui.hpp`)**

`apply_content` 선언 아래에 추가:
```cpp
	const std::vector<content_item> &content_presets(); // 서버가 내려준 프리셋 목록(마켓 UI)
```
include 에 `#include "sherbet_content_json.hpp"`(content_item), `#include <vector>` 없으면 추가.

- [ ] **Step 2: 구현 (`sherbet_ui.cpp`)**

`#include "sherbet_content_json.hpp"` 추가. 정적 저장소 추가(`s_entitled` 옆):
```cpp
	static std::vector<content_item> s_content_presets; // 서버 프리셋 목록(마켓 UI용)
```
`apply_content` 본문 끝(활성 테마 폴백 앞 또는 뒤)에 추가:
```cpp
		s_content_presets = parse_content_items(body, "presets");
```
`content_presets` 접근자 추가:
```cpp
	const std::vector<content_item> &content_presets() { return s_content_presets; }
```

- [ ] **Step 3: 로컬 문법 확인(참고, 전체 CI)**

`apply_content` 로직 문법만 Task 1 파서 위에서:
Run:
```bash
clang++ -std=c++17 -Ideps/imgui -fsyntax-only -x c++ - <<'EOF'
#include "source/sherbet_content_json.hpp"
namespace sherbet {
  static std::vector<content_item> s_content_presets;
  void apply_presets(const std::string& body){ s_content_presets = parse_content_items(body, "presets"); }
  const std::vector<content_item>& content_presets(){ return s_content_presets; }
}
int main(){ return 0; }
EOF
```
Expected: 문법 통과.

- [ ] **Step 4: Commit**

```bash
git add source/sherbet_ui.hpp source/sherbet_ui.cpp
git commit -m "feat(sherbet): apply_content 가 서버 프리셋 목록 파싱·보관

parse_content_items(body,\"presets\") → s_content_presets, content_presets()
접근자로 마켓 UI 에 노출. CI 컴파일 검증."
```

---

## Task 5: 마켓 프리셋 세그먼트 교체 + 검색경로/reload 연동 (`runtime_gui.cpp`)

**Files:**
- Modify: `source/runtime_gui.cpp`

**Interfaces:**
- Consumes: `content_presets()`/`apply_content`(Task 4), `_sherbet_auth.take_files_changed()`(Task 3), `set_current_preset_path`/`reload_effects`/`_effect_search_paths`/`_texture_search_paths`/`_config_path`(runtime).
- Produces: 없음(최종 UI/연동).

- [ ] **Step 1: 프레임 루프 — 파일 변경 시 검색경로 등록 + reload**

`apply_content` 호출 줄(`runtime_gui.cpp:1585`) 다음에 추가:
```cpp
		if (_sherbet_auth.take_files_changed())
		{
			const std::filesystem::path fx_dir = _config_path.parent_path() / L"Sherbet-Fx";
			bool added = false;
			if (std::find(_effect_search_paths.begin(), _effect_search_paths.end(), fx_dir) == _effect_search_paths.end())
			{ _effect_search_paths.push_back(fx_dir); added = true; }
			if (std::find(_texture_search_paths.begin(), _texture_search_paths.end(), fx_dir) == _texture_search_paths.end())
			{ _texture_search_paths.push_back(fx_dir); added = true; }
			if (added) save_config();
			reload_effects();
		}
```
(`<algorithm>` 는 runtime_gui.cpp 에 이미 포함. 미포함 시 추가.)

- [ ] **Step 2: 마켓 프리셋 세그먼트(seg==1) 교체**

`draw_gui_market()` 의 `else`(seg==1) 블록 전체(`runtime_gui.cpp:3834~3916`, 내장 프리셋+PRE-+체험)를 아래로 교체:
```cpp
	else
	{
		// 프리셋 마켓 — 서버가 내려준 "내 전용 프리셋"(디스코드 역할 기반)
		ImGui::TextUnformatted("\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 '\xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0'\xEB\xA1\x9C \xEB\xB0\x9B\xEC\x95\x84\xEC\x9A\x94."); // "디스코드 로그인 후 '내 전용 불러오기'로 받아요."
		ImGui::Spacing();

		if (sherbet::auth::enabled() && _sherbet_auth.is_authed())
		{
			if (sherbet::pill_button(ICON_FK_DOWNLOAD "  \xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0", true)) // "내 전용 불러오기"
				_sherbet_auth.begin_fetch_content();
			ImGui::Spacing();
		}

		const std::vector<sherbet::content_item> &presets = sherbet::content_presets();
		if (presets.empty())
		{
			sherbet::begin_card("##preset_empty");
			ImGui::TextWrapped("\xEB\xB0\x9B\xEC\x9D\x80 \xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEA\xB6\x8C\xED\x95\x9C\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x9C\xBC\xEB\xA9\xB4 \xEC\x9C\x84 \xEB\xB2\x84\xED\x8A\xBC\xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\x84\xB8\xEC\x9A\x94."); // "받은 프리셋이 없어요. 권한이 있으면 위 버튼으로 불러오세요."
			sherbet::end_card();
		}
		for (std::size_t i = 0; i < presets.size(); ++i)
		{
			const sherbet::content_item &it = presets[i];
			ImGui::PushID((int)i);
			sherbet::begin_card("##preset_card");
			ImGui::Text("%s", it.display_name.c_str());
			const std::filesystem::path preset_path = _config_path.parent_path() / L"Sherbet-Presets" /
				std::filesystem::u8path(it.filename);
			const bool active = _current_preset_path == preset_path;
			if (active)
				ImGui::TextDisabled("%s", ICON_FK_OK " \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"); // "사용 중"
			else if (sherbet::pill_button(ICON_FK_OK "  \xEC\xA0\x81\xEC\x9A\xA9", false)) // "적용"
				set_current_preset_path(preset_path.u8string().c_str());
			sherbet::end_card();
			ImGui::PopID();
		}
	}
```

- [ ] **Step 3: 제거 확인 — PRE-/체험/내장 프리셋 잔여 참조**

교체 후 아래가 draw_gui_market 에서 사라졌는지 확인(다른 곳의 프리셋 config 로드/저장은 별도 Step 4):

Run: `grep -n "IDR_SHERBET_PRESET_PERSONAL\|_sherbet_preset_trial\|verify_preset\|PRE-XXXX\|materialize" source/runtime_gui.cpp`
Expected: draw_gui_market 내 매치 없음. (config 로드/저장부의 `_sherbet_preset_*` 는 Step 4 에서 처리.)

- [ ] **Step 4: config 프리셋 코드 로드/저장 제거**

`runtime_gui.cpp` 의 프리셋 언락 관련 config 로드(약 377~380행: `PresetUnlocked`/`verify_preset`/`_sherbet_preset_unlocked`/`_sherbet_preset_code`)와 저장(약 507행: `config.set("SHERBET", "PresetUnlocked", ...)`), 그리고 체험 원복 관련(`TrialRestore`, `_sherbet_preset_trial*`) 로드/저장 줄을 제거한다. 관련 멤버(`_sherbet_preset_unlocked`/`_sherbet_preset_code`/`_sherbet_preset_trial`/`_sherbet_preset_trial_restore`)의 **선언(runtime.hpp)과 그 밖의 사용처**(예: on_present 의 체험 카운트다운·원복 로직)도 함께 제거한다.

Run(제거 전 사용처 전수 파악): `grep -rn "_sherbet_preset_trial\|_sherbet_preset_unlocked\|_sherbet_preset_code\|PresetUnlocked\|TrialRestore\|IDR_SHERBET_PRESET_PERSONAL" source/`
- 나온 모든 지점을 제거하거나(선언·정의·사용) 서버 모델에 맞게 정리. 프리셋 적용은 `set_current_preset_path` 로 일원화(코드/체험 없음).
- `resource.h`/`.rc` 의 `IDR_SHERBET_PRESET_PERSONAL` 리소스 정의와 임베드도 제거(빌드에서 리소스 누락 오류 안 나게 정의만 남기거나 완전 제거 — 참조가 모두 사라졌으면 정의 제거 가능).

> 이 스텝은 파급이 크다(멤버·리소스·on_present). 구현자는 grep 결과 전 지점을 반드시 확인하고, 하나라도 남기면 CI 컴파일 실패로 드러난다.

- [ ] **Step 5: Commit**

```bash
git add source/runtime_gui.cpp source/runtime.hpp source/runtime.cpp resource.h source/resource.rc
git commit -m "feat(sherbet): 프리셋 마켓을 서버 배달 기반으로 교체 + fx 검색경로/reload 연동

내 전용 불러오기 시 프리셋/이펙트 파일 다운로드→Sherbet-Fx 검색경로 등록+
reload_effects, 마켓은 content_presets() 카드로 적용. PRE- 코드/10초 체험/
내장 IDR_SHERBET_PRESET_PERSONAL 완전 제거. 프리셋 적용은 set_current_preset_path
일원화. CI 컴파일 검증(auth OFF/ON)."
```
> `git add` 대상은 실제 수정된 파일만(grep 결과에 따라 resource.rc/resource.h 포함 여부 조정).

---

## Task 6: CI 통합 빌드 검증 (auth OFF/ON)

**Files:** 없음.

- [ ] **Step 1: 푸시 + CI**

Run: `git push origin sherbet-base`
그리고 GitHub Actions 빌드가 auth OFF(push)·ON(`workflow_dispatch -f online_auth=true`) 둘 다 도는지 확인.

- [ ] **Step 2: 결과 확인**

Run: `gh run list --branch sherbet-base --limit 3` → `gh run watch <id> --exit-status`.
Expected: 둘 다 GREEN. 실패 시 로그의 컴파일 오류(특히 Task 5 잔여 `_sherbet_preset_*`/리소스 참조)를 수정→재커밋→재푸시.

- [ ] **Step 3: 흔한 오류 체크리스트**
- `_sherbet_preset_*` 잔여 참조(on_present/선언) → 전수 제거.
- `IDR_SHERBET_PRESET_PERSONAL` 리소스 참조 잔존 → resource.rc/h 정리.
- `content_item`/`content_presets` 미선언 → include 확인.
- `split_top_level_objects` 미정의 → Task 1 헤더.
- `ICON_FK_DOWNLOAD`(forkawesome.h:114 존재), `std::find`(`<algorithm>`).

---

## Self-Review (작성자 체크)

- **스펙 커버리지:** §14.5(다운로드/배치/로드·순서·경로안전)=Task 1·2·3·5, §14.6(마켓 교체·PRE-/체험/내장 제거·자동적용 안 함)=Task 4·5, §14.8 분해 준수. ✅
- **타입 일관성:** `content_item`/`parse_content_items`/`content_download_targets`(Task 1) → Task 3 워커·Task 4 apply_content 소비 일치. `fetch_file`(Task 2) → Task 3 소비. `take_files_changed`(Task 3) → Task 5 소비. `content_presets()`(Task 4) → Task 5 소비. ✅
- **스레드 규칙:** 다운로드=워커, 검색경로/reload/UI=렌더 스레드. `take_content`/`take_files_changed` 로 인계. 순서: 파일 다운로드 완료 후에만 `_content_files_changed` → reload 시 파일이 디스크에 존재. ✅
- **플레이스홀더:** Task 5 Step 4 만 "grep 결과 전 지점 제거"로 구현자 판단 필요(리소스/멤버 파급이 코드베이스 상태 의존 — 전수 grep 명령으로 범위 확정). 그 외 실제 코드. ✅
- **글로벌 제약:** 커밋 무해·한국어, Mac 컴파일 한계·저작권 헤더·PRE- 제거 반영. ✅
- **위험:** Task 5 Step 4(프리셋 코드/체험/리소스 제거)가 파급 최대 — 구현자에게 전수 grep 강제. CI 가 최종 안전망.
