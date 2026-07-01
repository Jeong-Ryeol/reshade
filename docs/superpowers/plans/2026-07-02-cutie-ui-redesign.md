# 큐티 커스텀 ReShade UI 전면 재작성 — 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ReShade 오버레이 UI를 파스텔+반짝임 "귀엽고 화려한" 감성으로 전면 재스킨하고, 테마별 git 브랜치로 분리 배포한다.

**Architecture:** `cutie_theme`(테마 데이터) + `cutie_ui`(공통 렌더 헬퍼) 두 모듈을 신설하고, `runtime_gui.cpp`의 기존 그리기 흐름에 매 프레임 스타일 적용·애니메이션 배경·파티클·글로우를 주입한다. 기능 위젯 로직은 건드리지 않고 스타일만 입힌다. 활성 테마는 `cutie_theme.cpp` 한 파일에만 정의되어, 각 `theme/*` 브랜치가 이 파일만 교체한다.

**Tech Stack:** C++17, Dear ImGui (ReShade 번들, 1.92+ 신 폰트 API), Visual Studio 2022 MSBuild, Google Fonts(OFL: Jua, Gaegu).

## Global Constraints

- 기준 브랜치: `cutie-base` (upstream/main `f191dc03`에서 생성됨). 공통 작업은 전부 여기서.
- ⚠️ **빌드/실행 검증은 Windows에서만 가능** (Mac은 코드 작성 전용). 각 태스크의 "테스트"는 **Windows VS2022 빌드 성공 + 오버레이 스크린샷 육안 확인**이다. 자동화 단위 테스트는 이 도메인에 없다.
- 빌드 방법: `git clone --recurse-submodules` → 브랜치 checkout → `ReShade.sln` 열기 → `64-bit` 타깃 → 빌드(`Ctrl+Shift+B`) → 산출물 `bin\x64\Release\`.
- 커밋 메시지에 Claude/AI 서명·저작권 문구를 **넣지 않는다** (사용자 전역 규칙).
- 기존 기능 위젯(이펙트/테크닉/코드/변수 에디터)의 **로직은 수정 금지** — 스타일/색/간격만.
- 신규 `.cpp`는 반드시 `ReShade.vcxproj`와 `ReShade.vcxproj.filters`에 등록해야 빌드에 포함된다.
- ImGui 색은 매 프레임 `ImGui::NewFrame()` 이후·위젯 그리기 이전에 설정해야 반영된다(애니메이션 색 때문).
- 한글 렌더링 필수 → 폰트에 한글 글리프 범위 포함.

---

### Task 1: 테마 데이터 모델 (`cutie_theme`) + 빌드 등록

**Files:**
- Create: `source/cutie_theme.hpp`
- Create: `source/cutie_theme.cpp`
- Modify: `ReShade.vcxproj` (`<ClCompile>` 목록에 추가, `source\addon.cpp` 근처 @538)
- Modify: `ReShade.vcxproj.filters`
- Test: Windows 빌드 성공(기능 변화 없음)

**Interfaces:**
- Produces:
  - `struct reshade::cutie::CutieTheme { ... }` (아래 필드)
  - `extern const reshade::cutie::CutieTheme g_cutie_theme;` — 전역 활성 테마
  - `ImU32`/`ImVec4` 색은 ImGui 타입 사용.

- [ ] **Step 1: `cutie_theme.hpp` 작성**

```cpp
#pragma once

#include <imgui.h>

namespace reshade::cutie
{
	// 하나의 테마를 완전히 기술하는 구조체. theme/* 브랜치는 g_cutie_theme 값만 바꾼다.
	struct CutieTheme
	{
		const char *name;            // "Peach Pastel" 등

		// --- 팔레트 ---
		ImVec4 bg_stop_a;            // 배경 그라디언트 시작색
		ImVec4 bg_stop_b;            // 배경 그라디언트 끝색
		ImVec4 panel;                // 창/자식 배경
		ImVec4 panel_alt;            // 프레임(입력창/슬라이더) 배경
		ImVec4 text;                 // 본문 텍스트
		ImVec4 text_dim;             // 흐린 텍스트
		ImVec4 border;               // 테두리

		// --- 강조 ---
		ImVec4 accent;               // 버튼/헤더/탭 기본
		ImVec4 accent_hover;
		ImVec4 accent_active;
		ImVec4 glow;                 // 글로우 색

		// --- 파티클 ---
		const char *particle_glyph;  // "✨" 등 (UTF-8)
		ImVec4 particle_color;
		int    particle_count;       // 화면당 파티클 수
		float  particle_speed;       // 떠오르는 속도(px/s)

		// --- 애니메이션 ---
		bool   bg_animated;          // 배경 색 순환 여부
		float  bg_anim_speed;        // 색 순환 속도(deg/s), rainbow용
		float  glow_intensity;       // 0~1

		// --- 라운드/여백 배율 ---
		float  rounding;             // 위젯 라운드 반경
		ImVec2 frame_padding;
		ImVec2 item_spacing;

		// --- 개인화(About) ---
		const char *maker_name;      // "정렬"
		const char *recipient_name;  // "OOO" (브랜치별로 채움)
		const char *about_message;   // 추가 문구(선택), 없으면 nullptr
		const char *discord_handle;  // "lovecat._.holic"
	};

	// 현재 빌드의 활성 테마. 정의는 cutie_theme.cpp (브랜치별 유일 차이 지점).
	extern const CutieTheme g_cutie_theme;
}
```

- [ ] **Step 2: `cutie_theme.cpp` 작성 (base 기본 테마 = 복숭아 파스텔 기준값)**

```cpp
#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// cutie-base의 기본값. theme/* 브랜치는 이 리터럴만 교체한다.
	const CutieTheme g_cutie_theme = {
		/* name           */ "Cutie Base",
		/* bg_stop_a       */ ImVec4(1.00f, 0.85f, 0.90f, 1.0f), // 연분홍
		/* bg_stop_b       */ ImVec4(0.80f, 0.90f, 1.00f, 1.0f), // 연하늘
		/* panel           */ ImVec4(1.00f, 0.97f, 0.99f, 0.92f),
		/* panel_alt       */ ImVec4(1.00f, 0.92f, 0.96f, 1.0f),
		/* text            */ ImVec4(0.35f, 0.22f, 0.30f, 1.0f),
		/* text_dim        */ ImVec4(0.55f, 0.45f, 0.52f, 1.0f),
		/* border          */ ImVec4(1.00f, 0.75f, 0.85f, 0.6f),
		/* accent          */ ImVec4(1.00f, 0.65f, 0.80f, 0.85f),
		/* accent_hover    */ ImVec4(1.00f, 0.55f, 0.75f, 1.0f),
		/* accent_active   */ ImVec4(0.95f, 0.45f, 0.70f, 1.0f),
		/* glow            */ ImVec4(1.00f, 0.70f, 0.85f, 1.0f),
		/* particle_glyph  */ u8"✨",  // ✨
		/* particle_color  */ ImVec4(1.00f, 0.95f, 0.70f, 1.0f),
		/* particle_count  */ 24,
		/* particle_speed  */ 22.0f,
		/* bg_animated     */ true,
		/* bg_anim_speed   */ 12.0f,
		/* glow_intensity  */ 0.6f,
		/* rounding        */ 14.0f,
		/* frame_padding   */ ImVec2(12.0f, 8.0f),
		/* item_spacing    */ ImVec2(8.0f, 6.0f),
		/* maker_name      */ u8"정렬",       // 정렬
		/* recipient_name  */ "OOO",
		/* about_message   */ nullptr,
		/* discord_handle  */ "lovecat._.holic",
	};
}
```

- [ ] **Step 3: `ReShade.vcxproj`에 등록**

@538 근처 `<ItemGroup>`의 `<ClCompile>` 목록에 추가:
```xml
    <ClCompile Include="source\cutie_theme.cpp" />
    <ClCompile Include="source\cutie_ui.cpp" />
```
그리고 헤더 `<ItemGroup>`(`<ClInclude>` 목록)에 추가:
```xml
    <ClInclude Include="source\cutie_theme.hpp" />
    <ClInclude Include="source\cutie_ui.hpp" />
```
(cutie_ui.* 는 Task 2에서 생성하지만, 지금 함께 등록해 둔다.)

- [ ] **Step 4: `ReShade.vcxproj.filters`에 등록**

기존 `source\*.cpp` 항목 형식을 따라 동일 필터로 4개 파일 추가:
```xml
    <ClCompile Include="source\cutie_theme.cpp"><Filter>Source Files</Filter></ClCompile>
    <ClCompile Include="source\cutie_ui.cpp"><Filter>Source Files</Filter></ClCompile>
    <ClInclude Include="source\cutie_theme.hpp"><Filter>Header Files</Filter></ClInclude>
    <ClInclude Include="source\cutie_ui.hpp"><Filter>Header Files</Filter></ClInclude>
```
(실제 필터명은 파일 상단의 기존 항목에서 확인해 맞춘다.)

- [ ] **Step 5: 커밋**

```bash
git add source/cutie_theme.hpp source/cutie_theme.cpp ReShade.vcxproj ReShade.vcxproj.filters
git commit -m "Add cutie theme data model and register build files"
```

> **Windows 검증:** 빌드 성공(경고/에러 없음). 아직 화면 변화 없음. cutie_ui.cpp가 아직 없어 빌드가 깨지면 Task 2를 함께 진행 후 검증.

---

### Task 2: 스타일 적용 레이어 (`cutie_ui::apply_style`)

**Files:**
- Create: `source/cutie_ui.hpp`
- Create: `source/cutie_ui.cpp`
- Modify: `source/runtime_gui.cpp` (`draw_gui()` 내 `ImGui::NewFrame()` 직후)
- Test: Windows 빌드 + 오버레이 색/라운드가 테마대로 바뀜

**Interfaces:**
- Consumes: `reshade::cutie::g_cutie_theme` (Task 1)
- Produces:
  - `void reshade::cutie::apply_style(ImGuiStyle &style, const CutieTheme &t);`

- [ ] **Step 1: `cutie_ui.hpp` 작성**

```cpp
#pragma once

#include <imgui.h>
#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// ImGuiStyle 전체를 테마 색/라운드/여백으로 덮어쓴다. 매 프레임 호출 가능.
	void apply_style(ImGuiStyle &style, const CutieTheme &t);
}
```

- [ ] **Step 2: `cutie_ui.cpp` 작성 (apply_style 구현)**

```cpp
#include "cutie_ui.hpp"

namespace reshade::cutie
{
	void apply_style(ImGuiStyle &style, const CutieTheme &t)
	{
		style.WindowRounding    = t.rounding + 6.0f;
		style.ChildRounding     = t.rounding;
		style.FrameRounding     = t.rounding;
		style.PopupRounding     = t.rounding;
		style.ScrollbarRounding = t.rounding;
		style.GrabRounding      = t.rounding;
		style.TabRounding       = t.rounding;
		style.WindowBorderSize  = 1.0f;
		style.FramePadding      = t.frame_padding;
		style.ItemSpacing       = t.item_spacing;
		style.WindowPadding     = ImVec2(16.0f, 16.0f);

		ImVec4 *c = style.Colors;
		c[ImGuiCol_Text]                 = t.text;
		c[ImGuiCol_TextDisabled]         = t.text_dim;
		c[ImGuiCol_WindowBg]             = t.panel;
		c[ImGuiCol_ChildBg]              = ImVec4(t.panel.x, t.panel.y, t.panel.z, 0.0f);
		c[ImGuiCol_PopupBg]              = t.panel;
		c[ImGuiCol_Border]               = t.border;
		c[ImGuiCol_FrameBg]              = t.panel_alt;
		c[ImGuiCol_FrameBgHovered]       = t.accent_hover;
		c[ImGuiCol_FrameBgActive]        = t.accent_active;
		c[ImGuiCol_TitleBg]              = t.accent;
		c[ImGuiCol_TitleBgActive]        = t.accent_active;
		c[ImGuiCol_TitleBgCollapsed]     = t.accent;
		c[ImGuiCol_CheckMark]            = t.accent_active;
		c[ImGuiCol_SliderGrab]           = t.accent;
		c[ImGuiCol_SliderGrabActive]     = t.accent_active;
		c[ImGuiCol_Button]               = t.accent;
		c[ImGuiCol_ButtonHovered]        = t.accent_hover;
		c[ImGuiCol_ButtonActive]         = t.accent_active;
		c[ImGuiCol_Header]               = t.accent;
		c[ImGuiCol_HeaderHovered]        = t.accent_hover;
		c[ImGuiCol_HeaderActive]         = t.accent_active;
		c[ImGuiCol_Tab]                  = t.accent;
		c[ImGuiCol_TabHovered]           = t.accent_hover;
		c[ImGuiCol_TabSelected]          = t.accent_active;
		c[ImGuiCol_ScrollbarBg]          = ImVec4(t.panel_alt.x, t.panel_alt.y, t.panel_alt.z, 0.4f);
		c[ImGuiCol_ScrollbarGrab]        = t.accent;
		c[ImGuiCol_ScrollbarGrabHovered] = t.accent_hover;
		c[ImGuiCol_ScrollbarGrabActive]  = t.accent_active;
		c[ImGuiCol_Separator]            = t.border;
		c[ImGuiCol_SeparatorHovered]     = t.accent_hover;
		c[ImGuiCol_SeparatorActive]      = t.accent_active;
	}
}
```

- [ ] **Step 3: `draw_gui()`에서 매 프레임 호출**

`source/runtime_gui.cpp` 상단 include에 추가:
```cpp
#include "cutie_ui.hpp"
```
`draw_gui()` 내부에서 `ImGui::NewFrame();` 을 찾아(대략 @1040 부근, 스플래시 그리기 직전) **바로 다음 줄**에 삽입:
```cpp
	reshade::cutie::apply_style(_imgui_context->Style, reshade::cutie::g_cutie_theme);
```

- [ ] **Step 4: 커밋**

```bash
git add source/cutie_ui.hpp source/cutie_ui.cpp source/runtime_gui.cpp
git commit -m "Apply cutie theme style to overlay every frame"
```

> **Windows 검증:** 오버레이 전체가 파스텔 색 + 둥근 위젯으로 바뀜. 기능 클릭 정상 동작 확인.

---

### Task 3: 애니메이션 그라디언트 배경

**Files:**
- Modify: `source/cutie_ui.hpp` / `source/cutie_ui.cpp`
- Modify: `source/runtime_gui.cpp` (메인 "Viewport" 윈도우 @1366 / @1457 내부)
- Test: 오버레이 뒤에 부드럽게 흐르는 파스텔 그라디언트

**Interfaces:**
- Consumes: `g_cutie_theme`
- Produces:
  - `void reshade::cutie::draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);`
  - 헬퍼 `ImU32 reshade::cutie::hsv_shift(const ImVec4 &base, float hue_deg);` (rainbow용)

- [ ] **Step 1: 헤더에 선언 추가 (`cutie_ui.hpp` namespace 안)**

```cpp
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);
	ImU32 hsv_shift(const ImVec4 &base, float hue_deg);
```

- [ ] **Step 2: 구현 추가 (`cutie_ui.cpp`)**

```cpp
	ImU32 hsv_shift(const ImVec4 &base, float hue_deg)
	{
		float h, s, v;
		ImGui::ColorConvertRGBtoHSV(base.x, base.y, base.z, h, s, v);
		h = fmodf(h + hue_deg / 360.0f, 1.0f);
		float r, g, b;
		ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, base.w));
	}

	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec)
	{
		ImVec4 a = t.bg_stop_a, b = t.bg_stop_b;
		if (t.bg_animated)
		{
			const float hue = t.bg_anim_speed * time_sec;
			const ImU32 ca = hsv_shift(a, hue);
			const ImU32 cb = hsv_shift(b, hue + 40.0f);
			dl->AddRectFilledMultiColor(min, max, ca, cb, cb, ca);
		}
		else
		{
			const ImU32 ca = ImGui::ColorConvertFloat4ToU32(a);
			const ImU32 cb = ImGui::ColorConvertFloat4ToU32(b);
			dl->AddRectFilledMultiColor(min, max, ca, cb, cb, ca);
		}
	}
```

- [ ] **Step 3: 메인 오버레이 윈도우에서 호출**

`source/runtime_gui.cpp`에서 메인 "Viewport" 윈도우의 `ImGui::Begin("Viewport", ...)` (@1366 또는 @1457) 직후, 위젯 그리기 이전에 삽입:
```cpp
	{
		ImDrawList *const dl = ImGui::GetWindowDrawList();
		const ImVec2 wmin = ImGui::GetWindowPos();
		const ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowSize().x, wmin.y + ImGui::GetWindowSize().y);
		const float t_sec = static_cast<float>(ImGui::GetTime());
		reshade::cutie::draw_background(dl, wmin, wmax, reshade::cutie::g_cutie_theme, t_sec);
	}
```

- [ ] **Step 4: 커밋**

```bash
git add source/cutie_ui.hpp source/cutie_ui.cpp source/runtime_gui.cpp
git commit -m "Add animated gradient background to overlay"
```

> **Windows 검증:** 오버레이 뒤 그라디언트가 천천히 색 이동. rainbow 아닌 테마는 정적/은은.

---

### Task 4: 반짝이 파티클 레이어

**Files:**
- Modify: `source/cutie_ui.hpp` / `source/cutie_ui.cpp`
- Modify: `source/runtime_gui.cpp` (메인 "Viewport" 윈도우 내부, 배경 다음)
- Test: 테마색 파티클(✨/🌸)이 위로 떠오름

**Interfaces:**
- Produces:
  - `void reshade::cutie::draw_sparkles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);`

- [ ] **Step 1: 헤더 선언 추가**

```cpp
	void draw_sparkles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);
```

- [ ] **Step 2: 구현 추가 (`cutie_ui.cpp`) — 결정론적 의사난수로 파티클 배치**

```cpp
	// 시드 기반 해시(랜덤 대체, 프레임 간 안정)
	static float hash01(int i, int salt)
	{
		unsigned int x = static_cast<unsigned int>(i * 374761393 + salt * 668265263);
		x = (x ^ (x >> 13)) * 1274126177u;
		return static_cast<float>((x ^ (x >> 16)) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
	}

	void draw_sparkles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec)
	{
		const float w = max.x - min.x, h = max.y - min.y;
		if (w <= 0.0f || h <= 0.0f)
			return;

		for (int i = 0; i < t.particle_count; ++i)
		{
			const float px   = min.x + hash01(i, 1) * w;
			const float phase = hash01(i, 2);
			// 아래->위로 이동, wrap
			float y = h - fmodf((time_sec * t.particle_speed) * (0.6f + phase) + phase * h, h);
			const float py = min.y + y;
			// 반짝임(알파 진동)
			const float tw = 0.5f + 0.5f * sinf(time_sec * 3.0f + phase * 6.28318f);
			ImVec4 col = t.particle_color; col.w *= tw;
			dl->AddText(nullptr, 14.0f + hash01(i, 3) * 8.0f, ImVec2(px, py),
				ImGui::ColorConvertFloat4ToU32(col), t.particle_glyph);
		}
	}
```

- [ ] **Step 3: 메인 윈도우에서 배경 다음에 호출 (Task 3 블록 안에 추가)**

```cpp
		reshade::cutie::draw_sparkles(dl, wmin, wmax, reshade::cutie::g_cutie_theme, t_sec);
```

- [ ] **Step 4: 커밋**

```bash
git add source/cutie_ui.hpp source/cutie_ui.cpp source/runtime_gui.cpp
git commit -m "Add sparkle particle layer to overlay"
```

> **Windows 검증:** 파티클이 위로 떠오르며 반짝임. glyph가 □로 나오면 Task 6(폰트) 후 해결됨 — 이 태스크에선 위치/이동만 확인.

---

### Task 5: 액티브/호버 글로우

**Files:**
- Modify: `source/cutie_ui.hpp` / `source/cutie_ui.cpp`
- Modify: `source/runtime_gui.cpp` (네비/주요 버튼 주변 — Task 8 네비와 연계)
- Test: 포커스 요소 뒤 부드러운 빛번짐

**Interfaces:**
- Produces:
  - `void reshade::cutie::draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float pulse);`

- [ ] **Step 1: 헤더 선언 추가**

```cpp
	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float pulse);
```

- [ ] **Step 2: 구현 추가 (`cutie_ui.cpp`) — 다중 반투명 라운드렉트로 빛번짐 근사**

```cpp
	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float pulse)
	{
		const int layers = 5;
		for (int i = layers; i >= 1; --i)
		{
			const float spread = static_cast<float>(i) * 3.0f;
			ImVec4 col = t.glow;
			col.w = t.glow_intensity * (0.12f * pulse) / static_cast<float>(i);
			dl->AddRectFilled(
				ImVec2(min.x - spread, min.y - spread),
				ImVec2(max.x + spread, max.y + spread),
				ImGui::ColorConvertFloat4ToU32(col), t.rounding + spread);
		}
	}
```

- [ ] **Step 3: 활용 지점**

Task 8(네비 필 버튼)에서 선택된 탭 버튼의 `ImGui::GetItemRectMin/Max()` 로 rect를 구해 호출:
```cpp
	{
		const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 2.5f);
		reshade::cutie::draw_glow(ImGui::GetWindowDrawList(),
			ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			reshade::cutie::g_cutie_theme, pulse);
	}
```
(단독 검증을 위해 임시로 홈 헤더 타이틀 rect에 적용해 확인 후, Task 8에서 네비로 이전.)

- [ ] **Step 4: 커밋**

```bash
git add source/cutie_ui.hpp source/cutie_ui.cpp source/runtime_gui.cpp
git commit -m "Add soft glow helper for active elements"
```

> **Windows 검증:** 대상 요소 뒤 은은한 맥동 글로우.

---

### Task 6: 큐티 폰트 임베드 (Jua 본문 + Gaegu 제목)

**Files:**
- Create: `res/fonts/Jua-Regular.ttf`, `res/fonts/Gaegu-Bold.ttf` (OFL, 수동 다운로드)
- Create: `source/cutie_fonts.h` (binary_to_compressed_c 산출 헤더 2종)
- Modify: `source/runtime_gui.cpp` (`build_font_atlas()` @139, 폰트 멤버)
- Test: 큐티 폰트로 한글/영문 렌더

**Interfaces:**
- Produces:
  - `CUTIE_JUA_compressed_data_base85`, `CUTIE_GAEGU_compressed_data_base85` (const char*)
  - 제목 폰트 포인터 접근: `ImFont *reshade::runtime::_cutie_title_font` (또는 정적 저장)

- [ ] **Step 1: 폰트 파일 확보 (Windows/Mac 공통, 수동)**

Google Fonts에서 다운로드해 `res/fonts/`에 저장:
- Jua: https://fonts.google.com/specimen/Jua (`Jua-Regular.ttf`)
- Gaegu: https://fonts.google.com/specimen/Gaegu (`Gaegu-Bold.ttf`)

- [ ] **Step 2: 압축 헤더 생성 (ImGui 툴 사용)**

ImGui 저장소의 `misc/fonts/binary_to_compressed_c.cpp`를 컴파일해 실행:
```bash
# ReShade 번들 ImGui 경로 예: deps/imgui/misc/fonts/binary_to_compressed_c.cpp
c++ -O2 deps/imgui/misc/fonts/binary_to_compressed_c.cpp -o b2c
./b2c -base85 res/fonts/Jua-Regular.ttf CUTIE_JUA   >  source/cutie_fonts.h
./b2c -base85 res/fonts/Gaegu-Bold.ttf  CUTIE_GAEGU >> source/cutie_fonts.h
```
결과: `source/cutie_fonts.h`에 `static const char CUTIE_JUA_compressed_data_base85[] = "...";` 등이 생성됨.

- [ ] **Step 3: `build_font_atlas()`에서 큐티 폰트 로드**

`source/runtime_gui.cpp` 상단에 include:
```cpp
#include "cutie_fonts.h"
```
`build_font_atlas()`의 "Add main font" 블록(@258 부근)에서, 기본 폰트 로드 대신 Jua를 메인으로 추가하도록 교체. 한글 글리프는 ImGui 1.92 동적 로더가 처리하지만, 명시적 범위가 필요하면 `cfg.GlyphRanges = atlas->GetGlyphRangesKorean();` 지정.
메인 폰트 추가부를 다음으로 변경:
```cpp
	// Add main font (Cutie: Jua)
	{
		ImFontConfig jcfg = cfg;
		jcfg.MergeMode = false;
		atlas->AddFontFromMemoryCompressedBase85TTF(CUTIE_JUA_compressed_data_base85, 0.0f, &jcfg);

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}
```
제목 폰트를 별도로 추가하고 포인터 저장(멤버 `_cutie_title_font` 를 `runtime` 클래스에 추가하거나 파일 정적):
```cpp
	// Add title font (Cutie: Gaegu) — 큰 제목 전용
	{
		ImFontConfig tcfg;
		_cutie_title_font = atlas->AddFontFromMemoryCompressedBase85TTF(CUTIE_GAEGU_compressed_data_base85, 0.0f, &tcfg);
	}
```

- [ ] **Step 4: 제목 폰트 멤버 선언**

`_cutie_title_font`를 저장할 곳을 정한다. 최소 침습: `runtime_gui.cpp` 파일 정적 `static ImFont *s_cutie_title_font = nullptr;` 로 두고 build_font_atlas에서 대입, 홈/스플래시에서 `ImGui::PushFont(s_cutie_title_font, <size>)` 사용.

- [ ] **Step 5: 커밋**

```bash
git add res/fonts/Jua-Regular.ttf res/fonts/Gaegu-Bold.ttf source/cutie_fonts.h source/runtime_gui.cpp
git commit -m "Embed cutie fonts (Jua body, Gaegu title)"
```

> **Windows 검증:** 본문이 Jua로, 한글 정상 렌더. Task 4 파티클 glyph(✨)도 이제 보임.

---

### Task 7: About 화면 개인화 재작성

**Files:**
- Modify: `source/runtime_gui.cpp` (`draw_gui_about()` @3133)
- Test: About에 "정렬이 OOO를 위해..." + 디스코드 + 라이선스

**Interfaces:**
- Consumes: `g_cutie_theme.maker_name/recipient_name/about_message/discord_handle`, 제목 폰트

- [ ] **Step 1: `draw_gui_about()` 본문을 큐티 개인화 버전으로 교체**

@3133 함수 본문 시작부에 삽입(기존 내부 텍스트를 아래로 대체):
```cpp
	const reshade::cutie::CutieTheme &t = reshade::cutie::g_cutie_theme;

	ImGui::PushFont(s_cutie_title_font, 32.0f);
	ImGui::TextUnformatted(u8"정렬 커스텀 리쉐이드");   // 정렬 커스텀 리쉐이드
	ImGui::PopFont();
	ImGui::Spacing();

	char line[256];
	ImFormatString(line, sizeof(line),
		u8"이 리쉐이드는 %s이 %s를 위해 제작한 커스텀 리쉐이드입니다.",
		t.maker_name, t.recipient_name);   // "이 리쉐이드는 {maker}이 {recipient}를 위해 제작한 커스텀 리쉐이드입니다."
	ImGui::TextWrapped("%s", line);

	if (t.about_message != nullptr)
		ImGui::TextWrapped("%s", t.about_message);

	ImGui::Spacing();
	ImFormatString(line, sizeof(line),
		u8"궁금한 점은 디스코드 %s 로 연락주세요", t.discord_handle);
	ImGui::TextWrapped("%s", line);   // "궁금한 점은 디스코드 {handle} 로 연락주세요"

	ImGui::Spacing();
	if (ImGui::CollapsingHeader(u8"오픈소스 라이선스 정보"))
	{
		ImGui::TextUnformatted(u8"본 프로그램은 ReShade(BSD 3-Clause)와 여러 오픈소스 라이브러리를 사용합니다.");
		ImGui::TextUnformatted(u8"폰트: Jua, Gaegu (SIL Open Font License).");
	}
	return;
```
(함수 나머지 기존 코드는 제거하되, ReShade 필수 표기가 있으면 위 라이선스 헤더 아래 유지.)

- [ ] **Step 2: 커밋**

```bash
git add source/runtime_gui.cpp
git commit -m "Rewrite About page with personalized cutie content"
```

> **Windows 검증:** About 탭에 제목(Gaegu) + 개인화 문구 + 디스코드 + 라이선스 접기.

---

### Task 8: 네비게이션 + 홈 히어로 재디자인

**Files:**
- Modify: `source/runtime_gui.cpp` (탭 목록 @1314–1321 렌더부, `draw_gui_home()` @1534)
- Modify: `source/cutie_ui.*` (필 버튼 헬퍼)
- Test: 둥근 필/글로우 네비 + 히어로 헤더 홈

**Interfaces:**
- Produces: `bool reshade::cutie::pill_button(const char *label, bool selected, const CutieTheme &t);`

- [ ] **Step 1: 필 버튼 헬퍼 (`cutie_ui.hpp` 선언 + `cutie_ui.cpp` 구현)**

```cpp
	// 헤더
	bool pill_button(const char *label, bool selected, const CutieTheme &t);
```
```cpp
	// 구현
	bool pill_button(const char *label, bool selected, const CutieTheme &t)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, selected ? t.accent_active : t.accent);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, t.accent_hover);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, t.rounding + 8.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18.0f, 10.0f));
		const bool clicked = ImGui::Button(label);
		if (selected)
		{
			const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 2.5f);
			draw_glow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), t, pulse);
		}
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);
		return clicked;
	}
```

- [ ] **Step 2: 탭 바를 필 버튼 행으로 교체**

@1314 부근 탭 목록을 순회하는 렌더 코드를, 각 항목을 `reshade::cutie::pill_button(name, i==_selected_tab, g_cutie_theme)` 로 그리고 `ImGui::SameLine()` 로 나열하도록 변경. 클릭 시 선택 탭 인덱스 갱신 후 해당 draw 함수 호출은 기존 로직 유지.

- [ ] **Step 3: `draw_gui_home()` 히어로 헤더 추가**

@1534 함수 시작부에 삽입:
```cpp
	{
		const reshade::cutie::CutieTheme &t = reshade::cutie::g_cutie_theme;
		ImGui::PushFont(s_cutie_title_font, 40.0f);
		ImGui::TextUnformatted(u8"안녕! ✨");   // 안녕! ✨
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, t.text_dim);
		ImGui::TextUnformatted(t.name);
		ImGui::PopStyleColor();
		ImGui::Separator();
		ImGui::Spacing();
	}
```
(기존 홈 위젯은 그 아래 유지.)

- [ ] **Step 4: 커밋**

```bash
git add source/cutie_ui.hpp source/cutie_ui.cpp source/runtime_gui.cpp
git commit -m "Redesign navigation as pills and add home hero header"
```

> **Windows 검증:** 상단 네비가 둥근 필 + 선택 글로우. 홈에 큰 제목 헤더.

---

### Task 9: 스플래시 재디자인

**Files:**
- Modify: `source/runtime_gui.cpp` (스플래시 @1052–1156)
- Test: 커스텀 애니메이션 스플래시

**Interfaces:**
- Consumes: `g_cutie_theme`, 제목 폰트, `draw_background`, `draw_sparkles`

- [ ] **Step 1: 스플래시 내용 교체**

@1052 스플래시 블록에서, 기본 텍스트를 테마 배경+파티클+큐티 문구로 교체:
```cpp
		const reshade::cutie::CutieTheme &t = reshade::cutie::g_cutie_theme;
		ImDrawList *const dl = ImGui::GetWindowDrawList();
		const ImVec2 wmin = ImGui::GetWindowPos();
		const ImVec2 wmax = ImVec2(wmin.x + ImGui::GetWindowSize().x, wmin.y + ImGui::GetWindowSize().y);
		const float ts = static_cast<float>(ImGui::GetTime());
		reshade::cutie::draw_background(dl, wmin, wmax, t, ts);
		reshade::cutie::draw_sparkles(dl, wmin, wmax, t, ts);
		ImGui::PushFont(s_cutie_title_font, 28.0f);
		char msg[256];
		ImFormatString(msg, sizeof(msg), u8"%s님을 위한 리쉐이드 ✨", t.recipient_name); // "{recipient}님을 위한 리쉐이드 ✨"
		ImGui::TextUnformatted(msg);
		ImGui::PopFont();
```
(스플래시의 로딩/카운트 로직은 유지.)

- [ ] **Step 2: 커밋**

```bash
git add source/runtime_gui.cpp
git commit -m "Redesign splash with cutie background and message"
```

> **Windows 검증:** 오버레이 열 때 스플래시에 배경+파티클+"OOO님을 위한 리쉐이드 ✨".

---

### Task 10: 테마 브랜치 4종 생성 (peach / pink / mint / rainbow)

**Files:**
- Modify: `source/cutie_theme.cpp` (브랜치별 `g_cutie_theme` 값만)
- Test: 각 브랜치 빌드 → 팔레트 확인

**Interfaces:**
- Consumes: Task 1의 `CutieTheme` 스키마.

- [ ] **Step 1: cutie-base 최종 커밋 확인 후 브랜치 분기**

```bash
git switch cutie-base
git switch -c theme/peach
```

- [ ] **Step 2: `theme/peach` — 복숭아 파스텔** (base 기본값 유지 또는 미세조정)
`g_cutie_theme.name = "Peach Pastel"`, particle_glyph = "🌸"(`u8"\U0001F338"`).
커밋:
```bash
git add source/cutie_theme.cpp && git commit -m "Set peach pastel theme"
```

- [ ] **Step 3: `theme/pink` — 핑크 하트**
```bash
git switch cutie-base && git switch -c theme/pink
```
`cutie_theme.cpp` 값 교체: accent 계열을 핫핑크(예: `ImVec4(1.0f,0.35f,0.6f,*)`), bg_stop을 핑크→로즈골드, particle_glyph = "💗"(`u8"\U0001F497"`), name="Pink Heart", bg_animated=false.
```bash
git add source/cutie_theme.cpp && git commit -m "Set pink heart theme"
```

- [ ] **Step 4: `theme/mint` — 민트 소다**
```bash
git switch cutie-base && git switch -c theme/mint
```
accent를 민트/소프트그린, bg_stop을 민트→크림, particle_glyph="🍃"(`u8"\U0001F343"`), name="Mint Soda", bg_animated=false.
```bash
git add source/cutie_theme.cpp && git commit -m "Set mint soda theme"
```

- [ ] **Step 5: `theme/rainbow` — 무지개 RGB (화려 MAX)**
```bash
git switch cutie-base && git switch -c theme/rainbow
```
bg_animated=true, bg_anim_speed=60.0f, particle_count=40, glow_intensity=1.0f, particle_glyph="✨", name="Rainbow RGB".
```bash
git add source/cutie_theme.cpp && git commit -m "Set rainbow RGB theme"
```

- [ ] **Step 6: 모든 브랜치 푸시**

```bash
git push -u origin cutie-base theme/peach theme/pink theme/mint theme/rainbow
```

> **Windows 검증:** 각 브랜치 checkout 후 빌드 → 팔레트/파티클/애니메이션이 브랜치대로 다름.

---

## Self-Review

**Spec coverage:**
- 저장소/브랜치 구조 → Task 10 (+ 이미 생성된 cutie-base). ✅
- 테마 엔진(struct+활성테마 분리) → Task 1. ✅
- 스타일 적용 → Task 2. ✅
- 애니 배경 → Task 3 · 파티클 → Task 4 · 글로우 → Task 5. ✅
- 큐티 폰트(한글) → Task 6. ✅
- 레이아웃 재디자인: About → Task 7, 네비/홈 → Task 8, 스플래시 → Task 9. ✅
- 기능 패널 리스킨 → Task 2의 전역 스타일로 자동 적용(로직 무손상). ✅
- 브랜치별 개인화 → Task 10. ✅
- 폰트 OFL/임베드 → Task 6. ✅
- 인게임 선택기 없음(YAGNI) → 전 태스크에서 선택기 미구현. ✅

**Placeholder scan:** 코드 단계는 실제 코드 포함. 앵커 라인(@번호)은 upstream f191dc03 기준 근사치이며 편집 시 grep로 재확인(각 태스크에 함수명 명시). `recipient_name="OOO"`는 의도된 브랜치별 개인화 값. ✅

**Type consistency:** `CutieTheme` 필드명이 Task 2~9에서 동일하게 사용됨(accent/accent_hover/accent_active/glow/particle_*/rounding/maker_name/recipient_name/discord_handle). 헬퍼 시그니처 `apply_style/draw_background/draw_sparkles/draw_glow/pill_button/hsv_shift` 일관. 제목 폰트는 `s_cutie_title_font`(파일 정적)로 통일. ✅

## 알려진 리스크 / 편집 시 확인 사항

- ImGui 1.92 신 폰트 API: `AddFontFromMemoryCompressedBase85TTF` 시그니처/`PushFont(font, size)` 형태를 실제 번들 버전 헤더로 확인.
- `_selected_tab` 실제 멤버명 확인(탭 인덱스 저장 변수) — @1314 렌더부에서 grep.
- `AddRectFilledMultiColor`가 현재 ImGui에서 `AddRectFilledMultiColor` 그대로인지(일부 버전 함수명 변화) 확인.
- 이모지 컬러 렌더: ImGui 기본은 단색 글리프. 컬러 이모지가 안 나오면 파티클을 도형(원/별 폴리곤)으로 대체하는 폴백을 둔다.
