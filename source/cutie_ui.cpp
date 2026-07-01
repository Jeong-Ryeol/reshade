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

	// 시드 기반 해시(랜덤 대체, 프레임 간 안정)
	static float hash01(int i, int salt)
	{
		unsigned int x = static_cast<unsigned int>(i) * 374761393u + static_cast<unsigned int>(salt) * 668265263u;
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
			const float s = 4.0f + hash01(i, 3) * 4.0f;          // sparkle radius
			const float thin = s * 0.28f;
			const ImU32 cu = ImGui::ColorConvertFloat4ToU32(col);
			// 4-point star = two crossed thin diamonds (no font glyph needed)
			dl->AddQuadFilled(ImVec2(px, py - s), ImVec2(px + thin, py), ImVec2(px, py + s), ImVec2(px - thin, py), cu);
			dl->AddQuadFilled(ImVec2(px - s, py), ImVec2(px, py - thin), ImVec2(px + s, py), ImVec2(px, py + thin), cu);
		}
	}

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
}
