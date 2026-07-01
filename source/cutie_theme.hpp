#pragma once

#include <imgui.h>

namespace reshade::cutie
{
	// 하나의 테마를 완전히 기술하는 구조체. theme/* 브랜치는 g_cutie_theme 값만 바꾼다.
	struct CutieTheme
	{
		const char *name;            // "Peach Pastel" 등

		// --- 팔레트 ---
		ImVec4 bg_stop_a;            // 배경 그라디언트 시작색
		ImVec4 bg_stop_b;            // 배경 그라디언트 끝색
		ImVec4 panel;                // 창/자식 배경
		ImVec4 panel_alt;            // 프레임(입력창/슬라이더) 배경
		ImVec4 text;                 // 본문 텍스트
		ImVec4 text_dim;             // 흐린 텍스트
		ImVec4 border;               // 테두리

		// --- 강조 ---
		ImVec4 accent;               // 버튼/헤더/탭 기본
		ImVec4 accent_hover;
		ImVec4 accent_active;
		ImVec4 glow;                 // 글로우 색

		// --- 파티클 ---
		const char *particle_glyph;  // "✨" 등 (UTF-8)
		ImVec4 particle_color;
		int    particle_count;       // 화면당 파티클 수
		float  particle_speed;       // 떠오르는 속도(px/s)

		// --- 애니메이션 ---
		bool   bg_animated;          // 배경 색 순환 여부
		float  bg_anim_speed;        // 색 순환 속도(deg/s), rainbow용
		float  glow_intensity;       // 0~1

		// --- 라운드/여백 배율 ---
		float  rounding;             // 위젯 라운드 반경
		ImVec2 frame_padding;
		ImVec2 item_spacing;

		// --- 개인화(About) ---
		const char *maker_name;      // "정렬"
		const char *recipient_name;  // "OOO" (브랜치별로 채움)
		const char *about_message;   // 추가 문구(선택), 없으면 nullptr
		const char *discord_handle;  // "lovecat._.holic"
	};

	// 현재 빌드의 활성 테마. 정의는 cutie_theme.cpp (브랜치별 유일 차이 지점).
	extern const CutieTheme g_cutie_theme;
}
