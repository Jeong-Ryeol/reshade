# SHERBET Plan 2 — Fonts & Skin Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development. Steps use `- [ ]`.

**Goal:** Jua/Gaegu 폰트를 DLL에 임베드하고, `sherbet_ui` 스킨 엔진(테마 스타일 적용 + 애니메이션 그라디언트 배경 + 파티클 + 글로우)을 만들어 오버레이가 테마 색으로 물들게 한다.

**Architecture:** 폰트 TTF 2개를 RCDATA 리소스로 임베드(`resource.h`/`resource.rc`), `build_font_atlas()`에서 본문 폰트를 Jua로 로드(한글 글리프 범위 포함) + Gaegu를 제목용 보조 폰트로 로드. 신규 `source/sherbet_ui.{hpp,cpp}`에 `apply_style()`, `draw_background()`, `draw_particles()`, `draw_glow()`를 구현하고 `draw_gui()`에서 매 프레임 호출.

**Tech Stack:** C++17, ImGui(ReShade 번들), ImDrawList, ForkAwesome.

## Global Constraints

- 외부 라이브러리 추가 금지. 폰트는 OFL(Jua/Gaegu) — 이미 `res/fonts/`에 배치됨.
- 이모지 렌더 금지. 아이콘 ForkAwesome, 도형 ImDrawList.
- 핵심 기능 로직(이펙트/프리셋/렌더러) 수정 금지.
- 커밋 메시지에 Claude/AI 저작권·공동저자 문구 금지.
- 검증 = CI green(msbuild 32/64). Mac은 컴파일 불가 → 플랜 마지막에 한 번 push + CI 확인.
- 신규 파일 헤더: `/* Copyright (C) 2026 정렬 (Jeong-Ryeol) / SPDX-License-Identifier: BSD-3-Clause */`.
- 폰트 파일: `res/fonts/Jua-Regular.ttf`(본문), `res/fonts/Gaegu-Bold.ttf`(제목). 이미 존재.
- 테마 값은 `sherbet::default_theme()` (Plan 1, `sherbet_theme.hpp`)에서 온다. 색은 `ImU32`(IM_COL32 패킹).

---

### Task 1: 폰트 리소스 임베드

**Files:**
- Modify: `res/resource.h` (ID 정의)
- Modify: `res/resource.rc` (RCDATA 엔트리)
- Modify: `ReShade.vcxproj` (리소스가 이미 포함되면 불필요할 수 있음 — 확인)

**Interfaces:**
- Produces: `IDR_FONT_SHERBET_BODY`(Jua), `IDR_FONT_SHERBET_TITLE`(Gaegu) 리소스 ID — `resources::load_data_resource(id)`로 로드 가능

- [ ] **Step 1: resource.h에 ID 추가**

`res/resource.h`에서 라이선스 ID 블록 아래(예: `IDR_LICENSE_S_JXL 711` 다음)에 추가:
```c
#define IDR_FONT_SHERBET_BODY           720
#define IDR_FONT_SHERBET_TITLE          721
```
그리고 `_APS_NEXT_RESOURCE_VALUE`를 `115` → `722`로 갱신(APS 자동값 충돌 방지).

- [ ] **Step 2: resource.rc에 RCDATA 추가**

`res/resource.rc`의 RCDATA 블록(예: `IDB_MAIN_ICON RCDATA "main_icon_small.png"` 근처, `#if !defined(RESHADE_FXC)` 안)에 추가:
```rc
IDR_FONT_SHERBET_BODY   RCDATA                  "fonts\\Jua-Regular.ttf"
IDR_FONT_SHERBET_TITLE  RCDATA                  "fonts\\Gaegu-Bold.ttf"
```

- [ ] **Step 3: 리소스 컴파일 대상 확인**

Run: `cd ~/reshade && grep -n "resource.rc\|ResourceCompile" ReShade.vcxproj | head`
Expected: `resource.rc`가 이미 `<ResourceCompile>`로 등록됨 → 추가 작업 불필요. (없으면 등록.)

- [ ] **Step 4: git add TTF + 리소스 파일, 확인**

Run: `cd ~/reshade && git add res/fonts/Jua-Regular.ttf res/fonts/Gaegu-Bold.ttf res/resource.h res/resource.rc && git status --short`
Expected: 4개(또는 그 이상) 파일 staged. TTF는 바이너리로 추가됨.

- [ ] **Step 5: Commit (push 안 함)**

```bash
cd ~/reshade
git commit -m "feat(sherbet): Jua/Gaegu 폰트 RCDATA 임베드"
```

---

### Task 2: 스킨 엔진 골격 (`sherbet_ui.hpp` / `sherbet_ui.cpp`) — apply_style

**Files:**
- Create: `source/sherbet_ui.hpp`
- Create: `source/sherbet_ui.cpp`
- Modify: `ReShade.vcxproj`, `ReShade.vcxproj.filters`, `CMakeLists.txt` (등록, `core\runtime` 필터)

**Interfaces:**
- Consumes: `sherbet::theme`, `sherbet::default_theme()` (Plan 1)
- Produces:
  - `void sherbet::apply_style(ImGuiStyle &style, const theme &t)` — 모든 `ImGuiCol_*`를 테마 색으로 채우고 rounding/padding/spacing을 Sherbet 값으로 설정
  - 헬퍼 `ImVec4 sherbet::to_vec4(ImU32)` (내부 사용 가능)

- [ ] **Step 1: 헤더 작성**

Create `source/sherbet_ui.hpp`:
```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <imgui.h>
#include "sherbet_theme.hpp"

namespace sherbet
{
	// 테마 색/라운드/간격을 ImGui 스타일에 적용 (매 프레임 또는 테마 변경 시 호출)
	void apply_style(ImGuiStyle &style, const theme &t);
}
```

- [ ] **Step 2: apply_style 구현**

Create `source/sherbet_ui.cpp`:
```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_ui.hpp"

namespace sherbet
{
	static ImVec4 to_vec4(ImU32 c)
	{
		return ImGui::ColorConvertU32ToFloat4(c);
	}

	void apply_style(ImGuiStyle &style, const theme &t)
	{
		// 라운드/간격 — 카드형 부드러운 룩
		style.WindowRounding = 12.0f;
		style.ChildRounding = 14.0f;
		style.FrameRounding = 10.0f;
		style.PopupRounding = 12.0f;
		style.GrabRounding = 10.0f;
		style.TabRounding = 10.0f;
		style.ScrollbarRounding = 10.0f;
		style.FrameBorderSize = 1.0f;
		style.WindowBorderSize = 1.0f;
		style.WindowPadding = ImVec2(14, 14);
		style.FramePadding = ImVec2(12, 8);
		style.ItemSpacing = ImVec2(10, 9);
		style.ItemInnerSpacing = ImVec2(8, 6);
		style.ScrollbarSize = 12.0f;
		style.GrabMinSize = 12.0f;

		ImVec4 *c = style.Colors;
		const ImVec4 text = to_vec4(t.text);
		const ImVec4 dim = to_vec4(t.text_dim);
		const ImVec4 accent = to_vec4(t.accent);
		const ImVec4 panel = to_vec4(t.panel);
		const ImVec4 panel_alt = to_vec4(t.panel_alt);
		const ImVec4 chip = to_vec4(t.chip);
		const ImVec4 border = to_vec4(t.border);
		const ImVec4 bg1 = to_vec4(t.bg1);

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = dim;
		c[ImGuiCol_WindowBg] = bg1;
		c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_PopupBg] = to_vec4(t.bg0);
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = panel;
		c[ImGuiCol_FrameBgHovered] = panel_alt;
		c[ImGuiCol_FrameBgActive] = panel_alt;
		c[ImGuiCol_TitleBg] = to_vec4(t.bg0);
		c[ImGuiCol_TitleBgActive] = to_vec4(t.bg0);
		c[ImGuiCol_TitleBgCollapsed] = to_vec4(t.bg0);
		c[ImGuiCol_MenuBarBg] = to_vec4(t.bg0);
		c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ScrollbarGrab] = chip;
		c[ImGuiCol_ScrollbarGrabHovered] = accent;
		c[ImGuiCol_ScrollbarGrabActive] = accent;
		c[ImGuiCol_CheckMark] = accent;
		c[ImGuiCol_SliderGrab] = accent;
		c[ImGuiCol_SliderGrabActive] = to_vec4(t.accent2);
		c[ImGuiCol_Button] = chip;
		c[ImGuiCol_ButtonHovered] = panel_alt;
		c[ImGuiCol_ButtonActive] = accent;
		c[ImGuiCol_Header] = panel;
		c[ImGuiCol_HeaderHovered] = panel_alt;
		c[ImGuiCol_HeaderActive] = panel_alt;
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_SeparatorHovered] = accent;
		c[ImGuiCol_SeparatorActive] = accent;
		c[ImGuiCol_ResizeGrip] = chip;
		c[ImGuiCol_ResizeGripHovered] = accent;
		c[ImGuiCol_ResizeGripActive] = accent;
		c[ImGuiCol_Tab] = chip;
		c[ImGuiCol_TabHovered] = accent;
		c[ImGuiCol_TabActive] = panel_alt;
		c[ImGuiCol_TabUnfocused] = chip;
		c[ImGuiCol_TabUnfocusedActive] = panel;
		c[ImGuiCol_PlotLines] = accent;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_NavHighlight] = accent;
		c[ImGuiCol_DragDropTarget] = to_vec4(t.accent2);
	}
}
```
주의: 위 `ImGuiCol_*` 상수명은 ReShade 번들 ImGui 버전에 모두 존재해야 한다. 만약 `ImGuiCol_TabActive`/`TabUnfocused` 등이 최신 ImGui에서 이름이 바뀌었다면(예: `ImGuiCol_TabSelected`), `deps/imgui/imgui.h`의 `enum ImGuiCol_`를 grep해 실제 존재하는 이름으로 맞춘다. 존재하지 않는 상수는 컴파일 에러가 되므로 반드시 확인.

- [ ] **Step 3: 실제 ImGuiCol enum 이름 검증**

Run: `cd ~/reshade && grep -n "ImGuiCol_Tab\|ImGuiCol_NavHighlight\|ImGuiCol_CheckMark" deps/imgui/imgui.h | head`
실제 존재하는 이름만 사용하도록 Step 2 코드를 수정. (버전에 따라 `ImGuiCol_TabActive`→`ImGuiCol_TabSelected`, `ImGuiCol_TabUnfocused`→`ImGuiCol_TabDimmed` 등으로 다를 수 있음. 없는 건 제거하거나 새 이름으로 교체.)

- [ ] **Step 4: 빌드 등록 + 파싱 확인**

vcxproj(`<ClCompile Include="source\sherbet_ui.cpp" />`, `<ClInclude Include="source\sherbet_ui.hpp" />`), filters(`core\runtime`), CMakeLists에 추가(알파벳 순). 그다음:
```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -Ideps/imgui -Isource source/sherbet_ui.cpp
python3 -c "import xml.dom.minidom as m; m.parse('ReShade.vcxproj'); m.parse('ReShade.vcxproj.filters'); print('XML OK')"
```
Expected: clang exit 0, XML OK.

- [ ] **Step 5: Commit (push 안 함)**

```bash
cd ~/reshade
git add source/sherbet_ui.hpp source/sherbet_ui.cpp ReShade.vcxproj ReShade.vcxproj.filters CMakeLists.txt
git commit -m "feat(sherbet): 스킨 엔진 apply_style 추가"
```

---

### Task 3: 본문/제목 폰트 로드 (`build_font_atlas`)

**Files:**
- Modify: `source/runtime_gui.cpp` (`build_font_atlas()`, ~139-281)
- Modify: `source/runtime.hpp` (제목 폰트 포인터 멤버 추가)

**Interfaces:**
- Consumes: `IDR_FONT_SHERBET_BODY`, `IDR_FONT_SHERBET_TITLE` (Task 1)
- Produces: `ImFont *_sherbet_title_font` 멤버(제목용). 본문 폰트는 기본 폰트로 Jua 사용.

**설명(구현자 필독):** `build_font_atlas()`는 언어별 시스템 폰트를 `_default_font_path`로 고르고, 사용자 `_font_path`가 있으면 그걸, 없으면 default를 main 폰트로 `add_font_from_file`한 뒤 ForkAwesome를 merge한다. 목표: **사용자가 폰트를 지정하지 않았을 때(`_font_path`가 비었을 때) 시스템 폰트 대신 임베드된 Jua를 main 폰트로 쓴다.** 한글이 깨지지 않도록 한글 글리프 범위를 지정한다. 그리고 별도로 Gaegu를 제목 폰트로 추가해 `_sherbet_title_font`에 저장한다. **기존 언어/CJK 로직은 사용자가 폰트를 지정한 경우를 위해 보존**한다(회귀 방지).

- [ ] **Step 1: runtime.hpp에 멤버 추가**

`source/runtime.hpp`의 `ImGuiContext *_imgui_context = nullptr;`(약 420행) 근처에 추가:
```cpp
		ImFont *_sherbet_title_font = nullptr;
```

- [ ] **Step 2: build_font_atlas의 main 폰트 로드 지점 수정**

`source/runtime_gui.cpp`에서 main 폰트를 추가하는 블록을 찾는다:
```cpp
	// Add main font
	resolved_font_path = _font_path.empty() ? _default_font_path : _font_path;
	{
		if (!add_font_from_file(resolved_font_path, &cfg, ec))
			log::message(log::level::error, "Failed to load font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());

		// Merge icons into main font
		cfg.MergeMode = true;
		cfg.PixelSnapH = true;

		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}
```
이 블록을 다음으로 교체(사용자 지정 폰트가 없으면 임베드 Jua 사용, 한글 범위 지정):
```cpp
	// Add main font — SHERBET: 사용자 지정 폰트가 없으면 임베드 Jua 사용(한글 포함)
	if (_font_path.empty())
	{
		ImFontConfig jua_cfg = cfg;
		const resources::data_resource jua = resources::load_data_resource(IDR_FONT_SHERBET_BODY);
		// 아틀라스는 데이터를 소유하지 않으므로 복사본을 넘긴다
		void *jua_data = IM_ALLOC(jua.data_size);
		memcpy(jua_data, jua.data, jua.data_size);
		atlas->AddFontFromMemoryTTF(jua_data, static_cast<int>(jua.data_size), 0.0f, &jua_cfg, atlas->GetGlyphRangesKorean());

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}
	else
	{
		resolved_font_path = _font_path;
		if (!add_font_from_file(resolved_font_path, &cfg, ec))
			log::message(log::level::error, "Failed to load font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}
```
주의: `resources::data_resource`의 필드명이 `data`/`data_size`가 맞는지 `source/dll_resources.hpp`에서 확인(다르면 맞춘다). `AddFontFromMemoryTTF`의 시그니처(마지막 glyph_ranges 인자)도 번들 ImGui에서 확인.

- [ ] **Step 3: 제목 폰트(Gaegu) 추가**

같은 함수에서 ForkAwesome merge 이후, `_rebuild_font_atlas = false;` 직전에 추가:
```cpp
	// SHERBET 제목 폰트(Gaegu) — merge 아님, 별도 폰트
	{
		ImFontConfig title_cfg;
		title_cfg.MergeMode = false;
		const resources::data_resource gaegu = resources::load_data_resource(IDR_FONT_SHERBET_TITLE);
		void *gaegu_data = IM_ALLOC(gaegu.data_size);
		memcpy(gaegu_data, gaegu.data, gaegu.data_size);
		_sherbet_title_font = atlas->AddFontFromMemoryTTF(gaegu_data, static_cast<int>(gaegu.data_size), _font_size * 1.4f, &title_cfg, atlas->GetGlyphRangesKorean());
	}
```
(`_font_size`가 이 함수 스코프에서 접근 가능한지 확인 — `_imgui_context->Style.FontSizeBase = _font_size;`가 함수 초반에 있으므로 멤버로 접근 가능.)

- [ ] **Step 4: resource include 확인**

`source/runtime_gui.cpp` 상단에 `#include "dll_resources.hpp"`와 `#include "resource.h"`가 이미 있는지 확인(스플래시/라이선스에서 `resources::load_data_resource`, `IDR_*`를 쓰므로 이미 있을 가능성 높음). 없으면 추가.

- [ ] **Step 5: Commit (push 안 함)**

```bash
cd ~/reshade
git add source/runtime_gui.cpp source/runtime.hpp
git commit -m "feat(sherbet): Jua 본문 + Gaegu 제목 폰트 로드"
```

---

### Task 4: 배경/파티클/글로우 + draw_gui 연결

**Files:**
- Modify: `source/sherbet_ui.hpp` / `source/sherbet_ui.cpp` (헬퍼 추가)
- Modify: `source/runtime_gui.cpp` (`draw_gui()` — 스타일 적용 + 배경/파티클 렌더)

**Interfaces:**
- Consumes: `sherbet::apply_style` (Task 2), `sherbet::default_theme()`
- Produces:
  - `void sherbet::draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time)` — 그라디언트 + 부드럽게 움직이는 오로라 블롭
  - `void sherbet::draw_particles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time)` — 테마 파티clesystem(스파클/하트/잎)을 위로 떠오르게
  - `void sherbet::draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color)` — 사각형 뒤 부드러운 글로우

- [ ] **Step 1: 헤더에 선언 추가**

`source/sherbet_ui.hpp`의 namespace 안에 추가:
```cpp
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time);
	void draw_particles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time);
	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color);
```

- [ ] **Step 2: 구현 추가**

`source/sherbet_ui.cpp`에 추가(파일 상단에 `#include <cmath>` 추가):
```cpp
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time)
	{
		// 대각선 그라디언트 (bg0 -> bg1 -> bg2)
		dl->AddRectFilledMultiColor(min, max, t.bg0, t.bg1, t.bg2, t.bg1);

		// 오로라 블롭 2개 — 부드럽게 드리프트하는 반투명 원(글로우색)
		const float w = max.x - min.x, h = max.y - min.y;
		ImU32 g = t.glow;
		auto blob = [&](float px, float py, float r) {
			dl->AddCircleFilled(ImVec2(min.x + px, min.y + py), r, g, 48);
		};
		const float t1 = time * 0.12f;
		blob(w * (0.20f + 0.05f * sinf(t1)), h * (0.15f + 0.06f * cosf(t1)), h * 0.32f);
		blob(w * (0.82f + 0.05f * cosf(t1 * 0.8f)), h * (0.85f + 0.05f * sinf(t1 * 0.8f)), h * 0.30f);
	}

	static void draw_shape(ImDrawList *dl, particle shape, ImVec2 p, float s, ImU32 col)
	{
		switch (shape)
		{
		case particle::spark: {
			dl->AddTriangleFilled(ImVec2(p.x, p.y - s), ImVec2(p.x + s * 0.28f, p.y - s * 0.28f), ImVec2(p.x + s, p.y), col);
			dl->AddTriangleFilled(ImVec2(p.x + s, p.y), ImVec2(p.x + s * 0.28f, p.y + s * 0.28f), ImVec2(p.x, p.y + s), col);
			dl->AddTriangleFilled(ImVec2(p.x, p.y + s), ImVec2(p.x - s * 0.28f, p.y + s * 0.28f), ImVec2(p.x - s, p.y), col);
			dl->AddTriangleFilled(ImVec2(p.x - s, p.y), ImVec2(p.x - s * 0.28f, p.y - s * 0.28f), ImVec2(p.x, p.y - s), col);
			break; }
		case particle::heart: {
			dl->AddCircleFilled(ImVec2(p.x - s * 0.35f, p.y - s * 0.25f), s * 0.42f, col, 12);
			dl->AddCircleFilled(ImVec2(p.x + s * 0.35f, p.y - s * 0.25f), s * 0.42f, col, 12);
			dl->AddTriangleFilled(ImVec2(p.x - s * 0.72f, p.y), ImVec2(p.x + s * 0.72f, p.y), ImVec2(p.x, p.y + s * 0.85f), col);
			break; }
		case particle::leaf:
		case particle::petal:
			dl->AddCircleFilled(p, s * 0.7f, col, 12);
			break;
		}
	}

	void draw_particles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time)
	{
		const float w = max.x - min.x, h = max.y - min.y;
		const int count = 10;
		for (int i = 0; i < count; ++i)
		{
			// 결정적 의사난수 배치(프레임마다 동일 시드)
			const float seed = i * 127.1f;
			const float fx = fmodf(sinf(seed) * 0.5f + 0.5f, 1.0f);
			const float speed = 0.05f + 0.03f * fmodf(cosf(seed) * 0.5f + 0.5f, 1.0f);
			const float phase = fmodf(time * speed + fmodf(seed, 1.0f), 1.0f); // 0..1 상승
			const float x = min.x + fx * w;
			const float y = max.y - phase * h;
			const float alpha = (phase < 0.15f ? phase / 0.15f : (phase > 0.8f ? (1.0f - phase) / 0.2f : 1.0f)) * 0.7f;
			ImU32 col = (t.accent & 0x00FFFFFF) | ((ImU32)(alpha * 255) << 24);
			draw_shape(dl, t.particle_shape, ImVec2(x, y), 6.0f, col);
		}
	}

	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color)
	{
		// 사각형 주변 3겹 확산 글로우
		for (int i = 3; i >= 1; --i)
		{
			const float e = i * 4.0f;
			ImU32 a = (glow_color & 0x00FFFFFF) | ((ImU32)(30 / i) << 24);
			dl->AddRect(ImVec2(min.x - e, min.y - e), ImVec2(max.x + e, max.y + e), a, 16.0f, 0, 3.0f);
		}
	}
```
주의: `AddRectFilledMultiColor`, `AddTriangleFilled`, `AddCircleFilled`, `AddRect` 시그니처를 번들 ImGui에서 확인. `AddRectFilledMultiColor` 인자 순서(상좌·상우·하우·하좌)도 버전마다 다를 수 있으니 `deps/imgui/imgui.h`에서 확인.

- [ ] **Step 3: draw_gui에서 스타일 적용 + 배경 렌더 연결**

`source/runtime_gui.cpp` `draw_gui()` 안, 오버레이가 그려지기 시작하는 지점(스플래시/메시지 이후, `Viewport` Begin 근처)에서:
1. 프레임 시간 확보: ReShade에 `_last_present_time` 등 시간원이 있으나, 간단히 `static float s_time; s_time += _imgui_context->IO.DeltaTime;` 사용.
2. 오버레이가 열려 있을 때(`_show_overlay`) 스타일 적용:
```cpp
	if (_show_overlay)
		sherbet::apply_style(_imgui_context->Style, sherbet::default_theme());
```
3. `Viewport` 창의 배경으로 그라디언트+파티클을 그린다. `ImGui::Begin("Viewport", ...)` 직후 그 창의 draw list에:
```cpp
		ImDrawList *bg = ImGui::GetBackgroundDrawList(viewport);
		const ImVec2 vmin = viewport->Pos;
		const ImVec2 vmax = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
		static float s_sherbet_time = 0.0f; s_sherbet_time += _imgui_context->IO.DeltaTime;
		if (_show_overlay)
		{
			sherbet::draw_background(bg, vmin, vmax, sherbet::default_theme(), s_sherbet_time);
			sherbet::draw_particles(bg, vmin, vmax, sherbet::default_theme(), s_sherbet_time);
		}
```
주의: `viewport` 변수와 `GetBackgroundDrawList(viewport)` 사용 가능 여부를 해당 스코프에서 확인. 불가하면 `ImGui::GetBackgroundDrawList()`(인자 없음) 사용. 배경이 오버레이 창들 뒤에 그려지도록 BackgroundDrawList를 쓴다.

- [ ] **Step 4: sherbet_ui include 추가**

`source/runtime_gui.cpp` 상단에 `#include "sherbet_ui.hpp"` 추가.

- [ ] **Step 5: 파싱 확인 + Commit (push 안 함)**

```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -Ideps/imgui -Isource source/sherbet_ui.cpp
git add source/sherbet_ui.hpp source/sherbet_ui.cpp source/runtime_gui.cpp
git commit -m "feat(sherbet): 애니메이션 배경·파티클·글로우 + draw_gui 연결"
```

---

### Task 5: 플랜 CI 검증 (컨트롤러 수행)

- [ ] **Step 1: push + CI watch**

```bash
cd ~/reshade && git push
gh run list -R Jeong-Ryeol/reshade --branch sherbet-base --limit 1
gh run watch <run-id> -R Jeong-Ryeol/reshade --exit-status
```
Expected: 32/64비트 build green. 실패 시 로그의 첫 에러(대개 없는 ImGuiCol 상수명 / data_resource 필드명 / AddFont 시그니처)를 픽스 서브에이전트로 수정 후 재검증.

---

## Self-Review 메모

- **커버리지:** 스펙 §5(폰트 Jua/Gaegu, 애니메이션 배경/파티클/글로우, 이모지 금지), §7의 스타일 기반. 위젯(토글/슬라이더/카드)·레일·탭은 Plan 3~4.
- **리스크:** (1) 폰트 아틀라스 수정이 최고 위험 — 한글 범위/메모리 소유권. 실패 시 오버레이 폰트 깨짐 → CI는 통과해도 시각 확인 필요. (2) ImGuiCol/ImDrawList API 이름·시그니처가 번들 ImGui 버전과 다를 수 있음 → 각 태스크에 grep 확인 스텝 포함. (3) 배경 draw list z-순서.
- **타입 일관성:** `apply_style`/`draw_background`/`draw_particles`/`draw_glow` 시그니처가 hpp/cpp/호출부에서 일치. `_sherbet_title_font`는 Plan 3(제목 렌더)에서 consume.
- **시각 확인(선택):** CI green 후 아티팩트로 오버레이 색/배경/파티클/한글 폰트 확인.
