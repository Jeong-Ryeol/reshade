#pragma once

#include <imgui.h>
#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// ImGuiStyle 전체를 테마 색/라운드/여백으로 덮어쓴다. 매 프레임 호출 가능.
	void apply_style(ImGuiStyle &style, const CutieTheme &t);

	// 오버레이 뒤 애니메이션 그라디언트 배경을 그린다.
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);
	// base 색상의 색조(hue)를 hue_deg만큼 회전시킨다 (rainbow 애니메이션용).
	ImU32 hsv_shift(const ImVec4 &base, float hue_deg);

	// 테마색 반짝이 파티클을 아래에서 위로 떠오르게 그린다.
	void draw_sparkles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const CutieTheme &t, float time_sec);
}
