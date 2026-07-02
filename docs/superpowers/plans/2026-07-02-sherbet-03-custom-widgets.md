# SHERBET Plan 3 — Custom Widgets Implementation Plan

> REQUIRED SUB-SKILL: subagent-driven-development. Steps use `- [ ]`.

**Goal:** ImGui 위에 Sherbet 커스텀 위젯(토글 스위치·카드 프레임·필 버튼·그라디언트 슬라이더)을 만들고, 이펙트 목록의 체크박스를 토글 스위치로 교체해 눈에 보이는 변화를 만든다.

**Architecture:** 모든 위젯은 `source/sherbet_ui.{hpp,cpp}`에 순수 함수로 추가(ImDrawList + InvisibleButton 기반). 기존 로직은 보존하고 체크박스만 토글로 교체.

## Global Constraints

- **ImGui 1.92.5**, 빌드는 `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` 정의됨 → **obsolete API 금지**. 로컬 검증은 반드시 `clang -std=c++17 -fsyntax-only -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp` (exit 0).
- 외부 라이브러리 금지. 이모지 금지(ForkAwesome/ImDrawList).
- 핵심 기능 로직 수정 금지 — 체크박스→토글은 표시만 교체, enable/disable 호출 보존.
- 커밋 메시지에 Claude/AI 문구 금지. 신규 코드 파일 없음(기존 sherbet_ui 확장).
- 테마 색은 `sherbet::default_theme()`.
- 검증: 플랜 끝에 push 1회 + CI green.

---

### Task 1: 토글 스위치 위젯

**Files:** Modify `source/sherbet_ui.hpp`, `source/sherbet_ui.cpp`

**Interfaces:**
- Produces: `bool sherbet::toggle(const char *label, bool *v)` — 필형 토글 스위치 + 오른쪽에 라벨. 값이 바뀌면 true 반환, `*v` 갱신. ImGui 아이템으로 동작(레이아웃/ID 정상).

- [ ] **Step 1: 헤더 선언 추가**

`sherbet_ui.hpp` namespace에 추가:
```cpp
	// 필형 토글 스위치. 값이 변경되면 true 반환.
	bool toggle(const char *label, bool *v);
```

- [ ] **Step 2: 구현 추가**

`sherbet_ui.cpp`에 추가:
```cpp
	bool toggle(const char *label, bool *v)
	{
		ImGuiWindow *window = ImGui::GetCurrentWindow();
		if (window->SkipItems)
			return false;

		const theme &t = default_theme();
		const float height = ImGui::GetFrameHeight() * 0.78f;
		const float width = height * 1.85f;
		const float radius = height * 0.5f;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		ImGuiContext &g = *ImGui::GetCurrentContext();
		const float label_w = (label && label[0] != '\0' && label[0] != '#') ? ImGui::CalcTextSize(label, NULL, true).x : 0.0f;

		ImGui::InvisibleButton(label, ImVec2(width + (label_w > 0 ? g.Style.ItemInnerSpacing.x + label_w : 0.0f), height));
		const bool clicked = ImGui::IsItemClicked();
		if (clicked)
			*v = !*v;

		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float tnorm = *v ? 1.0f : 0.0f;
		const ImU32 track = *v ? t.accent : t.chip;
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), track, radius);
		if (*v)
			draw_glow(dl, p, ImVec2(p.x + width, p.y + height), t.glow);
		const float knob_x = p.x + radius + tnorm * (width - 2 * radius);
		const ImU32 knob = *v ? IM_COL32(255, 255, 255, 255) : t.text_dim;
		dl->AddCircleFilled(ImVec2(knob_x, p.y + radius), radius - 2.0f, knob, 24);
		if (hovered)
			dl->AddRect(p, ImVec2(p.x + width, p.y + height), t.border, radius, 0, 1.5f);

		if (label_w > 0.0f)
		{
			ImGui::SameLine(0.0f, g.Style.ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
		}
		return clicked;
	}
```
주의: `ImGui::GetCurrentWindow()`, `ImGuiWindow::SkipItems`는 내부 API지만 ReShade가 `imgui_internal.h`를 이미 포함하는지 확인 필요. 포함 안 되면 `sherbet_ui.cpp` 상단에 `#include <imgui_internal.h>` 추가. `InvisibleButton`/`IsItemClicked`/`IsItemHovered`/`GetWindowDrawList`/`SameLine`/`AlignTextToFramePadding`/`TextUnformatted`는 공개 API.

- [ ] **Step 3: 로컬 검증 + Commit(push 안 함)**

```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp
git add source/sherbet_ui.hpp source/sherbet_ui.cpp
git commit -m "feat(sherbet): 토글 스위치 위젯"
```
Expected: clang exit 0. (imgui_internal.h가 -Ideps/imgui로 찾아지는지 확인 — 안 되면 include 경로/헤더명 조정.)

---

### Task 2: 카드 프레임 + 필 버튼 위젯

**Files:** Modify `source/sherbet_ui.hpp`, `source/sherbet_ui.cpp`

**Interfaces:**
- Produces:
  - `void sherbet::begin_card(const char *id, float height = 0.0f)` / `void sherbet::end_card()` — 라운드 패널 차일드 시작/종료(테마 panel 배경 + border + 패딩). height 0 = 자동.
  - `bool sherbet::pill_button(const char *label, bool active)` — 필형 버튼. 클릭 시 true.

- [ ] **Step 1: 헤더 선언**

```cpp
	void begin_card(const char *id, float height = 0.0f);
	void end_card();
	bool pill_button(const char *label, bool active);
```

- [ ] **Step 2: 구현**

```cpp
	void begin_card(const char *id, float height)
	{
		const theme &t = default_theme();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(t.panel));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(t.border));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
		ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
	}

	void end_card()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
	}

	bool pill_button(const char *label, bool active)
	{
		const theme &t = default_theme();
		const ImU32 bg = active ? t.accent : t.chip;
		const ImU32 fg = active ? IM_COL32(20, 20, 20, 255) : t.text_dim;
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(bg));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(active ? t.accent2 : t.panel_alt));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(t.accent));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(fg));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 7));
		const bool pressed = ImGui::Button(label);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		return pressed;
	}
```
주의: `ImGuiChildFlags_Borders`(1.92 이름 — 구버전 `ImGuiChildFlags_Border`가 obsolete일 수 있음), `ImGuiChildFlags_AutoResizeY` 존재 확인. `grep -n "ImGuiChildFlags_Borders\|ImGuiChildFlags_AutoResizeY" deps/imgui/imgui.h` 로 확인 후 실제 이름 사용. `height`가 0이 아니면 AutoResizeY와 충돌하므로, height>0이면 `ImGuiChildFlags_Borders`만 쓰고 size.y=height로.

- [ ] **Step 3: 로컬 검증 + Commit(push 안 함)**

```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp
git add source/sherbet_ui.hpp source/sherbet_ui.cpp
git commit -m "feat(sherbet): 카드 프레임 + 필 버튼 위젯"
```

---

### Task 3: 이펙트 목록 체크박스 → 토글 스위치 교체 (visible proof)

**Files:** Modify `source/runtime_gui.cpp` (`draw_technique_editor`, 약 4367행)

**Interfaces:** Consumes `sherbet::toggle` (Task 1)

- [ ] **Step 1: 교체**

`source/runtime_gui.cpp`에서:
```cpp
				if (bool status = tech.enabled;
					ImGui::Checkbox(label.c_str(), &status) && !force_enabled)
```
를 다음으로 교체:
```cpp
				if (bool status = tech.enabled;
					sherbet::toggle(label.c_str(), &status) && !force_enabled)
```
(그 아래 `modified = true; if (status) enable_technique... else disable_technique...` 로직은 그대로 둔다.)

- [ ] **Step 2: include 확인**

`source/runtime_gui.cpp`에 `#include "sherbet_ui.hpp"`가 이미 있음(Plan 2). 확인만.

- [ ] **Step 3: Commit(push 안 함)**

```bash
cd ~/reshade
git add source/runtime_gui.cpp
git commit -m "feat(sherbet): 이펙트 목록 토글 스위치 적용"
```

---

### Task 4: 플랜 CI 검증 (컨트롤러)

- [ ] **Step 1: push + CI watch**
```bash
cd ~/reshade && git push
RUN=$(gh run list -R Jeong-Ryeol/reshade --branch sherbet-base --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch $RUN -R Jeong-Ryeol/reshade --exit-status
```
Expected: green. 실패 시 첫 에러(대개 obsolete ImGui API / internal 헤더) 픽스 후 재검증.

---

## Self-Review 메모
- **커버리지:** 스펙 §5 커스텀 위젯(토글/카드/버튼). 그라디언트 슬라이더는 Plan 5(변수 에디터)에서. 아이콘 레일은 Plan 4.
- **리스크:** `imgui_internal.h`(GetCurrentWindow/SkipItems) 사용 — ReShade 포함 여부 확인 스텝 있음. `ImGuiChildFlags_*` 이름 1.92 확인 스텝 있음. 토글이 ImGui 아이템 규약(ID/레이아웃)을 지키는지 — InvisibleButton 사용으로 보장.
- **타입 일관성:** `toggle`/`begin_card`/`end_card`/`pill_button` 시그니처가 hpp/cpp/호출부 일치.
