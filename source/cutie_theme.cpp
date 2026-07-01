#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// theme/pink
	const CutieTheme g_cutie_theme = {
		/* name           */ "Pink Heart",
		/* bg_stop_a       */ ImVec4(1.00f, 0.45f, 0.65f, 1.0f),
		/* bg_stop_b       */ ImVec4(1.00f, 0.78f, 0.72f, 1.0f),
		/* panel           */ ImVec4(1.00f, 0.95f, 0.97f, 0.93f),
		/* panel_alt       */ ImVec4(1.00f, 0.90f, 0.94f, 1.0f),
		/* text            */ ImVec4(0.42f, 0.16f, 0.28f, 1.0f),
		/* text_dim        */ ImVec4(0.62f, 0.42f, 0.52f, 1.0f),
		/* border          */ ImVec4(1.00f, 0.60f, 0.75f, 0.65f),
		/* accent          */ ImVec4(1.00f, 0.40f, 0.62f, 0.88f),
		/* accent_hover    */ ImVec4(1.00f, 0.30f, 0.56f, 1.0f),
		/* accent_active   */ ImVec4(0.92f, 0.20f, 0.48f, 1.0f),
		/* glow            */ ImVec4(1.00f, 0.45f, 0.68f, 1.0f),
		/* particle_glyph  */ u8"heart",
		/* particle_color  */ ImVec4(1.00f, 0.80f, 0.90f, 1.0f),
		/* particle_count  */ 30,
		/* particle_speed  */ 24.0f,
		/* bg_animated     */ false,
		/* bg_anim_speed   */ 0.0f,
		/* glow_intensity  */ 0.85f,
		/* rounding        */ 16.0f,
		/* frame_padding   */ ImVec2(12.0f, 8.0f),
		/* item_spacing    */ ImVec2(8.0f, 6.0f),
		/* maker_name      */ u8"정렬",
		/* recipient_name  */ "OOO",
		/* about_message   */ nullptr,
		/* discord_handle  */ "lovecat._.holic",
	};
}
