#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// theme/peach
	const CutieTheme g_cutie_theme = {
		/* name           */ "Peach Pastel",
		/* bg_stop_a       */ ImVec4(1.00f, 0.80f, 0.72f, 1.0f),
		/* bg_stop_b       */ ImVec4(0.82f, 0.86f, 1.00f, 1.0f),
		/* panel           */ ImVec4(1.00f, 0.97f, 0.95f, 0.92f),
		/* panel_alt       */ ImVec4(1.00f, 0.93f, 0.90f, 1.0f),
		/* text            */ ImVec4(0.38f, 0.26f, 0.24f, 1.0f),
		/* text_dim        */ ImVec4(0.58f, 0.48f, 0.46f, 1.0f),
		/* border          */ ImVec4(1.00f, 0.78f, 0.70f, 0.6f),
		/* accent          */ ImVec4(1.00f, 0.70f, 0.62f, 0.85f),
		/* accent_hover    */ ImVec4(1.00f, 0.62f, 0.54f, 1.0f),
		/* accent_active   */ ImVec4(0.98f, 0.54f, 0.48f, 1.0f),
		/* glow            */ ImVec4(1.00f, 0.78f, 0.70f, 1.0f),
		/* particle_glyph  */ u8"flower",
		/* particle_color  */ ImVec4(1.00f, 0.92f, 0.80f, 1.0f),
		/* particle_count  */ 26,
		/* particle_speed  */ 22.0f,
		/* bg_animated     */ true,
		/* bg_anim_speed   */ 10.0f,
		/* glow_intensity  */ 0.6f,
		/* rounding        */ 14.0f,
		/* frame_padding   */ ImVec2(12.0f, 8.0f),
		/* item_spacing    */ ImVec2(8.0f, 6.0f),
		/* maker_name      */ u8"정렬",
		/* recipient_name  */ "OOO",
		/* about_message   */ nullptr,
		/* discord_handle  */ "lovecat._.holic",
	};
}
