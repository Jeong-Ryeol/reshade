/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <imgui.h>
#include <string>
#include "sherbet_theme.hpp"

namespace sherbet
{
	// 레이아웃 상수 (runtime_gui.cpp 와 rail_button 이 공유)
	constexpr float rail_width = 66.0f;
	constexpr float rail_button_size = 44.0f;
	constexpr float header_height = 40.0f;

	// color 의 RGB 는 유지하고 알파(0..255)만 교체
	inline ImU32 with_alpha(ImU32 color, ImU32 alpha) { return (color & 0x00FFFFFF) | (alpha << 24); }

	// 현재 런타임에서 활성화된 테마 (초기값 = SHERBET_DEFAULT_THEME)
	const theme &active_theme();
	const char *active_theme_id();
	void set_active_theme(const char *id);

	bool is_unlocked(const char *id);
	void mark_entitled(const char *id);   // 서버가 내려준 엔타이틀 테마 id 등록
	void clear_entitlements();            // 재페치 전 초기화
	void apply_content(const std::string &body); // /content/me 응답(JSON) 반영

	// 테마 색/라운드/간격을 ImGui 스타일에 적용 (매 프레임 또는 테마 변경 시 호출)
	void apply_style(ImGuiStyle &style, const theme &t);

	// 그라디언트 + 부드럽게 움직이는 오로라 블롭 배경. rounding>0 이면 둥근 모서리 패널.
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time, float rounding = 0.0f);
	// 테마 파티클(스파클/하트/잎)을 위로 떠오르게
	void draw_particles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time);
	// 사각형 뒤 부드러운 글로우
	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color);

	// 필형 토글 스위치. 값이 변경되면 true 반환.
	bool toggle(const char *label, bool *v);

	// 라운드 패널 차일드 시작/종료(테마 panel 배경 + border + 패딩). height 0 = 자동.
	void begin_card(const char *id, float height = 0.0f);
	void end_card();
	// 필형 버튼. 클릭 시 true.
	bool pill_button(const char *label, bool active);
	// 네비게이션 레일용 44x44 라운드 아이콘 버튼(활성 시 accent 배경+글로우). 클릭 시 true.
	bool rail_button(const char *id, const char *icon, bool active);
}
