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
