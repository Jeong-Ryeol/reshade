/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_ui.hpp"

#include <imgui_internal.h>
#include <cmath>
#include <cstring>

namespace sherbet
{
	// 테마 선택 상태(active/desired, 엔타이틀, apply_content)는 sherbet_theme_state.cpp 로 분리했다.
	// 여기 남은 것은 전부 ImGui 드로잉이라 host 테스트로 돌릴 수 없기 때문.
	static ImVec4 to_vec4(ImU32 c)
	{
		return ImGui::ColorConvertU32ToFloat4(c);
	}

	// 두 색을 섞는다(t=0 이면 a, 1 이면 b). 알파도 같이 섞는다.
	static ImU32 mix(ImU32 a, ImU32 b, float t)
	{
		const ImVec4 va = to_vec4(a), vb = to_vec4(b);
		return ImGui::ColorConvertFloat4ToU32(ImVec4(va.x + (vb.x - va.x) * t, va.y + (vb.y - va.y) * t, va.z + (vb.z - va.z) * t, va.w + (vb.w - va.w) * t));
	}
	// 색의 알파에 k(0..1)를 곱한다
	static ImU32 fade(ImU32 color, float k)
	{
		return with_alpha(color, static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * k));
	}

	float animate(ImGuiID id, bool on, float speed)
	{
		const float target = on ? 1.0f : 0.0f;
		float *const v = ImGui::GetStateStorage()->GetFloatRef(id, target); // 처음 보이는 프레임은 바로 목표값(켜지는 연출이 뜨지 않게)
		*v += (target - *v) * ImMin(1.0f, ImGui::GetIO().DeltaTime * speed);
		if (fabsf(target - *v) < 0.004f)
			*v = target;
		return *v;
	}

	void smooth_scroll()
	{
		ImGuiContext &g = *GImGui;
		ImGuiWindow *const window = g.CurrentWindow;
		ImGuiStorage *const st = &window->StateStorage;
		const ImGuiID id_target = window->GetID("##sherbet_scroll_target"), id_pos = window->GetID("##sherbet_scroll_pos"), id_active = window->GetID("##sherbet_scroll_active");
		// ⚠️ 저장소는 정렬된 배열이라 새 키가 끼어들면 먼저 받은 포인터가 무효가 된다 → 세 키를 먼저 만들어 놓고 받는다
		st->GetFloatRef(id_target, window->Scroll.y);
		st->GetFloatRef(id_pos, window->Scroll.y);
		st->GetBoolRef(id_active, false);
		float *const target = st->GetFloatRef(id_target);
		float *const pos = st->GetFloatRef(id_pos);
		bool *const active = st->GetBoolRef(id_active);

		// 휠을 받을 창 = 마우스 아래 창에서 위로 올라가며 처음 만나는 '세로로 굴릴 수 있는 창'(ImGui 기본 규칙과 같다).
		// Ctrl+휠은 글자 크기 조절이라 건드리지 않는다.
		if (g.IO.MouseWheel != 0.0f && !g.IO.KeyCtrl && !g.IO.KeyShift && g.HoveredWindow != nullptr)
		{
			ImGuiWindow *owner = g.HoveredWindow;
			while (owner != nullptr && owner != window && owner->ScrollMax.y == 0.0f)
				owner = (owner->Flags & ImGuiWindowFlags_ChildWindow) != 0 ? owner->ParentWindow : nullptr;
			if (owner == window && window->ScrollMax.y > 0.0f)
			{
				if (!*active)
					*target = *pos = window->Scroll.y;
				const float step = ImTrunc(ImMin(5.0f * window->FontRefSize, window->InnerRect.GetHeight() * 0.67f));
				*target = ImClamp(*target - g.IO.MouseWheel * step, 0.0f, window->ScrollMax.y);
				*active = true;
			}
		}
		// 스크롤바를 직접 끌면 그쪽이 이긴다
		if (*active && g.ActiveId != 0 && g.ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y))
			*active = false;
		if (*active)
		{
			// ImGui 는 스크롤 위치를 정수로 자르므로 창의 값을 되읽지 않고 따로 든 실수 위치를 굴린다(안 그러면 1px 앞에서 멈춘다)
			*target = ImClamp(*target, 0.0f, window->ScrollMax.y);
			*pos += (*target - *pos) * ImMin(1.0f, g.IO.DeltaTime * 16.0f);
			if (fabsf(*target - *pos) < 0.5f)
			{
				*pos = *target;
				*active = false;
			}
			ImGui::SetScrollY(*pos);
		}
	}

	static void fade_window_tree(ImGuiWindow *window, float alpha, float offset_y, float scale, const ImVec2 &pivot)
	{
		for (ImDrawVert &v : window->DrawList->VtxBuffer)
		{
			v.col = with_alpha(v.col, static_cast<ImU32>(((v.col >> IM_COL32_A_SHIFT) & 0xFF) * alpha));
			v.pos.x = pivot.x + (v.pos.x - pivot.x) * scale;
			v.pos.y = pivot.y + (v.pos.y - pivot.y) * scale + offset_y;
		}
		for (ImGuiWindow *child : window->DC.ChildWindows)
			fade_window_tree(child, alpha, offset_y, scale, pivot);
	}
	void fade_window(ImGuiWindow *window, float alpha, float offset_y, float scale)
	{
		if (window == nullptr || (alpha >= 1.0f && offset_y == 0.0f && scale == 1.0f))
			return;
		// 크기는 그 창의 가운데를 기준으로 줄인다(자식창도 같은 점 기준이라 한 덩어리로 움직인다)
		fade_window_tree(window, ImClamp(alpha, 0.0f, 1.0f), offset_y, scale, window->Rect().GetCenter());
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
		c[ImGuiCol_TabSelected] = panel_alt;
		c[ImGuiCol_TabDimmed] = chip;
		c[ImGuiCol_TabDimmedSelected] = panel;
		c[ImGuiCol_PlotLines] = accent;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_NavCursor] = accent;
		c[ImGuiCol_DragDropTarget] = to_vec4(t.accent2);
	}

	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time, float rounding)
	{
		const float w = max.x - min.x, h = max.y - min.y;

		// 둥근 모서리 베이스 패널 (창 영역에만 채움 — 게임은 창 밖에서 그대로 보임)
		dl->AddRectFilled(min, max, t.bg1, rounding);
		// 위→아래 은은한 톤 변화 (상단 bg0, 하단 bg2 를 반투명으로 덧칠)
		// 그라디언트는 rounding 만큼 안쪽에만 — 둥근 모서리 밖으로 각진 틴트가 새지 않게
		dl->AddRectFilledMultiColor(
			ImVec2(min.x + rounding, min.y + rounding), ImVec2(max.x - rounding, min.y + h * 0.5f),
			with_alpha(t.bg0, 0xB0), with_alpha(t.bg0, 0xB0),
			with_alpha(t.bg0, 0x00), with_alpha(t.bg0, 0x00));
		dl->AddRectFilledMultiColor(
			ImVec2(min.x + rounding, min.y + h * 0.5f), ImVec2(max.x - rounding, max.y - rounding),
			with_alpha(t.bg2, 0x00), with_alpha(t.bg2, 0x00),
			with_alpha(t.bg2, 0x88), with_alpha(t.bg2, 0x88));

		// 아주 은은한 상단 코너 글로우(작게, 저알파) — 큰 블롭은 제거해 시안처럼 깔끔하게
		const float t1 = time * 0.12f;
		dl->AddCircleFilled(ImVec2(min.x + w * 0.16f, min.y + h * (0.10f + 0.01f * sinf(t1))), w * 0.12f, with_alpha(t.glow, 26), 32);
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
			ImU32 col = with_alpha(t.accent, (ImU32)(alpha * 255));
			draw_shape(dl, t.particle_shape, ImVec2(x, y), 6.0f, col);
		}
	}

	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color, float strength)
	{
		// 사각형 주변 3겹 확산 글로우
		for (int i = 3; i >= 1; --i)
		{
			const float e = i * 4.0f;
			ImU32 a = with_alpha(glow_color, (ImU32)(30 / i * strength));
			dl->AddRect(ImVec2(min.x - e, min.y - e), ImVec2(max.x + e, max.y + e), a, 16.0f, 0, 3.0f);
		}
	}

	bool toggle(const char *label, bool *v)
	{
		const theme &t = active_theme();
		const float height = ImGui::GetFrameHeight() * 0.78f;
		const float width = height * 1.85f;
		const float radius = height * 0.5f;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImGuiStyle &style = ImGui::GetStyle();
		const float label_w = (label && label[0] != '\0' && label[0] != '#') ? ImGui::CalcTextSize(label, NULL, true).x : 0.0f;

		// 아이템 하나가 트랙+라벨 전체를 덮는다 — 라벨을 SameLine+Text 로 그리면 별도 아이템이 되어
		// 소비자 쪽 IsItemActive(드래그 재정렬)/우클릭 컨텍스트/툴팁이 라벨에서만 동작하는 버그가 있었음
		const bool clicked = ImGui::InvisibleButton(label, ImVec2(width + (label_w > 0.0f ? style.ItemInnerSpacing.x + label_w : 0.0f), height));
		if (clicked)
			*v = !*v;

		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		// 손잡이가 미끄러지고 색이 번지게(딱 끊기지 않게)
		const ImGuiID id = ImGui::GetItemID();
		const float tnorm = animate(id, *v);
		const float hov = animate(id ^ 0x9E3779B9, hovered, 20.0f);
		dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), mix(t.chip, t.accent, tnorm), radius);
		// 꺼진 트랙은 칩 색이라 어두운 카드 위에서 안 보였다(손잡이만 떠 보임) → 옅은 테두리로 자리를 보여 준다
		if (tnorm < 1.0f)
			dl->AddRect(p, ImVec2(p.x + width, p.y + height), with_alpha(t.text_dim, static_cast<ImU32>(120 * (1.0f - tnorm))), radius, 0, 1.0f);
		if (tnorm > 0.0f)
			draw_glow(dl, p, ImVec2(p.x + width, p.y + height), t.glow, tnorm);
		const float knob_x = p.x + radius + tnorm * (width - 2 * radius);
		dl->AddCircleFilled(ImVec2(knob_x, p.y + radius), radius - 2.0f, mix(t.text_dim, IM_COL32(255, 255, 255, 255), tnorm), 24);
		if (hov > 0.0f)
			dl->AddRect(p, ImVec2(p.x + width, p.y + height), fade(t.border, hov), radius, 0, 1.5f);

		if (label_w > 0.0f)
		{
			// 라벨은 draw list 로 직접 — 트랙 높이에 수직 중앙 정렬, "##" 이후는 표시 안 함
			const char *label_end = strstr(label, "##");
			const ImVec2 tp(p.x + width + style.ItemInnerSpacing.x, p.y + (height - ImGui::GetTextLineHeight()) * 0.5f);
			dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), label, label_end);
		}
		return clicked;
	}

	void begin_card(const char *id, float height)
	{
		const theme &t = active_theme();
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(t.panel));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(t.border));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
		// height == 0 -> 자동 높이(AutoResizeY). height > 0 -> 고정 높이(AutoResizeY와 충돌하므로 제외).
		const ImGuiChildFlags flags = height > 0.0f ? ImGuiChildFlags_Borders : (ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
		ImGui::BeginChild(id, ImVec2(0.0f, height), flags, ImGuiWindowFlags_None);
	}

	void end_card()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
	}

	bool pill_button(const char *label, bool active)
	{
		const theme &t = active_theme();
		const ImU32 bg = active ? t.accent : t.chip;
		const ImU32 fg = active ? IM_COL32(20, 20, 20, 255) : t.text_dim;
		// 마우스를 올렸을 때 색이 번지듯 바뀌게: 지난 프레임까지의 올림 정도로 바탕색을 섞는다
		const ImGuiID hover_id = ImGui::GetID(label) ^ 0x9E3779B9;
		const ImU32 bg_now = mix(bg, active ? t.accent2 : t.panel_alt, ImGui::GetStateStorage()->GetFloat(hover_id, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(bg_now));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(bg_now));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(t.accent));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(fg));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pill_padding);
		const bool pressed = ImGui::Button(label);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(4);
		animate(hover_id, ImGui::IsItemHovered(), 20.0f);
		return pressed;
	}

	void draw_crosshair_preview(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max,
		const crosshair::layer &layer, std::vector<crosshair::quad> &scratch)
	{
		// 좌표는 정수 픽셀이어야 한다(§3.1) — 서브픽셀로 그리면 텍셀 블렌딩이 생겨
		// 발로란트의 하드 에지가 사라지고, 카드 미리보기와 실제 화면이 달라 보인다.
		const int cx = static_cast<int>(std::floor((min.x + max.x) * 0.5f));
		const int cy = static_cast<int>(std::floor((min.y + max.y) * 0.5f));
		// 오차 애니메이션은 0 — 진열대는 '휴지 상태' 를 보여준다(카드마다 흔들리면 못 고른다).
		crosshair::build_crosshair(layer, cx, cy, 0.0f, 0.0f, scratch);

		// 카드보다 큰 조준점은 잘라낸다. 축소하지 않는 이유: 실제 픽셀 크기가 조준점
		// 선택의 핵심 정보이고, 축소하면 1px 선이 사라져 전혀 다른 물건으로 보인다.
		dl->PushClipRect(min, max, true);
		for (const crosshair::quad &q : scratch)
			dl->AddRectFilled(
				ImVec2(static_cast<float>(q.r.x), static_cast<float>(q.r.y)),
				ImVec2(static_cast<float>(q.r.x + q.r.w), static_cast<float>(q.r.y + q.r.h)),
				IM_COL32(q.color.r, q.color.g, q.color.b, q.alpha), 0.0f);
		dl->PopClipRect();
	}

	ImVec4 status_color(status s)
	{
		const theme &t = active_theme();
		// t.text 는 언제나 카드 위에서 읽히는 색이므로 배경 명도의 신뢰 가능한 대리값이다.
		// (7개 정적 테마 실측: mint 0.980 / peach 0.958 / pink 0.950 / rainbow 0.954 /
		//  lavender 0.938 / noir 0.939 / strawberry 0.218 — 딸기만 라이트다.)
		// 서버가 내려주는 동적 테마(add_dynamic_theme)도 같은 식으로 자동 처리된다.
		const float r = ((t.text >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
		const float g = ((t.text >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
		const float b = ((t.text >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
		const bool light_bg = (0.2126f * r + 0.7152f * g + 0.0722f * b) < 0.5f; // 본문이 어둡다 = 배경이 밝다
		switch (s)
		{
		case status::bad:  return light_bg ? ImVec4(0.62f, 0.10f, 0.16f, 1.0f) : ImVec4(0.95f, 0.42f, 0.42f, 1.0f);
		case status::warn: return light_bg ? ImVec4(0.55f, 0.33f, 0.02f, 1.0f) : ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
		default:           return light_bg ? ImVec4(0.06f, 0.42f, 0.19f, 1.0f) : ImVec4(0.36f, 0.86f, 0.45f, 1.0f);
		}
	}

	bool rail_button(const char *id, const char *icon, bool active, ImU32 idle_col)
	{
		const theme &t = active_theme();
		const float sz = rail_button_size;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const bool clicked = ImGui::InvisibleButton(id, ImVec2(sz, sz));
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList *dl = ImGui::GetWindowDrawList();
		// 선택 표시는 살짝 커지며 번져 들어오고, 올림 표시는 서서히 뜬다
		const ImGuiID item_id = ImGui::GetItemID();
		const float sel = animate(item_id, active, 14.0f);
		const float hov = animate(item_id ^ 0x9E3779B9, hovered, 20.0f);
		if (hov > 0.0f && sel < 1.0f)
			dl->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz), fade(t.panel_alt, hov * (1.0f - sel)), 14.0f);
		if (sel > 0.0f)
		{
			const float inset = (1.0f - sel) * sz * 0.08f;
			const ImVec2 a(p.x + inset, p.y + inset), b(p.x + sz - inset, p.y + sz - inset);
			dl->AddRectFilled(a, b, fade(t.accent, sel), 14.0f);
			draw_glow(dl, a, b, t.glow, sel);
		}
		const ImU32 col = mix(mix(idle_col != 0 ? idle_col : t.text_dim, t.text, hov), IM_COL32(20, 20, 20, 255), sel);
		const ImVec2 ts = ImGui::CalcTextSize(icon);
		dl->AddText(ImVec2(p.x + (sz - ts.x) * 0.5f, p.y + (sz - ts.y) * 0.5f), col, icon);
		return clicked;
	}
}
