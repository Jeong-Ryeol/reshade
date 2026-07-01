#pragma once

#include <imgui.h>
#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// ImGuiStyle 전체를 테마 색/라운드/여백으로 덮어쓴다. 매 프레임 호출 가능.
	void apply_style(ImGuiStyle &style, const CutieTheme &t);
}
