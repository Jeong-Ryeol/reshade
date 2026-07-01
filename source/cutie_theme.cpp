#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// theme/rainbow
	const CutieTheme g_cutie_theme = {
		/* name           */ "Rainbow RGB",
		/* bg_stop_a       */ ImVec4(1.00f, 0.55f, 0.80f, 1.0f),
		/* bg_stop_b       */ ImVec4(0.55f, 0.75f, 1.00f, 1.0f),
		/* panel           */ ImVec4(1.00f, 0.98f, 1.00f, 0.90f),
		/* panel_alt       */ ImVec4(0.98f, 0.94f, 1.00f, 1.0f),
		/* text            */ ImVec4(0.30f, 0.20f, 0.35f, 1.0f),
		/* text_dim        */ ImVec4(0.52f, 0.44f, 0.58f, 1.0f),
		/* border          */ ImVec4(0.90f, 0.70f, 1.00f, 0.65f),
		/* accent          */ ImVec4(0.85f, 0.55f, 1.00f, 0.88f),
		/* accent_hover    */ ImVec4(0.75f, 0.45f, 1.00f, 1.0f),
		/* accent_active   */ ImVec4(0.65f, 0.35f, 0.95f, 1.0f),
		/* glow            */ ImVec4(1.00f, 0.60f, 0.90f, 1.0f),
		/* particle_glyph  */ u8"star",
		/* particle_color  */ ImVec4(1.00f, 1.00f, 0.85f, 1.0f),
		/* particle_count  */ 40,
		/* particle_speed  */ 30.0f,
		/* bg_animated     */ true,
		/* bg_anim_speed   */ 60.0f,
		/* glow_intensity  */ 1.0f,
		/* rounding        */ 16.0f,
		/* frame_padding   */ ImVec2(12.0f, 8.0f),
		/* item_spacing    */ ImVec2(8.0f, 6.0f),
		/* maker_name      */ u8"정렬",
		/* recipient_name  */ "OOO",
		/* about_message   */ nullptr,
		/* discord_handle  */ "lovecat._.holic",
	};
}
