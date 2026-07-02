# SHERBET Plan 4 — Layout & Left-Rail Tabs Implementation Plan

> REQUIRED SUB-SKILL: subagent-driven-development. Steps use `- [ ]`.

**Goal:** 상단 탭바(도크스페이스)를 제거하고 **좌측 아이콘 레일 + 4탭(홈·마켓·설정·정보)** 단일 창 레이아웃으로 교체한다. 이것이 "리쉐이드로 안 보이게" 하는 핵심 변화다.

**Architecture:** `draw_gui()`의 메인 오버레이 창에서 DockBuilder/DockSpace/탭별 도킹 창 루프를 제거하고, 하나의 "Viewport" 창 안에 좌측 레일 차일드(아이콘 버튼) + 콘텐츠 차일드(선택 탭의 draw 함수 호출)를 그린다. 애드온/통계/로그 탭은 레일에서 제외. 로그 뷰어는 설정 하단에서 접근(Plan 5/6). 에디터 창은 별도 유지.

## Global Constraints

- **ImGui 1.92.5 + IMGUI_DISABLE_OBSOLETE_FUNCTIONS**. obsolete API 금지. sherbet_ui 변경은 로컬 `clang ... -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp` exit 0.
- runtime_gui.cpp는 Mac 컴파일 불가 → 리뷰 + CI로 검증. 매우 신중히.
- 외부 라이브러리 금지. 이모지 금지 — 레일 아이콘은 ForkAwesome(`ICON_FK_*`).
- 핵심 기능 로직 수정 금지 — draw_gui_home/settings/about 본문은 그대로 호출.
- 커밋 메시지 Claude/AI 문구 금지. 신규 파일 헤더 규칙 유지.
- 검증: 플랜 끝 push 1회 + CI green + 시각 확인 권장(레일/탭 전환 동작).

---

### Task 1: 레일 버튼 위젯 + 탭 상태 멤버 + 마켓 스텁

**Files:** Modify `source/sherbet_ui.hpp`/`.cpp`, `source/runtime.hpp`, `source/runtime_gui.cpp`

**Interfaces:**
- Produces:
  - `bool sherbet::rail_button(const char *id, const char *icon, bool active)` — 44x44 라운드 아이콘 버튼(활성 시 accent 배경+글로우). 클릭 true.
  - 멤버 `int _sherbet_tab = 0;` (0=홈,1=마켓,2=설정,3=정보)
  - `void reshade::runtime::draw_gui_market();` — Plan 6에서 채울 스텁(지금은 "준비 중" 카드)

- [ ] **Step 1: rail_button 구현 (sherbet_ui)**

hpp 선언:
```cpp
	bool rail_button(const char *id, const char *icon, bool active);
```
cpp 구현:
```cpp
	bool rail_button(const char *id, const char *icon, bool active)
	{
		const theme &t = default_theme();
		const float sz = 44.0f;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(sz, sz));
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		if (active)
		{
			dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), t.accent, 14.0f);
			draw_glow(dl, p, ImVec2(p.x + sz, p.y + sz), t.glow);
		}
		else if (hovered)
		{
			dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), t.panel_alt, 14.0f);
		}
		const ImU32 col = active ? IM_COL32(20, 20, 20, 255) : (hovered ? t.text : t.text_dim);
		const ImVec2 ts = ImGui::CalcTextSize(icon);
		dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f), col, icon);
		return clicked;
	}
```
로컬 검증: `clang ... source/sherbet_ui.cpp` exit 0.

- [ ] **Step 2: _sherbet_tab 멤버 추가 (runtime.hpp)**

`ImFont *_sherbet_title_font = nullptr;`(Plan 2) 아래에:
```cpp
		int _sherbet_tab = 0; // 0=Home 1=Market 2=Settings 3=About
```

- [ ] **Step 3: draw_gui_market 선언 + 스텁 정의**

`source/runtime.hpp`의 `void draw_gui_about();` 근처(선언부)에 추가:
```cpp
		void draw_gui_market();
```
`source/runtime_gui.cpp`의 `draw_gui_about()` 정의 근처(위나 아래)에 스텁 추가:
```cpp
void reshade::runtime::draw_gui_market()
{
	sherbet::begin_card("##market_soon");
	ImGui::TextUnformatted(ICON_FK_SHOPPING_CART "  Market");
	ImGui::Spacing();
	ImGui::TextUnformatted("\xEC\xA4\x80\xEB\xB9\x84 \xEC\xA4\x91\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4."); // "준비 중입니다."
	sherbet::end_card();
}
```
(`ICON_FK_SHOPPING_CART`는 forkawesome.h에 있음. include는 이미 있음.)

- [ ] **Step 4: Commit(push 안 함)**

```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp
git add source/sherbet_ui.hpp source/sherbet_ui.cpp source/runtime.hpp source/runtime_gui.cpp
git commit -m "feat(sherbet): 레일 버튼 위젯 + 탭 상태 + 마켓 스텁"
```

---

### Task 2: 메인 오버레이 창을 좌측 레일 + 콘텐츠로 재작성

**Files:** Modify `source/runtime_gui.cpp` (`draw_gui()` — overlay_callbacks 정의부터 per-tab 렌더 루프까지)

**Interfaces:** Consumes `sherbet::rail_button`, `_sherbet_tab`, `draw_gui_market` (Task 1); ForkAwesome 아이콘.

**설명(구현자 필독):** 아래 "제거 대상"과 "교체 코드"를 정확히 적용한다. 먼저 현재 코드를 읽어 정확한 경계를 파악하라:
`grep -n 'overlay_callbacks\[\]\|DockBuilder\|ImGui::DockSpace\|if (!_editors.empty' source/runtime_gui.cpp`

**제거 대상(연속 블록):** `const std::pair<std::string, void(runtime::*)()> overlay_callbacks[] = { ... };` 정의 → `root_space_id` / `init_window_layout` DockBuilder 초기화 블록 → `ImGui::Begin("Viewport", ...)` → (그 안의 Sherbet 배경 블록은 **보존**) → `ImGui::DockSpace(...)` → `ImGui::End();` → NavInputSource focus 블록 → per-tab 렌더 `for (... overlay_callbacks ...) { Begin/End }` 루프. **`if (!_editors.empty())` 에디터 블록은 그대로 남긴다.**

- [ ] **Step 1: 교체 코드 적용**

위 "제거 대상" 전체를 아래로 교체(단, Sherbet 배경 블록은 이 새 코드 안에 포함되어 있음):
```cpp
		// SHERBET: 단일 오버레이 창 — 좌측 아이콘 레일 + 콘텐츠
		ImGui::SetNextWindowPos(viewport->Pos + viewport_offset);
		ImGui::SetNextWindowSize(viewport->Size - viewport_offset);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Viewport", nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar();

		{
			// 애니메이션 배경 + 파티클 (오버레이 창들 뒤)
			ImDrawList *const sherbet_bg = ImGui::GetBackgroundDrawList();
			const ImVec2 sherbet_vmin = viewport->Pos;
			const ImVec2 sherbet_vmax = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
			static float s_sherbet_time = 0.0f; s_sherbet_time += _imgui_context->IO.DeltaTime;
			sherbet::draw_background(sherbet_bg, sherbet_vmin, sherbet_vmax, sherbet::default_theme(), s_sherbet_time);
			sherbet::draw_particles(sherbet_bg, sherbet_vmin, sherbet_vmax, sherbet::default_theme(), s_sherbet_time);
		}

		// 좌측 레일
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.28f));
		ImGui::BeginChild("##sherbet_rail", ImVec2(66.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		{
			ImGui::Dummy(ImVec2(0, 8));
			// 로고
			ImGui::SetCursorPosX((66.0f - 40.0f) * 0.5f);
			sherbet::rail_button("##logo", ICON_FK_MAGIC, false);
			ImGui::Dummy(ImVec2(0, 10));

			struct RailItem { const char *id; const char *icon; };
			const RailItem items[] = {
				{ "##tab_home", ICON_FK_HOME },
				{ "##tab_market", ICON_FK_SHOPPING_CART },
				{ "##tab_settings", ICON_FK_SLIDERS },
				{ "##tab_about", ICON_FK_INFO_CIRCLE },
			};
			for (int i = 0; i < 4; ++i)
			{
				ImGui::SetCursorPosX((66.0f - 44.0f) * 0.5f);
				if (sherbet::rail_button(items[i].id, items[i].icon, _sherbet_tab == i))
					_sherbet_tab = i;
				ImGui::Dummy(ImVec2(0, 4));
			}
		}
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 0.0f);

		// 콘텐츠
		ImGui::BeginChild("##sherbet_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoFocusOnAppearing);
		switch (_sherbet_tab)
		{
		case 0: draw_gui_home(); break;
		case 1: draw_gui_market(); break;
		case 2: draw_gui_settings(); break;
		case 3: draw_gui_about(); break;
		default: draw_gui_home(); break;
		}
		ImGui::EndChild();

		ImGui::End();
```
주의사항:
- `ICON_FK_MAGIC`/`ICON_FK_HOME`/`ICON_FK_SHOPPING_CART`/`ICON_FK_SLIDERS`/`ICON_FK_INFO_CIRCLE`는 `res/fonts/forkawesome.h`에 존재(확인 완료). include는 이미 있음.
- `ImGuiChildFlags_None`, `ImGuiWindowFlags_NoScrollbar` 등은 표준.
- 제거하는 코드에 있던 `root_space_id` 등 지역변수가 이 블록 뒤에서 더 쓰이지 않는지 확인(안 쓰임). DockBuilder 관련 심볼이 남지 않도록 완전히 제거.
- `#if RESHADE_ADDON` 애드온 도킹 루프(init_window_layout 안)도 제거된다. 애드온 오버레이 콜백은 이 플랜 범위 밖 — 별도 창으로 뜨던 것이 안 뜰 수 있으나 기능 파손 아님(콜백은 아래 다른 위치에서 별도 처리되면 유지). **애드온 콜백을 별도 렌더하는 코드가 draw_gui 다른 곳에 있으면 건드리지 말 것.**

- [ ] **Step 2: 컴파일 근거 확인(로컬 불가 → 정독)**

runtime_gui.cpp는 Mac 빌드 불가. 정독으로 확인: 제거 후 `root_space_id`/`main_space_id`/`right_space_id`/`init_window_layout`/`DockBuilder`/`DockSpace` 심볼이 draw_gui 내에 남아있지 않을 것(`grep -n 'root_space_id\|DockBuilder\|DockSpace\|init_window_layout' source/runtime_gui.cpp` → 결과 없음 또는 draw_gui_vr 등 무관한 곳만). 중괄호 균형 확인.

- [ ] **Step 3: Commit(push 안 함)**

```bash
cd ~/reshade
git add source/runtime_gui.cpp
git commit -m "feat(sherbet): 좌측 아이콘 레일 + 4탭 단일 창 레이아웃"
```

---

### Task 3: 플랜 CI 검증 (컨트롤러)

- [ ] **Step 1: push + CI watch**
```bash
cd ~/reshade && git push
RUN=$(gh run list -R Jeong-Ryeol/reshade --branch sherbet-base --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch $RUN -R Jeong-Ryeol/reshade --exit-status
```
Expected: green. 실패 시 첫 에러(대개 남은 DockBuilder 심볼 / obsolete flag / 아이콘 상수) 픽스 후 재검증.

---

## Self-Review 메모
- **커버리지:** 스펙 §5 좌측 레일 + 탭 4개(홈/마켓/설정/정보), 애드온/통계/로그 탭 제거. 마켓은 스텁(Plan 6). 로그는 설정>고급(Plan 5/6).
- **리스크(높음):** 도크스페이스 제거가 draw_gui의 창 구조를 크게 바꾼다. (1) draw_gui_* 를 차일드 안에서 호출 — 정상. (2) 제거 후 dangling 심볼 — grep 확인 스텝. (3) 에디터/애드온 창 — 에디터 블록 보존, 애드온 도킹만 제거(플로팅으로 뜨거나 별도 처리). (4) 통계 탭이 사라져도 OSD가 대체.
- **타입 일관성:** `rail_button`/`_sherbet_tab`/`draw_gui_market` 시그니처가 hpp/cpp/호출부 일치.
- **시각 확인 필수:** CI green 후 아티팩트로 레일 4버튼·탭 전환·홈 카드 확인.
