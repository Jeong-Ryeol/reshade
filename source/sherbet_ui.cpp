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
		c[ImGuiCol_TabActive] = panel_alt;
		c[ImGuiCol_TabUnfocused] = chip;
		c[ImGuiCol_TabUnfocusedActive] = panel;
		c[ImGuiCol_PlotLines] = accent;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
		c[ImGuiCol_NavHighlight] = accent;
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
}
