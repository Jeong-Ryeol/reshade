/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_ui.hpp"

#include <cmath>

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
		c[ImGuiCol_TabSelected] = panel_alt;
		c[ImGuiCol_TabDimmed] = chip;
		c[ImGuiCol_TabDimmedSelected] = panel;
		c[ImGuiCol_PlotLines] = accent;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_NavCursor] = accent;
		c[ImGuiCol_DragDropTarget] = to_vec4(t.accent2);
	}

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

	bool toggle(const char *label, bool *v)
	{
		const theme &t = default_theme();
		const float height = ImGui::GetFrameHeight() * 0.78f;
		const float width = height * 1.85f;
		const float radius = height * 0.5f;
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const ImGuiStyle &style = ImGui::GetStyle();
		const float label_w = (label && label[0] != '\0' && label[0] != '#') ? ImGui::CalcTextSize(label, NULL, true).x : 0.0f;

		// InvisibleButton은 SkipItems를 내부 처리하고 클릭 시 true 반환(공개 API만 사용)
		const bool clicked = ImGui::InvisibleButton(label, ImVec2(width + (label_w > 0 ? style.ItemInnerSpacing.x + label_w : 0.0f), height));
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
			ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
		}
		return clicked;
	}

	void begin_card(const char *id, float height)
	{
		const theme &t = default_theme();
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
}
