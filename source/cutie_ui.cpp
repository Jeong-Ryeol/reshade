#include "cutie_ui.hpp"
#include <cmath> // fmodf

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
		constexpr float TINT_ALPHA = 0.25f;
		ImVec4 a = t.bg_stop_a, b = t.bg_stop_b;
		a.w *= TINT_ALPHA;
		b.w *= TINT_ALPHA;
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
}
