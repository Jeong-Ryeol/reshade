/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <cstddef>
#include <vector>
#include <imgui.h>

namespace sherbet
{
	enum class particle { spark, heart, leaf, petal };

	struct theme
	{
		const char *id;
		const char *display_name;
		ImU32 bg0, bg1, bg2;      // 배경 그라디언트 스톱
		ImU32 panel, panel_alt;   // 카드/패널
		ImU32 chip, border;       // 칩/테두리
		ImU32 text, text_dim;     // 본문/흐린 텍스트
		ImU32 accent, accent2;    // 강조(그라디언트 2색)
		ImU32 glow;               // 글로우(알파 포함)
		particle particle_shape;
		bool hue_cycle;           // rainbow 전용 색상 순환
	};

	const theme &default_theme();
	const theme *find_theme(const char *id);
	const theme *all_themes(std::size_t &count);

	struct parsed_theme; // forward (sherbet_theme_json.hpp)
	std::vector<const theme *> themes_snapshot();
	void add_dynamic_theme(const parsed_theme &pt);
	void clear_dynamic_themes();
}
