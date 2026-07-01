#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// cutie-base의 기본값. theme/* 브랜치는 이 리터럴만 교체한다.
	const CutieTheme g_cutie_theme = {
		/* name           */ "Cutie Base",
		/* bg_stop_a       */ ImVec4(1.00f, 0.85f, 0.90f, 1.0f), // 연분홍
		/* bg_stop_b       */ ImVec4(0.80f, 0.90f, 1.00f, 1.0f), // 연하늘
		/* panel           */ ImVec4(1.00f, 0.97f, 0.99f, 0.92f),
		/* panel_alt       */ ImVec4(1.00f, 0.92f, 0.96f, 1.0f),
		/* text            */ ImVec4(0.35f, 0.22f, 0.30f, 1.0f),
		/* text_dim        */ ImVec4(0.55f, 0.45f, 0.52f, 1.0f),
		/* border          */ ImVec4(1.00f, 0.75f, 0.85f, 0.6f),
		/* accent          */ ImVec4(1.00f, 0.65f, 0.80f, 0.85f),
		/* accent_hover    */ ImVec4(1.00f, 0.55f, 0.75f, 1.0f),
		/* accent_active   */ ImVec4(0.95f, 0.45f, 0.70f, 1.0f),
		/* glow            */ ImVec4(1.00f, 0.70f, 0.85f, 1.0f),
		/* particle_glyph  */ u8"✨",  // ✨
		/* particle_color  */ ImVec4(1.00f, 0.95f, 0.70f, 1.0f),
		/* particle_count  */ 24,
		/* particle_speed  */ 22.0f,
		/* bg_animated     */ true,
		/* bg_anim_speed   */ 12.0f,
		/* glow_intensity  */ 0.6f,
		/* rounding        */ 14.0f,
		/* frame_padding   */ ImVec2(12.0f, 8.0f),
		/* item_spacing    */ ImVec2(8.0f, 6.0f),
		/* maker_name      */ u8"정렬",       // 정렬
		/* recipient_name  */ "OOO",
		/* about_message   */ nullptr,
		/* discord_handle  */ "lovecat._.holic",
	};
}
