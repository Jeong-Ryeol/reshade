#include "cutie_theme.hpp"

namespace reshade::cutie
{
	// theme/mint
	const CutieTheme g_cutie_theme = {
		/* name           */ "Mint Soda",
		/* bg_stop_a       */ ImVec4(0.68f, 1.00f, 0.86f, 1.0f),
		/* bg_stop_b       */ ImVec4(0.95f, 1.00f, 0.90f, 1.0f),
		/* panel           */ ImVec4(0.96f, 1.00f, 0.98f, 0.93f),
		/* panel_alt       */ ImVec4(0.90f, 0.99f, 0.94f, 1.0f),
		/* text            */ ImVec4(0.18f, 0.36f, 0.30f, 1.0f),
		/* text_dim        */ ImVec4(0.42f, 0.56f, 0.50f, 1.0f),
		/* border          */ ImVec4(0.60f, 0.90f, 0.78f, 0.65f),
		/* accent          */ ImVec4(0.45f, 0.85f, 0.70f, 0.88f),
		/* accent_hover    */ ImVec4(0.36f, 0.80f, 0.64f, 1.0f),
		/* accent_active   */ ImVec4(0.28f, 0.72f, 0.56f, 1.0f),
		/* glow            */ ImVec4(0.55f, 0.95f, 0.80f, 1.0f),
		/* particle_glyph  */ u8"leaf",
		/* particle_color  */ ImVec4(0.85f, 1.00f, 0.92f, 1.0f),
		/* particle_count  */ 24,
		/* particle_speed  */ 20.0f,
		/* bg_animated     */ false,
		/* bg_anim_speed   */ 0.0f,
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
