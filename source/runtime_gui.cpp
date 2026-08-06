/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if RESHADE_GUI

#include "runtime.hpp"
#include "runtime_internal.hpp"
#include "version.h"
#include "dll_log.hpp"
#include "dll_resources.hpp"
#include "ini_file.hpp"
#include "addon_manager.hpp"
#include "input.hpp"
#include "imgui_widgets.hpp"
#include "localization.hpp"
#include "platform_utils.hpp"
#include "fonts/forkawesome.inl"
#include "sherbet_theme.hpp"
#include "sherbet_ui.hpp"
#include "sherbet_owner.h"
#include "sherbet_nodelock.hpp"
#include "sherbet_update.hpp"
#include "sherbet_license.hpp"
#include "sherbet_paid.hpp"
#include <stb_image.h> // 커스텀 조준점 PNG 로딩
#include <fstream>
#include <iterator> // std::istreambuf_iterator (조준점 파일 읽기)
#include <vector> // 조준점 폴더 파일 목록
#include <cmath> // std::abs, std::ceil, std::floor
#include <cctype> // std::tolower
#include <cstdlib> // std::strtol
#include <cstring> // std::memcmp, std::memcpy
#include <algorithm> // std::any_of, std::count_if, std::find, std::find_if, std::max, std::min, std::replace, std::rotate, std::search, std::swap, std::transform

extern bool resolve_path(std::filesystem::path &path, std::error_code &ec, const std::filesystem::path &base = g_reshade_base_path);

static bool string_contains(const std::string_view text, const std::string_view filter)
{
	return filter.empty() ||
		std::search(text.cbegin(), text.cend(), filter.cbegin(), filter.cend(),
			[](const char c1, const char c2) { // Search case-insensitive
				return (('a' <= c1 && c1 <= 'z') ? static_cast<char>(c1 - ' ') : c1) == (('a' <= c2 && c2 <= 'z') ? static_cast<char>(c2 - ' ') : c2);
			}) != text.cend();
}
static auto is_invalid_path_element(ImGuiInputTextCallbackData *data) -> int
{
	return data->EventChar == L'\"' || data->EventChar == L'*' || data->EventChar == L':' || data->EventChar == L'<' || data->EventChar == L'>' || data->EventChar == L'?' || data->EventChar == L'|';
}
static auto is_invalid_filename_element(ImGuiInputTextCallbackData *data) -> int
{
	// A file name cannot contain any of the following characters
	return is_invalid_path_element(data) || data->EventChar == L'/' || data->EventChar == L'\\';
}

template <typename F>
static void parse_errors(const std::string_view errors, F &&callback)
{
	for (size_t offset = 0, next; offset != std::string_view::npos; offset = next)
	{
		const size_t pos_error = errors.find(": ", offset);
		const size_t pos_error_line = errors.rfind('(', pos_error); // Paths can contain '(', but no ": ", so search backwards from the error location to find the line info
		if (pos_error == std::string_view::npos)
			break;

		const size_t pos_linefeed = errors.find('\n', pos_error);

		if (pos_error_line != std::string_view::npos && pos_error_line >= offset)
		{
			const std::string_view error_file = errors.substr(offset, pos_error_line - offset);
			const int error_line = static_cast<int>(std::strtol(errors.data() + pos_error_line + 1, nullptr, 10));
			const std::string_view error_text = errors.substr(pos_error + 2 /* skip space */, pos_linefeed - pos_error - 2);

			callback(error_file, error_line, error_text);
		}
		else
		{
			callback(std::string_view(), 0, errors.substr(offset, pos_linefeed - offset));
		}

		next = pos_linefeed != std::string_view::npos ? pos_linefeed + 1 : std::string_view::npos;
	}
}

template <typename T>
static std::string_view get_localized_annotation(T &object, const std::string_view ann_name, [[maybe_unused]] std::string language)
{
#if RESHADE_LOCALIZATION
	if (language.size() >= 2)
	{
		// Transform language name from e.g. 'en-US' to 'en_us'
		std::transform(language.begin(), language.end(), language.begin(),
			[](std::string::value_type c) {
				if (c == '-')
					return '_';
				return static_cast<std::string::value_type>(std::tolower(c));
			});

		for (int attempt = 0; attempt < 2; ++attempt)
		{
			const std::string_view localized_result = object.annotation_as_string(std::string(ann_name) + '_' + language);
			if (!localized_result.empty())
				return localized_result;
			else if (attempt == 0)
				language.erase(2); // Remove location information from language name, so that it e.g. becomes 'en'
		}
	}
#endif

	return object.annotation_as_string(ann_name);
}

static const ImVec4 COLOR_RED = ImColor(240, 100, 100);
static const ImVec4 COLOR_YELLOW = ImColor(204, 204, 0);

void reshade::runtime::init_gui()
{
	// Default shortcut: Home
	_overlay_key_data[0] = 0x24;
	_overlay_key_data[1] = false;
	_overlay_key_data[2] = false;
	_overlay_key_data[3] = false;

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	_imgui_context = ImGui::CreateContext();

	ImGuiIO &imgui_io = _imgui_context->IO;
	imgui_io.IniFilename = nullptr;
	imgui_io.ConfigFlags = ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
	imgui_io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

	ImGuiStyle &imgui_style = _imgui_context->Style;
	// Disable rounding by default
	imgui_style.GrabRounding = 0.0f;
	imgui_style.FrameRounding = 0.0f;
	imgui_style.ChildRounding = 0.0f;
	imgui_style.ScrollbarRounding = 0.0f;
	imgui_style.WindowRounding = 0.0f;
	imgui_style.WindowBorderSize = 0.0f;

	// Restore previous context in case this was called from a new runtime being created from an add-on event triggered by an existing runtime
	ImGui::SetCurrentContext(backup_context);
}
void reshade::runtime::deinit_gui()
{
	ImGui::DestroyContext(_imgui_context);
}

void reshade::runtime::build_font_atlas()
{
	_imgui_context->Style.FontSizeBase = _font_size;

	if (!_rebuild_font_atlas)
		return;

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	// Remove any existing fonts from atlas first
	ImFontAtlas *const atlas = _imgui_context->IO.Fonts;
	atlas->Clear();

	std::error_code ec;
	_default_font_path.clear();

#if RESHADE_LOCALIZATION
	std::string language = _selected_language;
	if (language.empty())
		language = resources::get_current_language();

	if (language.compare(0, 2, "ar") == 0 ||
		language.compare(0, 2, "bg") == 0 ||
		language.compare(0, 2, "pl") == 0 ||
		language.compare(0, 2, "ru") == 0 ||
		language.compare(0, 2, "sl") == 0 ||
		language.compare(0, 2, "tr") == 0 ||
		language.compare(0, 2, "th") == 0)
	{
		// Microsoft Sans Serif
		_default_font_path = L"C:\\Windows\\Fonts\\micross.ttf";
	}
	else
	if (language.compare(0, 2, "ja") == 0)
	{
		// Morisawa BIZ UDGothic Regular, available since Windows 10 October 2018 Update (1809) Build 17763.1
		_default_font_path = L"C:\\Windows\\Fonts\\BIZ-UDGothicR.ttc";
		// Fall back to MS Gothic if it does not exist
		if (!std::filesystem::exists(_default_font_path, ec))
			_default_font_path = L"C:\\Windows\\Fonts\\msgothic.ttc";
	}
	else
	if (language.compare(0, 2, "ko") == 0)
	{
		// Malgun Gothic
		_default_font_path = L"C:\\Windows\\Fonts\\malgun.ttf";
	}
	else
	if (language.compare(0, 2, "zh") == 0)
	{
		// Simplified Chinese (zh-CN, zh-SG, ...)
		if (language.find("HK") == std::string::npos && language.find("TW") == std::string::npos && language.find("Hant") == std::string::npos)
		{
			// Microsoft YaHei
			_default_font_path = L"C:\\Windows\\Fonts\\msyh.ttc";
			// Fall back to SimSun if it does not exist
			if (!std::filesystem::exists(_default_font_path, ec))
				_default_font_path = L"C:\\Windows\\Fonts\\simsun.ttc";
		}
		// Traditional Chinese (zh-HK, zh-TW, zh-Hant, ...)
		else
		{
			// Microsoft JhengHei
			_default_font_path = L"C:\\Windows\\Fonts\\msjh.ttc";
			// Fall back to MingLiU if it does not exist
			if (!std::filesystem::exists(_default_font_path, ec))
				_default_font_path = L"C:\\Windows\\Fonts\\mingliu.ttc";
		}
	}
#endif

	const auto add_font_from_file = [atlas](std::filesystem::path &font_path, const ImFontConfig *font_config, std::error_code &ec) -> bool {
		if (font_path.empty())
		{
			atlas->AddFontDefault(font_config);
			return true;
		}

		if (resolve_path(font_path, ec))
		{
			if (FILE *const file = _wfsopen(font_path.c_str(), L"rb", SH_DENYNO))
			{
				fseek(file, 0, SEEK_END);
				const size_t file_size = ftell(file);
				fseek(file, 0, SEEK_SET);

				void *data = IM_ALLOC(file_size);
				const size_t file_size_read = fread(data, 1, file_size, file);
				fclose(file);

				if (file_size_read != file_size)
					IM_FREE(data);
				else if (atlas->AddFontFromMemoryTTF(data, static_cast<int>(file_size), 0.0f, font_config))
					return true;
			}
		}

		// Use default font if custom font failed to load
		atlas->AddFontDefault(font_config);
		return false;
	};

	ImFontConfig cfg;
	std::filesystem::path resolved_font_path;

	// SHERBET: 기본 빌드는 Latin/숫자 베이스로 임베드 Fredoka(동글동글 귀여운) 사용.
	// 병합 순서상 먼저 온 폰트가 해당 글자를 담당하므로, Fredoka 를 먼저 깔면
	// 숫자/영어=Fredoka, 뒤에 병합되는 Jua 는 한글만 채운다.
	const bool sherbet_font = _font_path.empty();
	if (sherbet_font)
	{
		ImFontConfig latin_cfg = cfg;
		const resources::data_resource fredoka = resources::load_data_resource(IDR_FONT_SHERBET_LATIN);
		void *fredoka_data = IM_ALLOC(fredoka.data_size);
		memcpy(fredoka_data, fredoka.data, fredoka.data_size);
		atlas->AddFontFromMemoryTTF(fredoka_data, static_cast<int>(fredoka.data_size), 0.0f, &latin_cfg);

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
	}

#if RESHADE_LOCALIZATION
	// Add latin font (Sherbet 기본 폰트를 쓸 땐 Fredoka 가 이미 Latin 베이스라 생략)
	if (!sherbet_font)
	{
		resolved_font_path = _latin_font_path;
		if (!_default_font_path.empty())
		{
			if (!add_font_from_file(resolved_font_path, &cfg, ec))
				log::message(log::level::error, "Failed to load latin font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());

			cfg.MergeMode = true;
			cfg.PixelSnapH = true;
		}
	}
#endif

	// Add main font — SHERBET: 사용자 지정 폰트가 없으면 임베드 Jua 사용(한글 포함)
	if (_font_path.empty())
	{
		ImFontConfig jua_cfg = cfg;
		const resources::data_resource jua = resources::load_data_resource(IDR_FONT_SHERBET_BODY);
		// 아틀라스는 데이터를 소유하지 않으므로 복사본을 넘긴다
		void *jua_data = IM_ALLOC(jua.data_size);
		memcpy(jua_data, jua.data, jua.data_size);
		atlas->AddFontFromMemoryTTF(jua_data, static_cast<int>(jua.data_size), 0.0f, &jua_cfg);

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}
	else
	{
		resolved_font_path = _font_path;
		if (!add_font_from_file(resolved_font_path, &cfg, ec))
			log::message(log::level::error, "Failed to load font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());

		cfg.MergeMode = true;
		cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &cfg);
	}

	// Add editor font
	resolved_font_path = _editor_font_path.empty() ? _default_editor_font_path : _editor_font_path;
	if (resolved_font_path != _font_path)
	{
		if (!add_font_from_file(resolved_font_path, nullptr, ec))
			log::message(log::level::error, "Failed to load editor font from '%s' with error code %d!", resolved_font_path.u8string().c_str(), ec.value());
	}

	// SHERBET 제목 폰트(Gaegu) — merge 아님, 별도 폰트
	{
		ImFontConfig title_cfg;
		title_cfg.MergeMode = false;
		const resources::data_resource gaegu = resources::load_data_resource(IDR_FONT_SHERBET_TITLE);
		void *gaegu_data = IM_ALLOC(gaegu.data_size);
		memcpy(gaegu_data, gaegu.data, gaegu.data_size);
		_sherbet_title_font = atlas->AddFontFromMemoryTTF(gaegu_data, static_cast<int>(gaegu.data_size), _font_size * 1.4f, &title_cfg);

		// ⚠️ 아이콘 글리프를 **제목 폰트에도** 병합한다(본문 폰트에만 병합하면 부족하다).
		// 안 하면 제목 폰트를 Push 한 상태에서 그린 ICON_FK_* 가 전부 '?' 로 나온다.
		// 해당되는 곳: 「에임」·「최적화」·「Market」 탭 제목, 로그인 패널, 노드락 안내, 롤백 화면 —
		// 하필 제품에서 가장 눈에 띄는 여섯 군데다(2026-07-31 실물 스크린샷으로 확인).
		// MergeMode 는 **직전에 추가한 폰트**에 붙으므로 반드시 Gaegu 바로 뒤에 와야 한다.
		ImFontConfig title_icon_cfg;
		title_icon_cfg.MergeMode = true;
		title_icon_cfg.PixelSnapH = true;
		atlas->AddFontFromMemoryCompressedBase85TTF(FONT_ICON_BUFFER_NAME_FK, 0.0f, &title_icon_cfg);

		// ⚠️ 아이콘 병합만으로는 부족하다. Gaegu cmap 에는 라틴 문장부호가 통째로 없다 —
		//    직접 파싱으로 확인: U+00B7(·) gid=0, U+2014(—) gid=0. ForkAwesome 은 0xF001~0xF32F
		//    범위뿐이라 그 구멍을 못 메운다. ImGui 1.92 는 폰트 간 폴백을 하지 않고
		//    FallbackChar 후보 {U+FFFD, '?', ' '} 중 첫 히트를 쓰는데 Gaegu 엔 U+FFFD 도 없어
		//    **'?' 로 확정 렌더**된다. 그래서 스플래시 제목이 "Sherbet <테마> ? by 정렬" 로 나왔다
		//    (오버레이를 한 번도 안 여는 구매자가 보는 유일한 화면이다).
		//    Fredoka 에는 U+00B7 gid=222, U+2014 gid=230 이 있어 구멍만 메워 준다 —
		//    먼저 추가된 Gaegu 가 우선이므로 한글·라틴의 **모양은 바뀌지 않는다**.
		// ⚠️ 위 Fredoka 포인터(fredoka_data)를 재사용하면 아틀라스가 두 번 free 해 크래시한다.
		//    반드시 새로 IM_ALLOC 한 복사본을 넘긴다.
		ImFontConfig title_fill_cfg;
		title_fill_cfg.MergeMode = true;
		title_fill_cfg.PixelSnapH = true;
		const resources::data_resource title_fill = resources::load_data_resource(IDR_FONT_SHERBET_LATIN);
		void *title_fill_data = IM_ALLOC(title_fill.data_size);
		memcpy(title_fill_data, title_fill.data, title_fill.data_size);
		atlas->AddFontFromMemoryTTF(title_fill_data, static_cast<int>(title_fill.data_size), 0.0f, &title_fill_cfg);
	}

	ImGui::SetCurrentContext(backup_context);

	_rebuild_font_atlas = false;
}

void reshade::runtime::load_config_gui(const ini_file &config)
{
	if (_input_gamepad != nullptr)
		_imgui_context->IO.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	else
		_imgui_context->IO.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;

	const auto config_get = [&config](const std::string &section, const std::string &key, auto &values) {
		if (config.get(section, key, values))
			return true;
		// Fall back to global configuration when an entry does not exist in the local configuration
		return global_config().get(section, key, values);
	};

	config_get("INPUT", "KeyOverlay", _overlay_key_data);
	config_get("INPUT", "KeyFPS", _fps_key_data);
	config_get("INPUT", "KeyFrameTime", _frametime_key_data);
	config_get("INPUT", "InputProcessing", _input_processing_mode);

#if RESHADE_LOCALIZATION
	config_get("OVERLAY", "Language", _selected_language);
#endif

	config.get("OVERLAY", "ClockFormat", _clock_format);
	config.get("OVERLAY", "SherbetOsdX", _sherbet_osd_x);
	config.get("OVERLAY", "SherbetOsdY", _sherbet_osd_y);
	config.get("OVERLAY", "SherbetOsdHorizontal", _sherbet_osd_horizontal);
	// SHERBET: 스프레이 트레이너. 두 토글은 독립이고 기본은 둘 다 꺼짐.
	config.get("OVERLAY", "SherbetSprayLive", _sherbet_spray_live);
	config.get("OVERLAY", "SherbetSprayChart", _sherbet_spray_chart);
	config.get("OVERLAY", "SherbetSprayOverlay5", _sherbet_spray_overlay5);
	config.get("OVERLAY", "SherbetAlarmOn", _sherbet_alarm_on);
	config.get("OVERLAY", "SherbetAlarmHour", _sherbet_alarm_hour);
	config.get("OVERLAY", "SherbetAlarmMin", _sherbet_alarm_min);
	config.get("OVERLAY", "SherbetAlarmSecs", _sherbet_alarm_secs);
	config.get("OVERLAY", "SherbetAlarmText", _sherbet_alarm_text);
	config.get("OVERLAY", "SherbetMagOn", _sherbet_mag_on);
	config.get("OVERLAY", "SherbetMagZoom", _sherbet_mag_zoom);
	config.get("OVERLAY", "SherbetMagOpacity", _sherbet_mag_opacity);
	{
		float mr[4] = { _sherbet_mag_rect.x, _sherbet_mag_rect.y, _sherbet_mag_rect.w, _sherbet_mag_rect.h };
		config.get("OVERLAY", "SherbetMagRect", mr);
		// 손으로 고친 ini 여도 화면 밖/0크기가 되지 않게 자른다.
		_sherbet_mag_rect = sherbet::mag::sanitize(sherbet::mag::rect{ mr[0], mr[1], mr[2], mr[3] });
	}
	config.get("OVERLAY", "SherbetMagAnchor", _sherbet_mag_anchor);
	config.get("OVERLAY", "SherbetMagCapRes", _sherbet_mag_cap_res);
	// 손으로 고친 ini 가 23:99 여도 '절대 안 울리는' 상태가 되지 않게 여기서 자른다.
	sherbet::alarm::clamp_time(_sherbet_alarm_hour, _sherbet_alarm_min);
	config.get("OVERLAY", "SherbetSprayScale", _sherbet_spray_scale);
	config.get("OVERLAY", "SherbetSprayGapMs", _sherbet_spray_gap_ms);
	// 손으로 고친 ini 방어 — 배율 0 이면 궤적이 한 점으로 뭉치고, 간격이 0 이면 매 프레임
	// 구간이 끊겨 기록이 무의미해진다(기록기 쪽에서도 한 번 더 클램프한다).
	_sherbet_spray_scale = ImClamp(_sherbet_spray_scale, 0.05f, 10.0f);
	_sherbet_spray_gap_ms = ImClamp(_sherbet_spray_gap_ms, 50, 2000);
	// SHERBET: 잠금 화면 미리보기(판매자 확인용). 기본 꺼짐.
	config.get("OVERLAY", "SherbetLockPreview", _sherbet_lock_preview);
	// SHERBET(실험): 화면 이동 추정. **기본 꺼짐** — 유일하게 화면 픽셀을 읽는 기능이라
	// 켜지 않은 구매자는 리드백 비용을 한 푼도 내지 않아야 한다.
	config.get("OVERLAY", "SherbetMotionSpike", _sherbet_motion_on);
	config.get("OVERLAY", "SherbetMotionHud", _sherbet_motion_hud);
	config.get("OVERLAY", "SherbetMotionLag", _sherbet_motion_lag);
	// 보정 상한이 +1 인 이유: 링에 아직 안 적힌 '미래' 프레임의 마우스 값을 가리키게 된다.
	_sherbet_motion_lag = ImClamp(_sherbet_motion_lag, -2, 1);
	// SHERBET: 에임 트레이너. 손으로 고친 ini 방어 — 회전값이 0 이면 표적이 영영 안 움직이고
	// 난이도/시간 번호가 범위를 벗어나면 tuning_for 가 기본값으로 떨어져 조용히 다른 판이 된다.
	config.get("OVERLAY", "SherbetAimLevel", _sherbet_aim_level);
	config.get("OVERLAY", "SherbetAimDuration", _sherbet_aim_duration);
	config.get("OVERLAY", "SherbetAimDpc", _sherbet_aim_dpc);
	config.get("OVERLAY", "SherbetAimFov", _sherbet_aim_fov);
	_sherbet_aim_level = ImClamp(_sherbet_aim_level, 0, sherbet::aim::kLevelCount - 1);
	_sherbet_aim_duration = ImClamp(_sherbet_aim_duration, 0, sherbet::aim::kDurationCount - 1);
	_sherbet_aim_dpc = ImClamp(_sherbet_aim_dpc, 0.002f, 0.200f);
	_sherbet_aim_fov = ImClamp(_sherbet_aim_fov, 40.0f, 120.0f);
	{
		// 최고 기록은 [난이도][시간] 을 한 줄로 편다. 길이가 안 맞으면 통째로 버린다 —
		// 반쯤 읽으면 엉뚱한 칸에 남의 기록이 들어간다.
		std::vector<int> aim_best;
		config.get("OVERLAY", "SherbetAimBest", aim_best);
		if (aim_best.size() == static_cast<std::size_t>(sherbet::aim::kLevelCount * sherbet::aim::kDurationCount))
			for (int li = 0; li < sherbet::aim::kLevelCount; ++li)
				for (int di = 0; di < sherbet::aim::kDurationCount; ++di)
				{
					const int v = aim_best[static_cast<std::size_t>(li * sherbet::aim::kDurationCount + di)];
					_sherbet_aim_best[li][di] = v > 0 ? v : 0;
				}
	}
	config.get("OVERLAY", "NoFontScaling", _no_font_scaling);
	config.get("OVERLAY", "ShowClock", _show_clock);
	config.get("OVERLAY", "ShowForceLoadEffectsButton", _show_force_load_effects_button);
	config.get("OVERLAY", "ShowFPS", _show_fps);
	config.get("OVERLAY", "ShowFrameTime", _show_frametime);
	config.get("OVERLAY", "ShowPresetName", _show_preset_name);
	// SHERBET: OSD 를 켬/끔 2단계로 단순화 — 예전 '오버레이일 때만'(값 2) 설정은 '항상'(1)으로 이관
	if (_show_clock > 1) _show_clock = 1;
	if (_show_fps > 1) _show_fps = 1;
	if (_show_frametime > 1) _show_frametime = 1;
	if (_show_preset_name > 1) _show_preset_name = 1;
	config.get("OVERLAY", "ShowScreenshotMessage", _show_screenshot_message);
	if (!global_config().get("OVERLAY", "TutorialProgress", _tutorial_index))
		config.get("OVERLAY", "TutorialProgress", _tutorial_index);
	config.get("OVERLAY", "VariableListHeight", _variable_editor_height);
	config.get("OVERLAY", "VariableListUseTabs", _variable_editor_tabs);
	config.get("OVERLAY", "AutoSavePreset", _auto_save_preset);
	config.get("OVERLAY", "ShowPresetTransitionMessage", _show_preset_transition_message);

	{ std::string s; config.get("SHERBET", "ActiveTheme", s); if (!s.empty()) sherbet::set_active_theme(s.c_str());
	  config.get("SHERBET", "EffectFilter", _sherbet_effect_filter);
	  std::string fav; config.get("SHERBET", "Favorites", fav);
	  _sherbet_fav.clear();
	  for (size_t p = 0, e; p <= fav.size(); p = e + 1) { e = fav.find(',', p); if (e == std::string::npos) e = fav.size(); if (e > p) _sherbet_fav.insert(fav.substr(p, e - p)); }
	  // 커스텀 조준점 설정
	  config.get("SHERBET", "CrosshairOn", _sherbet_crosshair_on);
	  config.get("SHERBET", "CrosshairBuiltin", _sherbet_crosshair_builtin);
	  config.get("SHERBET", "CrosshairFile", _sherbet_crosshair_file);
	  config.get("SHERBET", "CrosshairSize", _sherbet_crosshair_size);
	  config.get("SHERBET", "CrosshairThick", _sherbet_crosshair_thick);
	  config.get("SHERBET", "CrosshairGap", _sherbet_crosshair_gap);
	  config.get("SHERBET", "CrosshairOpacity", _sherbet_crosshair_opacity);
	  config.get("SHERBET", "CrosshairOffset", _sherbet_crosshair_off);
	  config.get("SHERBET", "CrosshairColor", _sherbet_crosshair_col);
	  _sherbet_crosshair_dirty = true; // 로드 후 이미지 재로딩 예약
	  // 발로란트 조준점 — 설정 전체가 공유 코드 문자열 하나다(설계 §3.8).
	  // 코드가 망가져 있으면 파싱이 out 을 건드리지 않으므로 프로필은 전 기본값으로 남는다.
	  config.get("SHERBET", "ValOn", _sherbet_val_on);
	  config.get("SHERBET", "ValCode", _sherbet_val_code);
	  sherbet::crosshair::parse_code(_sherbet_val_code, _sherbet_val_profile);
	  config.get("SHERBET", "ValErrOn", _sherbet_val_err_on);
	  config.get("SHERBET", "ValMoveAccel", _sherbet_val_tune.move_accel);
	  config.get("SHERBET", "ValMoveDecel", _sherbet_val_tune.move_decel);
	  config.get("SHERBET", "ValDeadzone", _sherbet_val_tune.deadzone);
	  config.get("SHERBET", "ValWalkErrPx", _sherbet_val_tune.walk_err_px);
	  config.get("SHERBET", "ValRunErrPx", _sherbet_val_tune.run_err_px);
	  config.get("SHERBET", "ValWalkSpeed", _sherbet_val_tune.walk_speed);
	  config.get("SHERBET", "ValWalkKeyIsRun", _sherbet_val_tune.walk_key_means_run);
	  config.get("SHERBET", "ValFirePerShot", _sherbet_val_tune.fire_per_shot_px);
	  config.get("SHERBET", "ValFireMax", _sherbet_val_tune.fire_max_px);
	  config.get("SHERBET", "ValFireRate", _sherbet_val_tune.fire_rate_rpm);
	  config.get("SHERBET", "ValRecovery", _sherbet_val_tune.recovery_time);
	  config.get("SHERBET", "ValFadeDepth", _sherbet_val_tune.fade_depth);
	  config.get("SHERBET", "ValKeyFwd", _sherbet_val_key_fwd);
	  config.get("SHERBET", "ValKeyBack", _sherbet_val_key_back);
	  config.get("SHERBET", "ValKeyLeft", _sherbet_val_key_left);
	  config.get("SHERBET", "ValKeyRight", _sherbet_val_key_right);
	  config.get("SHERBET", "ValKeyWalk", _sherbet_val_key_walk);
	  config.get("SHERBET", "ValKeyPause", _sherbet_val_key_pause);
	  // 조준점 마켓 — 내 조준점 슬롯과 되돌리기 스냅샷. 인코딩/상한은 sherbet_xhmarket.hpp 가
	  // 전부 판정한다(맥에서 단위테스트되는 층). 여기서는 문자열 한 줄을 주고받을 뿐이다.
	  { std::string xh_locals;
	    config.get("SHERBET", "ValLocals", xh_locals);
	    _sherbet_xh_locals = sherbet::xhmarket::decode_locals(xh_locals);
	    _sherbet_xh_locals_dirty = true; // 설정을 다시 읽으면 카드도 다시 만든다
	    config.get("SHERBET", "ValUndoCode", _sherbet_xh_session.undo_code);
	    config.get("SHERBET", "ValAppliedId", _sherbet_xh_session.applied_id); }
	  // 커스텀 배경 이미지 설정(custompicture 기능 전용)
	  config.get("SHERBET", "BgOn", _sherbet_bg_on);
	  config.get("SHERBET", "BgFile", _sherbet_bg_file);
	  config.get("SHERBET", "BgOpacity", _sherbet_bg_opacity);
	  config.get("SHERBET", "BgDim", _sherbet_bg_dim);
	  _sherbet_bg_dirty = true; } // 로드 후 이미지 재로딩 예약

	ImGuiStyle &imgui_style = _imgui_context->Style;
	config.get("STYLE", "Alpha", imgui_style.Alpha);
	config.get("STYLE", "ChildRounding", imgui_style.ChildRounding);
	config.get("STYLE", "ColFPSText", _fps_col);
	config.get("STYLE", "EditorFont", _editor_font_path);
	config.get("STYLE", "EditorFontSize", _editor_font_size);
	config.get("STYLE", "EditorStyleIndex", _editor_style_index);
	config.get("STYLE", "Font", _font_path);
	config.get("STYLE", "FontSize", _font_size);
	config.get("STYLE", "FontScale", _imgui_context->Style.FontScaleMain);
	config.get("STYLE", "FPSScale", _fps_scale);
	config.get("STYLE", "FrameRounding", imgui_style.FrameRounding);
	config.get("STYLE", "GrabRounding", imgui_style.GrabRounding);
	config.get("STYLE", "LatinFont", _latin_font_path);
	config.get("STYLE", "PopupRounding", imgui_style.PopupRounding);
	config.get("STYLE", "ScrollbarRounding", imgui_style.ScrollbarRounding);
	config.get("STYLE", "StyleIndex", _style_index);
	config.get("STYLE", "TabRounding", imgui_style.TabRounding);
	config.get("STYLE", "WindowRounding", imgui_style.WindowRounding);
	config.get("STYLE", "HdrOverlayBrightness", _hdr_overlay_brightness);
	config.get("STYLE", "HdrOverlayOverwriteColorSpaceTo", reinterpret_cast<int &>(_hdr_overlay_overwrite_color_space));

	// For compatibility with older versions, set the alpha value if it is missing
	if (_fps_col[3] == 0.0f)
		_fps_col[3]  = 1.0f;

	load_custom_style();

	if (_imgui_context->SettingsLoaded)
		return;

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	// Call all pre-read handlers, before reading config data (since they affect state that is then updated in the read handlers below)
	for (ImGuiSettingsHandler &handler : _imgui_context->SettingsHandlers)
		if (handler.ReadInitFn)
			handler.ReadInitFn(_imgui_context, &handler);

	for (ImGuiSettingsHandler &handler : _imgui_context->SettingsHandlers)
	{
		if (std::vector<std::string> lines;
			config.get("OVERLAY", handler.TypeName, lines))
		{
			void *entry_data = nullptr;

			for (const std::string &line : lines)
			{
				if (line.empty())
					continue;

				if (line[0] == '[')
				{
					const size_t name_beg = line.find('[', 1) + 1;
					const size_t name_end = line.rfind(']');

					entry_data = handler.ReadOpenFn(_imgui_context, &handler, line.substr(name_beg, name_end - name_beg).c_str());
				}
				else
				{
					assert(entry_data != nullptr);
					handler.ReadLineFn(_imgui_context, &handler, entry_data, line.c_str());
				}
			}
		}
	}

	_imgui_context->SettingsLoaded = true;

	for (ImGuiSettingsHandler &handler : _imgui_context->SettingsHandlers)
		if (handler.ApplyAllFn)
			handler.ApplyAllFn(_imgui_context, &handler);

	ImGui::SetCurrentContext(backup_context);
}
void reshade::runtime::save_config_gui(ini_file &config) const
{
	config.set("INPUT", "KeyOverlay", _overlay_key_data);
	config.set("INPUT", "KeyFPS", _fps_key_data);
	config.set("INPUT", "KeyFrametime", _frametime_key_data);
	config.set("INPUT", "InputProcessing", _input_processing_mode);

#if RESHADE_LOCALIZATION
	config.set("OVERLAY", "Language", _selected_language);
#endif

	config.set("OVERLAY", "ClockFormat", _clock_format);
	config.set("OVERLAY", "SherbetOsdX", _sherbet_osd_x);
	config.set("OVERLAY", "SherbetOsdY", _sherbet_osd_y);
	config.set("OVERLAY", "SherbetOsdHorizontal", _sherbet_osd_horizontal);
	config.set("OVERLAY", "SherbetSprayLive", _sherbet_spray_live);
	config.set("OVERLAY", "SherbetSprayChart", _sherbet_spray_chart);
	config.set("OVERLAY", "SherbetSprayOverlay5", _sherbet_spray_overlay5);
	config.set("OVERLAY", "SherbetAlarmOn", _sherbet_alarm_on);
	config.set("OVERLAY", "SherbetAlarmHour", _sherbet_alarm_hour);
	config.set("OVERLAY", "SherbetAlarmMin", _sherbet_alarm_min);
	config.set("OVERLAY", "SherbetAlarmSecs", _sherbet_alarm_secs);
	config.set("OVERLAY", "SherbetAlarmText", _sherbet_alarm_text);
	config.set("OVERLAY", "SherbetMagOn", _sherbet_mag_on);
	config.set("OVERLAY", "SherbetMagZoom", _sherbet_mag_zoom);
	config.set("OVERLAY", "SherbetMagOpacity", _sherbet_mag_opacity);
	{
		const float mr[4] = { _sherbet_mag_rect.x, _sherbet_mag_rect.y, _sherbet_mag_rect.w, _sherbet_mag_rect.h };
		config.set("OVERLAY", "SherbetMagRect", mr);
	}
	config.set("OVERLAY", "SherbetMagAnchor", _sherbet_mag_anchor);
	config.set("OVERLAY", "SherbetMagCapRes", _sherbet_mag_cap_res);
	config.set("OVERLAY", "SherbetSprayScale", _sherbet_spray_scale);
	config.set("OVERLAY", "SherbetSprayGapMs", _sherbet_spray_gap_ms);
	config.set("OVERLAY", "SherbetLockPreview", _sherbet_lock_preview);
	config.set("OVERLAY", "SherbetMotionSpike", _sherbet_motion_on);
	config.set("OVERLAY", "SherbetMotionHud", _sherbet_motion_hud);
	config.set("OVERLAY", "SherbetMotionLag", _sherbet_motion_lag);
	// SHERBET: 에임 트레이너
	config.set("OVERLAY", "SherbetAimLevel", _sherbet_aim_level);
	config.set("OVERLAY", "SherbetAimDuration", _sherbet_aim_duration);
	config.set("OVERLAY", "SherbetAimDpc", _sherbet_aim_dpc);
	config.set("OVERLAY", "SherbetAimFov", _sherbet_aim_fov);
	{
		std::vector<int> aim_best;
		aim_best.reserve(static_cast<std::size_t>(sherbet::aim::kLevelCount * sherbet::aim::kDurationCount));
		for (int li = 0; li < sherbet::aim::kLevelCount; ++li)
			for (int di = 0; di < sherbet::aim::kDurationCount; ++di)
				aim_best.push_back(_sherbet_aim_best[li][di]);
		config.set("OVERLAY", "SherbetAimBest", aim_best);
	}
	config.set("OVERLAY", "ShowClock", _show_clock);
	config.set("OVERLAY", "ShowForceLoadEffectsButton", _show_force_load_effects_button);
	config.set("OVERLAY", "ShowFPS", _show_fps);
	config.set("OVERLAY", "ShowFrameTime", _show_frametime);
	config.set("OVERLAY", "ShowPresetName", _show_preset_name);
	config.set("OVERLAY", "ShowScreenshotMessage", _show_screenshot_message);
	global_config().set("OVERLAY", "TutorialProgress", _tutorial_index);
	config.set("OVERLAY", "TutorialProgress", _tutorial_index);
	config.set("OVERLAY", "VariableListHeight", _variable_editor_height);
	config.set("OVERLAY", "VariableListUseTabs", _variable_editor_tabs);
	config.set("OVERLAY", "AutoSavePreset", _auto_save_preset);
	config.set("OVERLAY", "ShowPresetTransitionMessage", _show_preset_transition_message);

	// ⚠️ active 가 아니라 desired 를 저장한다. 서버 테마(deepdark 등)는 /content/me 도착 전엔
	// 적용될 수 없어 active 가 mint 인데, 그걸 저장하면 구매자가 고른 값이 영구 파괴된다.
	// (tools/sherbet_theme_state_test.cpp 가 이 계약을 못 박는다)
	config.set("SHERBET", "ActiveTheme", std::string(sherbet::desired_theme_id()));
	config.set("SHERBET", "EffectFilter", _sherbet_effect_filter);
	{ std::string fav; for (const std::string &s : _sherbet_fav) { if (!fav.empty()) fav += ','; fav += s; } config.set("SHERBET", "Favorites", fav); }
	config.set("SHERBET", "CrosshairOn", _sherbet_crosshair_on);
	config.set("SHERBET", "CrosshairBuiltin", _sherbet_crosshair_builtin);
	config.set("SHERBET", "CrosshairFile", _sherbet_crosshair_file);
	config.set("SHERBET", "CrosshairSize", _sherbet_crosshair_size);
	config.set("SHERBET", "CrosshairThick", _sherbet_crosshair_thick);
	config.set("SHERBET", "CrosshairGap", _sherbet_crosshair_gap);
	config.set("SHERBET", "CrosshairOpacity", _sherbet_crosshair_opacity);
	config.set("SHERBET", "CrosshairOffset", _sherbet_crosshair_off);
	config.set("SHERBET", "CrosshairColor", _sherbet_crosshair_col);
	config.set("SHERBET", "ValOn", _sherbet_val_on);
	config.set("SHERBET", "ValCode", _sherbet_val_code);
	config.set("SHERBET", "ValErrOn", _sherbet_val_err_on);
	config.set("SHERBET", "ValMoveAccel", _sherbet_val_tune.move_accel);
	config.set("SHERBET", "ValMoveDecel", _sherbet_val_tune.move_decel);
	config.set("SHERBET", "ValDeadzone", _sherbet_val_tune.deadzone);
	config.set("SHERBET", "ValWalkErrPx", _sherbet_val_tune.walk_err_px);
	config.set("SHERBET", "ValRunErrPx", _sherbet_val_tune.run_err_px);
	config.set("SHERBET", "ValWalkSpeed", _sherbet_val_tune.walk_speed);
	config.set("SHERBET", "ValWalkKeyIsRun", _sherbet_val_tune.walk_key_means_run);
	config.set("SHERBET", "ValFirePerShot", _sherbet_val_tune.fire_per_shot_px);
	config.set("SHERBET", "ValFireMax", _sherbet_val_tune.fire_max_px);
	config.set("SHERBET", "ValFireRate", _sherbet_val_tune.fire_rate_rpm);
	config.set("SHERBET", "ValRecovery", _sherbet_val_tune.recovery_time);
	config.set("SHERBET", "ValFadeDepth", _sherbet_val_tune.fade_depth);
	config.set("SHERBET", "ValKeyFwd", _sherbet_val_key_fwd);
	config.set("SHERBET", "ValKeyBack", _sherbet_val_key_back);
	config.set("SHERBET", "ValKeyLeft", _sherbet_val_key_left);
	config.set("SHERBET", "ValKeyRight", _sherbet_val_key_right);
	config.set("SHERBET", "ValKeyWalk", _sherbet_val_key_walk);
	config.set("SHERBET", "ValKeyPause", _sherbet_val_key_pause);
	config.set("SHERBET", "ValLocals", sherbet::xhmarket::encode_locals(_sherbet_xh_locals));
	config.set("SHERBET", "ValUndoCode", _sherbet_xh_session.undo_code);
	config.set("SHERBET", "ValAppliedId", _sherbet_xh_session.applied_id);
	config.set("SHERBET", "BgOn", _sherbet_bg_on);
	config.set("SHERBET", "BgFile", _sherbet_bg_file);
	config.set("SHERBET", "BgOpacity", _sherbet_bg_opacity);
	config.set("SHERBET", "BgDim", _sherbet_bg_dim);

	const ImGuiStyle &imgui_style = _imgui_context->Style;
	config.set("STYLE", "Alpha", imgui_style.Alpha);
	config.set("STYLE", "ChildRounding", imgui_style.ChildRounding);
	config.set("STYLE", "ColFPSText", _fps_col);
	config.set("STYLE", "EditorFont", _editor_font_path);
	config.set("STYLE", "EditorFontSize", _editor_font_size);
	config.set("STYLE", "EditorStyleIndex", _editor_style_index);
	config.set("STYLE", "Font", _font_path);
	config.set("STYLE", "FontSize", _font_size);
	config.set("STYLE", "FontScale", _imgui_context->Style.FontScaleMain);
	config.set("STYLE", "FPSScale", _fps_scale);
	config.set("STYLE", "FrameRounding", imgui_style.FrameRounding);
	config.set("STYLE", "GrabRounding", imgui_style.GrabRounding);
	config.set("STYLE", "LatinFont", _latin_font_path);
	config.set("STYLE", "PopupRounding", imgui_style.PopupRounding);
	config.set("STYLE", "ScrollbarRounding", imgui_style.ScrollbarRounding);
	config.set("STYLE", "StyleIndex", _style_index);
	config.set("STYLE", "TabRounding", imgui_style.TabRounding);
	config.set("STYLE", "WindowRounding", imgui_style.WindowRounding);
	config.set("STYLE", "HdrOverlayBrightness", _hdr_overlay_brightness);
	config.set("STYLE", "HdrOverlayOverwriteColorSpaceTo", static_cast<int>(_hdr_overlay_overwrite_color_space));

	// Do not save custom style colors by default, only when actually used and edited

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	for (ImGuiSettingsHandler &handler : _imgui_context->SettingsHandlers)
	{
		ImGuiTextBuffer buffer;
		handler.WriteAllFn(_imgui_context, &handler, &buffer);

		std::vector<std::string> lines;
		for (int i = 0, offset = 0; i < buffer.size(); ++i)
		{
			if (buffer[i] == '\n')
			{
				lines.emplace_back(buffer.c_str() + offset, i - offset);
				offset = i + 1;
			}
		}

		if (!lines.empty())
			config.set("OVERLAY", handler.TypeName, lines);
	}

	ImGui::SetCurrentContext(backup_context);
}

void reshade::runtime::load_custom_style()
{
	const ini_file &config = ini_file::load_cache(_config_path);

	ImVec4 *const colors = _imgui_context->Style.Colors;
	switch (_style_index)
	{
	case 0:
		ImGui::StyleColorsDark(&_imgui_context->Style);
		break;
	case 1:
		ImGui::StyleColorsLight(&_imgui_context->Style);
		break;
	case 2:
		colors[ImGuiCol_Text] = ImVec4(0.862745f, 0.862745f, 0.862745f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.58f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.117647f, 0.117647f, 0.117647f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.156863f, 0.156863f, 0.156863f, 0.00f);
		colors[ImGuiCol_Border] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.30f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.156863f, 0.156863f, 0.156863f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.470588f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.588235f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.45f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.35f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.58f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.156863f, 0.156863f, 0.156863f, 0.57f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.156863f, 0.156863f, 0.156863f, 1.00f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.31f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.78f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.117647f, 0.117647f, 0.117647f, 0.92f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.80f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.784314f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.44f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.86f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.76f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.86f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.32f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.862745f, 0.862745f, 0.862745f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.20f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.78f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_Tab] = colors[ImGuiCol_Button];
		colors[ImGuiCol_TabSelected] = colors[ImGuiCol_ButtonActive];
		colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_ButtonActive];
		colors[ImGuiCol_TabHovered] = colors[ImGuiCol_ButtonHovered];
		colors[ImGuiCol_TabDimmed] = ImLerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
		colors[ImGuiCol_TabDimmedSelected] = ImLerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
		colors[ImGuiCol_TabDimmedSelectedOverline] = colors[ImGuiCol_TabDimmedSelected];
		colors[ImGuiCol_DockingPreview] = colors[ImGuiCol_Header] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.63f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.862745f, 0.862745f, 0.862745f, 0.63f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.392157f, 0.588235f, 0.941176f, 1.00f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.392157f, 0.588235f, 0.941176f, 0.43f);
		break;
	case 5:
		colors[ImGuiCol_Text] = ImColor(0xff969483);
		colors[ImGuiCol_TextDisabled] = ImColor(0xff756e58);
		colors[ImGuiCol_WindowBg] = ImColor(0xff362b00);
		colors[ImGuiCol_ChildBg] = ImColor();
		colors[ImGuiCol_PopupBg] = ImColor(0xfc362b00); // Customized
		colors[ImGuiCol_Border] = ImColor(0xff423607);
		colors[ImGuiCol_BorderShadow] = ImColor();
		colors[ImGuiCol_FrameBg] = ImColor(0xfc423607); // Customized
		colors[ImGuiCol_FrameBgHovered] = ImColor(0xff423607);
		colors[ImGuiCol_FrameBgActive] = ImColor(0xff423607);
		colors[ImGuiCol_TitleBg] = ImColor(0xff362b00);
		colors[ImGuiCol_TitleBgActive] = ImColor(0xff362b00);
		colors[ImGuiCol_TitleBgCollapsed] = ImColor(0xff362b00);
		colors[ImGuiCol_MenuBarBg] = ImColor(0xff423607);
		colors[ImGuiCol_ScrollbarBg] = ImColor(0xff362b00);
		colors[ImGuiCol_ScrollbarGrab] = ImColor(0xff423607);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(0xff423607);
		colors[ImGuiCol_ScrollbarGrabActive] = ImColor(0xff423607);
		colors[ImGuiCol_CheckMark] = ImColor(0xff756e58);
		colors[ImGuiCol_SliderGrab] = ImColor(0xff5e5025); // Customized
		colors[ImGuiCol_SliderGrabActive] = ImColor(0xff5e5025); // Customized
		colors[ImGuiCol_Button] = ImColor(0xff423607);
		colors[ImGuiCol_ButtonHovered] = ImColor(0xff423607);
		colors[ImGuiCol_ButtonActive] = ImColor(0xff362b00);
		colors[ImGuiCol_Header] = ImColor(0xff423607);
		colors[ImGuiCol_HeaderHovered] = ImColor(0xff423607);
		colors[ImGuiCol_HeaderActive] = ImColor(0xff423607);
		colors[ImGuiCol_Separator] = ImColor(0xff423607);
		colors[ImGuiCol_SeparatorHovered] = ImColor(0xff423607);
		colors[ImGuiCol_SeparatorActive] = ImColor(0xff423607);
		colors[ImGuiCol_ResizeGrip] = ImColor(0xff423607);
		colors[ImGuiCol_ResizeGripHovered] = ImColor(0xff423607);
		colors[ImGuiCol_ResizeGripActive] = ImColor(0xff756e58);
		colors[ImGuiCol_Tab] = ImColor(0xff362b00);
		colors[ImGuiCol_TabHovered] = ImColor(0xff423607);
		colors[ImGuiCol_TabSelected] = ImColor(0xff423607);
		colors[ImGuiCol_TabSelectedOverline] = ImColor(0xff423607);
		colors[ImGuiCol_TabDimmed] = ImColor(0xff362b00);
		colors[ImGuiCol_TabDimmedSelected] = ImColor(0xff423607);
		colors[ImGuiCol_TabDimmedSelectedOverline] = ImColor(0xff423607);
		colors[ImGuiCol_DockingPreview] = ImColor(0xee837b65); // Customized
		colors[ImGuiCol_DockingEmptyBg] = ImColor();
		colors[ImGuiCol_PlotLines] = ImColor(0xff756e58);
		colors[ImGuiCol_PlotLinesHovered] = ImColor(0xff756e58);
		colors[ImGuiCol_PlotHistogram] = ImColor(0xff756e58);
		colors[ImGuiCol_PlotHistogramHovered] = ImColor(0xff756e58);
		colors[ImGuiCol_TextSelectedBg] = ImColor(0xff756e58);
		colors[ImGuiCol_DragDropTarget] = ImColor(0xff756e58);
		colors[ImGuiCol_NavCursor] = ImColor();
		colors[ImGuiCol_NavWindowingHighlight] = ImColor(0xee969483); // Customized
		colors[ImGuiCol_NavWindowingDimBg] = ImColor(0x20e3f6fd); // Customized
		colors[ImGuiCol_ModalWindowDimBg] = ImColor(0x20e3f6fd); // Customized
		break;
	case 6:
		colors[ImGuiCol_Text] = ImColor(0xff837b65);
		colors[ImGuiCol_TextDisabled] = ImColor(0xffa1a193);
		colors[ImGuiCol_WindowBg] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_ChildBg] = ImColor();
		colors[ImGuiCol_PopupBg] = ImColor(0xfce3f6fd); // Customized
		colors[ImGuiCol_Border] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_BorderShadow] = ImColor();
		colors[ImGuiCol_FrameBg] = ImColor(0xfcd5e8ee); // Customized
		colors[ImGuiCol_FrameBgHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_FrameBgActive] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_TitleBg] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_TitleBgActive] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_TitleBgCollapsed] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_MenuBarBg] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ScrollbarBg] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_ScrollbarGrab] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ScrollbarGrabActive] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_CheckMark] = ImColor(0xffa1a193);
		colors[ImGuiCol_SliderGrab] = ImColor(0xffc3d3d9); // Customized
		colors[ImGuiCol_SliderGrabActive] = ImColor(0xffc3d3d9); // Customized
		colors[ImGuiCol_Button] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ButtonHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ButtonActive] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_Header] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_HeaderHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_HeaderActive] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_Separator] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_SeparatorHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_SeparatorActive] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ResizeGrip] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ResizeGripHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_ResizeGripActive] = ImColor(0xffa1a193);
		colors[ImGuiCol_Tab] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_TabHovered] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_TabSelected] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_TabSelectedOverline] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_TabDimmed] = ImColor(0xffe3f6fd);
		colors[ImGuiCol_TabDimmedSelected] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_TabDimmedSelectedOverline] = ImColor(0xffd5e8ee);
		colors[ImGuiCol_DockingPreview] = ImColor(0xeea1a193); // Customized
		colors[ImGuiCol_DockingEmptyBg] = ImColor();
		colors[ImGuiCol_PlotLines] = ImColor(0xffa1a193);
		colors[ImGuiCol_PlotLinesHovered] = ImColor(0xffa1a193);
		colors[ImGuiCol_PlotHistogram] = ImColor(0xffa1a193);
		colors[ImGuiCol_PlotHistogramHovered] = ImColor(0xffa1a193);
		colors[ImGuiCol_TextSelectedBg] = ImColor(0xffa1a193);
		colors[ImGuiCol_DragDropTarget] = ImColor(0xffa1a193);
		colors[ImGuiCol_NavCursor] = ImColor();
		colors[ImGuiCol_NavWindowingHighlight] = ImColor(0xee837b65); // Customized
		colors[ImGuiCol_NavWindowingDimBg] = ImColor(0x20362b00); // Customized
		colors[ImGuiCol_ModalWindowDimBg] = ImColor(0x20362b00); // Customized
		break;
	default:
		for (ImGuiCol i = 0; i < ImGuiCol_COUNT; i++)
			config.get("STYLE", ImGui::GetStyleColorName(i), (float(&)[4])colors[i]);
		break;
	}

	switch (_editor_style_index)
	{
	case 0: // Dark
		_editor_palette[imgui::code_editor::color_default] = 0xffffffff;
		_editor_palette[imgui::code_editor::color_keyword] = 0xffd69c56;
		_editor_palette[imgui::code_editor::color_number_literal] = 0xff00ff00;
		_editor_palette[imgui::code_editor::color_string_literal] = 0xff7070e0;
		_editor_palette[imgui::code_editor::color_punctuation] = 0xffffffff;
		_editor_palette[imgui::code_editor::color_preprocessor] = 0xff409090;
		_editor_palette[imgui::code_editor::color_identifier] = 0xffaaaaaa;
		_editor_palette[imgui::code_editor::color_known_identifier] = 0xff9bc64d;
		_editor_palette[imgui::code_editor::color_preprocessor_identifier] = 0xffc040a0;
		_editor_palette[imgui::code_editor::color_comment] = 0xff206020;
		_editor_palette[imgui::code_editor::color_multiline_comment] = 0xff406020;
		_editor_palette[imgui::code_editor::color_background] = 0xff101010;
		_editor_palette[imgui::code_editor::color_cursor] = 0xffe0e0e0;
		_editor_palette[imgui::code_editor::color_selection] = 0x80a06020;
		_editor_palette[imgui::code_editor::color_error_marker] = 0x800020ff;
		_editor_palette[imgui::code_editor::color_warning_marker] = 0x8000ffff;
		_editor_palette[imgui::code_editor::color_line_number] = 0xff707000;
		_editor_palette[imgui::code_editor::color_current_line_fill] = 0x40000000;
		_editor_palette[imgui::code_editor::color_current_line_fill_inactive] = 0x40808080;
		_editor_palette[imgui::code_editor::color_current_line_edge] = 0x40a0a0a0;
		break;
	case 1: // Light
		_editor_palette[imgui::code_editor::color_default] = 0xff000000;
		_editor_palette[imgui::code_editor::color_keyword] = 0xffff0c06;
		_editor_palette[imgui::code_editor::color_number_literal] = 0xff008000;
		_editor_palette[imgui::code_editor::color_string_literal] = 0xff2020a0;
		_editor_palette[imgui::code_editor::color_punctuation] = 0xff000000;
		_editor_palette[imgui::code_editor::color_preprocessor] = 0xff409090;
		_editor_palette[imgui::code_editor::color_identifier] = 0xff404040;
		_editor_palette[imgui::code_editor::color_known_identifier] = 0xff606010;
		_editor_palette[imgui::code_editor::color_preprocessor_identifier] = 0xffc040a0;
		_editor_palette[imgui::code_editor::color_comment] = 0xff205020;
		_editor_palette[imgui::code_editor::color_multiline_comment] = 0xff405020;
		_editor_palette[imgui::code_editor::color_background] = 0xffffffff;
		_editor_palette[imgui::code_editor::color_cursor] = 0xff000000;
		_editor_palette[imgui::code_editor::color_selection] = 0x80600000;
		_editor_palette[imgui::code_editor::color_error_marker] = 0xa00010ff;
		_editor_palette[imgui::code_editor::color_warning_marker] = 0x8000ffff;
		_editor_palette[imgui::code_editor::color_line_number] = 0xff505000;
		_editor_palette[imgui::code_editor::color_current_line_fill] = 0x40000000;
		_editor_palette[imgui::code_editor::color_current_line_fill_inactive] = 0x40808080;
		_editor_palette[imgui::code_editor::color_current_line_edge] = 0x40000000;
		break;
	case 3: // Solarized Dark
		_editor_palette[imgui::code_editor::color_default] = 0xff969483;
		_editor_palette[imgui::code_editor::color_keyword] = 0xff0089b5;
		_editor_palette[imgui::code_editor::color_number_literal] = 0xff98a12a;
		_editor_palette[imgui::code_editor::color_string_literal] = 0xff98a12a;
		_editor_palette[imgui::code_editor::color_punctuation] = 0xff969483;
		_editor_palette[imgui::code_editor::color_preprocessor] = 0xff164bcb;
		_editor_palette[imgui::code_editor::color_identifier] = 0xff969483;
		_editor_palette[imgui::code_editor::color_known_identifier] = 0xff969483;
		_editor_palette[imgui::code_editor::color_preprocessor_identifier] = 0xffc4716c;
		_editor_palette[imgui::code_editor::color_comment] = 0xff756e58;
		_editor_palette[imgui::code_editor::color_multiline_comment] = 0xff756e58;
		_editor_palette[imgui::code_editor::color_background] = 0xff362b00;
		_editor_palette[imgui::code_editor::color_cursor] = 0xff969483;
		_editor_palette[imgui::code_editor::color_selection] = 0xa0756e58;
		_editor_palette[imgui::code_editor::color_error_marker] = 0x7f2f32dc;
		_editor_palette[imgui::code_editor::color_warning_marker] = 0x7f0089b5;
		_editor_palette[imgui::code_editor::color_line_number] = 0xff756e58;
		_editor_palette[imgui::code_editor::color_current_line_fill] = 0x7f423607;
		_editor_palette[imgui::code_editor::color_current_line_fill_inactive] = 0x7f423607;
		_editor_palette[imgui::code_editor::color_current_line_edge] = 0x7f423607;
		break;
	case 4: // Solarized Light
		_editor_palette[imgui::code_editor::color_default] = 0xff837b65;
		_editor_palette[imgui::code_editor::color_keyword] = 0xff0089b5;
		_editor_palette[imgui::code_editor::color_number_literal] = 0xff98a12a;
		_editor_palette[imgui::code_editor::color_string_literal] = 0xff98a12a;
		_editor_palette[imgui::code_editor::color_punctuation] = 0xff756e58;
		_editor_palette[imgui::code_editor::color_preprocessor] = 0xff164bcb;
		_editor_palette[imgui::code_editor::color_identifier] = 0xff837b65;
		_editor_palette[imgui::code_editor::color_known_identifier] = 0xff837b65;
		_editor_palette[imgui::code_editor::color_preprocessor_identifier] = 0xffc4716c;
		_editor_palette[imgui::code_editor::color_comment] = 0xffa1a193;
		_editor_palette[imgui::code_editor::color_multiline_comment] = 0xffa1a193;
		_editor_palette[imgui::code_editor::color_background] = 0xffe3f6fd;
		_editor_palette[imgui::code_editor::color_cursor] = 0xff837b65;
		_editor_palette[imgui::code_editor::color_selection] = 0x60a1a193;
		_editor_palette[imgui::code_editor::color_error_marker] = 0x7f2f32dc;
		_editor_palette[imgui::code_editor::color_warning_marker] = 0x7f0089b5;
		_editor_palette[imgui::code_editor::color_line_number] = 0xffa1a193;
		_editor_palette[imgui::code_editor::color_current_line_fill] = 0x7fd5e8ee;
		_editor_palette[imgui::code_editor::color_current_line_fill_inactive] = 0x7fd5e8ee;
		_editor_palette[imgui::code_editor::color_current_line_edge] = 0x7fd5e8ee;
		break;
	case 2:
	default:
		ImVec4 value;
		for (ImGuiCol i = 0; i < imgui::code_editor::color_palette_max; i++)
			value = ImGui::ColorConvertU32ToFloat4(_editor_palette[i]), // Get default value first
			config.get("STYLE",  imgui::code_editor::get_palette_color_name(i), (float(&)[4])value),
			_editor_palette[i] = ImGui::ColorConvertFloat4ToU32(value);
		break;
	}
}
void reshade::runtime::save_custom_style() const
{
	ini_file &config = ini_file::load_cache(_config_path);

	if (_style_index == 3 || _style_index == 4) // Custom Simple, Custom Advanced
	{
		for (ImGuiCol i = 0; i < ImGuiCol_COUNT; i++)
			config.set("STYLE", ImGui::GetStyleColorName(i), (const float(&)[4])_imgui_context->Style.Colors[i]);
	}

	if (_editor_style_index == 2) // Custom
	{
		ImVec4 value;
		for (ImGuiCol i = 0; i < imgui::code_editor::color_palette_max; i++)
			value = ImGui::ColorConvertU32ToFloat4(_editor_palette[i]),
			config.set("STYLE",  imgui::code_editor::get_palette_color_name(i), (const float(&)[4])value);
	}
}

void reshade::runtime::draw_gui()
{
	assert(_is_initialized);

	bool show_overlay = _show_overlay;
	api::input_source show_overlay_source = _imgui_context->NavInputSource == ImGuiInputSource_Mouse ? api::input_source::mouse : api::input_source::keyboard;

	if (_input != nullptr)
	{
		if (_show_overlay && !_ignore_shortcuts && _input->is_key_pressed(input::key_escape) &&
			(_input_processing_mode == 2 || (_input_processing_mode == 1 && (_imgui_context->IO.WantCaptureMouse || _imgui_context->IO.WantCaptureKeyboard))) && !_imgui_context->IO.NavVisible)
			show_overlay = false; // Close when pressing the escape button, input focus is on the overlay and not currently navigating with the keyboard
		else if (!_ignore_shortcuts && _input->is_key_pressed(_overlay_key_data, _force_shortcut_modifiers) && _imgui_context->ActiveId == 0)
			show_overlay = !_show_overlay;

		if (!_ignore_shortcuts)
		{
			if (_input->is_key_pressed(_fps_key_data, _force_shortcut_modifiers))
				_show_fps = _show_fps ? 0 : 1;
			if (_input->is_key_pressed(_frametime_key_data, _force_shortcut_modifiers))
				_show_frametime = _show_frametime ? 0 : 1;
		}
	}

	if (_input_gamepad != nullptr)
	{
		if (_input_gamepad->is_button_down(input_gamepad::button_left_shoulder) &&
			_input_gamepad->is_button_down(input_gamepad::button_right_shoulder) &&
			_input_gamepad->is_button_pressed(input_gamepad::button_start))
		{
			show_overlay = !_show_overlay;
			show_overlay_source = api::input_source::gamepad;
		}
	}

	if (show_overlay != _show_overlay)
		open_overlay(show_overlay, show_overlay_source);

	const bool show_splash_window = _show_splash && (is_loading() || (_reload_count <= 1 && (_last_present_time - _last_reload_time) < std::chrono::seconds(5)) || (!_show_overlay && _tutorial_index == 0 && _input != nullptr));

	// Do not show this message in the same frame the screenshot is taken (so that it won't show up on the GUI screenshot)
	const bool show_screenshot_message = (_show_screenshot_message || !_last_screenshot_save_successful) && !_should_save_screenshot && (_last_present_time - _last_screenshot_time) < std::chrono::seconds(_last_screenshot_save_successful ? 3 : 5);
	const bool show_preset_transition_message = _show_preset_transition_message && _is_in_preset_transition;
	const bool show_message_window = show_screenshot_message || show_preset_transition_message || !_preset_save_successful;

	const bool show_clock = _show_clock == 1 || (_show_overlay && _show_clock > 1);
	const bool show_fps = _show_fps == 1 || (_show_overlay && _show_fps > 1);
	const bool show_frametime = _show_frametime == 1 || (_show_overlay && _show_frametime > 1);
	const bool show_preset_name = _show_preset_name == 1 || (_show_overlay && _show_preset_name > 1);
	bool show_statistics_window = show_clock || show_fps || show_frametime || show_preset_name;
#if RESHADE_ADDON
	for (const addon_info &info : addon_loaded_info)
	{
		for (const addon_info::overlay_callback &widget : info.overlay_callbacks)
		{
			if (widget.title == "OSD")
			{
				show_statistics_window = true;
				break;
			}
		}
	}
#endif

	_ignore_shortcuts = false;
	_gather_gpu_statistics = false;
	_effects_expanded_state &= 2;

	// SHERBET: 「스프레이 트레이너가 지금 살아 있는가」 — 이 프레임 단 한 번의 판정.
	// 아래 early-out 조건과, 한참 밑의 기록기 게이트가 **둘 다** 이 값에서 나온다.
	// ⚠️ 여기를 다시 손으로 풀어 쓰지 말 것. early-out 조건과 기록기 게이트를 각각 적으면
	//    둘이 어긋나도 컴파일도 CI 도 전부 초록불인 채 기록만 조용히 0 이 된다(실제로 겪었다).
	//    잠금 판정이 여기 들어 있는 것도 같은 이유다 — 잠긴 기능은 화면뿐 아니라 **일도 멈춘다**.
	const bool sherbet_spray_unlocked = sherbet_feature_unlocked("spray");
	const bool sherbet_spray_on = sherbet::paid::spray_enabled(sherbet_spray_unlocked, _sherbet_spray_live, _sherbet_spray_chart);

	if (!show_splash_window && !show_message_window && !show_statistics_window && !_show_overlay && _preview_texture == std::numeric_limits<size_t>::max()
		// SHERBET: 커스텀 조준점/반반 비교는 오버레이가 닫혀도 항상 그려야 하므로 early-out 하지 않는다.
		// (안 그러면 다른 GUI 요소가 없는 유저는 오버레이 닫을 때 조준점이 같이 사라진다)
		&& !_sherbet_crosshair_on && !_sherbet_val_on && !_sherbet_compare_active
		// SHERBET: 스프레이 기록도 오버레이가 닫힌 채로 돌아야 한다 — 실제로 총을 쏘는
		// 순간이 바로 그때다. 여기서 early-out 하면 기록 자체가 한 프레임도 돌지 않아
		// 「에임」 탭이 영영 0 을 보여준다.
		// 두 토글이 모두 꺼져 있으면(기본) 또는 기능이 잠겨 있으면 이 조건은 예전과 똑같이
		// 참이 되므로, 안 켠 구매자도 안 산 사람도 프레임 비용을 한 푼도 내지 않는다.
		&& !sherbet_spray_on
		// SHERBET(실험): 화면 이동 추정도 마찬가지다. 여기서 early-out 하면 프레임별 마우스
		// 이동 링이 한 번도 채워지지 않아, 리드백은 도는데 짝지을 마우스 값이 영영 0 이 된다
		// (= "화면은 움직이는데 마우스는 0" 이라는 완전히 틀린 그래프가 나온다).
		&& !_sherbet_motion_on
		// SHERBET: 일일 알림. **이 조건이 없으면 기능이 통째로 죽는다** — 알림이 떠야 하는
		// 밤 11시 50분은 정확히 오버레이가 닫혀 있는 시각이라, 여기서 early-out 하면
		// 판정 코드가 한 프레임도 돌지 않는다. CI 도 호스트 테스트도 전부 초록불인 채로
		// 아무 일도 안 일어난다(1.2.0 스프레이 트레이너에서 실제로 겪은 사고와 같은 함정).
		// 옵션이 꺼져 있으면(기본) 이 조건은 예전과 똑같이 참이 되어 프레임 비용이 0 이다.
		&& !_sherbet_alarm_on
		// SHERBET: HUD 돋보기도 오버레이가 닫힌 채로 그려야 한다 — 게임하면서 보는 것이 목적이다.
		// ⚠️ **영역 잡는 중**도 반드시 면제한다. 잡을 땐 오버레이를 닫아야 HUD 가 보이는데,
		//    돋보기가 아직 꺼져 있는 상태(처음 잡는 경우)라 _sherbet_mag_on 만으로는 여기서
		//    early-out 되어 드래그가 한 프레임도 처리되지 않는다.
		&& !_sherbet_mag_on && !_sherbet_mag_picking
#if RESHADE_ADDON
		&& !has_addon_event<addon_event::reshade_overlay>()
#endif
		)
	{
		if (_primary_input_handler && _input != nullptr)
		{
			_input->block_mouse_input(_block_input_next_frame);
			_input->block_keyboard_input(_block_input_next_frame);
			_input->block_mouse_cursor_warping(_block_input_next_frame);
		}
		return; // Early-out to avoid costly ImGui calls when no GUI elements are on the screen
	}

	build_font_atlas();

	ImGuiContext *const backup_context = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(_imgui_context);

	ImGuiIO &imgui_io = _imgui_context->IO;
	imgui_io.DeltaTime = _last_frame_duration.count() * 1e-9f;
	imgui_io.DisplaySize.x = static_cast<float>(_width);
	imgui_io.DisplaySize.y = static_cast<float>(_height);

	if (_input != nullptr)
	{
		imgui_io.MouseDrawCursor = _show_overlay && (!_should_save_screenshot || !_screenshot_save_gui);

		// Scale mouse position in case render resolution does not match the window size
		unsigned int max_position[2];
		_input->max_mouse_position(max_position);
		imgui_io.AddMousePosEvent(
			_input->mouse_position_x() * (imgui_io.DisplaySize.x / max_position[0]),
			_input->mouse_position_y() * (imgui_io.DisplaySize.y / max_position[1]));

		// Add wheel delta to the current absolute mouse wheel position
		imgui_io.AddMouseWheelEvent(0.0f, _input->mouse_wheel_delta());

		// Update all the button states
		constexpr std::pair<ImGuiKey, unsigned int> key_mappings[] = {
			{ ImGuiKey_Tab, input::key_tab },
			{ ImGuiKey_LeftArrow, input::key_left },
			{ ImGuiKey_RightArrow, input::key_right },
			{ ImGuiKey_UpArrow, input::key_up },
			{ ImGuiKey_DownArrow, input::key_down },
			{ ImGuiKey_PageUp, input::key_page_up },
			{ ImGuiKey_PageDown, input::key_page_down },
			{ ImGuiKey_End, input::key_end },
			{ ImGuiKey_Home, input::key_home },
			{ ImGuiKey_Insert, input::key_insert },
			{ ImGuiKey_Delete, input::key_delete },
			{ ImGuiKey_Backspace, input::key_backspace },
			{ ImGuiKey_Space, input::key_space },
			{ ImGuiKey_Enter, input::key_return },
			{ ImGuiKey_Escape, input::key_escape },
			{ ImGuiKey_LeftCtrl, input::key_left_ctrl },
			{ ImGuiKey_LeftShift, input::key_left_shift },
			{ ImGuiKey_LeftAlt, input::key_left_alt },
			{ ImGuiKey_LeftSuper, input::key_left_windows },
			{ ImGuiKey_RightCtrl, input::key_right_ctrl },
			{ ImGuiKey_RightShift, input::key_right_shift },
			{ ImGuiKey_RightAlt, input::key_right_alt },
			{ ImGuiKey_RightSuper, input::key_right_windows },
			{ ImGuiKey_Menu, input::key_application },
			{ ImGuiKey_0, '0' },
			{ ImGuiKey_1, '1' },
			{ ImGuiKey_2, '2' },
			{ ImGuiKey_3, '3' },
			{ ImGuiKey_4, '4' },
			{ ImGuiKey_5, '5' },
			{ ImGuiKey_6, '6' },
			{ ImGuiKey_7, '7' },
			{ ImGuiKey_8, '8' },
			{ ImGuiKey_9, '9' },
			{ ImGuiKey_A, 'A' },
			{ ImGuiKey_B, 'B' },
			{ ImGuiKey_C, 'C' },
			{ ImGuiKey_D, 'D' },
			{ ImGuiKey_E, 'E' },
			{ ImGuiKey_F, 'F' },
			{ ImGuiKey_G, 'G' },
			{ ImGuiKey_H, 'H' },
			{ ImGuiKey_I, 'I' },
			{ ImGuiKey_J, 'J' },
			{ ImGuiKey_K, 'K' },
			{ ImGuiKey_L, 'L' },
			{ ImGuiKey_M, 'M' },
			{ ImGuiKey_N, 'N' },
			{ ImGuiKey_O, 'O' },
			{ ImGuiKey_P, 'P' },
			{ ImGuiKey_Q, 'Q' },
			{ ImGuiKey_R, 'R' },
			{ ImGuiKey_S, 'S' },
			{ ImGuiKey_T, 'T' },
			{ ImGuiKey_U, 'U' },
			{ ImGuiKey_V, 'V' },
			{ ImGuiKey_W, 'W' },
			{ ImGuiKey_X, 'X' },
			{ ImGuiKey_Y, 'Y' },
			{ ImGuiKey_Z, 'Z' },
			{ ImGuiKey_F1, input::key_f1 },
			{ ImGuiKey_F2, input::key_f2 },
			{ ImGuiKey_F3, input::key_f3 },
			{ ImGuiKey_F4, input::key_f4 },
			{ ImGuiKey_F5, input::key_f5 },
			{ ImGuiKey_F6, input::key_f6 },
			{ ImGuiKey_F7, input::key_f7 },
			{ ImGuiKey_F8, input::key_f8 },
			{ ImGuiKey_F9, input::key_f9 },
			{ ImGuiKey_F10, input::key_f10 },
			{ ImGuiKey_F11, input::key_f11 },
			{ ImGuiKey_F12, input::key_f12 },
			{ ImGuiKey_Apostrophe, input::key_apostrophe },
			{ ImGuiKey_Comma, input::key_comma },
			{ ImGuiKey_Minus, input::key_minus },
			{ ImGuiKey_Period, input::key_period },
			{ ImGuiKey_Slash, input::key_slash },
			{ ImGuiKey_Semicolon, input::key_semicolon },
			{ ImGuiKey_Equal, input::key_plus },
			{ ImGuiKey_LeftBracket, input::key_left_bracket },
			{ ImGuiKey_Backslash, input::key_backslash },
			{ ImGuiKey_RightBracket, input::key_right_bracket },
			{ ImGuiKey_GraveAccent, input::key_grave_accent },
			{ ImGuiKey_CapsLock, input::key_caps_lock },
			{ ImGuiKey_ScrollLock, input::key_scroll_lock },
			{ ImGuiKey_NumLock, input::key_num_lock },
			{ ImGuiKey_PrintScreen, input::key_print_screen },
			{ ImGuiKey_Pause, input::key_pause },
			{ ImGuiKey_Keypad0, input::key_numpad_0 },
			{ ImGuiKey_Keypad1, input::key_numpad_1 },
			{ ImGuiKey_Keypad2, input::key_numpad_2 },
			{ ImGuiKey_Keypad3, input::key_numpad_3 },
			{ ImGuiKey_Keypad4, input::key_numpad_4 },
			{ ImGuiKey_Keypad5, input::key_numpad_5 },
			{ ImGuiKey_Keypad6, input::key_numpad_6 },
			{ ImGuiKey_Keypad7, input::key_numpad_7 },
			{ ImGuiKey_Keypad8, input::key_numpad_8 },
			{ ImGuiKey_Keypad9, input::key_numpad_9 },
			{ ImGuiKey_KeypadDecimal, input::key_numpad_decimal },
			{ ImGuiKey_KeypadDivide, input::key_numpad_divide },
			{ ImGuiKey_KeypadMultiply, input::key_numpad_multiply },
			{ ImGuiKey_KeypadSubtract, input::key_numpad_subtract },
			{ ImGuiKey_KeypadAdd, input::key_numpad_add },
			{ ImGuiMod_Ctrl, input::key_ctrl },
			{ ImGuiMod_Shift, input::key_shift },
			{ ImGuiMod_Alt, input::key_alt },
			{ ImGuiMod_Super, input::key_application },
		};

		for (const std::pair<ImGuiKey, unsigned int> &mapping : key_mappings)
			imgui_io.AddKeyEvent(mapping.first, _input->is_key_down(mapping.second));
		for (ImGuiMouseButton i = 0; i < ImGuiMouseButton_COUNT; i++)
			imgui_io.AddMouseButtonEvent(i, _input->is_mouse_button_down(i));
		for (ImWchar16 c : _input->text_input())
			imgui_io.AddInputCharacterUTF16(c);
	}

	if (_input_gamepad != nullptr)
	{
		if (_input_gamepad->is_connected())
		{
			imgui_io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

			constexpr std::pair<ImGuiKey, input_gamepad::button> button_mappings[] = {
				{ ImGuiKey_GamepadStart, input_gamepad::button_start },
				{ ImGuiKey_GamepadBack, input_gamepad::button_back },
				{ ImGuiKey_GamepadFaceLeft, input_gamepad::button_x },
				{ ImGuiKey_GamepadFaceRight, input_gamepad::button_b },
				{ ImGuiKey_GamepadFaceUp, input_gamepad::button_y },
				{ ImGuiKey_GamepadFaceDown, input_gamepad::button_a },
				{ ImGuiKey_GamepadDpadLeft, input_gamepad::button_dpad_left },
				{ ImGuiKey_GamepadDpadRight, input_gamepad::button_dpad_right },
				{ ImGuiKey_GamepadDpadUp, input_gamepad::button_dpad_up },
				{ ImGuiKey_GamepadDpadDown, input_gamepad::button_dpad_down },
				{ ImGuiKey_GamepadL1, input_gamepad::button_left_shoulder },
				{ ImGuiKey_GamepadR1, input_gamepad::button_right_shoulder },
				{ ImGuiKey_GamepadL3, input_gamepad::button_left_thumb },
				{ ImGuiKey_GamepadR3, input_gamepad::button_right_thumb },
			};

			for (const std::pair<ImGuiKey, input_gamepad::button> &mapping : button_mappings)
				imgui_io.AddKeyEvent(mapping.first, _input_gamepad->is_button_down(mapping.second));

			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, _input_gamepad->left_trigger_position() != 0.0f, _input_gamepad->left_trigger_position());
			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, _input_gamepad->right_trigger_position() != 0.0f, _input_gamepad->right_trigger_position());
			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft, _input_gamepad->left_thumb_axis_x() < 0.0f, -std::min(_input_gamepad->left_thumb_axis_x(), 0.0f));
			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, _input_gamepad->left_thumb_axis_x() > 0.0f, std::max(_input_gamepad->left_thumb_axis_x(), 0.0f));
			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, _input_gamepad->left_thumb_axis_y() > 0.0f, std::max(_input_gamepad->left_thumb_axis_y(), 0.0f));
			imgui_io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, _input_gamepad->left_thumb_axis_y() < 0.0f, -std::min(_input_gamepad->left_thumb_axis_y(), 0.0f));
		}
		else
		{
			imgui_io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
		}
	}

	ImGui::NewFrame();

	// SHERBET: 반반 비교 슬라이더. on_present() 에서 떠 둔 '효과 적용 전' 스냅샷의 왼쪽 split
	// 영역을 게임 위(오버레이 아래)에 겹쳐 그리고, 가운데에 드래그 가능한 분할선을 표시한다.
	// 비교가 꺼져 있으면(기본) 전부 스킵되어 렌더에 영향 없음.
	if (_sherbet_compare_active && _sherbet_before_srv != 0 && !is_loading() && !_techniques.empty())
	{
		const ImGuiViewport *const cmp_vp = ImGui::GetMainViewport();
		const ImVec2 p0 = cmp_vp->Pos, sz = cmp_vp->Size;
		const float split = ImClamp(_sherbet_compare_split, 0.02f, 0.98f);
		const float split_x = p0.x + sz.x * split;

		ImDrawList *const dl = ImGui::GetBackgroundDrawList();
		dl->AddImage(_sherbet_before_srv.handle, p0, ImVec2(split_x, p0.y + sz.y), ImVec2(0, 0), ImVec2(split, 1));
		dl->AddLine(ImVec2(split_x, p0.y), ImVec2(split_x, p0.y + sz.y), IM_COL32(255, 255, 255, 235), 2.0f);
		const ImVec2 handle(split_x, p0.y + sz.y * 0.5f);
		dl->AddCircleFilled(handle, 13.0f, IM_COL32(255, 255, 255, 240));
		dl->AddCircle(handle, 13.0f, IM_COL32(0, 0, 0, 90), 0, 2.0f);
		dl->AddText(ImVec2(p0.x + 14, p0.y + 12), IM_COL32(255, 255, 255, 220), "BEFORE");
		const char *const cmp_after = "AFTER";
		dl->AddText(ImVec2(p0.x + sz.x - ImGui::CalcTextSize(cmp_after).x - 14, p0.y + 12), IM_COL32(255, 255, 255, 220), cmp_after);

		// 드래그 — 오버레이가 열려 있을 때만. 분할선 근처를 누르면 잡고, 마우스를 따라 이동.
		if (_input != nullptr && show_overlay)
		{
			const ImVec2 m = _imgui_context->IO.MousePos;
			const bool near_line = (m.x > split_x ? m.x - split_x : split_x - m.x) < 26.0f;
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && near_line && !_imgui_context->IO.WantCaptureMouse)
				_sherbet_compare_dragging = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
				_sherbet_compare_dragging = false;
			if (_sherbet_compare_dragging && sz.x > 1.0f)
				_sherbet_compare_split = ImClamp((m.x - p0.x) / sz.x, 0.0f, 1.0f);
		}
	}

	// SHERBET: 스프레이 트레이너 입력 샘플링. **오버레이 게이트 바깥**이라 오버레이를 열지
	// 않아도 매 프레임 돈다 — 실제로 총을 쏘는 순간은 오버레이가 닫혀 있을 때다.
	// ⚠️ raw_mouse_delta_x/y() 는 **읽으면 리셋**이다. 프레임당 정확히 한 번, 여기서만 읽는다.
	//    진단 표시도 기록도 이 한 번의 값을 나눠 쓴다(두 곳에서 읽으면 각자 절반만 본다).
	if (_input != nullptr)
	{
		const int raw_dx = _input->raw_mouse_delta_x();
		const int raw_dy = _input->raw_mouse_delta_y();
		const bool lmb = _input->is_mouse_button_down(0);
		const bool fire = lmb && !_sherbet_spray_lmb_prev; // 상승 엣지
		// 직전 상태는 오버레이 상태와 무관하게 갱신한다 — 오버레이에서 누른 채로 닫으면
		// 다음 프레임에 가짜 엣지가 잡힌다.
		_sherbet_spray_lmb_prev = lmb;

		// SHERBET(실험): 화면 이동 추정용 마우스 이동 링. 화면 쪽 추정치는 리드백 지연 때문에
		// **두 프레임 늦게** 나오므로, 그때 짝지을 수 있도록 프레임별로 남겨 둔다.
		// ⚠️ 위에서 이미 읽어 리셋한 값을 나눠 쓴다(raw_mouse_delta_x/y 를 여기서 또 읽으면
		//    스프레이 기록과 서로 절반씩만 보게 된다).
		// ⚠️ 카운터는 **오버레이가 열려 있어도** 증가한다 — runtime.cpp 의 추정 쪽이
		//    "슬롯이 몇 프레임 묵었나"로 유효성을 판단하므로 프레임 축이 멈추면 안 된다.
		if (_sherbet_motion_on)
		{
			const int mi = static_cast<int>(_sherbet_motion_frame % kSherbetMotionMouseRing);
			_sherbet_motion_mouse[mi][0] = static_cast<float>(raw_dx);
			_sherbet_motion_mouse[mi][1] = static_cast<float>(raw_dy);
			_sherbet_motion_frame++;
		}

		// 두 토글이 모두 꺼져 있으면(기본) 아무것도 기록하지 않는다. 기능이 잠겨 있어도 마찬가지다 —
		// 안 산 사람의 기록을 몰래 쌓아두지 않는다(잠긴 기능은 화면뿐 아니라 **일도 멈춘다**).
		// 오버레이가 열려 있는 동안도 기록하지 않는다(스펙 §5.3) — UI 를 조작하는 클릭·이동은
		// 사격이 아니다. 위에서 이미 읽어 리셋했으므로 그 이동이 다음 구간으로 새지도 않는다.
		// ⚠️ 위 early-out 조건(sherbet_spray_on)과 **같은 술어 집합**에서 나온다. 여기를
		//    손으로 풀어 쓰면 둘이 어긋나 "그릴 게 없어 return 했는데 기록은 돌아야 한다"가 된다.
		if (sherbet::paid::spray_recording(sherbet_spray_unlocked, _sherbet_spray_live, _sherbet_spray_chart, _show_overlay))
		{
			if (fire)
			{
				_sherbet_spray_clicks++;
				_sherbet_spray_fade = 2.0f; // 마지막 발사 기준 2초 페이드아웃 재장전
			}
			// 부호 있는 합은 좌우로 흔들면 상쇄돼 0 근처에 머문다 — 진단용으로는
			// "흔들면 확실히 올라가는" 절대값 합이 맞다. INT32_MIN 방어로 long long 경유.
			const long long adx = raw_dx < 0 ? -static_cast<long long>(raw_dx) : raw_dx;
			const long long ady = raw_dy < 0 ? -static_cast<long long>(raw_dy) : raw_dy;
			_sherbet_spray_move_total += static_cast<unsigned long long>(adx + ady);

			// 슬라이더 값이 바뀌었을 수 있으므로 매 프레임 반영한다(정수 비교 두 번).
			_sherbet_spray.set_gap_ms(_sherbet_spray_gap_ms);
			_sherbet_spray.on_frame(imgui_io.DeltaTime, raw_dx, raw_dy, fire);
		}

		// SHERBET: 에임 트레이너도 **같은 한 번 읽은 델타**를 쓴다. 여기서 또 읽으면
		// 스프레이와 서로 절반씩만 보게 된다(raw_mouse_delta 는 읽으면 리셋이다).
		sherbet_aim_frame(imgui_io.DeltaTime, raw_dx, raw_dy, fire);

		if (_sherbet_spray_fade > 0.0f)
			_sherbet_spray_fade = ImMax(0.0f, _sherbet_spray_fade - imgui_io.DeltaTime);
	}

	// SHERBET: 일일 알림 — 정한 시각이 되면 몇 초만 떴다 사라진다. 상시 표시가 아니다.
	// (평소에는 remaining 이 0 이라 아무것도 그리지 않는다.)
	// 시각 조회는 1초에 한 번이면 충분하다(분 단위 판정). 매 프레임 localtime 을 부르지 않는다.
	if (_sherbet_alarm_on && sherbet::alarm::due_for_check(_sherbet_alarm, imgui_io.DeltaTime))
	{
		const std::time_t alarm_now = std::time(nullptr);
		struct tm alarm_tm; localtime_s(&alarm_tm, &alarm_now);
		if (sherbet::alarm::should_fire(_sherbet_alarm,
				sherbet::alarm::minute_of_day(alarm_tm.tm_hour, alarm_tm.tm_min),
				sherbet::alarm::minute_of_day(_sherbet_alarm_hour, _sherbet_alarm_min), true))
			_sherbet_alarm.remaining = ImMax(1.0f, _sherbet_alarm_secs); // 여기서만 켜진다
	}
	// 남은 시간이 있을 때만 그린다. 설정 탭의 「테스트」 버튼도 remaining 을 올려 같은 길로 온다.
	if (sherbet::alarm::tick_visible(_sherbet_alarm, imgui_io.DeltaTime))
	{
		static const char *const kAlarmDefault =
			"\xEC\x9D\xBC\xEC\x9D\xBC\xEB\xB3\xB4\xEC\x83\x81\xEC\x9D\x84 \xEB\xB0\x9B\xEC\x95\x84\xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94!"; // "일일보상을 받아주세요!"
		const char *const al_text = _sherbet_alarm_text.empty() ? kAlarmDefault : _sherbet_alarm_text.c_str();
		const float al_a = sherbet::alarm::fade_alpha(_sherbet_alarm);
		const sherbet::theme &al_t = sherbet::active_theme();
		const ImGuiViewport *const al_vp = ImGui::GetMainViewport();
		ImDrawList *const al = ImGui::GetForegroundDrawList();
		const float al_sz = _imgui_context->Style.FontSizeBase * 1.5f;
		const ImVec2 al_ts = _sherbet_title_font->CalcTextSizeA(al_sz, FLT_MAX, 0.0f, al_text);
		// 화면 위쪽 가운데 — 조준점(중앙)·OSD(모서리)와 안 겹치는 자리.
		const ImVec2 al_p(al_vp->Pos.x + (al_vp->Size.x - al_ts.x) * 0.5f, al_vp->Pos.y + al_vp->Size.y * 0.12f);
		const float al_pad = 18.0f;
		al->AddRectFilled(ImVec2(al_p.x - al_pad, al_p.y - al_pad * 0.5f),
			ImVec2(al_p.x + al_ts.x + al_pad, al_p.y + al_ts.y + al_pad * 0.5f),
			sherbet::with_alpha(al_t.panel, static_cast<int>(220 * al_a)), 14.0f);
		al->AddText(_sherbet_title_font, al_sz, al_p,
			sherbet::with_alpha(al_t.text, static_cast<int>(255 * al_a)), al_text);
	}

	// SHERBET: HUD 돋보기 영역 잡기 — 화면 위에서 직접 드래그해 사각형을 정한다.
	//
	// ⚠️ **반드시 오버레이가 열려 있어야 한다.** 처음엔 "닫고 드래그" 로 만들었는데 셋 다 어긋난다:
	//    1) `imgui_io.MouseDrawCursor = _show_overlay && …` (이 파일 1142행) → 닫으면 **커서가 안 그려진다.**
	//       어디를 잡는지 보이지 않는다.
	//    2) `block_input = _input_processing_mode != 0 && (_show_overlay || …)` (2490행) → 닫으면 좌클릭이
	//       **게임으로 간다. 영역 잡다가 실제로 총이 나간다.**
	//    3) FiveM 마우스룩이 커서를 화면 중앙에 고정하므로 IO.MousePos 가 아예 안 움직인다.
	//    이 저장소의 기존 화면 드래그(비교 분할선 1335행, OSD)도 전부 show_overlay 게이트다.
	// 오버레이가 열려 있으면 입력 처리 기본값(_input_processing_mode = 2)이 게임 입력을 통째로
	// 막아 주므로 오발 위험도 없다. 잡는 동안에는 아래 draw_gui 본문이 Sherbet 창을 그리지 않아
	// 화면 전체가 보인다(창이 HUD 를 가리지 않게).
	if (_sherbet_mag_picking && _input != nullptr && _show_overlay)
	{
		const ImGuiViewport *const pk_vp = ImGui::GetMainViewport();
		const ImVec2 pk_m = _imgui_context->IO.MousePos;
		const float pk_nx = pk_vp->Size.x > 1.0f ? (pk_m.x - pk_vp->Pos.x) / pk_vp->Size.x : 0.0f;
		const float pk_ny = pk_vp->Size.y > 1.0f ? (pk_m.y - pk_vp->Pos.y) / pk_vp->Size.y : 0.0f;

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !_imgui_context->IO.WantCaptureMouse)
		{
			_sherbet_mag_drag[0] = pk_nx;
			_sherbet_mag_drag[1] = pk_ny;
		}
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && !_imgui_context->IO.WantCaptureMouse)
		{
			// 끄는 동안 실시간으로 보여 준다(뒤집어 끌어도 from_drag 가 바로잡는다).
			const sherbet::mag::rect pk_r =
				sherbet::mag::from_drag(_sherbet_mag_drag[0], _sherbet_mag_drag[1], pk_nx, pk_ny);
			ImDrawList *const pk = ImGui::GetForegroundDrawList();
			const ImVec2 a(pk_vp->Pos.x + pk_r.x * pk_vp->Size.x, pk_vp->Pos.y + pk_r.y * pk_vp->Size.y);
			const ImVec2 b(a.x + pk_r.w * pk_vp->Size.x, a.y + pk_r.h * pk_vp->Size.y);
			pk->AddRectFilled(a, b, IM_COL32(120, 200, 255, 60));
			pk->AddRect(a, b, IM_COL32(120, 200, 255, 255), 0.0f, 0, 2.0f);
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !_imgui_context->IO.WantCaptureMouse)
		{
			const sherbet::mag::rect pk_done =
				sherbet::mag::from_drag(_sherbet_mag_drag[0], _sherbet_mag_drag[1], pk_nx, pk_ny);
			// ⚠️ 실수로 클릭만 한 경우를 확정하지 않는다. sanitize 가 최소 크기를 만들어 주므로
			//    크래시는 없지만, 사용자는 "엉뚱한 데가 확대됨" 을 보게 된다.
			// ⚠️⚠️ **거부하면 반드시 알린다.** 처음엔 else 없이 그냥 무시했는데, 사용자에게는
			//      "드래그해도 아무 일이 안 일어남" 으로 보였다 — 실제 신고가 그것이었다.
			//      조용한 거부는 고장과 구분되지 않는다.
			if (sherbet::mag::is_usable(pk_done, static_cast<int>(_width), static_cast<int>(_height)))
			{
				_sherbet_mag_rect = sherbet::mag::sanitize(pk_done);
				_sherbet_mag_cap_res[0] = static_cast<int>(_width);
				_sherbet_mag_cap_res[1] = static_cast<int>(_height);
				_sherbet_mag_picking = false;
				_sherbet_mag_on = true; // 잡았으면 바로 보여 준다
				save_config();
			}
			else
			{
				_sherbet_mag_pick_msg = 3.0f; // 잡기 모드를 유지한 채 안내만 띄운다
			}
		}
		if (_sherbet_mag_pick_msg > 0.0f)
			_sherbet_mag_pick_msg -= _imgui_context->IO.DeltaTime;
		// 취소 — 우클릭이나 Esc. (잡는 동안 Sherbet 창을 안 그리므로 버튼으로 나갈 길이 없다.)
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			_sherbet_mag_picking = false;

		// 안내 — 창이 없으니 화면에 직접 띄운다.
		{
			// 너무 작게 끌어 거부됐으면 그 이유를 대신 띄운다 — 조용히 무시하면 고장으로 보인다.
			static const char *const kPickTooSmall =
				"\xEB\x84\x88\xEB\xAC\xB4 \xEC\x9E\x91\xEC\x95\x84\xEC\x9A\x94 \xE2\x80\x94 \xEC\xA1\xB0\xEA\xB8\x88 \xEB\x8D\x94 \xED\x81\xAC\xEA\xB2\x8C \xEB\x81\x8C\xEC\x96\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "너무 작아요 — 조금 더 크게 끌어 주세요"
			static const char *const kPickHint =
				"\xED\x99\x95\xEB\x8C\x80\xED\x95\x98\xEA\xB3\xA0 \xEC\x8B\xB6\xEC\x9D\x80 \xEA\xB3\xB3\xEC\x9D\x84 \xEB\x93\x9C\xEB\x9E\x98\xEA\xB7\xB8\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94" " \xC2\xB7 " "\xEC\xB7\xA8\xEC\x86\x8C\xEB\x8A\x94 \xEC\x9A\xB0\xED\x81\xB4\xEB\xA6\xAD/Esc"; // "확대하고 싶은 곳을 드래그하세요 · 취소는 우클릭/Esc"
			ImDrawList *const hint = ImGui::GetForegroundDrawList();
			const float hs = _imgui_context->Style.FontSizeBase * 1.2f;
			const char *const pk_msg = (_sherbet_mag_pick_msg > 0.0f) ? kPickTooSmall : kPickHint;
			const ImVec2 hts = _sherbet_title_font->CalcTextSizeA(hs, FLT_MAX, 0.0f, pk_msg);
			const ImVec2 hp(pk_vp->Pos.x + (pk_vp->Size.x - hts.x) * 0.5f, pk_vp->Pos.y + pk_vp->Size.y * 0.06f);
			hint->AddRectFilled(ImVec2(hp.x - 16.0f, hp.y - 9.0f), ImVec2(hp.x + hts.x + 16.0f, hp.y + hts.y + 9.0f),
				IM_COL32(20, 22, 28, 225), 12.0f);
			hint->AddText(_sherbet_title_font, hs, hp, (_sherbet_mag_pick_msg > 0.0f)
				? IM_COL32(255, 180, 90, 255) : IM_COL32(235, 240, 250, 255), pk_msg);
		}
	}

	// SHERBET: HUD 돋보기 — 지난 프레임에 잘라 둔 조각을 확대해 그린다.
	// 좌표 계산은 sherbet_magnifier.hpp(맥에서 해상도별 단위테스트됨)가 한다.
	if (_sherbet_mag_on && _sherbet_mag_srv != 0 && _sherbet_mag_tex_w > 0)
	{
		const ImGuiViewport *const mg_vp = ImGui::GetMainViewport();
		const float mg_zoom = sherbet::mag::clamp_zoom(_sherbet_mag_zoom);
		const float mg_w = _sherbet_mag_tex_w * mg_zoom;
		const float mg_h = _sherbet_mag_tex_h * mg_zoom;
		float mg_x = 0.0f, mg_y = 0.0f;
		sherbet::mag::dest_pos(_sherbet_mag_anchor[0], _sherbet_mag_anchor[1], mg_w, mg_h,
			static_cast<int>(mg_vp->Size.x), static_cast<int>(mg_vp->Size.y), mg_x, mg_y);
		const ImVec2 mg_p0(mg_vp->Pos.x + mg_x, mg_vp->Pos.y + mg_y);
		const ImVec2 mg_p1(mg_p0.x + mg_w, mg_p0.y + mg_h);
		const int mg_a = static_cast<int>(ImClamp(_sherbet_mag_opacity, 0.0f, 1.0f) * 255.0f);
		ImDrawList *const mg = ImGui::GetForegroundDrawList();
		// ⚠️ **테두리를 먼저 그린다.** 확대한 내용이 어두우면(기본 영역이 어두운 화면 구석이면
		//    흔하다) 그림만으로는 켜졌는지조차 알 수 없다 — 실제로 "켜도 아무것도 안 뜬다" 는
		//    신고로 나타났다. 틀이 보이면 최소한 "켜졌고 여기를 보고 있다" 는 것이 전달된다.
		const sherbet::theme &mg_t = sherbet::active_theme();
		mg->AddRectFilled(ImVec2(mg_p0.x - 3.0f, mg_p0.y - 3.0f), ImVec2(mg_p1.x + 3.0f, mg_p1.y + 3.0f),
			sherbet::with_alpha(mg_t.panel, static_cast<int>(200 * ImClamp(_sherbet_mag_opacity, 0.0f, 1.0f))), 8.0f);
		mg->AddImage(_sherbet_mag_srv.handle, mg_p0, mg_p1, ImVec2(0, 0), ImVec2(1, 1),
			IM_COL32(255, 255, 255, mg_a));
		mg->AddRect(ImVec2(mg_p0.x - 3.0f, mg_p0.y - 3.0f), ImVec2(mg_p1.x + 3.0f, mg_p1.y + 3.0f),
			sherbet::with_alpha(mg_t.accent, static_cast<int>(230 * ImClamp(_sherbet_mag_opacity, 0.0f, 1.0f))), 8.0f, 0, 2.0f);

	}

	// SHERBET: 커스텀 조준점 — 화면 중앙(+오프셋)에 커스텀 이미지 또는 내장 도형을 그린다.
	// ForegroundDrawList 라 항상 최상단에 보이며, 렌더 파이프라인은 건드리지 않는다.
	if (_sherbet_crosshair_on)
	{
		if (_sherbet_crosshair_dirty)
			sherbet_load_crosshair();

		const ImGuiViewport *const xh_vp = ImGui::GetMainViewport();
		const ImVec2 c(xh_vp->Pos.x + xh_vp->Size.x * 0.5f + _sherbet_crosshair_off[0],
		               xh_vp->Pos.y + xh_vp->Size.y * 0.5f + _sherbet_crosshair_off[1]);
		ImDrawList *const xh = ImGui::GetForegroundDrawList();
		const float op = ImClamp(_sherbet_crosshair_opacity, 0.0f, 1.0f);

		if (_sherbet_crosshair_builtin == 0 && _sherbet_crosshair_srv != 0 && _sherbet_crosshair_w > 0)
		{
			const float iw = _sherbet_crosshair_size;
			const float ih = iw * static_cast<float>(_sherbet_crosshair_h) / static_cast<float>(_sherbet_crosshair_w);
			const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(op * 255.0f));
			xh->AddImage(_sherbet_crosshair_srv.handle, ImVec2(c.x - iw * 0.5f, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.5f, c.y + ih * 0.5f), ImVec2(0, 0), ImVec2(1, 1), tint);
		}
		else
		{
			const ImU32 col = IM_COL32(
				static_cast<int>(_sherbet_crosshair_col[0] * 255.0f), static_cast<int>(_sherbet_crosshair_col[1] * 255.0f),
				static_cast<int>(_sherbet_crosshair_col[2] * 255.0f), static_cast<int>(_sherbet_crosshair_col[3] * op * 255.0f));
			const float s = _sherbet_crosshair_size * 0.5f; // 반길이/반지름
			const float g = _sherbet_crosshair_gap;
			const float th = ImMax(1.0f, _sherbet_crosshair_thick);
			const bool draw_cross = (_sherbet_crosshair_builtin == 2 || _sherbet_crosshair_builtin == 4);
			const bool draw_dot   = (_sherbet_crosshair_builtin == 1 || _sherbet_crosshair_builtin == 4);
			const bool draw_circle = (_sherbet_crosshair_builtin == 3);
			if (draw_cross)
			{
				xh->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x - g, c.y), col, th);
				xh->AddLine(ImVec2(c.x + g, c.y), ImVec2(c.x + s, c.y), col, th);
				xh->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y - g), col, th);
				xh->AddLine(ImVec2(c.x, c.y + g), ImVec2(c.x, c.y + s), col, th);
			}
			if (draw_circle)
				xh->AddCircle(c, s, col, 0, th);
			if (draw_dot)
				xh->AddCircleFilled(c, ImMax(1.5f, th), col);
		}
	}

	// SHERBET: 발로란트급 조준점 — 위의 「클래식」 조준점과 별개 토글이고, 같은
	// ForegroundDrawList 에 오버레이 게이트 바깥에서 그린다(렌더 파이프라인 무영향).
	//
	// **여기에는 산술이 없다.** 어떤 사각형이 어디에 있는지는 sherbet_crosshair.hpp 가
	// 전부 정하고(설계 §3), 이 블록은 그 목록을 순서대로 AddRectFilled 로 옮기기만 한다.
	// 그래야 좌표 판정 100% 가 맥에서 단위테스트된다(tools/sherbet_crosshair_test.cpp).
	if (_sherbet_val_on)
	{
		// §3.1 좌표계. 모든 좌표는 **정수 픽셀**이다 — 서브픽셀 좌표를 쓰면 텍셀 블렌딩이
		// 생겨 발로란트의 하드 에지가 사라진다. 절대 픽셀이므로 DPI 스케일을 곱하지 않는다(§0.4).
		const ImGuiViewport *const val_vp = ImGui::GetMainViewport();
		const int val_cx = static_cast<int>(std::floor(val_vp->Pos.x + val_vp->Size.x * 0.5f));
		const int val_cy = static_cast<int>(std::floor(val_vp->Pos.y + val_vp->Size.y * 0.5f));

		// ── §4 오차 애니메이션 ─────────────────────────────────────────────
		// 게임 메모리는 읽지 않는다. 이미 후킹 중인 WASD·좌클릭만 본다 — 그래서 근사이고,
		// 가장 크게 어긋나는 항목(캐릭터 속도 대신 키 입력)은 「에임」 탭이 글로 설명한다.
		// 판정은 전부 sherbet_crosshair.hpp 안에서 끝난다. 여기는 입력을 모아 넘길 뿐이다.
		if (_sherbet_val_err_on && _input != nullptr)
		{
			// 일시정지 핫키(§4.4 #2) — 게임 내 채팅 상태를 알 수 없어서 필요한 장치다.
			if (_sherbet_val_key_pause[0] != 0)
			{
				const bool pk = _input->is_key_down(_sherbet_val_key_pause[0]);
				if (pk && !_sherbet_val_pause_prev)
					_sherbet_val_err_paused = !_sherbet_val_err_paused;
				_sherbet_val_pause_prev = pk;
			}

			sherbet::crosshair::error_input ei;
			ei.dt = imgui_io.DeltaTime;
			ei.fwd = _input->is_key_down(_sherbet_val_key_fwd[0]);
			ei.back = _input->is_key_down(_sherbet_val_key_back[0]);
			ei.left = _input->is_key_down(_sherbet_val_key_left[0]);
			ei.right = _input->is_key_down(_sherbet_val_key_right[0]);
			ei.walk_key = _sherbet_val_key_walk[0] != 0 && _input->is_key_down(_sherbet_val_key_walk[0]);
			ei.fire = _input->is_mouse_button_down(0);
			// 오버레이가 열려 있으면 WASD 도 클릭도 UI 조작이지 사격이 아니다.
			ei.paused = _sherbet_val_err_paused || _show_overlay;
			sherbet::crosshair::update_error(_sherbet_val_err, _sherbet_val_tune, ei);
		}
		else
		{
			// 꺼져 있으면 상태를 초기화해 둔다 — 다시 켰을 때 옛날 오차가 남아 있으면 안 된다.
			_sherbet_val_err = sherbet::crosshair::error_state();
		}

		// FiveM 에는 ADS/스나이퍼 개념이 없으므로 Primary 만 그린다(설계 §5).
		// A/S 섹션은 파싱·보관·재출력만 한다.
		// 오차가 꺼져 있으면 error_state 가 전부 0 이라 정적 조준점과 **완전히 같은 결과**가 나온다.
		sherbet::crosshair::build_crosshair_animated(
			_sherbet_val_profile.primary, val_cx, val_cy, _sherbet_val_err, _sherbet_val_tune, _sherbet_val_quads);

		// §3.1 AddRectFilled(rounding 0) 만 쓴다. 축 정렬 사각형은 ImGui 의 AA 경로를
		// 타지 않으므로 ImDrawListFlags_AntiAliased* 를 만질 필요가 없다.
		// AddLine/AddCircle 은 절대 쓰지 않는다 — 좌표 중심 기준 + AA 라 홀수 두께에서
		// 반픽셀이 생긴다(위 클래식 조준점이 그래서 "비슷한데 다르다").
		ImDrawList *const val_dl = ImGui::GetForegroundDrawList();
		for (const sherbet::crosshair::quad &q : _sherbet_val_quads)
			val_dl->AddRectFilled(
				ImVec2(static_cast<float>(q.r.x), static_cast<float>(q.r.y)),
				ImVec2(static_cast<float>(q.r.x + q.r.w), static_cast<float>(q.r.y + q.r.h)),
				IM_COL32(q.color.r, q.color.g, q.color.b, q.alpha), 0.0f);
	}

	// SHERBET: 실시간 궤적(토글 ①). 조준점과 같은 ForegroundDrawList 라 오버레이를 열지
	// 않아도 그려지고, 렌더 파이프라인은 건드리지 않는다.
	// ⚠️ 이동을 읽을 수 없는 게임에서는 그리지 않는다 — 점이 전부 원점에 겹쳐 한 덩어리로
	//    보일 뿐이라 "고장난 것"처럼 읽힌다. 이유는 「에임」 탭 진단이 글로 설명한다.
	// ⚠️ sherbet_spray_on 도 함께 본다 — 잠긴 상태에서 예전 기록이 화면에 남아 그려지면
	//    잠금이 UI 만 가린 게 된다(기록기가 멈춰도 history 는 세션에 남아 있을 수 있다).
	if (sherbet_spray_on && _sherbet_spray_live && _sherbet_spray_fade > 0.0f && _input != nullptr && _input->raw_mouse_available())
	{
		// 구간이 끝나도(마지막 발 후 gap 경과) 페이드아웃이 남아 있는 동안은 계속 보여준다.
		// 종료된 구간은 current() 가 아니라 history 의 맨 뒤에 있다.
		const sherbet::spray::segment *seg = _sherbet_spray.current();
		if (seg == nullptr && !_sherbet_spray.history().empty())
			seg = &_sherbet_spray.history().back();

		if (seg != nullptr && !seg->shots.empty())
		{
			const ImGuiViewport *const sp_vp = ImGui::GetMainViewport();
			const ImVec2 sc(sp_vp->Pos.x + sp_vp->Size.x * 0.5f, sp_vp->Pos.y + sp_vp->Size.y * 0.5f);
			ImDrawList *const sp = ImGui::GetForegroundDrawList();
			const sherbet::theme &sp_theme = sherbet::active_theme();
			// 페이드: 마지막 발사 직후 1.0 에서 2초에 걸쳐 0 으로.
			const float sp_a = ImClamp(_sherbet_spray_fade * 0.5f, 0.0f, 1.0f);
			const ImU32 sp_line = sherbet::with_alpha(sp_theme.accent, static_cast<ImU32>(sp_a * 210.0f));
			const ImU32 sp_dot = sherbet::with_alpha(sp_theme.accent2, static_cast<ImU32>(sp_a * 255.0f));
			// 마지막 발만 흰색 — 테마색이 배경과 비슷해도 '지금 여기'는 보여야 한다.
			const ImU32 sp_last = IM_COL32(255, 255, 255, static_cast<int>(sp_a * 255.0f));

			ImVec2 prev(0.0f, 0.0f);
			for (std::size_t i = 0; i < seg->shots.size(); ++i)
			{
				// raw 단위 → 픽셀. y 는 아래가 양수라 화면 좌표와 방향이 같다(총구를 내리면 아래로).
				const ImVec2 p(sc.x + seg->shots[i].x * _sherbet_spray_scale,
				               sc.y + seg->shots[i].y * _sherbet_spray_scale);
				if (i != 0)
					sp->AddLine(prev, p, sp_line, 2.0f);
				sp->AddCircleFilled(p, 3.0f, (i + 1 == seg->shots.size()) ? sp_last : sp_dot);
				prev = p;
			}
		}
	}

	// SHERBET: 에임 트레이너 — 카운트다운·표적·좌측 HUD. 오버레이가 닫혀 있을 때만 그린다.
	draw_sherbet_aim_overlay();

	// SHERBET(실험): 화면 이동 추정 숫자판. 조준점·궤적과 같은 ForegroundDrawList 라
	// **오버레이 게이트 바깥**이고, 그래서 오버레이를 닫은 채로 검증할 수 있다 —
	// 세 단계 검증은 전부 오버레이가 닫혀 있어야 성립한다(열려 있으면 게임이 마우스를
	// 못 받아 화면이 아예 안 움직인다). 메인 토글이 꺼져 있으면 그리지 않는다.
	if (_sherbet_motion_on && _sherbet_motion_hud && !_show_overlay && _sherbet_motion.count() > 0)
	{
		const ImGuiViewport *const mo_vp = ImGui::GetMainViewport();
		ImDrawList *const mo = ImGui::GetForegroundDrawList();
		const sherbet::motion::sample &s = _sherbet_motion.last();

		char l1[96], l2[96], l3[96], l4[96];
		snprintf(l1, sizeof(l1), "\xED\x99\x94\xEB\xA9\xB4   dx %+6.1f  dy %+6.1f", s.screen_dx, s.screen_dy);  // "화면"
		snprintf(l2, sizeof(l2), "\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 dx %+6.1f  dy %+6.1f", s.mouse_dx, s.mouse_dy); // "마우스"
		snprintf(l3, sizeof(l3), "\xEB\xB0\x98\xEB\x8F\x99   dx %+6.1f  dy %+6.1f", s.recoil_dx(), s.recoil_dy()); // "반동"
		snprintf(l4, sizeof(l4), "\xEC\x8B\xA0\xEB\xA2\xB0\xEB\x8F\x84 x %3.0f%%  y %3.0f%%", s.conf_x * 100.0f, s.conf_y * 100.0f); // "신뢰도"

		float tw = 0.0f;
		for (const char *t : { l1, l2, l3, l4 })
			tw = ImMax(tw, ImGui::CalcTextSize(t).x);

		const float lh = ImGui::GetTextLineHeightWithSpacing();
		const float pad = 10.0f, plot_h = 44.0f;
		const ImVec2 p0(mo_vp->Pos.x + 24.0f, mo_vp->Pos.y + mo_vp->Size.y * 0.28f);
		const ImVec2 p1(p0.x + tw + pad * 2.0f, p0.y + lh * 4.0f + plot_h + pad * 2.0f);

		mo->AddRectFilled(p0, p1, IM_COL32(12, 12, 16, 170), 10.0f);
		const sherbet::theme &mo_t = sherbet::active_theme();
		mo->AddText(ImVec2(p0.x + pad, p0.y + pad), IM_COL32(235, 235, 235, 235), l1);
		mo->AddText(ImVec2(p0.x + pad, p0.y + pad + lh), IM_COL32(180, 180, 180, 225), l2);
		mo->AddText(ImVec2(p0.x + pad, p0.y + pad + lh * 2.0f), sherbet::with_alpha(mo_t.accent, 255), l3);
		mo->AddText(ImVec2(p0.x + pad, p0.y + pad + lh * 3.0f), IM_COL32(170, 170, 170, 210), l4);

		// 미니 그래프 — 반동 dy. 한 발 쏠 때 위로 튀는지가 여기서 바로 보인다.
		// dy 는 아래가 + 이고 화면 좌표도 아래가 + 라, 그대로 더하면 위로 튄 것이 위로 나온다.
		const int n = _sherbet_motion.count();
		const int show = n > 120 ? 120 : n;
		const float gy0 = p0.y + pad + lh * 4.0f, gy1 = gy0 + plot_h;
		const float gmid = (gy0 + gy1) * 0.5f;
		mo->AddLine(ImVec2(p0.x + pad, gmid), ImVec2(p1.x - pad, gmid), IM_COL32(255, 255, 255, 45));
		if (show >= 2)
		{
			float peak = 6.0f;
			for (int i = n - show; i < n; ++i)
				peak = ImMax(peak, std::abs(_sherbet_motion.at(i).recoil_dy()));
			const float sy = (plot_h * 0.5f - 3.0f) / peak;
			const float stepx = (tw) / static_cast<float>(show - 1);
			for (int i = n - show + 1; i < n; ++i)
			{
				const sherbet::motion::sample &a = _sherbet_motion.at(i - 1);
				const sherbet::motion::sample &b = _sherbet_motion.at(i);
				const int k = i - (n - show);
				const float xa = p0.x + pad + stepx * static_cast<float>(k - 1);
				const float xb = p0.x + pad + stepx * static_cast<float>(k);
				const bool ok = ImMin(a.conf_y, b.conf_y) > 0.25f;
				mo->AddLine(ImVec2(xa, gmid + a.recoil_dy() * sy), ImVec2(xb, gmid + b.recoil_dy() * sy),
					ok ? IM_COL32(255, 255, 255, 230) : IM_COL32(140, 140, 140, 110), ok ? 1.8f : 1.0f);
			}
		}
	}

	// Reset input source to mouse when the cursor is moved
	if (_input != nullptr && (_input->mouse_movement_delta_x() != 0 || _input->mouse_movement_delta_y() != 0))
		_imgui_context->NavInputSource = ImGuiInputSource_Mouse;

#if RESHADE_LOCALIZATION
	const std::string prev_language = resources::set_current_language(_selected_language);
	_current_language = resources::get_current_language();
#endif

	ImVec2 viewport_offset = ImVec2(0, 0);
	const bool show_spinner = _reload_count > 1 && _tutorial_index != 0;

	// Create ImGui widgets and windows
	if (show_splash_window && !(show_spinner && show_overlay))
	{
		// SHERBET: 테마색 둥근 패널 스플래시 (기본 회색 바 → 브랜드 패널)
		const sherbet::theme &splash_theme = sherbet::default_theme();
		ImGui::SetNextWindowPos(_imgui_context->Style.WindowPadding);
		// 전체 폭 배너 — 좁게 자르면 긴 문구가 잘려서 안 예쁨(사용자 피드백)
		ImGui::SetNextWindowSize(ImVec2(imgui_io.DisplaySize.x - 20.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(splash_theme.text));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(sherbet::with_alpha(splash_theme.panel, show_spinner ? 0x00u : 0xE6u)));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(splash_theme.border));
		ImGui::Begin("Splash Window", nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoFocusOnAppearing);

		if (show_spinner)
		{
			imgui::spinner((_effects.size() - _reload_remaining_effects) / float(_effects.size()), 16.0f * ImGui::GetFontSize() / 13, 10.0f * ImGui::GetFontSize() / 13);
		}
		else
		{
			// SHERBET 리브랜드 스플래시 제목 (타이틀 폰트 + 액센트 색)
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(splash_theme.accent));
			const std::string splash_owner = sherbet::auth::effective_owner_name(_sherbet_auth);
			if (!splash_owner.empty())
				ImGui::Text("Sherbet %s \xC2\xB7 %s\xEB\x8B\x98\xEC\x9D\x84 \xEC\x9C\x84\xED\x95\x9C \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80", splash_theme.display_name, splash_owner.c_str());
			else
				ImGui::Text("Sherbet %s \xC2\xB7 by \xEC\xA0\x95\xEB\xA0\xAC", splash_theme.display_name);
			ImGui::PopStyleColor();
			ImGui::PopFont();

			// 판매 제품이라 reshade.me 업데이트 안내는 숨기고, 디스코드만 노출
			ImGui::TextDisabled("\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C: %s", SHERBET_DISCORD_URL); // "디스코드: ..."

			// (c) 부팅 스플래시 한 줄 — 오버레이를 한 번도 안 여는 구매자에게 도달하는 유일한 경로
			draw_sherbet_update_card(true);

			ImGui::Spacing();

			if (_reload_remaining_effects != 0 && _reload_remaining_effects != std::numeric_limits<size_t>::max())
			{
				ImGui::ProgressBar((_effects.size() - _reload_remaining_effects) / float(_effects.size()), ImVec2(ImGui::GetContentRegionAvail().x, 0), "");
				ImGui::SameLine(15);
				ImGui::Text(_(
					"Compiling (%zu effects remaining) ... "
					"This might take a while. The application could become unresponsive for some time."),
					_reload_remaining_effects.load());
			}
			else
			{
				ImGui::ProgressBar(0.0f, ImVec2(ImGui::GetContentRegionAvail().x, 0), "");
				ImGui::SameLine(15);

				if (_input == nullptr)
				{
					ImGui::TextColored(COLOR_YELLOW, _("No keyboard or mouse input available."));
					if (_input_gamepad != nullptr)
					{
						ImGui::SameLine();
						ImGui::TextColored(COLOR_YELLOW, _("Use gamepad instead: Press 'left + right shoulder + start button' to open the configuration overlay."));
					}
				}
				else if (_tutorial_index == 0)
				{
					const std::string label = _("ReShade is now installed successfully! Press '%s' to start the tutorial.");
					const size_t key_offset = label.find("%s");

					ImGui::TextUnformatted(label.c_str(), label.c_str() + key_offset);
					ImGui::SameLine(0.0f, 0.0f);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					ImGui::TextUnformatted(input::key_name(_overlay_key_data).c_str());
					ImGui::PopStyleColor();
					ImGui::SameLine(0.0f, 0.0f);
					ImGui::TextUnformatted(label.c_str() + key_offset + 2, label.c_str() + label.size());
				}
				else
				{
					const std::string label = _("Press '%s' to open the configuration overlay.");
					const size_t key_offset = label.find("%s");

					ImGui::TextUnformatted(label.c_str(), label.c_str() + key_offset);
					ImGui::SameLine(0.0f, 0.0f);
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
					ImGui::TextUnformatted(input::key_name(_overlay_key_data).c_str());
					ImGui::PopStyleColor();
					ImGui::SameLine(0.0f, 0.0f);
					ImGui::TextUnformatted(label.c_str() + key_offset + 2, label.c_str() + label.size());
				}
			}

			std::string error_message;
#if RESHADE_ADDON
			if (!addon_all_loaded)
				error_message += static_cast<std::string &&>(_("There were errors loading some add-ons.") + " ");
#endif
			if (!_last_reload_successful)
				error_message += static_cast<std::string &&>(_("There were errors loading some effects.") + " ");

			if (!error_message.empty())
			{
				error_message += static_cast<std::string &&>(_("Check the log for more details."));
				ImGui::Spacing();
				ImGui::TextColored(COLOR_RED, error_message.c_str());
			}
		}

		viewport_offset.y += ImGui::GetWindowHeight() + _imgui_context->Style.WindowPadding.x; // Add small space between windows

		ImGui::End();
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar(3);
	}

	if (show_message_window)
	{
		ImGui::SetNextWindowPos(_imgui_context->Style.WindowPadding + viewport_offset);
		ImGui::SetNextWindowSize(ImVec2(imgui_io.DisplaySize.x - 20.0f, 0.0f));
		ImGui::Begin("Message Window", nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoFocusOnAppearing);

		if (!_preset_save_successful)
		{
			ImGui::TextColored(COLOR_RED, _("Unable to save configuration and/or current preset. Make sure file permissions are set up to allow writing to these paths and their parent directories:\n%s\n%s"), _config_path.u8string().c_str(), _current_preset_path.u8string().c_str());
		}
		else if (show_screenshot_message)
		{
			if (!_last_screenshot_save_successful)
				if (_screenshot_directory_creation_successful)
					ImGui::TextColored(COLOR_RED, _("Unable to save screenshot because of an internal error (the format may not be supported or the drive may be full)."));
				else
					ImGui::TextColored(COLOR_RED, _("Unable to save screenshot because path could not be created: %s"), (g_reshade_base_path / _screenshot_path).u8string().c_str());
			else
				ImGui::Text(_("Screenshot successfully saved to %s"), _last_screenshot_file.u8string().c_str());
		}
		else if (show_preset_transition_message)
		{
			ImGui::Text(_("Switching preset to %s ..."), _current_preset_path.stem().u8string().c_str());
		}

		viewport_offset.y += ImGui::GetWindowHeight() + _imgui_context->Style.WindowPadding.x; // Add small space between windows

		ImGui::End();
	}

	if (show_statistics_window && !show_splash_window && !show_message_window)
	{
		ImVec2 fps_window_pos(5, 5);
		ImVec2 fps_window_size(200, 0);

		// Get last calculated window size (because of 'ImGuiWindowFlags_AlwaysAutoResize')
		if (ImGuiWindow *const fps_window = ImGui::FindWindowByName("OSD"))
		{
			fps_window_size  = fps_window->Size;
			const int osd_lines = _sherbet_osd_horizontal
				? ((show_clock || show_fps || show_frametime || show_preset_name) ? 1 : 0)
				: ((show_clock ? 1 : 0) + (show_fps ? 1 : 0) + (show_frametime ? 1 : 0) + (show_preset_name ? 1 : 0));
			fps_window_size.y = std::max(fps_window_size.y, _imgui_context->Style.FramePadding.y * 4.0f + _imgui_context->Style.ItemSpacing.y +
				(_imgui_context->Style.ItemSpacing.y + _imgui_context->Style.FontSizeBase * _fps_scale) * osd_lines);
		}

		// SHERBET: 자유 X/Y 배치 (화면 비율)
		fps_window_pos.x = _sherbet_osd_x * ImMax(0.0f, imgui_io.DisplaySize.x - fps_window_size.x);
		fps_window_pos.y = _sherbet_osd_y * ImMax(0.0f, imgui_io.DisplaySize.y - fps_window_size.y);
		const bool osd_align_right = _sherbet_osd_x > 0.5f;

		ImGuiWindowFlags osd_flags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBackground |
			ImGuiWindowFlags_AlwaysAutoResize;
		if (!_show_overlay)
			osd_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		// 오버레이 열림 시엔 SetNextWindowPos를 강제하지 않아 사용자가 드래그로 옮길 수 있게 함
		if (!_show_overlay)
			ImGui::SetNextWindowPos(fps_window_pos);
		else
			ImGui::SetNextWindowPos(fps_window_pos, ImGuiCond_Appearing);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(_fps_col[0], _fps_col[1], _fps_col[2], _fps_col[3]));
		ImGui::Begin("OSD", nullptr, osd_flags);

		ImGui::PushFont(nullptr, _imgui_context->Style.FontSizeBase * _fps_scale);

		const float content_width = ImGui::GetContentRegionAvail().x;
		char temp[32];

		// SHERBET: 가로 배치 옵션 — 켜면 항목을 " | " 로 이어 한 줄로, 끄면 기존처럼 세로로 쌓는다.
		std::string osd_row; // 가로 모드 누적 버퍼
		auto osd_emit = [&](const char *text, int len) {
			if (_sherbet_osd_horizontal)
			{
				if (!osd_row.empty()) osd_row += "  |  ";
				osd_row.append(text, len);
			}
			else
			{
				if (osd_align_right) // Align text to the right of the window(세로 모드만)
					ImGui::SetCursorPosX(content_width - ImGui::CalcTextSize(text, text + len).x + _imgui_context->Style.ItemSpacing.x);
				ImGui::TextUnformatted(text, text + len);
			}
		};

		if (show_clock)
		{
			const std::time_t t = std::chrono::system_clock::to_time_t(_current_time);
			struct tm tm; localtime_s(&tm, &t);

			int temp_size;
			switch (_clock_format)
			{
			default:
			case 0:
				temp_size = ImFormatString(temp, IM_ARRAYSIZE(temp), "%02d:%02d", tm.tm_hour, tm.tm_min);
				break;
			case 1:
				temp_size = ImFormatString(temp, IM_ARRAYSIZE(temp), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
				break;
			case 2:
				temp_size = ImFormatString(temp, IM_ARRAYSIZE(temp), "%.4d-%.2d-%.2d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
				break;
			}
			osd_emit(temp, temp_size);
		}
		if (show_fps)
		{
			const int temp_size = ImFormatString(temp, IM_ARRAYSIZE(temp), "%.0f fps", imgui_io.Framerate);
			osd_emit(temp, temp_size);
		}
		if (show_frametime)
		{
			const int temp_size = ImFormatString(temp, IM_ARRAYSIZE(temp), "%5.2f ms", 1000.0f / imgui_io.Framerate);
			osd_emit(temp, temp_size);
		}
		if (show_preset_name)
		{
			const std::string preset_name = _current_preset_path.stem().u8string();
			osd_emit(preset_name.c_str(), static_cast<int>(preset_name.size()));
		}

		// 가로 모드: 누적한 한 줄을 그린다(오른쪽 정렬 지원)
		if (_sherbet_osd_horizontal && !osd_row.empty())
		{
			if (osd_align_right)
				ImGui::SetCursorPosX(content_width - ImGui::CalcTextSize(osd_row.c_str(), osd_row.c_str() + osd_row.size()).x + _imgui_context->Style.ItemSpacing.x);
			ImGui::TextUnformatted(osd_row.c_str(), osd_row.c_str() + osd_row.size());
		}

		ImGui::Dummy(ImVec2(200, 0)); // Force a minimum window width

		ImGui::PopFont();

		if (_show_overlay)
		{
			const ImVec2 wp = ImGui::GetWindowPos();
			const ImVec2 ws = ImGui::GetWindowSize();
			const float nx = (imgui_io.DisplaySize.x - ws.x) > 1.0f ? wp.x / (imgui_io.DisplaySize.x - ws.x) : 0.0f;
			const float ny = (imgui_io.DisplaySize.y - ws.y) > 1.0f ? wp.y / (imgui_io.DisplaySize.y - ws.y) : 0.0f;
			const float cnx = ImClamp(nx, 0.0f, 1.0f);
			const float cny = ImClamp(ny, 0.0f, 1.0f);
			if (fabsf(cnx - _sherbet_osd_x) > 0.001f || fabsf(cny - _sherbet_osd_y) > 0.001f)
			{
				_sherbet_osd_x = cnx;
				_sherbet_osd_y = cny;
				save_config();
			}
		}

		ImGui::End();
		ImGui::PopStyleColor();
	}

	// ⚠️ 영역을 잡는 동안에는 Sherbet 창을 그리지 않는다. HUD 는 게임 화면에 있으므로
	//    창이 그 위를 덮으면 무엇을 잡는지 볼 수 없다. 창이 없으면 IO.WantCaptureMouse 가
	//    false 라 드래그가 그대로 들어오고, _show_overlay 는 여전히 true 라 게임 입력은
	//    계속 막힌다(오발 없음). 나가는 길은 우클릭/Esc 이며 안내를 화면에 띄운다.
	if (_show_overlay && !_sherbet_mag_picking)
	{
		const ImGuiViewport *const viewport = ImGui::GetMainViewport();

		sherbet::apply_style(_imgui_context->Style, sherbet::active_theme());

		// Change font size if user presses the control key and moves the mouse wheel
		if (!_no_font_scaling && imgui_io.KeyCtrl && imgui_io.MouseWheel != 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
		{
			_imgui_context->Style.FontScaleMain = ImClamp(_imgui_context->Style.FontScaleMain + imgui_io.MouseWheel * 0.25f, 0.5f, 4.0f);
			save_config();

			_is_font_scaling = true;
		}

		if (_is_font_scaling)
		{
			if (!imgui_io.KeyCtrl)
				_is_font_scaling = false;

			ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, _imgui_context->Style.WindowPadding * 2.0f);
			ImGui::Begin("FontScaling", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
			ImGui::Text(_("Scaling font size (%d) with 'Ctrl' + mouse wheel"), static_cast<int>(_imgui_context->Style.FontSizeBase * _imgui_context->Style.FontScaleMain));
			ImGui::End();
			ImGui::PopStyleVar();
		}

		// SHERBET: 아담한 플로팅 오버레이 창 — 전체화면이 아니라 게임 위에 뜨는 카드형 창.
		// 이동/크기조절 가능하며, 옮긴 위치·크기는 ImGui 설정으로 유지된다.
		// 기본/최소 높이를 넉넉히 — 리쉐이드 원본 홈의 하단바(다시 로드/성능 모드)가
		// 낮은 창에서 화면 밖으로 밀려 잘리던 문제(사용자 피드백) 방지
		const ImVec2 sherbet_win_size(770.0f, ImClamp(viewport->Size.y * 0.84f, 560.0f, 900.0f));
		ImGui::SetNextWindowPos(
			viewport->Pos + ImVec2((viewport->Size.x - sherbet_win_size.x) * 0.5f, viewport->Size.y * 0.07f),
			ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(sherbet_win_size, ImGuiCond_FirstUseEver);
		// 최소 높이도 하단바가 항상 보이도록 상향(단, 화면보다 크게는 안 되게 max=viewport)
		ImGui::SetNextWindowSizeConstraints(ImVec2(540.0f, ImMin(560.0f, viewport->Size.y)), viewport->Size);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("Sherbet###Viewport", nullptr,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoBackground);
		ImGui::PopStyleVar();

		{
			// 애니메이션 배경 + 파티클 — '창 영역'에만 그린다(게임은 창 밖에서 그대로 보임)
			// 창 전체가 항상 화면 안에 있도록 위치를 매 프레임 보정.
			// (예전 전체화면 빌드가 imgui.ini 에 남긴 큰 창 크기/아래로 치우친 위치 때문에
			//  창 하단이 화면 밖으로 밀려 하단바(다시 로드/성능 모드)가 잘리던 문제 방지)
			const ImVec2 sherbet_wpos = ImGui::GetWindowPos();
			const ImVec2 sherbet_wsize = ImGui::GetWindowSize();
			const ImVec2 sherbet_clamped(
				ImClamp(sherbet_wpos.x, viewport->Pos.x, viewport->Pos.x + ImMax(0.0f, viewport->Size.x - sherbet_wsize.x)),
				ImClamp(sherbet_wpos.y, viewport->Pos.y, viewport->Pos.y + ImMax(0.0f, viewport->Size.y - sherbet_wsize.y)));
			if (sherbet_clamped.x != sherbet_wpos.x || sherbet_clamped.y != sherbet_wpos.y)
				ImGui::SetWindowPos(sherbet_clamped);

			ImDrawList *const sherbet_bg = ImGui::GetWindowDrawList();
			const ImVec2 sherbet_vmin = ImGui::GetWindowPos();
			const ImVec2 sherbet_vmax = ImVec2(sherbet_vmin.x + ImGui::GetWindowSize().x, sherbet_vmin.y + ImGui::GetWindowSize().y);
			static float s_sherbet_time = 0.0f; s_sherbet_time += _imgui_context->IO.DeltaTime;
			// SHERBET: custompicture 기능 구매자 — 켜져 있고 이미지가 로딩됐으면 그라디언트 대신 사진 배경 + 가독성 스크림
			if (_sherbet_bg_dirty)
				sherbet_load_background();
			// 판정은 sherbet_feature_unlocked 로 한다 — has_feature 만 보면 「잠금 화면
			// 미리보기」를 켜도 배경은 계속 그려져서, 판매자가 잠긴 화면을 확인할 수 없다.
			// (잠긴 기능은 UI 뿐 아니라 일도 돌면 안 된다 — sherbet_paid.hpp 규약.)
			const bool sherbet_photo_bg = _sherbet_bg_on && sherbet_feature_unlocked("custompicture") && _sherbet_bg_srv != 0 && _sherbet_bg_w > 0;
			if (sherbet_photo_bg)
			{
				// 창을 덮도록 커버-핏(비율 유지, 넘치는 부분은 잘라냄) UV 계산
				const float win_w = sherbet_vmax.x - sherbet_vmin.x, win_h = sherbet_vmax.y - sherbet_vmin.y;
				const float img_ar = static_cast<float>(_sherbet_bg_w) / static_cast<float>(_sherbet_bg_h);
				const float win_ar = win_h > 0.0f ? win_w / win_h : 1.0f;
				ImVec2 uv0(0, 0), uv1(1, 1);
				if (img_ar > win_ar) { const float u = win_ar / img_ar; uv0.x = 0.5f - u * 0.5f; uv1.x = 0.5f + u * 0.5f; }
				else                 { const float v = img_ar / win_ar; uv0.y = 0.5f - v * 0.5f; uv1.y = 0.5f + v * 0.5f; }
				const float op = ImClamp(_sherbet_bg_opacity, 0.0f, 1.0f);
				sherbet_bg->AddImageRounded(_sherbet_bg_srv.handle, sherbet_vmin, sherbet_vmax, uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(op * 255.0f)), 12.0f);
				const int dim = static_cast<int>(ImClamp(_sherbet_bg_dim, 0.0f, 1.0f) * 255.0f);
				if (dim > 0)
					sherbet_bg->AddRectFilled(sherbet_vmin, sherbet_vmax, IM_COL32(0, 0, 0, dim), 12.0f);
			}
			else
			{
				sherbet::draw_background(sherbet_bg, sherbet_vmin, sherbet_vmax, sherbet::active_theme(), s_sherbet_time, 12.0f);
			}
			sherbet::draw_particles(sherbet_bg, sherbet_vmin, sherbet_vmax, sherbet::active_theme(), s_sherbet_time);
			// 얇은 테두리(카드 느낌)
			sherbet_bg->AddRect(sherbet_vmin, sherbet_vmax, sherbet::active_theme().border, 12.0f, 0, 1.5f);
		}

		// SHERBET: 온라인 인증 — 미인증이면 로그인 패널만 노출(효과/오버레이 잠금)
		_sherbet_auth.tick();
		// 콘텐츠 페치 완료 엣지 감지(진행중→끝) → "불러오기 완료" 배너 3초 표시
		{
			const bool active_now = _sherbet_auth.content_active();
			if (_sherbet_content_was_active && !active_now)
				_sherbet_content_done_timer = 3.0f;
			_sherbet_content_was_active = active_now;
			if (_sherbet_content_done_timer > 0.0f)
				_sherbet_content_done_timer -= _imgui_context->IO.DeltaTime;
		}
		{ std::string _content; if (_sherbet_auth.take_content(_content)) sherbet::apply_content(_content); }
		if (_sherbet_auth.take_files_changed())
		{
			const std::filesystem::path fx_dir = _config_path.parent_path() / L"Sherbet-Fx";
			bool added = false;
			if (std::find(_effect_search_paths.begin(), _effect_search_paths.end(), fx_dir) == _effect_search_paths.end())
			{ _effect_search_paths.push_back(fx_dir); added = true; }
			if (std::find(_texture_search_paths.begin(), _texture_search_paths.end(), fx_dir) == _texture_search_paths.end())
			{ _texture_search_paths.push_back(fx_dir); added = true; }
			if (added) save_config();
			reload_effects();
		}
		// SHERBET: 자동 롤백 직후 세션(스펙 §5.4 R11) — 매핑된 이미지가 마커가 비난하는
		// 그 바이너리라 update_effects 가 조기 반환한다. 그러면 오버레이가 평소처럼 보이는데
		// 효과만 안 걸리는 상태가 되므로, 정상 UI 대신 **무슨 일이 있었는지**만 설명한다.
		// ⚠️ 이 상태에서도 오버레이는 그린다. DllMain 에서 `return FALSE` 로 막으면
		//    dxgi 프록시 export 가 사라져 게임 자체가 안 켜진다.
		if (sherbet::update::safe_mode())
		{
			ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			ImGui::TextUnformatted("Sherbet");
			ImGui::PopFont();

			const float avail_w = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y * 0.24f));
			auto centered_text = [avail_w](const char *text) {
				const float tw = ImGui::CalcTextSize(text).x;
				ImGui::SetCursorPosX((avail_w - tw) * 0.5f);
				ImGui::TextUnformatted(text);
			};

			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.4f);
			centered_text(ICON_FK_UNDO "  \xEC\x9D\xB4\xEC\xA0\x84 \xEB\xB2\x84\xEC\xA0\x84\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"); // "이전 버전으로 되돌렸습니다"
			ImGui::PopFont();
			ImGui::Spacing();

			// 어떤 버전이 실패했는지는 마커가 알고 있다(state 와 무관하게 읽힌다).
			const std::string bad = sherbet::update::rolled_back_version();
			if (!bad.empty())
			{
				char line[160];
				std::snprintf(line, sizeof(line),
					"v%s \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8\xEA\xB0\x80 \xEC\x8B\xA4\xED\x8C\xA8\xED\x95\xB4\xEC\x84\x9C \xEC\x9D\xB4\xEC\xA0\x84 \xEB\xB2\x84\xEC\xA0\x84\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94.", bad.c_str()); // "v… 업데이트가 실패해서 이전 버전으로 되돌렸어요."
				centered_text(line);
			}
			centered_text("\xEC\x9D\xB4\xEB\xB2\x88 \xED\x8C\x90\xEC\x97\x90\xEB\x8A\x94 \xED\x9A\xA8\xEA\xB3\xBC\xEA\xB0\x80 \xEA\xBA\xBC\xEC\xA0\xB8 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94. \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xBB\x90\xEB\x8B\xA4 \xEC\xBC\x9C\xEB\xA9\xB4 \xEC\xA0\x95\xEC\x83\x81\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4."); // "이번 판에는 효과가 꺼져 있어요. 게임을 껐다 켜면 정상으로 돌아옵니다."

			ImGui::Spacing();
			ImGui::Spacing();
			// 복구 수단([그래도 다시 시도] · 새 버전이 있으면 [업데이트])
			draw_sherbet_update_card(false);

			ImGui::End();
		}
		else
		if (sherbet::auth::enabled() && !_sherbet_auth.is_authed())
		{
			ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			ImGui::TextUnformatted("Sherbet");
			ImGui::PopFont();

			const float avail_w = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y * 0.30f));
			auto centered_text = [avail_w](const char *text) {
				const float tw = ImGui::CalcTextSize(text).x;
				ImGui::SetCursorPosX((avail_w - tw) * 0.5f);
				ImGui::TextUnformatted(text);
			};

			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.4f);
			centered_text(ICON_FK_LOCK "  \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8\xEC\x9D\xB4 \xED\x95\x84\xEC\x9A\x94\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "로그인이 필요합니다"
			ImGui::PopFont();
			ImGui::Spacing();

			// 로그인 버튼(진행 중이면 비활성 + 스피너 대신 텍스트)
			const bool busy = _sherbet_auth.login_active();
			const char *btn = ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8"; // "디스코드로 로그인"
			const float bw = ImGui::CalcTextSize(btn).x + 40.0f;
			ImGui::SetCursorPosX((avail_w - bw) * 0.5f);
			ImGui::BeginDisabled(busy);
			if (ImGui::Button(btn, ImVec2(bw, 0.0f)))
				_sherbet_auth.begin_login();
			ImGui::EndDisabled();

			// 상태 텍스트(대기중/거부 사유/실패) — status_text()는 락 안에서 복사한 std::string 값 반환
			const std::string st = _sherbet_auth.status_text();
			if (!st.empty()) { ImGui::Spacing(); centered_text(st.c_str()); }

			ImGui::Spacing();
			centered_text("\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 \xEC\x9E\x90\xEB\x8F\x99\xEC\x9C\xBC\xEB\xA1\x9C \xEC\xA7\x84\xED\x96\x89\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "로그인 후 자동으로 진행됩니다"

			// (b) 인증 게이트 패널 안 — ★ **로그인이 깨진 빌드를 구제하는 유일한 경로다.**
			// 매니페스트 엔드포인트가 미인증이라 여기서도 업데이트가 실제로 동작한다.
			ImGui::Spacing();
			ImGui::Spacing();
			draw_sherbet_update_card(false);

			ImGui::End();
		}
		else
		// SHERBET: 노드락 — 등록되지 않은 PC면 오버레이 콘텐츠 대신 안내문만 표시
		// (헤더의 원클릭 버튼도 인증 통과 후에만 그려져, 미승인 PC에서 기능이 눌리지 않음)
		if (!sherbet::nodelock::is_authorized(_config_path.parent_path().u8string()))
		{
			ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			ImGui::TextUnformatted("Sherbet");
			ImGui::PopFont();
			const float avail_w = ImGui::GetContentRegionAvail().x;
			ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y * 0.32f));
			auto centered = [avail_w](const char *text) {
				const float tw = ImGui::CalcTextSize(text).x;
				ImGui::SetCursorPosX((avail_w - tw) * 0.5f);
				ImGui::TextUnformatted(text);
			};
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			centered(ICON_FK_LOCK "  \xEC\x9D\xB4 \xEB\xB9\x8C\xEB\x93\x9C\xEB\x8A\x94 \xEB\x8B\xA4\xEB\xA5\xB8 PC\xEC\x97\x90 \xEB\x93\xB1\xEB\xA1\x9D\xEB\x90\x98\xEC\x96\xB4 \xEC\x9E\x88\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"); // "이 빌드는 다른 PC에 등록되어 있습니다"
			ImGui::PopFont();
			ImGui::Spacing();
			centered("\xEC\xB2\x98\xEC\x9D\x8C \xEC\x8B\xA4\xED\x96\x89\xED\x95\x9C PC\xEC\x97\x90\xEC\x84\x9C\xEB\xA7\x8C \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEA\xB0\x80 \xEC\x97\xB4\xEB\xA6\xBD\xEB\x8B\x88\xEB\x8B\xA4"); // "처음 실행한 PC에서만 오버레이가 열립니다"
			centered("PC \xEB\xB3\x80\xEA\xB2\xBD/\xEC\x9E\xAC\xEC\x84\xA4\xEC\xB9\x98\xEB\x8A\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\xAC\xB8\xEC\x9D\x98\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"); // "PC 변경/재설치는 디스코드로 문의해 주세요"
			ImGui::Spacing();
			const char *btn = ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xAC\xB8\xEC\x9D\x98"; // "디스코드 문의"
			ImGui::SetCursorPosX((avail_w - ImGui::CalcTextSize(btn).x) * 0.5f);
			ImGui::TextLinkOpenURL(btn, SHERBET_DISCORD_URL);
			ImGui::End();
		}
		else
		{
		// 상단 헤더 = 브랜드 + 원클릭 버튼(스샷·성능·리로드) + 드래그 핸들(빈 공간을 끌면 창 이동)
		{
			ImGui::SetCursorPos(ImVec2(16.0f, 9.0f));
			ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.5f);
			const float sherbet_title_h = ImGui::GetFontSize(); // 실제 렌더 크기(FontScaleMain 반영)
			ImGui::TextUnformatted("Sherbet");
			ImGui::PopFont();
			ImGui::SameLine(0.0f, 8.0f);
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("%s", sherbet::active_theme().display_name);

			// 폰트 확대(Ctrl+휠)에도 겹치지 않게 헤더 높이/버튼 크기를 폰트 기준으로 계산
			const float sherbet_header_h = ImMax(sherbet::header_height, sherbet_title_h + 18.0f);
			// 우측 원클릭 버튼 3종 (기존 기능 호출만)
			const float bh = ImGui::GetFrameHeight() * 0.9f, bw = bh * 1.4f, gap = 6.0f;
			const float win_w = ImGui::GetWindowSize().x;
			const float buttons_left = win_w - 14.0f - (bw * 4.0f + gap * 3.0f);

			// SHERBET: 구매자 각인 — 테마 이름 오른쪽. 스플래시·정보 탭과 **같은 이름**을 쓴다
			// (서버가 릴레이한 디스코드 표시이름 우선 → SHERBET_OWNER 폴백 → 빈 문자열).
			if (const std::string hdr_owner = sherbet::auth::effective_owner_name(_sherbet_auth); !hdr_owner.empty())
			{
				// ⚠️ 오른쪽 원클릭 버튼 4개를 침범하면 안 된다. 남는 폭을 재서 긴 문구가 안 들어가면
				//    짧은 문구로, 그것도 안 들어가면 **아예 그리지 않는다** — 반쯤 잘린 각인은
				//    각인이 아니라 버그로 보인다("Sherbet" 이 "bet" 으로 보이던 그 건과 같은 실수).
				//    폰트 크기는 사용자가 바꿀 수 있으므로 고정 픽셀로 재지 않고 매번 잰다.
				ImGui::SameLine(0.0f, 8.0f);
				const float room = buttons_left - ImGui::GetCursorPosX() - 8.0f;
				const std::string mark_full = "\xC2\xB7 " + hdr_owner + " \xEC\xA0\x84\xEC\x9A\xA9 \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEB\xA6\xAC\xEC\x89\x90\xEC\x9D\xB4\xEB\x93\x9C"; // "· <이름> 전용 커스텀 리쉐이드"
				const std::string mark_short = "\xC2\xB7 " + hdr_owner + " \xEC\xA0\x84\xEC\x9A\xA9 \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80"; // "· <이름> 전용 커스텀"
				if (ImGui::CalcTextSize(mark_full.c_str()).x <= room)
					ImGui::TextDisabled("%s", mark_full.c_str());
				else if (ImGui::CalcTextSize(mark_short.c_str()).x <= room)
					ImGui::TextDisabled("%s", mark_short.c_str());
				// 자리가 없으면 그리지 않는다. 바로 아래 SetCursorPos 가 커서를 절대좌표로 옮기므로
				// 여기서 줄을 정리해 줄 필요는 없다.
			}

			ImGui::SetCursorPos(ImVec2(buttons_left, (sherbet_header_h - bh) * 0.5f));
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(sherbet::active_theme().chip));
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(sherbet::active_theme().text));
			// 반반 비교 토글 — 켜져 있으면 액센트 색으로 강조
			const bool cmp_on = _sherbet_compare_active;
			if (cmp_on)
				ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(sherbet::active_theme().accent));
			if (ImGui::Button(ICON_FK_ADJUST "##sherbet_compare", ImVec2(bw, bh)))
				_sherbet_compare_active = !_sherbet_compare_active;
			if (cmp_on)
				ImGui::PopStyleColor();
			ImGui::SetItemTooltip("\xEB\xB0\x98\xEB\xB0\x98 \xEB\xB9\x84\xEA\xB5\x90(\xEC\xA0\x81\xEC\x9A\xA9 \xEC\xA0\x84/\xED\x9B\x84)"); // "반반 비교(적용 전/후)"
			ImGui::SameLine(0.0f, gap);
			if (ImGui::Button(ICON_FK_CAMERA "##sherbet_shot", ImVec2(bw, bh)))
				save_screenshot(nullptr);
			ImGui::SetItemTooltip("\xEC\x8A\xA4\xED\x81\xAC\xEB\xA6\xB0\xEC\x83\xB7"); // "스크린샷"
			ImGui::SameLine(0.0f, gap);
			ImGui::BeginDisabled(is_loading());
			if (ImGui::Button(ICON_FK_BOLT "##sherbet_perf", ImVec2(bw, bh)))
			{ _performance_mode = !_performance_mode; save_config(); reload_effects(); }
			ImGui::SetItemTooltip("\xEC\x84\xB1\xEB\x8A\xA5 \xEB\xAA\xA8\xEB\x93\x9C"); // "성능 모드"
			ImGui::SameLine(0.0f, gap);
			if (ImGui::Button(ICON_FK_REFRESH "##sherbet_reload", ImVec2(bw, bh)))
				reload_effects();
			ImGui::SetItemTooltip("\xEB\x8B\xA4\xEC\x8B\x9C \xEB\xA1\x9C\xEB\x93\x9C"); // "다시 로드"
			ImGui::EndDisabled();
			ImGui::PopStyleColor(2);
			ImGui::PopStyleVar();

			ImGui::SetCursorPos(ImVec2(0.0f, sherbet_header_h));
		}

		// 좌측 레일
		// 레일 배경은 테마의 bg0 를 살짝 얹는다 — 검정 고정이면 라이트 테마(딸기)에서 회색 띠로 떠 보임.
		// 단, 커스텀 사진 배경이 켜져 있으면 테마색이 사진과 충돌하므로 중립적인 어두운 스크림으로 대체한다.
		const bool sherbet_rail_photo = _sherbet_bg_on && sherbet_feature_unlocked("custompicture") && _sherbet_bg_srv != 0 && _sherbet_bg_w > 0;
		const ImU32 sherbet_rail_bg = sherbet_rail_photo
			? IM_COL32(12, 12, 16, 200)                                   // 사진 위: 어떤 사진이든 어울리는 중립 다크 스크림
			: sherbet::with_alpha(sherbet::active_theme().bg0, 110);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(sherbet_rail_bg));
		ImGui::BeginChild("##sherbet_rail", ImVec2(sherbet::rail_width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleColor();
		{
			ImGui::Dummy(ImVec2(0, 8));
			// 로고 (클릭 시 홈으로 — 반응 없던 문제 해결)
			ImGui::SetCursorPosX((sherbet::rail_width - sherbet::rail_button_size) * 0.5f);
			// ⚠️ 로고는 **선택 표시를 하지 않는다**(active=false). 예전엔 홈 탭 조건을 그대로
			//    넘겨서, 오버레이를 열면 기본이 홈이라 로고와 홈 아이콘이 **둘 다** 액센트 알약 +
			//    글로우로 켜졌다 — 어느 쪽이 지금 탭인지 알 수 없는 첫 화면이었다.
			//    대신 idle_col 로 브랜드 색은 살린다(안 그러면 로고가 text_dim 으로 죽는다).
			if (sherbet::rail_button("##logo", ICON_FK_MAGIC, false, sherbet::active_theme().accent))
				_sherbet_tab = 0;
			ImGui::SetItemTooltip("Sherbet \xED\x99\x88"); // "Sherbet 홈"
			ImGui::Dummy(ImVec2(0, 10));

			struct RailItem { const char *id; const char *icon; int tab; };
			// ⚠️ tab 번호는 config 에 저장되므로 기존 번호(0~4)를 재사용/재배치하지 않는다.
			//    새 「에임」 탭은 남는 번호 5 를 쓰고, 화면 순서만 마켓 다음으로 둔다.
			const RailItem items[] = {
				{ "##tab_home", ICON_FK_HOME, 0 },
				{ "##tab_market", ICON_FK_SHOPPING_CART, 1 },
				{ "##tab_aim", ICON_FK_CROSSHAIRS, 5 },
				{ "##tab_aimlab", ICON_FK_BULLSEYE, 7 },
				{ "##tab_optimize", ICON_FK_DASHBOARD, 6 },
				{ "##tab_settings", ICON_FK_SLIDERS, 2 },
				{ "##tab_about", ICON_FK_INFO_CIRCLE, 3 },
				{ "##tab_addons", ICON_FK_PUZZLE_PIECE, 4 },
			};
			// 애드온 탭은 배열 마지막이므로 개수만 늘리면 노출된다.
			// ⚠️ 배열에 항목을 추가하면 **반드시 여기도 같이 늘린다** — 안 그러면 마지막
			//    탭이 조용히 사라진다(에임 탭 때 실제로 겪었다).
			//    에임 4→5, 최적화 5→6, 사격 훈련 6→7.
			int item_count = 7;
#if RESHADE_ADDON
			// 서드파티 애드온(REST 등)이 로드돼 있을 때만 Add-ons 탭을 노출한다 (일반 구매자에겐 숨김).
			// .addon 파일 로드분은 external=false 이지만 file 이 채워지고(REST), .asi 등 외부 등록분은 external=true.
			// 빌트인(Generic Depth 등)은 external=false + file 이 비어 있어 자연히 제외된다.
			for (const addon_info &info : addon_loaded_info)
				if (info.external || !info.file.empty()) { item_count = 8; break; }
#endif
			for (int i = 0; i < item_count; ++i)
			{
				ImGui::SetCursorPosX((sherbet::rail_width - sherbet::rail_button_size) * 0.5f);
				if (sherbet::rail_button(items[i].id, items[i].icon, _sherbet_tab == items[i].tab))
					_sherbet_tab = items[i].tab;
				ImGui::Dummy(ImVec2(0, 4));
			}
		}
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 0.0f);

		// 콘텐츠
		// ⚠️ 자식창 ID 를 탭마다 갈라 준다. 고정 문자열 하나로 두면 ImGui 가 7개 탭을 **같은 창**
		//    으로 취급해 스크롤 위치(ImGuiWindow::Scroll)와 GetStateStorage() 가 탭끼리 새어 넘어간다:
		//    설정 탭을 끝까지 내린 뒤 마켓을 열면 맨 위가 아니라 중간부터 보이고, 전환 첫 프레임엔
		//    Begin() 의 스크롤 클램프가 **직전 프레임(=이전 탭)의 ContentSize** 로 계산돼 화면이 튄다.
		//    마켓은 세그먼트마다 내용 길이가 크게 달라 세그먼트까지 시드에 넣는다.
		ImGui::PushID(_sherbet_tab * 10 + (_sherbet_tab == 1 ? _sherbet_market_seg : 0));
		ImGui::BeginChild("##sherbet_content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoFocusOnAppearing);
		switch (_sherbet_tab)
		{
		case 0: draw_gui_home(); break;
		case 1: draw_gui_market(); break;
		case 2: draw_gui_settings(); break;
		case 3: draw_gui_about(); break;
#if RESHADE_ADDON
		case 4: draw_gui_addons(); break;
#endif
		case 5: draw_gui_aim(); break;
		case 6: draw_gui_optimize(); break;
		case 7: draw_gui_aimlab(); break;
		default: draw_gui_home(); break;
		}
		ImGui::EndChild();
		ImGui::PopID(); // ⚠️ 반드시 EndChild() 뒤 — 짝이 어긋나면 ID 스택이 다음 프레임으로 샌다

		ImGui::End();
		} // SHERBET: 노드락 else 블록 끝

		if (!_editors.empty())
		{
			if (ImGui::Begin(_("Edit###editor"), nullptr, ImGuiWindowFlags_NoFocusOnAppearing) &&
				ImGui::BeginTabBar("editor_tabs"))
			{
				for (auto it = _editors.begin(); it != _editors.end();)
				{
					std::string title = it->entry_point_name.empty() ? it->file_path.filename().u8string() : it->entry_point_name;
					title += " ###editor" + std::to_string(std::distance(_editors.begin(), it));

					bool is_open = true;
					ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
					if (it->editor.is_modified())
						flags |= ImGuiTabItemFlags_UnsavedDocument;
					if (it->selected)
						flags |= ImGuiTabItemFlags_SetSelected;

					if (ImGui::BeginTabItem(title.c_str(), &is_open, flags))
					{
						draw_code_editor(*it);
						ImGui::EndTabItem();
					}

					it->selected = false;

					if (!is_open)
						it = _editors.erase(it);
					else
						++it;
				}

				ImGui::EndTabBar();
			}
			ImGui::End();
		}
	}

#if RESHADE_ADDON == 1
	if (addon_enabled)
#endif
#if RESHADE_ADDON
	{
		for (const addon_info &info : addon_loaded_info)
		{
			for (const addon_info::overlay_callback &widget : info.overlay_callbacks)
			{
				if (widget.title == "OSD" ? show_splash_window : !_show_overlay)
					continue;

				if (ImGui::Begin(widget.title.c_str(), nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
					widget.callback(this);
				ImGui::End();
			}
		}

		invoke_addon_event<addon_event::reshade_overlay>(this);
	}
#endif

	if (_effects_enabled &&
		_preview_texture < _textures.size() &&
		std::any_of(_textures[_preview_texture].shared.begin(), _textures[_preview_texture].shared.end(),
			[this](const size_t effect_index) { return _effects[effect_index].rendering; }))
	{
		if (!_show_overlay)
		{
			// Create a temporary viewport window to attach image to when overlay is not open
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(imgui_io.DisplaySize.x, imgui_io.DisplaySize.y));
			ImGui::Begin("Viewport", nullptr,
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoNav |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoDocking |
				ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoBringToFrontOnFocus |
				ImGuiWindowFlags_NoBackground);
			ImGui::End();
		}

		// Scale image to fill the entire viewport by default
		ImVec2 preview_min = ImVec2(0, 0);
		ImVec2 preview_max = imgui_io.DisplaySize;

		// Positing image in the middle of the viewport when using original size
		if (_preview_size[0])
		{
			preview_min.x = (preview_max.x * 0.5f) - (_preview_size[0] * 0.5f);
			preview_max.x = (preview_max.x * 0.5f) + (_preview_size[0] * 0.5f);
		}
		if (_preview_size[1])
		{
			preview_min.y = (preview_max.y * 0.5f) - (_preview_size[1] * 0.5f);
			preview_max.y = (preview_max.y * 0.5f) + (_preview_size[1] * 0.5f);
		}

		const api::resource_view srv = _textures[_preview_texture].srv[0];
		assert(srv != 0);

		ImGui::FindWindowByName("Viewport")->DrawList->AddImage(srv.handle, preview_min, preview_max, ImVec2(0, 0), ImVec2(1, 1), _preview_size[2]);
	}

#if RESHADE_LOCALIZATION
	resources::set_current_language(prev_language);
#endif

	// Disable keyboard shortcuts while typing into input boxes
	_ignore_shortcuts |= ImGui::IsAnyItemActive();

	// Render ImGui widgets and windows
	ImGui::Render();

	if (_primary_input_handler && _input != nullptr)
	{
		const bool block_input = _input_processing_mode != 0 && (_show_overlay || _block_input_next_frame);
		const bool block_mouse_input = block_input && (imgui_io.WantCaptureMouse || _input_processing_mode == 2);
		const bool block_keyboard_input = block_input && (imgui_io.WantCaptureKeyboard || _input_processing_mode == 2);

		_input->block_mouse_input(block_mouse_input);
		_input->block_keyboard_input(block_keyboard_input);
		_input->block_mouse_cursor_warping(_show_overlay || _block_input_next_frame || block_mouse_input);
	}

	if (ImDrawData *const draw_data = ImGui::GetDrawData();
		draw_data != nullptr && draw_data->CmdListsCount != 0 && draw_data->TotalVtxCount != 0)
	{
		api::command_list *const cmd_list = _graphics_queue->get_immediate_command_list();

		if (_back_buffer_resolved != 0)
		{
			render_imgui_draw_data(cmd_list, draw_data, _back_buffer_targets[0]);
		}
		else
		{
			uint32_t back_buffer_index = get_current_back_buffer_index() * 2;
			const api::resource back_buffer_resource = _device->get_resource_from_view(_back_buffer_targets[back_buffer_index]);

			cmd_list->barrier(back_buffer_resource, api::resource_usage::present, api::resource_usage::render_target);
			render_imgui_draw_data(cmd_list, draw_data, _back_buffer_targets[back_buffer_index]);
			cmd_list->barrier(back_buffer_resource, api::resource_usage::render_target, api::resource_usage::present);
		}
	}

	ImGui::SetCurrentContext(backup_context);
}


// SHERBET: 자동 업데이트 배너(스펙 §6 '배너 배치', §3.5, §5.4 R13).
// ⚠️ **오버레이 안에만 두면 Home 키를 안 누르는 구매자에게 영영 도달하지 않는다.**
//    그래서 홈 탭 최상단 · 인증 게이트 패널 안 · 부팅 스플래시 세 곳에서 부른다.
//    (인증 게이트 안이 특히 중요하다 — 로그인이 깨진 빌드를 구제하는 유일한 경로다.)
void reshade::runtime::draw_sherbet_update_card(bool compact)
{
	sherbet::update::controller &uc = sherbet::update::instance();

	const bool personal = uc.personalized();
	const bool done = uc.need_restart();
	const bool busy = uc.busy();
	const bool offer = uc.has_offer();
	// ⚠️ 롤백 배너는 **rolled_back_version() 으로** 판단한다. safe_mode() 로 하면 안 된다 —
	//    safe_mode 는 게이트1(마커 version == 지금 매핑된 버전)이 걸려 있어 롤백에 성공한
	//    다음 부팅부터 false 가 되는데, 그때 배너까지 사라지면 [그래도 다시 시도] 가 함께
	//    사라져 고객이 블랙리스트를 풀 방법이 영영 없어진다.
	const std::string rolled = sherbet::update::rolled_back_version();

	const sherbet::theme &t = sherbet::active_theme();
	const ImVec4 col_accent = ImGui::ColorConvertU32ToFloat4(t.accent);
	const ImVec4 col_danger = ImVec4(1.0f, 0.42f, 0.42f, 1.0f);

	if (compact)
	{
		// 스플래시 한 줄. 개인화 빌드는 아무것도 띄우지 않는다(매 실행 노이즈가 된다).
		if (personal)
			return;
		char line[320];
		if (done)
			std::snprintf(line, sizeof(line), "Sherbet \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\x99\x84\xEB\xA3\x8C \xC2\xB7 " "\xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xBB\x90\xEB\x8B\xA4 \xEC\xBC\x9C\xEB\xA9\xB4 v%s \xEA\xB0\x80 \xEC\xA0\x81\xEC\x9A\xA9\xEB\x8F\xBC\xEC\x9A\x94", uc.offer_version().c_str()); // "업데이트 완료 · 게임을 껐다 켜면 v… 가 적용돼요"
		else if (busy)
			std::snprintf(line, sizeof(line), "Sherbet \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 %d%%", static_cast<int>(uc.progress() * 100.0f)); // "업데이트 …%"
		else if (offer)
			std::snprintf(line, sizeof(line), "Sherbet \xEC\x83\x88 \xEB\xB2\x84\xEC\xA0\x84 v%s \xC2\xB7 " "Home \xED\x82\xA4\xEB\xA5\xBC \xEB\x88\x8C\xEB\x9F\xAC\xEC\x84\x9C \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94", uc.offer_version().c_str()); // "새 버전 v… · Home 키를 눌러서 업데이트하세요"
		else if (!rolled.empty())
			std::snprintf(line, sizeof(line), "Sherbet v%s \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8\xEB\xA5\xBC \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94 \xC2\xB7 " "Home \xED\x82\xA4", rolled.c_str()); // "v… 업데이트를 되돌렸어요 · Home 키"
		else
			return;
		ImGui::TextColored(col_accent, "%s", line);
		return;
	}

	if (!personal && !done && !busy && !offer && rolled.empty())
		return; // 그릴 것이 없다

	sherbet::begin_card("##sherbet_update_card");

	if (personal)
	{
		// 개인화 빌드 보호: 공용 릴리스가 각인·전용 프리셋을 지우고 노드락을 조용히 끈다.
		ImGui::TextDisabled(ICON_FK_INFO_CIRCLE "  \xEA\xB0\x9C\xEC\x9D\xB8 \xEB\xB9\x8C\xEB\x93\x9C\xEB\x8A\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\xAC\xB8\xEC\x9D\x98\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"); // "개인 빌드는 디스코드로 문의해 주세요"
		sherbet::end_card();
		return;
	}

	// ── 롤백 안내 + 복구 수단(§5.4 R13) ────────────────────────────────────
	if (!rolled.empty())
	{
		ImGui::TextColored(col_danger, ICON_FK_UNDO "  v%s \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8\xEA\xB0\x80 \xEC\x8B\xA4\xED\x8C\xA8\xED\x95\xB4\xEC\x84\x9C \xEC\x9D\xB4\xEC\xA0\x84 \xEB\xB2\x84\xEC\xA0\x84\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94", rolled.c_str()); // "v… 업데이트가 실패해서 이전 버전으로 되돌렸어요"
		ImGui::TextLinkOpenURL(ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xAC\xB8\xEC\x9D\x98", SHERBET_DISCORD_URL); // "디스코드 문의"
		ImGui::SameLine();
		// ★ [그래도 다시 시도] — 빼지 말 것. 자동 롤백 오탐(교체 rename 이 그 순간 AV 에
		//   거부된 경우 등)의 비용을 클릭 1회로 떨어뜨린다. 이게 없으면 그 고객은 그
		//   업데이트를 영영 못 받는다.
		ImGui::BeginDisabled(busy);
		if (sherbet::pill_button(ICON_FK_REFRESH "  \xEA\xB7\xB8\xEB\x9E\x98\xEB\x8F\x84 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84", false)) // "그래도 다시 시도"
			uc.clear_blacklist();
		ImGui::EndDisabled();
		if (done || busy || offer)
			ImGui::Separator();
	}

	if (done)
	{
		ImGui::TextColored(col_accent, ICON_FK_OK "  \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\x99\x84\xEB\xA3\x8C \xE2\x80\x94 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xBB\x90\xEB\x8B\xA4 \xEC\xBC\x9C\xEB\xA9\xB4 v%s \xEA\xB0\x80 \xEC\xA0\x81\xEC\x9A\xA9\xEB\x8F\xBC\xEC\x9A\x94", uc.offer_version().c_str()); // "업데이트 완료 — 게임을 껐다 켜면 v… 가 적용돼요"
	}
	else if (busy)
	{
		const std::string st = uc.status_text(); // ⚠️ 락 안에서 복사된 값
		if (!st.empty())
			ImGui::TextUnformatted(st.c_str());
		// 폭이 좁을 때 음수가 되지 않게 자른다(ImGui 는 음수를 '남은 폭에서 빼기' 로 해석한다).
		// ⚠️ 취소 버튼 자리는 **실측한다**. 고정 110px 이었을 땐 폰트를 21 이상으로 올리면
		//    버튼이 카드 밖으로 잘려 나갔다 — 다운로드를 멈출 수 있는 유일한 버튼이다.
		static const char *const kUpdCancel = ICON_FK_CANCEL "  \xEC\xB7\xA8\xEC\x86\x8C"; // "취소"
		const float cancel_w = ImGui::CalcTextSize(kUpdCancel).x + sherbet::pill_padding.x * 2.0f;
		const float bar_w = ImMax(60.0f,
			ImGui::GetContentRegionAvail().x - cancel_w - _imgui_context->Style.ItemSpacing.x);
		ImGui::ProgressBar(uc.progress(), ImVec2(bar_w, 0.0f));
		ImGui::SameLine();
		if (sherbet::pill_button(kUpdCancel, false))
			uc.cancel();
	}
	else if (offer)
	{
		const bool mandatory = uc.is_mandatory();
		if (mandatory)
			ImGui::TextColored(col_danger, ICON_FK_EXCLAMATION_CIRCLE "  \xED\x95\x84\xEC\x88\x98 \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 v%s", uc.offer_version().c_str()); // "필수 업데이트 v…"
		else
			ImGui::TextColored(col_accent, ICON_FK_DOWNLOAD "  \xEC\x83\x88 \xEB\xB2\x84\xEC\xA0\x84 v%s \xEC\x9D\xB4 \xEB\x82\x98\xEC\x99\x94\xEC\x96\xB4\xEC\x9A\x94", uc.offer_version().c_str()); // "새 버전 v… 이 나왔어요"

		// 변경내역 — 줄바꿈을 그대로 렌더한다(json_string 이 \n 을 실제 개행으로 푼다).
		// 표시 전용이므로 길이만 자른다. 서버 오타 하나가 배너를 화면 밖으로 밀지 않게.
		std::string notes = uc.offer_notes();
		if (notes.size() > 600)
		{
			notes.resize(600);
			// ⚠️ UTF-8 경계까지 뒤로 물린다. 바이트 단위로 자르면 한글 한 글자가 반토막 나
			//    깨진 글리프가 렌더된다(notes 는 서버가 주는 임의 길이 텍스트다).
			while (!notes.empty() && (static_cast<unsigned char>(notes.back()) & 0xC0) == 0x80)
				notes.pop_back();
			if (!notes.empty() && (static_cast<unsigned char>(notes.back()) & 0x80) != 0)
				notes.pop_back(); // 후속 바이트가 잘린 선행 바이트
			notes += "\xE2\x80\xA6"; // "…"
		}
		if (!notes.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(t.text_dim));
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(notes.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		if (sherbet::pill_button(ICON_FK_DOWNLOAD "  \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8", true)) // "업데이트"
			uc.begin_update();
		ImGui::SameLine();
		// ⚠️ **필수 업데이트도 세션 단위 닫기를 허용한다.** 서버 오타 하나(예: min_version
		//    9.9.9)로 전 고객 UI 를 잠그면 안 된다 — sherbet_nodelock.hpp:131 의
		//    "기록 실패 → 잠그지 않음" 원칙과 같다. 오버레이도 효과도 절대 막지 않는다.
		if (sherbet::pill_button("\xEB\x82\x98\xEC\xA4\x91\xEC\x97\x90", false)) // "나중에"
			uc.dismiss_session();
		if (mandatory)
		{
			ImGui::SameLine();
			ImGui::TextLinkOpenURL(ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xAC\xB8\xEC\x9D\x98", SHERBET_DISCORD_URL); // "디스코드 문의"
		}
	}

	// 실패 사유 등(제안도 진행도 아닌데 문구가 남아 있는 경우)
	if (!done && !busy && !offer)
	{
		const std::string st = uc.status_text();
		if (!st.empty())
			ImGui::TextDisabled("%s", st.c_str());
	}

	sherbet::end_card();
}

void reshade::runtime::draw_gui_home()
{
	// (a) 홈 탭 최상단 — 오버레이를 여는 구매자가 가장 먼저 보는 자리
	draw_sherbet_update_card(false);

	// (b) 프리셋 줄 — 아래 이펙트 목록을 하나씩 만지기 전에 "통째로 바꾸는" 길을 먼저 보여준다.
	//     아래 스톡 프리셋 바(파일명·저장·새로 만들기)는 그대로 둔다 — 직접 만들어 쓰는 사람의
	//     동선이다. 이 줄은 그 위에 얹기만 한다.
	draw_sherbet_preset_bar();

	std::string tutorial_text;

	// It is not possible to follow some of the tutorial steps while performance mode is active, so skip them
	if (_performance_mode && _tutorial_index <= 3)
		_tutorial_index = 4;

	const float auto_save_button_spacing = 2.0f;
	const float button_width = 12.5f * ImGui::GetFontSize();

	if (_tutorial_index > 0)
	{
		if (_tutorial_index == 1)
		{
			tutorial_text = static_cast<std::string &&>(_(
				"This is the preset selection. All changes will be saved to the selected preset file.\n\n"
				"Click on the '+' button to add a new one.\n"
				"Use the right mouse button and click on the preset button to open a context menu with additional options."));

			ImGui::PushStyleColor(ImGuiCol_FrameBg, COLOR_RED);
			ImGui::PushStyleColor(ImGuiCol_Button, COLOR_RED);
		}

		const float button_height = ImGui::GetFrameHeight();
		const float button_spacing = _imgui_context->Style.ItemInnerSpacing.x;

		bool reload_preset = false;

		ImGui::BeginDisabled(is_loading());

		if (ImGui::ArrowButtonEx("<", ImGuiDir_Left, ImVec2(button_height, button_height), ImGuiButtonFlags_NoNavFocus))
			if (switch_to_next_preset(_current_preset_path.parent_path(), true))
				reload_preset = true;
		ImGui::SetItemTooltip(_("Previous preset"));

		ImGui::SameLine(0, button_spacing);

		if (ImGui::ArrowButtonEx(">", ImGuiDir_Right, ImVec2(button_height, button_height), ImGuiButtonFlags_NoNavFocus))
			if (switch_to_next_preset(_current_preset_path.parent_path(), false))
				reload_preset = true;
		ImGui::SetItemTooltip(_("Next preset"));

		ImGui::SameLine();

		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

		const auto browse_button_pos = ImGui::GetCursorScreenPos();
		const auto browse_button_width = ImGui::GetContentRegionAvail().x - (button_height + button_spacing + _imgui_context->Style.ItemSpacing.x + auto_save_button_spacing + button_width);

		if (ImGui::ButtonEx((_current_preset_path.stem().u8string() + "###browse_button").c_str(), ImVec2(browse_button_width, 0), ImGuiButtonFlags_NoNavFocus))
		{
			_file_selection_path = _current_preset_path;
			ImGui::OpenPopup("##browse");
		}

		if (_preset_is_modified)
			ImGui::RenderBullet(ImGui::GetWindowDrawList(), browse_button_pos + ImVec2(browse_button_width - ImGui::GetFontSize() * 0.5f - _imgui_context->Style.FramePadding.x, button_height * 0.5f), ImGui::GetColorU32(ImGuiCol_Text));

		ImGui::PopStyleVar();

		if (_input != nullptr &&
			ImGui::BeginPopupContextItem())
		{
			auto preset_shortcut_it = std::find_if(_preset_shortcuts.begin(), _preset_shortcuts.end(),
				[this](const preset_shortcut &shortcut) { return shortcut.preset_path == _current_preset_path; });

			preset_shortcut shortcut;
			if (preset_shortcut_it != _preset_shortcuts.end())
				shortcut = *preset_shortcut_it;
			else
				shortcut.preset_path = _current_preset_path;

			ImGui::SetNextItemWidth(18.0f * ImGui::GetFontSize());
			if (imgui::key_input_box("##toggle_key", shortcut.key_data, *_input))
			{
				if (preset_shortcut_it != _preset_shortcuts.end())
					*preset_shortcut_it = std::move(shortcut);
				else
					_preset_shortcuts.push_back(std::move(shortcut));
			}

			ImGui::EndPopup();
		}

		ImGui::SameLine(0, button_spacing);

		if (ImGui::Button(ICON_FK_FOLDER, ImVec2(button_height, button_height)))
			utils::open_explorer(_current_preset_path);
		ImGui::SetItemTooltip(_("Open folder in explorer"));

		ImGui::SameLine();

		// Cannot save in performance mode, since there are no variables to retrieve values from then
		ImGui::BeginDisabled(_performance_mode || _is_in_preset_transition);

		const bool was_auto_save_preset = _auto_save_preset;

		if (imgui::toggle_button(
				(was_auto_save_preset ? _("Auto Save on") : _("Auto Save")) + "###auto_save",
				_auto_save_preset,
				(was_auto_save_preset ? 0.0f : auto_save_button_spacing) + button_width - (button_spacing + button_height) * (was_auto_save_preset ? 2 : 3)))
		{
			if (!was_auto_save_preset)
				save_current_preset();
			save_config();

			_preset_is_modified = false;
		}

		ImGui::SetItemTooltip(_("Save current preset automatically on every modification."));

		if (was_auto_save_preset)
		{
			ImGui::SameLine(0, button_spacing + auto_save_button_spacing);
		}
		else
		{
			ImGui::SameLine(0, button_spacing);

			ImGui::BeginDisabled(!_preset_is_modified);

			if (imgui::confirm_button(ICON_FK_UNDO, button_height, _("Do you really want to reset all effects?")))
				reload_preset = true;

			ImGui::SetItemTooltip(_("Reset all effects to those of the current preset."));

			ImGui::EndDisabled();

			ImGui::SameLine(0, button_spacing);
		}

		const auto save_and_clean_preset = _auto_save_preset || (_imgui_context->IO.KeyCtrl || _imgui_context->IO.KeyShift);

		if (ImGui::ButtonEx(ICON_FK_FLOPPY, ImVec2(button_height, button_height), ImGuiButtonFlags_NoNavFocus))
		{
			if (save_and_clean_preset)
				ini_file::load_cache(_current_preset_path).clear();
			save_current_preset();
			ini_file::flush_cache(_current_preset_path);

			_preset_is_modified = false;
		}

		ImGui::SetItemTooltip(save_and_clean_preset ?
			_("Clean up and save the current preset (removes all values for disabled effects).") : _("Save the current preset."));

		ImGui::EndDisabled();

		ImGui::SameLine(0, button_spacing);
		if (ImGui::ButtonEx(ICON_FK_PLUS, ImVec2(button_height, button_height), ImGuiButtonFlags_NoNavFocus | ImGuiButtonFlags_PressedOnClick))
		{
			_inherit_current_preset = false;
			_template_preset_path.clear();
			_file_selection_path = _current_preset_path.parent_path();
			ImGui::OpenPopup("##create");
		}

		ImGui::SetItemTooltip(_("Add a new preset."));

		ImGui::EndDisabled();

		ImGui::SetNextWindowPos(browse_button_pos + ImVec2(-_imgui_context->Style.WindowPadding.x, ImGui::GetFrameHeightWithSpacing()));
		if (imgui::file_dialog("##browse", _file_selection_path, std::max(browse_button_width, 450.0f), { L".ini", L".txt" }, { _config_path, g_reshade_base_path / L"ReShade.ini" }))
		{
			std::error_code ec;
			resolve_path(_file_selection_path, ec);

			// Check that this is actually a valid preset file
			if (ini_file::load_cache(_file_selection_path).has({}, "Techniques"))
			{
				reload_preset = true;
				_current_preset_path = _file_selection_path;
			}
			else
			{
				ini_file::clear_cache(_file_selection_path);
				ImGui::OpenPopup("##preseterror");
			}
		}

		if (ImGui::BeginPopup("##create"))
		{
			ImGui::Checkbox(_("Inherit current preset"), &_inherit_current_preset);

			if (!_inherit_current_preset)
				imgui::file_input_box(_("Template"), nullptr, _template_preset_path, _file_selection_path, { L".ini", L".txt" });

			if (ImGui::IsWindowAppearing())
				ImGui::SetKeyboardFocusHere();

			char preset_name[260] = "";
			if (ImGui::InputText(_("Preset name"), preset_name, sizeof(preset_name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCharFilter, &is_invalid_filename_element) && preset_name[0] != '\0')
			{
				std::filesystem::path new_preset_path = _current_preset_path.parent_path() / std::filesystem::u8path(preset_name);
				if (new_preset_path.extension() != L".ini" && new_preset_path.extension() != L".txt")
					new_preset_path += L".ini";

				std::error_code ec;
				resolve_path(new_preset_path, ec);

				if (const std::filesystem::file_type file_type = std::filesystem::status(new_preset_path, ec).type();
					file_type != std::filesystem::file_type::directory)
				{
					reload_preset =
						file_type == std::filesystem::file_type::not_found ||
						ini_file::load_cache(new_preset_path).has({}, "Techniques");

					if (file_type == std::filesystem::file_type::not_found)
					{
						if (_inherit_current_preset)
						{
							_current_preset_path = new_preset_path;
							save_current_preset();
						}
						else if (!_template_preset_path.empty() && !std::filesystem::copy_file(_template_preset_path, new_preset_path, std::filesystem::copy_options::overwrite_existing, ec))
						{
							log::message(log::level::error, "Failed to copy preset template '%s' to '%s' with error code %d!", _template_preset_path.u8string().c_str(), new_preset_path.u8string().c_str(), ec.value());
						}
					}
				}

				if (reload_preset)
				{
					ImGui::CloseCurrentPopup();
					_current_preset_path = new_preset_path;
				}
				else
				{
					ImGui::SetKeyboardFocusHere(-1);
				}
			}

			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("##preseterror"))
		{
			ImGui::TextColored(COLOR_RED, _("The selected file is not a valid preset!"));
			ImGui::EndPopup();
		}

		if (reload_preset)
		{
			save_config();
			load_current_preset();

			if (_preset_is_incomplete)
				ImGui::OpenPopup("##presetincomplete");

			_show_splash = true;
			_preset_is_modified = false;
			_last_preset_switching_time = _last_present_time;
			_is_in_preset_transition = true;

#if RESHADE_ADDON
			if (!is_loading()) // Will be called by 'update_effects' when 'load_current_preset' forced a reload
				invoke_addon_event<addon_event::reshade_set_current_preset_path>(this, _current_preset_path.u8string().c_str());
#endif
		}

		if (ImGui::BeginPopup("##presetincomplete"))
		{
			ImGui::TextColored(COLOR_RED, _("The selected preset uses unknown effects. Please install all required effect files!"));
			ImGui::EndPopup();
		}

		if (_tutorial_index == 1)
			ImGui::PopStyleColor(2);
	}
	else
	{
		tutorial_text = static_cast<std::string &&>(_(
			"Welcome! Since this is the first time you start ReShade, we'll go through a quick tutorial covering the most important features.\n\n"
			"If you have difficulties reading this text, press the 'Ctrl' key and adjust the font size with your mouse wheel. "
			"The window size is variable as well, just grab the right edge and move it around.\n\n"
			"You can also use the keyboard for navigation in case mouse input does not work. Use the arrow keys to navigate, space bar to confirm an action or enter a control and the 'Esc' key to leave a control. "
			"Press 'Ctrl + Tab' to switch between tabs and windows (use this to focus this page in case the other navigation keys do not work at first).\n\n"
			"Click on the 'Continue' button to continue the tutorial."));
	}

	if (_tutorial_index > 1)
	{
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
	}

	if (_reload_remaining_effects != std::numeric_limits<size_t>::max())
	{
		ImGui::SetCursorPos(ImGui::GetWindowSize() * 0.5f - ImVec2(21, 21));
		imgui::spinner((_effects.size() - _reload_remaining_effects) / float(_effects.size()), 16.0f * ImGui::GetFontSize() / 13, 10.0f * ImGui::GetFontSize() / 13);
		return; // Cannot show techniques and variables while effects are loading, since they are being modified in other threads during that time
	}

	if (_tutorial_index > 1)
	{
		if (imgui::search_input_box(_effect_filter, sizeof(_effect_filter), -((_variable_editor_tabs ? 1 : 2) * (_imgui_context->Style.ItemSpacing.x + 2.0f + button_width))))
		{
			_effects_expanded_state = 3;

			for (technique &tech : _techniques)
			{
				std::string_view label = tech.annotation_as_string("ui_label");
				if (label.empty())
					label = tech.name;

				tech.hidden = tech.annotation_as_int("hidden") != 0 || !(string_contains(label, _effect_filter) || string_contains(_effects[tech.effect_index].source_file.filename().u8string(), _effect_filter));
			}
		}

		ImGui::SameLine();

		ImGui::BeginDisabled(_is_in_preset_transition);

		if (ImGui::Button(_("Active to top"), ImVec2(auto_save_button_spacing + button_width, 0)))
		{
			std::vector<size_t> technique_indices = _technique_sorting;

			for (auto it = technique_indices.begin(), target_it = it; it != technique_indices.end(); ++it)
			{
				const technique &tech = _techniques[*it];

				if (tech.enabled || tech.toggle_key_data[0] != 0)
				{
					target_it = std::rotate(target_it, it, std::next(it));
				}
			}

			reorder_techniques(std::move(technique_indices));

			if (_auto_save_preset)
				save_current_preset();
			else
				_preset_is_modified = true;
		}

		ImGui::EndDisabled();

		if (!_variable_editor_tabs)
		{
			ImGui::SameLine();

			if (ImGui::Button((_effects_expanded_state & 2) ? _("Collapse all") : _("Expand all"), ImVec2(auto_save_button_spacing + button_width, 0)))
				_effects_expanded_state = (~_effects_expanded_state & 2) | 1;
		}

		if (_tutorial_index == 2)
		{
			tutorial_text = static_cast<std::string &&>(_(
				"This is the list of effects. It contains all effects exposed by effect files (.fx) found in the effect search paths specified in the settings.\n\n"
				"Enter text in the \"Search\" box at the top to filter it and search for specific effects.\n\n"
				"Click on an effect to enable or disable it or drag it to a new location in the list to change the order in which the effects are applied (from top to bottom).\n"
				"Use the right mouse button and click on an item to open a context menu with additional options."));

			ImGui::PushStyleColor(ImGuiCol_Border, COLOR_RED);
		}

		ImGui::Spacing();

		if (!_last_reload_successful)
		{
			ImGui::PushTextWrapPos();
			ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
			ImGui::TextUnformatted(_("There were errors loading some effects."));
			ImGui::TextUnformatted(_("Hover the cursor over any red entries below to see the related error messages and/or check the log for more details if there are none."));
			ImGui::PopStyleColor();
			ImGui::PopTextWrapPos();
			ImGui::Spacing();
		}

		if (!_effects_enabled)
		{
			ImGui::Text(_("Effects are disabled. Press '%s' to enable them again."), input::key_name(_effects_key_data).c_str());
			ImGui::Spacing();
		}

		// SHERBET: 위 프리셋 줄과 짝이 되는 라벨. "프리셋 = 통째로 / 이펙트 = 하나씩" 이
		//          한눈에 읽혀야, 아래 목록을 일일이 켜고 끄는 것이 유일한 조작으로 보이지 않는다.
		ImGui::TextDisabled("%s", ICON_FK_SLIDERS "  \xEC\x9D\xB4\xED\x8E\x99\xED\x8A\xB8 \xE2\x80\x94 \xEC\xA7\x81\xEC\xA0\x91 \xEC\x84\xB8\xEB\xB0\x80\xED\x95\x98\xEA\xB2\x8C \xEC\xA1\xB0\xEC\xA0\x88"); // "이펙트 — 직접 세밀하게 조절"
		ImGui::Spacing();

		float bottom_height = _variable_editor_height;
		bottom_height = std::max(bottom_height, 20.0f);
		bottom_height = ImGui::GetFrameHeightWithSpacing() + _imgui_context->Style.ItemSpacing.y + (
			_performance_mode ? 0 : (17 /* splitter */ + (bottom_height + (_tutorial_index == 3 ? 175 : 0))));
		bottom_height = std::min(bottom_height, ImGui::GetContentRegionAvail().y - 20.0f);

		if (ImGui::BeginChild("##techniques", ImVec2(0, -bottom_height), ImGuiChildFlags_Borders))
		{
			if (_effect_load_skipping && _show_force_load_effects_button)
			{
				const size_t skipped_effects = std::count_if(_effects.cbegin(), _effects.cend(),
					[](const effect &effect) { return effect.skipped; });

				if (skipped_effects > 0)
				{
					char label[64] = "";
					ImFormatString(label, IM_ARRAYSIZE(label), _("Force load all effects (%zu remaining)") + "###force_reload_button", skipped_effects);

					if (ImGui::ButtonEx(label, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
					{
						reload_effects(true);

						ImGui::EndChild();
						return;
					}
				}
			}

			draw_technique_editor();
		}
		ImGui::EndChild();

		if (_tutorial_index == 2)
			ImGui::PopStyleColor();
	}

	if (_tutorial_index > 2 && !_performance_mode)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::ButtonEx("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, 5));
		ImGui::PopStyleVar();

		if (ImGui::IsItemHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (ImGui::IsItemActive())
		{
			ImVec2 move_delta = _imgui_context->IO.MouseDelta;
			move_delta += ImGui::GetKeyMagnitude2d(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown) * _imgui_context->IO.DeltaTime * 500.0f;

			_variable_editor_height = std::max(_variable_editor_height - move_delta.y, 0.0f);
			save_config();
		}

		if (_tutorial_index == 3)
		{
			tutorial_text = static_cast<std::string &&>(_(
				"This is the list of variables. It contains all tweakable options the active effects expose. Values here apply in real-time.\n\n"
				"Press 'Ctrl' and click on a widget to manually edit the value (can also hold 'Ctrl' while adjusting the value in a widget to have it ignore any minimum or maximum values).\n"
				"Use the right mouse button and click on an item to open a context menu with additional options.\n\n"
				"Once you have finished tweaking your preset, be sure to enable the 'Performance Mode' check box. "
				"This will reload all effects into a more optimal representation that can give a performance boost, but disables variable tweaking and this list."));

			ImGui::PushStyleColor(ImGuiCol_Border, COLOR_RED);
		}

		const float bottom_height = ImGui::GetFrameHeightWithSpacing() + _imgui_context->Style.ItemSpacing.y + (_tutorial_index == 3 ? 175 : 0);

		if (ImGui::BeginChild("##variables", ImVec2(0, -bottom_height), ImGuiChildFlags_Borders))
			draw_variable_editor();
		ImGui::EndChild();

		if (_tutorial_index == 3)
			ImGui::PopStyleColor();
	}

	if (_tutorial_index > 3)
	{
		ImGui::Spacing();

		if (ImGui::Button(ICON_FK_REFRESH " " + _("Reload"), ImVec2(-(auto_save_button_spacing + button_width), 0)))
		{
			load_config(); // Reload configuration too

			if (!_no_effect_cache && (_imgui_context->IO.KeyCtrl || _imgui_context->IO.KeyShift))
				clear_effect_cache();

			reload_effects();
		}

		ImGui::SetItemTooltip(_("Reload all effects (can hold 'Ctrl' while clicking to clear the effect cache before loading)."));

		ImGui::SameLine();

		if (ImGui::Checkbox(_("Performance Mode"), &_performance_mode))
		{
			save_config();
			reload_effects(); // Reload effects after switching
		}

		ImGui::SetItemTooltip(_("Reload all effects into a more optimal representation that can give a performance boost, but disables variable tweaking."));
	}
	else
	{
		const float max_frame_width = ImGui::GetContentRegionAvail().x;
		const float max_frame_height = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight() - _imgui_context->Style.ItemSpacing.y;
		const float required_frame_height =
			ImGui::CalcTextSize(tutorial_text.data(), tutorial_text.data() + tutorial_text.size(), false, max_frame_width - _imgui_context->Style.FramePadding.x * 2).y +
			_imgui_context->Style.FramePadding.y * 2;

		if (ImGui::BeginChild("##tutorial", ImVec2(max_frame_width, std::min(max_frame_height, required_frame_height)), ImGuiChildFlags_FrameStyle))
		{
			ImGui::PushTextWrapPos();
			ImGui::TextUnformatted(tutorial_text.data(), tutorial_text.data() + tutorial_text.size());
			ImGui::PopTextWrapPos();
		}
		ImGui::EndChild();

		if (_tutorial_index == 0)
		{
			if (ImGui::Button(_("Continue") + "###tutorial_button", ImVec2(max_frame_width * 0.66666666f, 0)))
			{
				_tutorial_index++;

				save_config();
			}

			ImGui::SameLine();

			if (ImGui::Button(_("Skip Tutorial"), ImVec2(max_frame_width * 0.33333333f - _imgui_context->Style.ItemSpacing.x, 0)))
			{
				_tutorial_index = 4;

				save_config();
			}
		}
		else
		{
			if (ImGui::Button((_tutorial_index == 3 ? _("Finish") : _("Continue")) + "###tutorial_button", ImVec2(max_frame_width, 0)))
			{
				_tutorial_index++;

				if (_tutorial_index == 4)
					save_config();
			}
		}
	}
}
// 선택된 커스텀 조준점 PNG 를 텍스처로 로딩한다. 내장 도형 모드거나 파일이 없으면 텍스처를 해제만 한다.
// 렌더 스레드(draw_gui)에서만 호출 — _device 사용이 안전한 시점.
// SHERBET: 긴 한 줄 안내문. TextDisabled 는 DC.TextWrapPos < 0 이면 줄바꿈하지 않고,
// 컨테이너에 가로 스크롤바가 없어 넘친 글자는 말줄임표도 없이 잘려 나간다.
// 기본 폰트(13)에서는 안 넘치지만 폰트를 16 이상으로 올리면 잘린다.
// ⚠️ BulletText 에는 쓰지 마라 — 접힌 둘째 줄이 불릿 아래가 아니라 왼쪽 끝으로 흐른다.
static void sherbet_hint(const char *text)
{
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextDisabled("%s", text);
	ImGui::PopTextWrapPos();
}

void reshade::runtime::sherbet_load_crosshair()
{
	_sherbet_crosshair_dirty = false;
	// ⚠️ 배경 로더와 같은 이유로 실패를 기록한다. 클래식 조준점은 오버레이 안에 미리보기가
	//    없어서, 실패하면 **게임 화면에 조준점이 아예 안 그려지는데 이유를 알 길이 없다.**
	//    '내장 도형/미선택' 은 실패가 아니라 미시도(0)다.
	_sherbet_crosshair_load = 0;
	if (_sherbet_crosshair_srv != 0) { _device->destroy_resource_view(_sherbet_crosshair_srv); _sherbet_crosshair_srv = {}; }
	if (_sherbet_crosshair_tex != 0) { _device->destroy_resource(_sherbet_crosshair_tex); _sherbet_crosshair_tex = {}; }
	_sherbet_crosshair_w = _sherbet_crosshair_h = 0;

	if (_sherbet_crosshair_builtin != 0 || _sherbet_crosshair_file.empty())
		return; // 내장 도형 모드거나 선택 파일 없음 → 이미지 불필요(미시도 0 으로 남긴다)

	const std::filesystem::path path = _config_path.parent_path() / L"Sherbet-Crosshairs" / std::filesystem::u8path(_sherbet_crosshair_file);
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		{ _sherbet_crosshair_load = 2; return; }

	std::ifstream file(path, std::ios::binary);
	if (!file)
		{ _sherbet_crosshair_load = 2; return; }
	const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (data.empty())
		{ _sherbet_crosshair_load = 2; return; }

	int w = 0, h = 0, ch = 0;
	stbi_uc *const pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), static_cast<int>(data.size()), &w, &h, &ch, STBI_rgb_alpha);
	if (pixels == nullptr)
		{ _sherbet_crosshair_load = 2; return; }

	const api::subresource_data initial = { pixels, static_cast<uint32_t>(w * 4), static_cast<uint32_t>(w * 4 * h) };
	if (_device->create_resource(
			api::resource_desc(w, h, 1, 1, api::format::r8g8b8a8_unorm, 1, api::memory_heap::default_, api::resource_usage::shader_resource | api::resource_usage::copy_dest),
			&initial, api::resource_usage::shader_resource, &_sherbet_crosshair_tex))
	{
		if (_device->create_resource_view(_sherbet_crosshair_tex, api::resource_usage::shader_resource, api::resource_view_desc(api::format::r8g8b8a8_unorm), &_sherbet_crosshair_srv))
		{
			_sherbet_crosshair_w = w;
			_sherbet_crosshair_h = h;
			_sherbet_crosshair_load = 1;
		}
		else
		{
			_device->destroy_resource(_sherbet_crosshair_tex);
			_sherbet_crosshair_tex = {};
			_sherbet_crosshair_load = 2;
		}
	}
	else
	{
		_sherbet_crosshair_tex = {};
		_sherbet_crosshair_load = 2;
	}

	stbi_image_free(pixels);
}
// 선택된 커스텀 배경 이미지를 텍스처로 로딩한다(Sherbet-Backgrounds 폴더). 끔이거나 파일이 없으면 해제만.
// 렌더 스레드(draw_gui)에서만 호출 — _device 사용이 안전한 시점.
void reshade::runtime::sherbet_load_background()
{
	_sherbet_bg_dirty = false;
	// ⚠️ 실패를 **조용히 넘기지 않는다.** 예전엔 모든 실패 경로가 그냥 return 이라, 콤보는
	//    파일명만 보고 '선택됨' 처럼 그리는데 화면엔 아무것도 안 나오고 이유가 어디에도
	//    없었다. stb_image 는 프로그레시브/CMYK JPEG 를 못 푸는데 콤보는 .jpg 를 그대로
	//    목록에 올리므로 발생률이 낮지 않다.
	// ⚠️ '끔/미선택' 은 실패가 아니라 **미시도(0)** 다. 2 로 두면 배경을 꺼 놓은 사람에게
	//    빨간 에러가 뜬다.
	_sherbet_bg_load = 0;
	if (_sherbet_bg_srv != 0) { _device->destroy_resource_view(_sherbet_bg_srv); _sherbet_bg_srv = {}; }
	if (_sherbet_bg_tex != 0) { _device->destroy_resource(_sherbet_bg_tex); _sherbet_bg_tex = {}; }
	_sherbet_bg_w = _sherbet_bg_h = 0;

	if (!_sherbet_bg_on || _sherbet_bg_file.empty())
		return; // 끔이거나 선택 파일 없음 → 이미지 불필요(미시도 0 으로 남긴다)

	const std::filesystem::path path = _config_path.parent_path() / L"Sherbet-Backgrounds" / std::filesystem::u8path(_sherbet_bg_file);
	std::error_code ec;
	if (!std::filesystem::exists(path, ec))
		{ _sherbet_bg_load = 2; return; }

	std::ifstream file(path, std::ios::binary);
	if (!file)
		{ _sherbet_bg_load = 2; return; }
	const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (data.empty())
		{ _sherbet_bg_load = 2; return; }

	int w = 0, h = 0, ch = 0;
	stbi_uc *const pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc *>(data.data()), static_cast<int>(data.size()), &w, &h, &ch, STBI_rgb_alpha);
	if (pixels == nullptr)
		{ _sherbet_bg_load = 2; return; }

	const api::subresource_data initial = { pixels, static_cast<uint32_t>(w * 4), static_cast<uint32_t>(w * 4 * h) };
	if (_device->create_resource(
			api::resource_desc(w, h, 1, 1, api::format::r8g8b8a8_unorm, 1, api::memory_heap::default_, api::resource_usage::shader_resource | api::resource_usage::copy_dest),
			&initial, api::resource_usage::shader_resource, &_sherbet_bg_tex))
	{
		if (_device->create_resource_view(_sherbet_bg_tex, api::resource_usage::shader_resource, api::resource_view_desc(api::format::r8g8b8a8_unorm), &_sherbet_bg_srv))
		{
			_sherbet_bg_w = w;
			_sherbet_bg_h = h;
			_sherbet_bg_load = 1;
		}
		else
		{
			_device->destroy_resource(_sherbet_bg_tex);
			_sherbet_bg_tex = {};
			_sherbet_bg_load = 2;
		}
	}
	else
	{
		_sherbet_bg_tex = {};
		_sherbet_bg_load = 2;
	}

	stbi_image_free(pixels);
}

// ── SHERBET: 유료 기능 잠금 ──────────────────────────────────────────────────
// 잠긴 기능은 **숨기지 않는다.** 지금까지 잠금 기능(custompicture)은 그냥 안 보이게
// 처리했는데, 안 보이는 기능은 한 개도 안 팔린다 — 있는 줄도 모르는 걸 살 수는 없다.
// 그래서 잠긴 상태는 "빈 자리"가 아니라 **진열대**다. 구매자가 실제로 제일 많이 보게 될
// 화면이 이쪽이므로, 여기 들이는 공을 아끼면 안 된다.
//
// 잠금 카드 한 장의 구성:
//   머리(sherbet_draw_lock_header)  자물쇠 배너 + 이름 + 한 줄 소개  → "이게 뭐냐"
//   가운데(호출부)                  진짜 렌더러 + 예시 데이터        → "말고 보여줘"
//   발(sherbet_draw_lock_footer)    사면 생기는 것 + 구매/불러오기   → "그래서 뭘 하면 되냐"

// 잠금 판정의 호출부 단일 입구. 서버 엔타이틀 + 판매자 미리보기 스위치.
bool reshade::runtime::sherbet_feature_unlocked(const char *id) const
{
	return sherbet::paid::unlocked(id, sherbet::has_feature(id), _sherbet_lock_preview);
}

// 라운드 알약 안에 글자 하나. 배지/칩용(클릭 없음).
static void sherbet_lock_chip(const char *text, ImU32 bg, ImU32 fg)
{
	ImDrawList *const dl = ImGui::GetWindowDrawList();
	const ImVec2 ts = ImGui::CalcTextSize(text);
	const ImVec2 pad(9.0f, 3.0f);
	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + ts.x + pad.x * 2.0f, p0.y + ts.y + pad.y * 2.0f);
	dl->AddRectFilled(p0, p1, bg, (p1.y - p0.y) * 0.5f);
	dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y), fg, text);
	ImGui::Dummy(ImVec2(p1.x - p0.x, p1.y - p0.y));
}

void reshade::runtime::sherbet_draw_lock_header(const sherbet::paid::feature &f)
{
	const sherbet::theme &t = sherbet::active_theme();
	ImDrawList *const dl = ImGui::GetWindowDrawList();

	// 상단 그라디언트 띠 — 테마 마켓 카드와 **같은** 시각 언어를 쓴다(새 언어를 만들지 않는다).
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float bw = ImGui::GetContentRegionAvail().x;
	constexpr float kBandH = 46.0f;
	dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + kBandH), t.bg1, t.accent, t.accent, t.bg1);
	// 자물쇠는 띠 위에 크게 — 첫 시선이 "잠겨 있다"에 닿아야 다음 줄을 읽는다.
	const ImVec2 lk = ImGui::CalcTextSize(ICON_FK_LOCK);
	dl->AddText(ImVec2(p.x + 14.0f, p.y + (kBandH - lk.y) * 0.5f), sherbet::with_alpha(t.text, 235), ICON_FK_LOCK);
	// 칩 한 줄이 들어갈 자리를 남기고 띠 높이만큼 커서를 내린다. 글꼴이 커지면 이 값이
	// 음수가 될 수 있으므로 클램프한다(음수 Dummy 는 레이아웃을 거꾸로 밀어 카드가 깨진다).
	ImGui::Dummy(ImVec2(bw, ImMax(1.0f, kBandH - ImGui::GetTextLineHeight() - 6.0f)));

	// "유료 기능" 칩 — 오른쪽 끝에 붙여 띠와 겹치게 둔다.
	static const char *const kLockPaid = "\xEC\x9C\xA0\xEB\xA3\x8C \xEA\xB8\xB0\xEB\x8A\xA5"; // "유료 기능"
	const float chip_w = ImGui::CalcTextSize(kLockPaid).x + 18.0f;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, bw - chip_w));
	sherbet_lock_chip(kLockPaid, sherbet::with_alpha(t.bg0, 220), sherbet::with_alpha(t.accent2, 255));

	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.3f);
	ImGui::TextUnformatted(f.name);
	ImGui::PopFont();

	// 한 줄 소개. 이름만 보여주는 건 "여기 뭔가 있다"까지만 말하는 것이라 아무것도 못 판다.
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(t.text));
	ImGui::TextWrapped("%s", f.pitch);
	ImGui::PopStyleColor();
	ImGui::Spacing();
}

void reshade::runtime::sherbet_draw_lock_footer(const sherbet::paid::feature &f)
{
	const sherbet::theme &t = sherbet::active_theme();

	static const char *const kLockGet = "\xEC\x9D\xB4\xEA\xB1\xB8 \xEC\x82\xAC\xEB\xA9\xB4 \xEC\x83\x9D\xEA\xB8\xB0\xEB\x8A\x94 \xEA\xB2\x83"; // "이걸 사면 생기는 것"
	static const char *const kLockBuy = ICON_FK_SHOPPING_CART "  " "\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90\xEC\x84\x9C \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEA\xB8\xB0"; // "디스코드에서 구매하기"
	static const char *const kLockFetch = ICON_FK_DOWNLOAD "  " "\xEC\x9D\xB4\xEB\xAF\xB8 \xEA\xB5\xAC\xEB\xA7\xA4\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\xA7\x80\xEA\xB8\x88 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0"; // "이미 구매했어요 — 지금 불러오기"
	static const char *const kLockFetchHint = "\xEA\xB5\xAC\xEB\xA7\xA4 \xED\x9B\x84 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEC\x97\xAD\xED\x95\xA0\xEC\x9D\x84 \xEB\xB0\x9B\xEC\x95\x98\xEB\x8B\xA4\xEB\xA9\xB4 \xEC\x9D\xB4 \xEB\xB2\x84\xED\x8A\xBC \xED\x95\x9C \xEB\xB2\x88\xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB0\x94\xEB\xA1\x9C \xEC\x97\xB4\xEB\xA0\xA4\xEC\x9A\x94. \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xBB\x90\xEB\x8B\xA4 \xEC\xBC\xA4 \xED\x95\x84\xEC\x9A\x94 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94."; // "구매 후 디스코드 역할을 받았다면 이 버튼 한 번으로 바로 열려요. 게임을 껐다 켤 필요 없어요."
	static const char *const kLockLoginHint = "\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 '\xEB\xA7\x88\xEC\xBC\x93' \xED\x83\xAD\xEC\x97\x90\xEC\x84\x9C '\xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0'\xEB\xA5\xBC \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEC\x97\xB4\xEB\xA0\xA4\xEC\x9A\x94."; // "디스코드 로그인 후 「마켓」 탭에서 「내 전용 불러오기」를 누르면 열려요."
	static const char *const kLockGoMarket = ICON_FK_SHOPPING_CART "  " "\xEB\xA7\x88\xEC\xBC\x93 \xED\x83\xAD\xEC\x9C\xBC\xEB\xA1\x9C \xEA\xB0\x80\xEA\xB8\xB0"; // "마켓 탭으로 가기"
	static const char *const kLockLoading = "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91"; // "불러오는 중"
	static const char *const kLockDone = "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0 \xEC\x99\x84\xEB\xA3\x8C\x21"; // "불러오기 완료!"

	ImGui::Spacing();
	ImGui::TextDisabled("%s", kLockGet);
	for (const char *b : f.bullets)
	{
		if (b == nullptr)
			break;
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.accent), "%s", ICON_FK_OK);
		ImGui::SameLine(0.0f, 8.0f);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(b);
		ImGui::PopTextWrapPos();
	}

	ImGui::Spacing();
	// 다음 한 걸음을 눌러 준다. "디스코드로 오세요" 한 줄만 두면 "그래서 뭘 하라는 거지"에서 끝난다.
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(t.accent2));
	ImGui::TextLinkOpenURL(kLockBuy, SHERBET_DISCORD_URL);
	ImGui::PopStyleColor();

	// 방금 역할을 받은 사람에게 필요한 건 **재시작이 아니라 재페치**다. 그걸 여기서 바로 눌러 준다
	// (마켓 탭의 「내 전용 불러오기」와 완전히 같은 /content/me 페치다).
	// 이 안내가 없으면 산 사람이 "결제했는데 안 열려요" 로 문의를 넣게 된다.
	if (sherbet::auth::enabled() && _sherbet_auth.is_authed())
	{
		ImGui::SameLine(0.0f, 16.0f);
		if (_sherbet_auth.content_active())
		{
			const int dots = 1 + static_cast<int>(ImGui::GetTime() * 2.0) % 3; // 1~3, 애니메이션용
			char buf[96];
			snprintf(buf, sizeof(buf), ICON_FK_DOWNLOAD "  %s%.*s", kLockLoading, dots, "...");
			ImGui::BeginDisabled();
			sherbet::pill_button(buf, true);
			ImGui::EndDisabled();
		}
		else if (sherbet::pill_button(kLockFetch, true))
		{
			_sherbet_auth.begin_fetch_content();
		}
		// ⚠️ 원래 중괄호 없는 한 줄 if 였다 — 여러 줄로 늘릴 때 중괄호를 반드시 같이 넣는다.
		if (!_sherbet_auth.content_active() && _sherbet_content_done_timer > 0.0f)
		{
			ImVec4 done_col = sherbet::status_color(sherbet::status::good); // 테마 명도에 맞는 초록
			done_col.w = ImMin(1.0f, _sherbet_content_done_timer);         // 마지막 1초 페이드
			ImGui::TextColored(done_col, ICON_FK_OK "  %s", kLockDone);
		}
		ImGui::TextDisabled("%s", kLockFetchHint);
	}
	else
	{
		// 아직 로그인 전(또는 오프라인 빌드) — 로그인 동선이 있는 마켓 탭으로 보낸다.
		ImGui::SameLine(0.0f, 16.0f);
		if (sherbet::pill_button(kLockGoMarket, false))
		{
			_sherbet_tab = 1;          // 「마켓」 탭
			_sherbet_market_seg = 0;   // 테마 세그먼트에 로그인/불러오기 동선이 있다
		}
		ImGui::TextDisabled("%s", kLockLoginHint);
	}
}

// SHERBET: 스프레이 차트 캔버스 한 장(배경·격자·중앙 십자·구간 궤적).
// ⚠️ 잠금 미리보기와 실제 화면이 **같은 함수**를 쓴다. 미리보기를 따로 그리면 (1) 렌더러가
//    바뀔 때 한쪽만 낡고 (2) 실물과 다른 그림을 파는 셈이 된다. 다른 것은 먹이는 데이터뿐이다.
// 반환값은 원점(= 각 구간의 첫 발 자리)이라 호출부가 그 위에 글자를 얹을 수 있다.
static ImVec2 sherbet_draw_spray_canvas(ImDrawList *dl, const ImVec2 &c0, const ImVec2 &c1,
	const sherbet::theme &ct, const std::vector<sherbet::spray::segment> &segs,
	const sherbet::spray::segment *focus, bool overlay5, float scale)
{
	const ImVec2 org((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f); // 원점 = 첫 발
	const float cw = c1.x - c0.x, ch = c1.y - c0.y;

	dl->AddRectFilled(c0, c1, sherbet::with_alpha(ct.bg1, 220), 12.0f);
	dl->PushClipRect(c0, c1, true);
	{
		// 격자 + 중앙 십자
		const ImU32 grid_col = sherbet::with_alpha(ct.border, 90);
		for (float gx = 0.0f; gx <= cw * 0.5f; gx += 32.0f)
		{
			dl->AddLine(ImVec2(org.x + gx, c0.y), ImVec2(org.x + gx, c1.y), grid_col);
			dl->AddLine(ImVec2(org.x - gx, c0.y), ImVec2(org.x - gx, c1.y), grid_col);
		}
		for (float gy = 0.0f; gy <= ch * 0.5f; gy += 32.0f)
		{
			dl->AddLine(ImVec2(c0.x, org.y + gy), ImVec2(c1.x, org.y + gy), grid_col);
			dl->AddLine(ImVec2(c0.x, org.y - gy), ImVec2(c1.x, org.y - gy), grid_col);
		}
		const ImU32 cross_col = sherbet::with_alpha(ct.text_dim, 160);
		dl->AddLine(ImVec2(org.x - 8.0f, org.y), ImVec2(org.x + 8.0f, org.y), cross_col, 1.5f);
		dl->AddLine(ImVec2(org.x, org.y - 8.0f), ImVec2(org.x, org.y + 8.0f), cross_col, 1.5f);

		// 한 구간을 그린다. 과거 구간은 흐리게, 강조 구간은 진하게.
		auto draw_segment = [&](const sherbet::spray::segment &s, bool strong) {
			if (s.shots.empty())
				return;
			const ImU32 lc = sherbet::with_alpha(strong ? ct.accent : ct.text_dim, strong ? 230u : 70u);
			const ImU32 pc = sherbet::with_alpha(strong ? ct.accent2 : ct.text_dim, strong ? 255u : 90u);
			ImVec2 prev(0.0f, 0.0f);
			for (std::size_t i = 0; i < s.shots.size(); ++i)
			{
				const ImVec2 p(org.x + s.shots[i].x * scale, org.y + s.shots[i].y * scale);
				if (i != 0)
					dl->AddLine(prev, p, lc, strong ? 2.0f : 1.0f);
				dl->AddCircleFilled(p, strong ? 3.5f : 2.0f, pc);
				prev = p;
			}
			// 마지막 점(= 스프레이가 끝난 자리)에 링 하나 — 일관성은 여기가 얼마나
			// 겹치는지로 보인다(종료 지점 편차와 같은 이야기).
			if (strong)
				dl->AddCircle(prev, 7.0f, sherbet::with_alpha(ct.accent, 200), 0, 1.5f);
		};

		if (overlay5)
		{
			std::size_t drawn = 0;
			for (std::size_t i = segs.size(); i > 0 && drawn < 5; --i, ++drawn)
				if (&segs[i - 1] != focus)
					draw_segment(segs[i - 1], false);
		}
		if (focus != nullptr)
			draw_segment(*focus, true);
	}
	dl->PopClipRect();
	return org;
}

// SHERBET: 스프레이 트레이너 잠금 카드 — 「에임」 탭 안의 판매 진열대.
//
// 정지 화면 한 장으로는 이 기능을 설명할 수가 없다("궤적을 보여줍니다"라는 글자는
// 아무것도 보여주지 않는다). 그래서 **진짜 차트 렌더러**에 만들어 둔 예시 스프레이를
// 먹여 실물 그대로 그린다. 강조 구간은 몇 초마다 바뀌어 겹쳐보기가 무슨 뜻인지도 보인다.
// ⚠️ 그리는 데이터는 sherbet::paid::spray_chart() 가 정한다. 잠긴 동안 사용자의 실제
//    기록은 이 함수에 들어올 수 없고, 예시라는 사실을 캔버스 안팎에 두 번 적는다 —
//    미리보기를 자기 기록으로 오해하면 그건 광고가 아니라 거짓말이 된다.
void reshade::runtime::sherbet_draw_spray_lock_card(const sherbet::paid::feature &f)
{
	static const char *const kLockExample = "\xEC\x98\x88\xEC\x8B\x9C"; // "예시"
	static const char *const kLockSprayCaption = ICON_FK_ARROW_UP " \xEC\x98\x88\xEC\x8B\x9C \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEB\xA9\xB4 \xEC\x97\xAC\xEA\xB8\xB0\xEC\x97\x90 \xEB\x82\xB4\xEA\xB0\x80 \xEC\x8F\x9C \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEA\xB7\xB8\xEB\xA0\xA4\xEC\xA0\xB8\xEC\x9A\x94"; // "↑ 예시 화면이에요 — 구매하면 여기에 내가 쏜 기록이 그대로 그려져요"

	sherbet::begin_card("##spray_lock");
	{
		sherbet_draw_lock_header(f);

		const sherbet::theme &t = sherbet::active_theme();

		// ── 예시 차트 ──
		// spray_chart(false, ...) 는 반드시 예시를 돌려준다. 실제 기록을 넘길 방법 자체가 없도록
		// **호출부에서도** 사용자 기록을 참조하지 않는다(빈 벡터를 넘긴다).
		static const std::vector<sherbet::spray::segment> kNoUserData;
		const sherbet::paid::chart_source src = sherbet::paid::spray_chart(false, kNoUserData);
		const std::vector<sherbet::spray::segment> &segs = *src.segments;

		const ImVec2 c0 = ImGui::GetCursorScreenPos();
		const float cw = ImMax(64.0f, ImGui::GetContentRegionAvail().x);
		const float ch = 200.0f;
		const ImVec2 c1(c0.x + cw, c0.y + ch);
		ImGui::Dummy(ImVec2(cw, ch));
		ImDrawList *const dl = ImGui::GetWindowDrawList();

		// 강조 구간을 2.5초마다 돌린다 — 정지 화면이 아니라 "쓰고 있는 화면"으로 보이게.
		const sherbet::spray::segment *focus = nullptr;
		if (!segs.empty())
			focus = &segs[static_cast<std::size_t>(ImGui::GetTime() / 2.5) % segs.size()];

		sherbet_draw_spray_canvas(dl, c0, c1, t, segs, focus, true, 1.6f);

		// 캔버스 안 '예시' 배지 — 스크린샷만 잘라 봐도 예시라는 걸 알 수 있어야 한다.
		if (src.is_example)
		{
			const ImVec2 ts = ImGui::CalcTextSize(kLockExample);
			const ImVec2 b0(c0.x + 10.0f, c0.y + 10.0f);
			const ImVec2 b1(b0.x + ts.x + 16.0f, b0.y + ts.y + 6.0f);
			dl->AddRectFilled(b0, b1, sherbet::with_alpha(t.bg0, 225), (b1.y - b0.y) * 0.5f);
			dl->AddText(ImVec2(b0.x + 8.0f, b0.y + 3.0f), sherbet::with_alpha(t.accent2, 255), kLockExample);
		}

		// 예시 데이터의 통계도 **실제 통계 함수**로 낸다(숫자를 지어내지 않는다).
		if (focus != nullptr)
		{
			float iv = 0.0f, spread = 0.0f;
			ImGui::TextDisabled("\xEB\xB0\x9C\xEC\x88\x98 %d", static_cast<int>(focus->shots.size())); // "발수 %d"
			if (sherbet::spray::avg_interval(*focus, iv) && iv > 0.0f)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("\xED\x8F\x89\xEA\xB7\xA0 %.0f RPM", 60.0f / iv); // "평균 %.0f RPM"
			}
			if (sherbet::spray::end_spread(segs, 5, spread))
			{
				ImGui::SameLine();
				ImGui::TextDisabled("\xEC\xA2\x85\xEB\xA3\x8C \xEC\xA7\x80\xEC\xA0\x90 \xED\x8E\xB8\xEC\xB0\xA8 %.1fpx", spread * 1.6f); // "종료 지점 편차 %.1fpx"
			}
		}
		if (src.is_example)
			ImGui::TextDisabled("%s", kLockSprayCaption);

		sherbet_draw_lock_footer(f);
	}
	sherbet::end_card();
	ImGui::Spacing();
}

// SHERBET: 「에임」 탭의 스프레이 트레이너 구획(입력 진단 + 궤적 차트). **유료 기능 'spray'.**
// 잠겨 있으면 아예 호출되지 않는다 — 잠금 판정은 draw_gui_aim() 이 한 번만 하고, 기록기 쪽
// 판정은 draw_gui() 가 같은 술어(sherbet::paid::spray_*)로 한다. 두 곳이 갈라질 수 없다.
// 설정이 바뀌었으면 true (호출부가 save_config() 를 부른다).
bool reshade::runtime::draw_gui_spray_trainer()
{
	bool modified = false;

	// ── 입력 진단 ────────────────────────────────────────────────────────────
	// ⚠️ 테스트용 임시 UI 가 아니라 영구 기능이다. 나중에 구매자가 "궤적이 안 그려져요"
	//    할 때 이 화면 하나로 진단이 끝난다 — 게임이 raw input 을 등록하지 않으면
	//    이동량을 얻을 방법이 아예 없고(스펙 §3), 그 사실을 조용히 숨기면 안 된다.
	sherbet::begin_card("##aim_diag");
	{
		const sherbet::theme &t = sherbet::active_theme();
		ImGui::TextUnformatted(ICON_FK_INFO_CIRCLE "  \xEC\x9E\x85\xEB\xA0\xA5 \xEC\xA7\x84\xEB\x8B\xA8"); // "입력 진단"
		ImGui::Spacing();

		// (1) 좌클릭 — 발사 기록의 재료. 이건 버튼만 있으면 되므로 거의 항상 동작한다.
		ImGui::TextUnformatted(ICON_FK_MOUSE_POINTER "  \xEC\xA2\x8C\xED\x81\xB4\xEB\xA6\xAD \xEA\xB0\x90\xEC\xA7\x80"); // "좌클릭 감지"
		ImGui::SameLine();
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(t.accent), "%u\xED\x9A\x8C", _sherbet_spray_clicks); // "%u회"

		// (2) 마우스 이동 — 궤적의 재료. 여기가 ❌ 면 기능의 절반이 성립하지 않는다.
		const bool raw_ok = _input != nullptr && _input->raw_mouse_available();
		if (raw_ok)
		{
			ImGui::TextColored(sherbet::status_color(sherbet::status::good), "%s", ICON_FK_OK "  \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEC\x9D\xB4\xEB\x8F\x99 \xEC\x9D\xBD\xEA\xB8\xB0 \xEA\xB0\x80\xEB\x8A\xA5"); // "마우스 이동 읽기 가능"
			ImGui::SameLine();
			ImGui::TextDisabled("\xEB\x88\x84\xEC\xA0\x81 %llu", _sherbet_spray_move_total); // "누적 %llu"
		}
		else
		{
			ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s", ICON_FK_CANCEL "  \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEC\x9D\xB4\xEB\x8F\x99"); // "마우스 이동"
			ImGui::TextWrapped("%s", "\xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C\xEB\x8A\x94 \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEC\x9D\xB4\xEB\x8F\x99\xEC\x9D\x84 \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB6\xA4\xEC\xA0\x81 \xEB\x8C\x80\xEC\x8B\xA0 \xEB\xB0\x9C\xEC\x82\xAC \xEA\xB8\xB0\xEB\xA1\x9D\xEB\xA7\x8C \xED\x91\x9C\xEC\x8B\x9C\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "이 게임에서는 마우스 이동을 읽을 수 없어요 — 궤적 대신 발사 기록만 표시됩니다"
		}

		ImGui::Spacing();
		// 두 토글이 모두 꺼져 있으면 기록 자체가 돌지 않는다. 숫자가 0 에서 멈춰 있는 이유를
		// 사용자가 추측하게 두지 않는다.
		if (!_sherbet_spray_live && !_sherbet_spray_chart)
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  \xEC\x8A\xA4\xED\x94\x84\xEB\xA0\x88\xEC\x9D\xB4 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEA\xBA\xBC\xEC\xA0\xB8 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x95\x84\xEB\x9E\x98\xEC\x97\x90\xEC\x84\x9C \xEC\xBC\x9C\xEB\xA9\xB4 \xEC\x9D\xB4 \xEC\x88\xAB\xEC\x9E\x90\xEA\xB0\x80 \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x91\xEB\x8B\x88\xEB\x8B\xA4"); // "스프레이 기록이 꺼져 있어요 — 아래에서 켜면 이 숫자가 올라갑니다"
		sherbet_hint("\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEA\xB3\xA0 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C \xEC\x8F\xB4 \xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94 \xE2\x80\x94 \xEC\x9D\xB4 \xEC\x88\xAB\xEC\x9E\x90\xEA\xB0\x80 \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x80\xEB\xA9\xB4 \xEB\xA6\xAC\xEC\x89\x90\xEC\x9D\xB4\xEB\x93\x9C\xEA\xB0\x80 \xEC\x9E\x85\xEB\xA0\xA5\xEC\x9D\x84 \xEC\xA0\x9C\xEB\x8C\x80\xEB\xA1\x9C \xEB\xB3\xB4\xEA\xB3\xA0 \xEC\x9E\x88\xEB\x8A\x94 \xEA\xB1\xB0\xEC\x98\x88\xEC\x9A\x94"); // 안내
		ImGui::TextDisabled("%s", "\xEA\xB2\x8C\xEC\x9E\x84 \xEB\xA9\x94\xEB\xAA\xA8\xEB\xA6\xAC\xEB\x82\x98 \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\x84 \xEC\x9D\xBD\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x84\xEC\x9A\x94. \xEC\x9D\xB4\xEB\xAF\xB8 \xED\x9B\x84\xED\x82\xB9 \xEC\xA4\x91\xEC\x9D\xB8 \xEC\x9E\x85\xEB\xA0\xA5\xEB\xA7\x8C \xEA\xB4\x80\xEC\xB0\xB0\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4."); // 안전 안내
	}
	sherbet::end_card();
	ImGui::Spacing();

	// ── 스프레이 트레이너 ────────────────────────────────────────────────────
	// 사격 방식을 분류하지 않는다 — 무발사 간격으로만 구간을 나누므로 탭/버스트/연발이
	// 저절로 다른 모양(점 하나 / 짧은 궤적 / 긴 궤적)으로 나온다.
	if (ImGui::CollapsingHeader(ICON_FK_CHART_LINE "  \xEC\x8A\xA4\xED\x94\x84\xEB\xA0\x88\xEC\x9D\xB4 \xED\x8A\xB8\xEB\xA0\x88\xEC\x9D\xB4\xEB\x84\x88", ImGuiTreeNodeFlags_DefaultOpen)) // "스프레이 트레이너"
	{
		// 두 토글은 독립이다. 둘 다 끄거나, 하나만 켜거나, 둘 다 켤 수 있다(기본은 둘 다 꺼짐).
		modified |= ImGui::Checkbox("\xED\x99\x94\xEB\xA9\xB4\xEC\x97\x90 \xEC\x8B\xA4\xEC\x8B\x9C\xEA\xB0\x84 \xED\x91\x9C\xEC\x8B\x9C##spray", &_sherbet_spray_live); // "화면에 실시간 표시"
		modified |= ImGui::Checkbox("\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEC\x97\x90 \xEC\xB0\xA8\xED\x8A\xB8 \xED\x91\x9C\xEC\x8B\x9C##spray", &_sherbet_spray_chart); // "오버레이에 차트 표시"
		// raw 단위는 감도가 사람마다 달라 픽셀로 바꾸는 계수가 필요하다(스펙 §5.1).
		modified |= ImGui::SliderFloat("\xEA\xB6\xA4\xEC\xA0\x81 \xEB\xB0\xB0\xEC\x9C\xA8##spray", &_sherbet_spray_scale, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp); // "궤적 배율"
		modified |= ImGui::SliderInt("\xEA\xB5\xAC\xEA\xB0\x84 \xEB\x82\x98\xEB\x88\x84\xEA\xB8\xB0 \xEA\xB0\x84\xEA\xB2\xA9(ms)##spray", &_sherbet_spray_gap_ms, 50, 2000, "%d", ImGuiSliderFlags_AlwaysClamp); // "구간 나누기 간격(ms)"

		if (_sherbet_spray_chart)
		{
			const std::vector<sherbet::spray::segment> &hist = _sherbet_spray.history();
			const sherbet::spray::segment *const cur = _sherbet_spray.current();
			const bool raw_ok2 = _input != nullptr && _input->raw_mouse_available();

			static int spray_sel = -1;          // 목록에서 고른 구간(history 인덱스). -1 = 최신
			modified |= ImGui::Checkbox("\xEC\xB5\x9C\xEA\xB7\xBC 5\xEA\xB0\x9C \xEA\xB2\xB9\xEC\xB3\x90\xEB\xB3\xB4\xEA\xB8\xB0##spray", &_sherbet_spray_overlay5); // "최근 5개 겹쳐보기"
			ImGui::SameLine();
			if (sherbet::pill_button(ICON_FK_TRASH "  \xEA\xB8\xB0\xEB\xA1\x9D \xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0", false)) // "기록 지우기"
			{
				_sherbet_spray.clear();
				spray_sel = -1;
			}
			if (spray_sel >= static_cast<int>(hist.size()))
				spray_sel = -1; // 링버퍼가 돌아 선택이 사라졌다

			// 강조해서 보여줄 구간: 고른 것 > 진행 중 > 가장 최근 완료분
			const sherbet::spray::segment *focus = nullptr;
			if (spray_sel >= 0)
				focus = &hist[static_cast<std::size_t>(spray_sel)];
			else if (cur != nullptr)
				focus = cur;
			else if (!hist.empty())
				focus = &hist.back();

			// ── 차트 캔버스 ──
			const ImVec2 c0 = ImGui::GetCursorScreenPos();
			// 창을 아주 좁게 줄이면 남는 폭이 0 이 될 수 있는데, InvisibleButton 은 0 크기를
			// 허용하지 않는다(디버그 빌드에서 단언). 최소 폭을 준다.
			const float cw = ImMax(64.0f, ImGui::GetContentRegionAvail().x);
			const float ch = 220.0f;
			const ImVec2 c1(c0.x + cw, c0.y + ch);
			ImGui::InvisibleButton("##spray_canvas", ImVec2(cw, ch));
			ImDrawList *const dl = ImGui::GetWindowDrawList();
			const sherbet::theme &ct = sherbet::active_theme();

			// 캔버스 그리기는 sherbet_draw_spray_canvas() 하나로 모았다 — 잠금 미리보기가
			// **같은 함수**를 쓰기 때문이다(미리보기와 실물이 갈라지면 그건 광고가 아니다).
			// 이동을 못 읽는 게임이면 점이 전부 원점에 겹친다 — 구간을 아예 넘기지 않고
			// (배경·격자·십자만 그리게) 그 자리에 안내만 낸다.
			const ImVec2 org = sherbet_draw_spray_canvas(dl, c0, c1, ct, hist,
				raw_ok2 ? focus : nullptr, raw_ok2 && _sherbet_spray_overlay5, _sherbet_spray_scale);
			if (!raw_ok2)
			{
				const ImVec2 tsz = ImGui::CalcTextSize("\xEB\xB0\x9C\xEC\x82\xAC \xEA\xB8\xB0\xEB\xA1\x9D\xEB\xA7\x8C \xED\x91\x9C\xEC\x8B\x9C \xEC\xA4\x91 \xE2\x80\x94 \xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C\xEB\x8A\x94 \xEC\x9D\xB4\xEB\x8F\x99\xEC\x9D\x84 \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94"); // "발사 기록만 표시 중 — 이 게임에서는 이동을 읽을 수 없어요"
				dl->PushClipRect(c0, c1, true);
				dl->AddText(ImVec2(org.x - tsz.x * 0.5f, org.y + 14.0f), sherbet::with_alpha(ct.text_dim, 220),
					"\xEB\xB0\x9C\xEC\x82\xAC \xEA\xB8\xB0\xEB\xA1\x9D\xEB\xA7\x8C \xED\x91\x9C\xEC\x8B\x9C \xEC\xA4\x91 \xE2\x80\x94 \xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C\xEB\x8A\x94 \xEC\x9D\xB4\xEB\x8F\x99\xEC\x9D\x84 \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94");
				dl->PopClipRect();
			}

			// ── 통계 ──
			if (focus != nullptr && !focus->shots.empty())
			{
				ImGui::Text("\xEB\xB0\x9C\xEC\x88\x98 %d", static_cast<int>(focus->shots.size())); // "발수 %d"
				float iv = 0.0f;
				if (sherbet::spray::avg_interval(*focus, iv) && iv > 0.0f)
				{
					ImGui::SameLine();
					ImGui::Text("\xED\x8F\x89\xEA\xB7\xA0 %.0f RPM", 60.0f / iv); // "평균 %.0f RPM"
				}
			}
			// 종료 지점 편차 — 최근 5구간의 마지막 점들이 그 중심에서 떨어진 평균 거리(스펙 §5.2).
			// 배율을 적용해 화면 픽셀로 보여준다. 구간이 2개 미만이면 아예 표시하지 않는다.
			float spread = 0.0f;
			if (raw_ok2 && sherbet::spray::end_spread(hist, 5, spread))
			{
				ImGui::SameLine();
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ct.accent), "\xEC\xA2\x85\xEB\xA3\x8C \xEC\xA7\x80\xEC\xA0\x90 \xED\x8E\xB8\xEC\xB0\xA8 %.1fpx", spread * _sherbet_spray_scale); // "종료 지점 편차 %.1fpx"
			}
			else if (raw_ok2)
			{
				ImGui::TextDisabled("%s", "\xEA\xB5\xAC\xEA\xB0\x84\xEC\x9D\xB4 2\xEA\xB0\x9C \xEC\x9D\xB4\xEC\x83\x81\xEC\x9D\xB4\xEC\x96\xB4\xEC\x95\xBC \xED\x8E\xB8\xEC\xB0\xA8\xEA\xB0\x80 \xEB\x82\x98\xEC\x99\x80\xEC\x9A\x94"); // "구간이 2개 이상이어야 편차가 나와요"
			}

			// ── 구간 목록 (최근이 위) ──
			ImGui::Spacing();
			ImGui::TextDisabled("%s", "\xEA\xB5\xAC\xEA\xB0\x84 \xEB\xAA\xA9\xEB\xA1\x9D (\xEC\xB5\x9C\xEA\xB7\xBC\xEC\x9D\xB4 \xEC\x9C\x84)"); // "구간 목록 (최근이 위)"
			if (hist.empty() && cur == nullptr)
			{
				ImGui::TextDisabled("%s", "\xEC\x95\x84\xEC\xA7\x81 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEA\xB3\xA0 \xED\x95\x9C \xEB\xB2\x88 \xEC\x8F\xB4 \xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94"); // "아직 기록이 없어요 — 오버레이를 닫고 한 번 쏴 보세요"
			}
			else
			{
				if (cur != nullptr)
					ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ct.accent2), ICON_FK_CIRCLE "  \xEC\xA7\x84\xED\x96\x89 \xEC\xA4\x91 \xC2\xB7 %d\xEB\xB0\x9C", static_cast<int>(cur->shots.size())); // "진행 중 · %d발"
				// 최신이 #1 — 링버퍼가 돌아도 "방금 쏜 것"의 번호가 바뀌지 않는다.
				for (std::size_t i = hist.size(), n = 1; i > 0; --i, ++n)
				{
					const sherbet::spray::segment &s = hist[i - 1];
					const int idx = static_cast<int>(i - 1);
					char label[96];
					float iv = 0.0f;
					if (sherbet::spray::avg_interval(s, iv))
						snprintf(label, sizeof(label), "#%d \xC2\xB7 %d\xEB\xB0\x9C \xC2\xB7 %.0fms \xEA\xB0\x84\xEA\xB2\xA9##sprayseg%d", static_cast<int>(n), static_cast<int>(s.shots.size()), iv * 1000.0f, idx); // "#%d · %d발 · %.0fms 간격"
					else
						snprintf(label, sizeof(label), "#%d \xC2\xB7 %d\xEB\xB0\x9C##sprayseg%d", static_cast<int>(n), static_cast<int>(s.shots.size()), idx); // "#%d · %d발"
					if (ImGui::Selectable(label, spray_sel == idx))
						spray_sel = (spray_sel == idx) ? -1 : idx; // 다시 누르면 선택 해제
				}
				ImGui::TextDisabled("%s", "\xED\x81\xB4\xEB\xA6\xAD\xED\x95\x98\xEB\xA9\xB4 \xEA\xB7\xB8 \xEA\xB5\xAC\xEA\xB0\x84\xEB\xA7\x8C \xEC\xA7\x84\xED\x95\x98\xEA\xB2\x8C \xEB\xB3\xB4\xEC\x97\xAC\xEC\x9A\x94"); // "클릭하면 그 구간만 진하게 보여요"
			}
		}
		ImGui::Spacing();
	}

	return modified;
}

// SHERBET: 「에임」 탭 — 입력 진단 + 커스텀 조준점(설정 탭에서 이전).
// 이 탭은 게임 메모리도 화면 픽셀도 읽지 않는다. 리쉐이드가 이미 후킹 중인 입력을 볼 뿐이다.
void reshade::runtime::draw_gui_aim()
{
	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.6f);
	ImGui::TextUnformatted(ICON_FK_CROSSHAIRS "  \xEC\x97\x90\xEC\x9E\x84"); // "에임"
	ImGui::PopFont();
	ImGui::Spacing();

	bool modified = false;

	// ── 스프레이 트레이너 (유료 기능 'spray') ────────────────────────────────
	// ⚠️ 이 탭의 나머지 — 커스텀 조준점 편집기와 「마켓에서 조준점 고르기」 — 는 **무료**다.
	//    잠그는 것은 스프레이 트레이너 구획(입력 진단 + 궤적 차트) 하나뿐이다.
	// 잠겨 있으면 감추는 대신 판매 카드를 그린다 — 안 보이는 기능은 한 개도 안 팔린다.
	if (sherbet_feature_unlocked("spray"))
	{
		modified |= draw_gui_spray_trainer();
	}
	else if (const sherbet::paid::feature *const spray_f = sherbet::paid::find("spray"))
	{
		sherbet_draw_spray_lock_card(*spray_f);
	}

	// ── 화면 이동 추정 (실험) ────────────────────────────────────────────────
	// 스프레이 트레이너는 "내 손이 어떻게 움직였나"만 보여준다. **기준**이 되는 게임의
	// 실제 반동을 알려면 화면이 얼마나 밀렸는지가 필요하다:
	//     화면 이동 = 게임 반동 + 내 마우스 이동   →   반동 = 화면 − 마우스
	// ⚠️ 이 기능만 **화면 픽셀을 읽는다**(스프레이 트레이너는 읽지 않는다).
	//    그래서 기본이 꺼짐이고, 꺼져 있으면 리드백도 계산도 전혀 일어나지 않는다.
	//    쓸 만한지 판정하기 위한 실험이며, 결과를 보고 정식 기능 여부를 정한다.
	if (ImGui::CollapsingHeader(ICON_FK_FLASK "  \xED\x99\x94\xEB\xA9\xB4 \xEC\x9D\xB4\xEB\x8F\x99 \xEC\xB6\x94\xEC\xA0\x95 (\xEC\x8B\xA4\xED\x97\x98)")) // "화면 이동 추정 (실험)"
	{
		const sherbet::theme &mt = sherbet::active_theme();

		if (ImGui::Checkbox("\xEC\xBC\x9C\xEA\xB8\xB0 \xE2\x80\x94 \xEC\x9D\xB4 \xEA\xB8\xB0\xEB\x8A\xA5\xEB\xA7\x8C \xED\x99\x94\xEB\xA9\xB4 \xED\x94\xBD\xEC\x85\x80\xEC\x9D\x84 \xEC\x9D\xBD\xEC\x96\xB4\xEC\x9A\x94##motion", &_sherbet_motion_on)) // "켜기 — 이 기능만 화면 픽셀을 읽어요"
		{
			modified = true;
			// 확정 실패 상태(포맷 미지원·리소스 실패)는 다시 켤 때 한 번 더 시도하게 푼다.
			// 안 그러면 해상도만 바꾸고 다시 켠 사용자가 영영 같은 에러를 본다.
			_sherbet_motion_state = 0;
		}
		sherbet_hint("\xED\x99\x94\xEB\xA9\xB4 \xEA\xB0\x80\xEC\x9A\xB4\xEB\x8D\xB0\xEB\xA5\xBC \xEC\xA1\xB0\xEA\xB8\x88 \xEB\x96\xBC\xEC\x96\xB4 CPU \xEB\xA1\x9C \xEA\xB0\x80\xEC\xA0\xB8\xEC\x99\x80, \xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84 \xEC\x82\xAC\xEC\x9D\xB4\xEC\x97\x90 \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\xB4 \xEC\x96\xBC\xEB\xA7\x88\xEB\x82\x98 \xEB\xB0\x80\xEB\xA0\xB8\xEB\x8A\x94\xEC\xA7\x80 \xEC\x9E\xBD\xEB\x8B\x88\xEB\x8B\xA4. \xEB\x81\x84\xEB\xA9\xB4 \xEB\xB3\xB5\xEC\x82\xAC\xEB\x8F\x84 \xEA\xB3\x84\xEC\x82\xB0\xEB\x8F\x84 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94."); // 설명

		if (_sherbet_motion_on)
		{
			modified |= ImGui::Checkbox("\xED\x99\x94\xEB\xA9\xB4\xEC\x97\x90 \xEC\x88\xAB\xEC\x9E\x90\xED\x8C\x90 \xED\x91\x9C\xEC\x8B\x9C##motion", &_sherbet_motion_hud); // "화면에 숫자판 표시"
			// 엔진의 입력 지연 때문에 '화면이 밀린 프레임'과 '그 마우스 입력이 들어온 프레임'이
			// 한 칸쯤 어긋난다. 마우스만 움직였을 때 반동이 0 에 가장 가까운 값이 정답이다.
			modified |= ImGui::SliderInt("\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEC\xA0\x95\xEB\xA0\xAC \xEB\xB3\xB4\xEC\xA0\x95(\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84)##motion", &_sherbet_motion_lag, -2, 1, "%d", ImGuiSliderFlags_AlwaysClamp); // "마우스 정렬 보정(프레임)"

			// ── 상태 ──
			if (_sherbet_motion_state == 2)
				ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s", ICON_FK_CANCEL "  \xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x98 \xEB\xB0\xB1\xEB\xB2\x84\xED\x8D\xBC \xED\x98\x95\xEC\x8B\x9D\xEC\x9D\x80 \xEC\x95\x84\xEC\xA7\x81 \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 (8\xEB\xB9\x84\xED\x8A\xB8/10\xEB\xB9\x84\xED\x8A\xB8 \xEC\x83\x89\xEC\x83\x81\xEB\xA7\x8C \xEC\xA7\x80\xEC\x9B\x90)"); // 포맷 미지원
			else if (_sherbet_motion_state == 3)
				ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s", ICON_FK_CANCEL "  \xEB\xA6\xAC\xEB\x93\x9C\xEB\xB0\xB1 \xED\x85\x8D\xEC\x8A\xA4\xEC\xB2\x98\xEB\xA5\xBC \xEB\xA7\x8C\xEB\x93\xA4\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x9D\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C\xEB\x8A\x94 \xED\x99\x94\xEB\xA9\xB4 \xEC\x9D\xB4\xEB\x8F\x99\xEC\x9D\x84 \xEC\x9E\xB4 \xEC\x88\x98 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"); // 리소스 실패
			else if (_sherbet_motion_state == 0)
				// ⚠️ 캡처 지점이 render_effects() 안이라 **이펙트가 실제로 도는 프레임**에서만 뜬다
				//    (반반 비교 스냅샷과 같은 자리다). 이펙트를 전부 끈 상태로는 한 장도 못 받는다.
				ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  \xEC\x95\x84\xEC\xA7\x81 \xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84\xEC\x9D\x84 \xEB\xAA\xBB \xEB\xB0\x9B\xEC\x95\x98\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEA\xB3\xA0, \xED\x9A\xA8\xEA\xB3\xBC\xEA\xB0\x80 \xEC\xBC\x9C\xEC\xA0\xB8 \xEC\x9E\x88\xEB\x8A\x94 \xEC\x83\x81\xED\x83\x9C\xEC\x97\xAC\xEC\x95\xBC \xEC\x9E\xBD\xEB\x8B\x88\xEB\x8B\xA4"); // 아직 프레임 없음
			else
				ImGui::TextDisabled("\xED\x81\xAC\xEB\xA1\xAD %ux%u \xC2\xB7 CPU %.2fms/\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84 \xC2\xB7 \xEC\xB6\x94\xEC\xA0\x95 %u\xED\x9A\x8C", _sherbet_motion_w, _sherbet_motion_h, _sherbet_motion_cpu_ms, _sherbet_motion_samples); // "크롭 … · CPU … · 추정 …회"

			ImGui::TextDisabled("%s", "\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEC\x97\xAC\xEB\x8A\x94 \xEB\x8F\x99\xEC\x95\x88\xEC\x9D\x80 \xEC\x9E\xAC\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x84\xEC\x9A\x94 (\xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\xB4 \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4\xEB\xA5\xBC \xEB\xAA\xBB \xEB\xB0\x9B\xEC\x95\x84 \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\xB4 \xEB\xA9\x88\xEC\xB6\x94\xEB\xAF\x80\xEB\xA1\x9C)"); // 기록 정지 조건
			ImGui::Spacing();

			// ── 마지막 값 ──
			// (오버레이가 열려 있는 동안은 측정이 멈추므로, 이 값은 '오버레이를 열기 직전'
			//  프레임이다. 실시간으로 보려면 위의 숫자판을 켜고 오버레이를 닫는다)
			if (_sherbet_motion.count() > 0)
			{
				const sherbet::motion::sample &s = _sherbet_motion.last();
				// ⚠️ 값 열은 **폰트 상대값**이어야 한다(sherbet_stat_row 와 같은 규약).
				//    SameLine(offset) 은 커서를 window->Pos.x + offset 으로 절대 이동시키고
				//    직전 아이템 끝으로 클램프하지 않는다 — 고정 110px 이었을 땐 폰트 21 부터
				//    값이 라벨 **위에 겹쳐** 찍혔다(폰트 슬라이더 상한이 32 라 도달 가능하다).
				const float mv_col = _imgui_context->Style.WindowPadding.x + 7.0f * ImGui::GetFontSize();
				ImGui::Text("%s", "\xED\x99\x94\xEB\xA9\xB4"); // "화면"
				ImGui::SameLine(mv_col);
				ImGui::Text("dx %+7.2f   dy %+7.2f", s.screen_dx, s.screen_dy);
				ImGui::Text("%s", "\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4"); // "마우스"
				ImGui::SameLine(mv_col);
				ImGui::Text("dx %+7.2f   dy %+7.2f", s.mouse_dx, s.mouse_dy);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(mt.accent), "%s", "\xEB\xB0\x98\xEB\x8F\x99"); // "반동"
				ImGui::SameLine(mv_col);
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(mt.accent), "dx %+7.2f   dy %+7.2f", s.recoil_dx(), s.recoil_dy());
				ImGui::Text("%s", "\xEC\x8B\xA0\xEB\xA2\xB0\xEB\x8F\x84"); // "신뢰도"
				ImGui::SameLine(mv_col);
				ImGui::Text("x %3.0f%%   y %3.0f%%", s.conf_x * 100.0f, s.conf_y * 100.0f);
			}

			// ── 롤링 히스토리 ──
			// 검증 3단계(쏘기)는 오버레이를 닫고 해야 하므로, 라이브로 잡는 대신 여기 남는다.
			const int total = _sherbet_motion.count();
			const ImVec2 m0 = ImGui::GetCursorScreenPos();
			const float mw = ImMax(64.0f, ImGui::GetContentRegionAvail().x);
			const float mh = 170.0f;
			const ImVec2 m1(m0.x + mw, m0.y + mh);
			ImGui::InvisibleButton("##motion_canvas", ImVec2(mw, mh));
			ImDrawList *const mdl = ImGui::GetWindowDrawList();
			mdl->AddRectFilled(m0, m1, sherbet::with_alpha(mt.bg1, 220), 12.0f);
			mdl->PushClipRect(m0, m1, true);
			{
				const float mid = (m0.y + m1.y) * 0.5f;
				mdl->AddLine(ImVec2(m0.x, mid), ImVec2(m1.x, mid), sherbet::with_alpha(mt.border, 140));

				if (total < 2)
				{
					mdl->AddText(ImVec2(m0.x + 14.0f, mid - 8.0f), sherbet::with_alpha(mt.text_dim, 220),
						"\xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEA\xB3\xA0 \xEC\x9E\xA0\xEA\xB9\x90 \xEC\x9B\x80\xEC\xA7\x81\xEC\x9D\xB4\xEA\xB1\xB0\xEB\x82\x98 \xEC\x8F\x9C \xEB\x92\xA4 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB4\xEC\x96\xB4\xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94."); // "기록이 없어요…"
				}
				else
				{
					// 세로 축 자동 배율. 하한을 두어 잡음만 있을 때 화면이 꽉 차 보이지 않게 한다.
					float peak = 6.0f;
					for (int i = 0; i < total; ++i)
					{
						const sherbet::motion::sample &s = _sherbet_motion.at(i);
						peak = ImMax(peak, ImMax(std::abs(s.screen_dy), ImMax(std::abs(s.mouse_dy), std::abs(s.recoil_dy()))));
					}
					const float sy = (mh * 0.5f - 14.0f) / peak;
					const float step = mw / static_cast<float>(total > 1 ? total - 1 : 1);
					// dy 는 마우스 규약(아래가 +)이고 화면 좌표도 아래가 + 라, 그대로 더하면
					// **위로 튄 반동이 그래프에서도 위로** 나온다. 부호를 또 뒤집지 않는다.
					auto py = [&](float v) { return mid + v * sy; };

					// 마우스(흐리게) → 화면(테마색) → 반동(흰색) 순으로 겹쳐 그린다.
					for (int i = 1; i < total; ++i)
					{
						const sherbet::motion::sample &a = _sherbet_motion.at(i - 1);
						const sherbet::motion::sample &b = _sherbet_motion.at(i);
						const float xa = m0.x + step * static_cast<float>(i - 1), xb = m0.x + step * static_cast<float>(i);
						mdl->AddLine(ImVec2(xa, py(a.mouse_dy)), ImVec2(xb, py(b.mouse_dy)), sherbet::with_alpha(mt.text_dim, 150), 1.0f);
					}
					for (int i = 1; i < total; ++i)
					{
						const sherbet::motion::sample &a = _sherbet_motion.at(i - 1);
						const sherbet::motion::sample &b = _sherbet_motion.at(i);
						const float xa = m0.x + step * static_cast<float>(i - 1), xb = m0.x + step * static_cast<float>(i);
						// 신뢰도가 낮은 구간은 회색 — 숫자가 아니라 쓰레기라는 표시다.
						const bool ok = ImMin(a.conf_y, b.conf_y) > 0.25f;
						mdl->AddLine(ImVec2(xa, py(a.screen_dy)), ImVec2(xb, py(b.screen_dy)),
							ok ? sherbet::with_alpha(mt.accent, 200) : IM_COL32(130, 130, 130, 110), ok ? 1.6f : 1.0f);
						mdl->AddLine(ImVec2(xa, py(a.recoil_dy())), ImVec2(xb, py(b.recoil_dy())),
							ok ? IM_COL32(255, 255, 255, 235) : IM_COL32(130, 130, 130, 90), ok ? 2.0f : 1.0f);
					}
					// 바닥에 신뢰도 막대 — 어느 구간이 쓰레기인지 한눈에 보인다.
					for (int i = 0; i < total; ++i)
					{
						const float c = ImClamp(_sherbet_motion.at(i).conf_y, 0.0f, 1.0f);
						const float x = m0.x + step * static_cast<float>(i);
						mdl->AddLine(ImVec2(x, m1.y - 2.0f), ImVec2(x, m1.y - 2.0f - c * 12.0f),
							IM_COL32(static_cast<int>(255 * (1.0f - c)), static_cast<int>(200 * c + 40), 60, 200), ImMax(1.0f, step));
					}

					char cap[64];
					snprintf(cap, sizeof(cap), "\xC2\xB1%.0fpx", peak); // "±…px" — 세로 축 배율
					mdl->AddText(ImVec2(m0.x + 8.0f, m0.y + 6.0f), sherbet::with_alpha(mt.text_dim, 200), cap);
				}
			}
			mdl->PopClipRect();

			// 위로 튄 최대값 — 검증 3단계에서 "튀었나"를 숫자 하나로 답한다.
			float up_max = 0.0f;
			for (int i = 0; i < total; ++i)
			{
				const sherbet::motion::sample &s = _sherbet_motion.at(i);
				if (s.conf_y > 0.25f && -s.recoil_dy() > up_max)
					up_max = -s.recoil_dy();
			}
			ImGui::Text("%s", "\xEC\x9C\x84\xEB\xA1\x9C \xED\x8A\x84 \xEC\xB5\x9C\xEB\x8C\x80\xEA\xB0\x92"); // "위로 튄 최대값"
			// 이 라벨은 6자라 값 열이 더 오른쪽이다. 위와 같은 이유로 폰트 상대값을 쓴다.
			ImGui::SameLine(_imgui_context->Style.WindowPadding.x + 9.0f * ImGui::GetFontSize());
			if (up_max > 0.0f)
				ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(mt.accent), "%.1f px", up_max);
			else
				ImGui::TextDisabled("%s", "\xEC\x97\x86\xEC\x9D\x8C"); // "없음"

			if (sherbet::pill_button(ICON_FK_TRASH "  \xEA\xB8\xB0\xEB\xA1\x9D \xEC\xA7\x80\xEC\x9A\xB0\xEA\xB8\xB0##motion", false)) // "기록 지우기"
			{
				_sherbet_motion.reset();
				_sherbet_motion_samples = 0;
			}

			ImGui::Spacing();
			ImGui::TextDisabled("%s", "\xED\x99\x95\xEC\x9D\xB8\xED\x95\x98\xEB\x8A\x94 \xEB\xB2\x95 \xE2\x80\x94 \xEC\x85\x8B \xEB\x8B\xA4 \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEC\x9D\x80 \xEC\xB1\x84\xEB\xA1\x9C \xED\x95\x98\xEA\xB3\xA0, \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x97\xB4\xEC\x96\xB4\xEC\x84\x9C \xEC\x95\x84\xEB\x9E\x98 \xEA\xB7\xB8\xEB\x9E\x98\xED\x94\x84\xEB\xA5\xBC \xEB\xB4\x85\xEB\x8B\x88\xEB\x8B\xA4"); // 안내
			ImGui::BulletText("%s", "\xEA\xB0\x80\xEB\xA7\x8C\xED\x9E\x88 \xEC\x9E\x88\xEA\xB8\xB0 - \xED\x99\x94\xEB\xA9\xB4\xC2\xB7\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEB\x91\x98 \xEB\x8B\xA4 0 \xEA\xB7\xBC\xEC\xB2\x98\xEC\x97\x90 \xEB\xB6\x99\xEC\x96\xB4 \xEC\x9E\x88\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xB4\xEC\x9A\x94"); // ①
			ImGui::BulletText("%s", "\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4\xEB\xA7\x8C \xEC\x9B\x80\xEC\xA7\x81\xEC\x9D\xB4\xEA\xB8\xB0 - \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\xB4 \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4\xEC\x99\x80 \xEA\xB0\x99\xEC\x9D\x80 \xEB\xB0\xA9\xED\x96\xA5\xC2\xB7\xEB\xB9\x84\xEC\x8A\xB7\xED\x95\x9C \xED\x81\xAC\xEA\xB8\xB0\xEB\xA1\x9C \xEB\x94\xB0\xEB\x9D\xBC\xEA\xB0\x80\xEC\x95\xBC \xED\x95\xB4\xEC\x9A\x94"); // ②
			ImGui::BulletText("%s", "\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 \xEA\xB3\xA0\xEC\xA0\x95\xED\x95\x98\xEA\xB3\xA0 \xEC\x8F\x98\xEA\xB8\xB0 - \xED\x95\x9C \xEB\xB0\x9C\xEB\xA7\x88\xEB\x8B\xA4 \xEB\xB0\x98\xEB\x8F\x99\xEC\x9D\xB4 \xEC\x9C\x84\xEB\xA1\x9C \xED\x8A\x80\xEC\x96\xB4\xEC\x95\xBC \xED\x95\xB4\xEC\x9A\x94 (\xEC\x9D\xB4\xEA\xB2\x8C \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x98 \xEC\x8B\xA4\xEC\xA0\x9C \xEB\xB0\x98\xEB\x8F\x99)"); // ③
			ImGui::TextDisabled("%s", "\xED\x9A\x8C\xEC\x83\x89 \xEA\xB5\xAC\xEA\xB0\x84\xEC\x9D\x80 \xEC\x8B\xA0\xEB\xA2\xB0\xEB\x8F\x84\xEA\xB0\x80 \xEB\x82\xAE\xEC\x9D\x80 \xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94 \xE2\x80\x94 \xEC\x88\xAB\xEC\x9E\x90\xEA\xB0\x80 \xEC\x95\x84\xEB\x8B\x88\xEB\x9D\xBC \xEC\x93\xB0\xEB\xA0\x88\xEA\xB8\xB0\xEB\xA1\x9C \xEB\xB4\x90\xEC\x95\xBC \xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // 신뢰도 안내
		}
		ImGui::Spacing();
	}

	// ── 조준점 모드 ──────────────────────────────────────────────────────────
	// 클래식(점/십자/원/이미지)과 발로란트는 **저장이 완전히 별개**다(설계 §1.6):
	// 기존 ini 키 SHERBET/Crosshair* 는 그대로 두고 새 설정은 SHERBET/Val* 로 간다.
	// 그래서 업데이트해도 쓰던 조준점이 그대로 나오고, 언제든 되돌릴 수 있다.
	// UI 에서만 3지 선택으로 묶는다 — 둘 다 켜면 조준점이 두 개 겹쳐 보이기 때문이다.
	{
		int xh_mode = _sherbet_val_on ? 2 : (_sherbet_crosshair_on ? 1 : 0);
		const int xh_mode_prev = xh_mode;
		ImGui::TextUnformatted(ICON_FK_CROSSHAIRS "  " "\xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90" /* 조준점 */);
		ImGui::SameLine();
		ImGui::RadioButton("\xEB\x81\x84\xEA\xB8\xB0" /* 끄기 */ "##xhmode", &xh_mode, 0);
		ImGui::SameLine();
		ImGui::RadioButton("\xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D" /* 클래식 */ "##xhmode", &xh_mode, 1);
		ImGui::SameLine();
		ImGui::RadioButton("\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8" /* 발로란트 */ "##xhmode", &xh_mode, 2);
		if (xh_mode != xh_mode_prev)
		{
			_sherbet_crosshair_on = (xh_mode == 1);
			_sherbet_val_on = (xh_mode == 2);
			modified = true;
		}
		ImGui::TextDisabled("%s", "\xEB\x91\x90 \xEB\xAA\xA8\xEB\x93\x9C\xEC\x9D\x98 \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\x80 \xEB\x94\xB0\xEB\xA1\x9C \xEC\xA0\x80\xEC\x9E\xA5\xEB\x8F\xBC\xEC\x9A\x94 \xE2\x80\x94 \xEB\xB0\x94\xEA\xBF\x94\xEB\x8F\x84 \xEC\x9B\x90\xEB\x9E\x98 \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\xB4 \xEC\x82\xAC\xEB\x9D\xBC\xEC\xA7\x80\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4" /* 두 모드의 설정은 따로 저장돼요 — 바꿔도 원래 설정이 사라지지 않습니다 */);
		// (안티치트 경고문 삭제 — 사용자 결정 2026-07-31: FiveM 은 커스텀 조준점 소프트웨어를
		//  허용하므로 겁주는 문구가 오히려 상품 신뢰를 깎는다. 되살리지 말 것.)
	}
	ImGui::Spacing();

	// ── 발로란트 조준점 ──────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader(ICON_FK_SLIDERS "  " "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90" /* 발로란트 조준점 */, ImGuiTreeNodeFlags_DefaultOpen))
	{
		namespace xhr = sherbet::crosshair;
		xhr::layer &VL = _sherbet_val_profile.primary;
		bool val_changed = false; // 프로필이 바뀌었나 → 공유 코드 재생성

		// 공유 코드 입력 상자는 **가져오기 대기실**이다. 여기서 타이핑해도 조준점은
		// 안 바뀌고, [가져오기] 를 눌러야 적용된다. 적용/변경 후에는 Sherbet 이 이해한
		// 코드로 다시 채워 준다(= 무엇이 해석됐는지 눈으로 확인할 수 있다).
		static char val_code_buf[1024] = "0";
		static bool val_code_fresh = false;
		static int val_msg_kind = 0;       // 0=없음 1=성공 2=실패 3=경고
		static float val_msg_timer = 0.0f; // 없으면 메시지가 영영 안 사라진다(아래 주석 참고)
		static std::string val_msg;

		// _sherbet_val_code_dirty 는 마켓에서 조준점을 적용했다는 신호다. 이걸 안 보면
		// 두 탭이 서로 다른 코드를 보여 주고, 그 상태에서 [코드 복사]를 누르면 화면에
		// 그려지는 것과 다른 코드가 복사된다.
		if (!val_code_fresh || _sherbet_val_code_dirty)
		{
			val_code_fresh = true;
			_sherbet_val_code_dirty = false;
			const size_t n = _sherbet_val_code.size() < sizeof(val_code_buf) - 1 ? _sherbet_val_code.size() : sizeof(val_code_buf) - 1;
			std::memcpy(val_code_buf, _sherbet_val_code.c_str(), n);
			val_code_buf[n] = '\0';
		}

		// ── 공유 코드 (이 기능의 존재 이유다. 맨 위에 크게 둔다) ──────────────
		sherbet::begin_card("##val_code");
		{
			ImGui::TextUnformatted(ICON_FK_DOWNLOAD "  " "\xEA\xB3\xB5\xEC\x9C\xA0 \xEC\xBD\x94\xEB\x93\x9C" /* 공유 코드 */);
			ImGui::TextDisabled("%s", "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8\xEC\x97\x90\xEC\x84\x9C \xEC\x93\xB0\xEB\x8D\x98 \xEC\xBD\x94\xEB\x93\x9C\xEB\xA5\xBC \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEB\xB6\x99\xEC\x97\xAC\xEB\x84\xA3\xEC\x9C\xBC\xEC\x84\xB8\xEC\x9A\x94" /* 발로란트에서 쓰던 코드를 그대로 붙여넣으세요 */);
			ImGui::Spacing();

			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputText("##val_code_box", val_code_buf, sizeof(val_code_buf));

			if (ImGui::Button(ICON_FK_DOWNLOAD "  " "\xEC\xBD\x94\xEB\x93\x9C \xEA\xB0\x80\xEC\xA0\xB8\xEC\x98\xA4\xEA\xB8\xB0" /* 코드 가져오기 */))
			{
				xhr::profile tmp;
				const xhr::parse_report rep = xhr::parse_code(val_code_buf, tmp);
				if (rep.ok())
				{
					// 성공했을 때만 갈아 끼운다. 실패 시 parse_code 는 tmp 를 건드리지도 않는다.
					_sherbet_val_profile = tmp;
					val_changed = true;
					if (rep.unknown_items > 0 || rep.bad_values > 0)
					{
						val_msg_kind = 3; val_msg_timer = 4.0f;
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEB\xA5\xBC \xEB\xB6\x88\xEB\x9F\xAC\xEC\x99\x94\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\x8B\xA4\xEB\xA7\x8C Sherbet \xEC\x9D\xB4 \xEB\xAA\xA8\xEB\xA5\xB4\xEB\x8A\x94 \xED\x95\xAD\xEB\xAA\xA9\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x96\xB4 \xEA\xB7\xB8 \xEB\xB6\x80\xEB\xB6\x84\xEC\x9D\x80 \xEA\xB8\xB0\xEB\xB3\xB8\xEA\xB0\x92\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4" /* 코드를 불러왔어요 — 다만 Sherbet 이 모르는 항목이 있어 그 부분은 기본값입니다 */;
					}
					else
					{
						val_msg_kind = 1; val_msg_timer = 4.0f;
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEB\xA5\xBC \xEB\xB6\x88\xEB\x9F\xAC\xEC\x99\x94\xEC\x96\xB4\xEC\x9A\x94" /* 코드를 불러왔어요 */;
					}
				}
				else
				{
					// ⚠️ 실패해도 지금 조준점은 **한 글자도 안 바뀐다.** 이유를 그대로 보여준다.
					val_msg_kind = 2; val_msg_timer = 4.0f;
					switch (rep.error)
					{
					case xhr::parse_error::ok:
						break;
					case xhr::parse_error::empty:
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEB\xB9\x84\xEC\x96\xB4 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94" /* 코드가 비어 있어요 */;
						break;
					case xhr::parse_error::too_long:
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEB\x84\x88\xEB\xAC\xB4 \xEA\xB8\xB8\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEB\xA7\x9E\xEB\x8A\x94\xEC\xA7\x80 \xED\x99\x95\xEC\x9D\xB8\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94" /* 코드가 너무 길어요 — 조준점 코드가 맞는지 확인해 주세요 */;
						break;
					case xhr::parse_error::illegal_char:
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90 \xEC\x93\xB8 \xEC\x88\x98 \xEC\x97\x86\xEB\x8A\x94 \xEB\xAC\xB8\xEC\x9E\x90\xEA\xB0\x80 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB3\xB5\xEB\xB0\xB1\xEC\x9D\xB4\xEB\x82\x98 \xED\x8A\xB9\xEC\x88\x98\xEB\xAC\xB8\xEC\x9E\x90\xEA\xB0\x80 \xEC\x84\x9E\xEC\x9D\xB4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x98\xEB\x8A\x94\xEC\xA7\x80 \xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94" /* 코드에 쓸 수 없는 문자가 있어요 — 공백이나 특수문자가 섞이지 않았는지 보세요 */;
						break;
					case xhr::parse_error::bad_prefix:
						val_msg = "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEC\x95\x84\xEB\x8B\x88\xEC\x97\x90\xEC\x9A\x94 \xE2\x80\x94 \xEC\xBD\x94\xEB\x93\x9C\xEB\x8A\x94 \xED\x95\xAD\xEC\x83\x81 0 \xEC\x9C\xBC\xEB\xA1\x9C \xEC\x8B\x9C\xEC\x9E\x91\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 발로란트 조준점 코드가 아니에요 — 코드는 항상 0 으로 시작합니다 */;
						break;
					case xhr::parse_error::empty_token:
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C \xEC\xA4\x91\xEA\xB0\x84\xEC\x9D\xB4 \xEB\xB9\x84\xEC\x96\xB4 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x84\xB8\xEB\xAF\xB8\xEC\xBD\x9C\xEB\xA1\xA0\xEC\x9D\xB4 \xEB\x91\x90 \xEB\xB2\x88 \xEB\xB6\x99\xEC\x97\x88\xEA\xB1\xB0\xEB\x82\x98 \xEB\xA7\xA8 \xEB\x81\x9D\xEC\x97\x90 \xEB\x82\xA8\xEC\x95\x84 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94" /* 코드 중간이 비어 있어요 — 세미콜론이 두 번 붙었거나 맨 끝에 남아 있어요 */;
						break;
					case xhr::parse_error::dangling_key:
						val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEC\x9E\x98\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\x81\x9D\xEC\x9D\xB4 \xEC\x88\xAB\xEC\x9E\x90\xEB\xA1\x9C \xEB\x81\x9D\xEB\x82\xA0 \xEB\x95\x8C\xEA\xB9\x8C\xEC\xA7\x80 \xEC\xA7\x80\xEC\x9A\xB0\xEA\xB3\xA0 \xEB\x8B\xA4\xEC\x8B\x9C \xEB\xB6\x99\xEC\x97\xAC\xEB\x84\xA3\xEC\x9C\xBC\xEC\x84\xB8\xEC\x9A\x94" /* 코드가 잘렸어요 — 끝이 숫자로 끝날 때까지 지우고 다시 붙여넣으세요 */;
						break;
					}
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_FLOPPY "  " "\xEC\xBD\x94\xEB\x93\x9C \xEB\xB3\xB5\xEC\x82\xAC" /* 코드 복사 */))
			{
				ImGui::SetClipboardText(_sherbet_val_code.c_str());
				val_msg_kind = 1; val_msg_timer = 4.0f;
				val_msg = "\xEC\xBD\x94\xEB\x93\x9C\xEB\xA5\xBC \xED\x81\xB4\xEB\xA6\xBD\xEB\xB3\xB4\xEB\x93\x9C\xEC\x97\x90 \xEB\xB3\xB5\xEC\x82\xAC\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94" /* 코드를 클립보드에 복사했어요 */;
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_UNDO "  " "\xEA\xB8\xB0\xEB\xB3\xB8\xEA\xB0\x92" /* 기본값 */))
			{
				_sherbet_val_profile = xhr::profile();
				val_changed = true;
				val_msg_kind = 1; val_msg_timer = 4.0f;
				val_msg = "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEA\xB8\xB0\xEB\xB3\xB8 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94" /* 발로란트 기본 조준점으로 되돌렸어요 */;
			}
			// 코드를 직접 구할 필요 없이 진열대에서 고르는 길. 여기서 안내하지 않으면
			// 마켓 안에 조준점이 있다는 것을 아무도 모른다(마켓 = 테마라는 인식이 이미 있다).
			ImGui::SameLine();
			if (ImGui::Button(ICON_FK_SHOPPING_CART "  " "\xEB\xA7\x88\xEC\xBC\x93\xEC\x97\x90\xEC\x84\x9C \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEA\xB3\xA0\xEB\xA5\xB4\xEA\xB8\xB0" /* 마켓에서 조준점 고르기 */))
			{
				_sherbet_tab = 1;          // 「마켓」 탭
				_sherbet_market_seg = 2;   // 조준점 세그먼트
			}
			ImGui::TextDisabled("%s", "'\xEB\xA7\x88\xEC\xBC\x93' \xED\x83\xAD > \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\x97\x90\xEC\x84\x9C \xEA\xB3\xA8\xEB\x9D\xBC \xEC\x93\xB0\xEA\xB1\xB0\xEB\x82\x98, \xEC\xA7\x80\xEA\xB8\x88 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x84 \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xB4 \xEB\x91\x98 \xEC\x88\x98 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94" /* 「마켓」 탭 > 조준점 에서 골라 쓰거나, 지금 조준점을 저장해 둘 수 있어요 */);

			val_msg_timer -= _imgui_context->IO.DeltaTime;
			if (val_msg_timer <= 0.0f)
				{ val_msg_kind = 0; val_msg.clear(); }
			if (val_msg_kind != 0 && !val_msg.empty())
			{
				ImVec4 c = (val_msg_kind == 2) ? sherbet::status_color(sherbet::status::bad)
				         : (val_msg_kind == 3) ? sherbet::status_color(sherbet::status::warn)
				                               : sherbet::status_color(sherbet::status::good);
				c.w = ImMin(1.0f, val_msg_timer); // 마지막 1초 페이드
				const char *const icon = (val_msg_kind == 2) ? ICON_FK_CANCEL : (val_msg_kind == 3) ? ICON_FK_WARNING : ICON_FK_OK;
				ImGui::PushStyleColor(ImGuiCol_Text, c);
				ImGui::TextWrapped("%s  %s", icon, val_msg.c_str());
				ImGui::PopStyleColor();
			}
		}
		sherbet::end_card();
		ImGui::Spacing();

		// ── 미리보기 ─────────────────────────────────────────────────────
		// 오버레이가 열려 있는 동안은 진짜 조준점이 이 창 뒤에 가려진다. 조준점은
		// 눈으로 보면서 맞추는 물건이므로 같은 build_crosshair() 로 여기에도 그린다.
		// 배경을 반은 밝게 반은 어둡게 둔다 — 검은 윤곽선이 실제로 어떻게 보이는지는
		// 두 배경에서 완전히 다르고, 그게 윤곽선을 켜는 이유 그 자체다.
		{
			const float pv_w = ImGui::GetContentRegionAvail().x;
			const float pv_h = 150.0f;
			const ImVec2 pv0 = ImGui::GetCursorScreenPos();
			const ImVec2 pv1(pv0.x + pv_w, pv0.y + pv_h);
			ImGui::InvisibleButton("##val_preview", ImVec2(pv_w, pv_h));

			ImDrawList *const pdl = ImGui::GetWindowDrawList();
			const float pv_mid = pv0.x + pv_w * 0.5f;
			pdl->AddRectFilled(pv0, ImVec2(pv_mid, pv1.y), IM_COL32(206, 209, 214, 255));
			pdl->AddRectFilled(ImVec2(pv_mid, pv0.y), pv1, IM_COL32(24, 25, 30, 255));
			pdl->PushClipRect(pv0, pv1, true);

			static std::vector<sherbet::crosshair::quad> pv_quads;
			xhr::build_crosshair(VL,
				static_cast<int>(std::floor(pv0.x + pv_w * 0.5f)),
				static_cast<int>(std::floor(pv0.y + pv_h * 0.5f)), 0.0f, 0.0f, pv_quads);
			for (const xhr::quad &q : pv_quads)
				pdl->AddRectFilled(
					ImVec2(static_cast<float>(q.r.x), static_cast<float>(q.r.y)),
					ImVec2(static_cast<float>(q.r.x + q.r.w), static_cast<float>(q.r.y + q.r.h)),
					IM_COL32(q.color.r, q.color.g, q.color.b, q.alpha), 0.0f);

			pdl->PopClipRect();
			ImGui::TextDisabled("%s", "\xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0 (\xEC\x8B\xA4\xEC\xA0\x9C \xED\x81\xAC\xEA\xB8\xB0) \xE2\x80\x94 \xEC\x99\xBC\xEC\xAA\xBD\xEC\x9D\x80 \xEB\xB0\x9D\xEC\x9D\x80 \xEB\xB0\xB0\xEA\xB2\xBD, \xEC\x98\xA4\xEB\xA5\xB8\xEC\xAA\xBD\xEC\x9D\x80 \xEC\x96\xB4\xEB\x91\x90\xEC\x9A\xB4 \xEB\xB0\xB0\xEA\xB2\xBD" /* 미리보기 (실제 크기) — 왼쪽은 밝은 배경, 오른쪽은 어두운 배경 */);
		}
		ImGui::Spacing();

		if (xhr::exceeds_ui_range(VL))
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  " "\xEC\x9D\xB4 \xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90\xEB\x8A\x94 \xEC\x8A\xAC\xEB\x9D\xBC\xEC\x9D\xB4\xEB\x8D\x94 \xEB\xB2\x94\xEC\x9C\x84\xEB\xA5\xBC \xEB\x84\x98\xEB\x8A\x94 \xEA\xB0\x92\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEA\xB7\xB8\xEB\xA0\xA4\xEC\xA7\x80\xEC\xA7\x80\xEB\xA7\x8C, \xED\x95\xB4\xEB\x8B\xB9 \xEC\x8A\xAC\xEB\x9D\xBC\xEC\x9D\xB4\xEB\x8D\x94\xEB\xA5\xBC \xEA\xB1\xB4\xEB\x93\x9C\xEB\xA6\xAC\xEB\xA9\xB4 \xEB\xB2\x94\xEC\x9C\x84 \xEC\x95\x88\xEC\x9C\xBC\xEB\xA1\x9C \xEC\x9E\x98\xEB\xA6\xBD\xEB\x8B\x88\xEB\x8B\xA4" /* 이 코드에는 슬라이더 범위를 넘는 값이 있어요 — 그대로 그려지지만, 해당 슬라이더를 건드리면 범위 안으로 잘립니다 */);

		// ── 조준점 (색 · 윤곽선 · 중앙 점 · 오차 토글) ────────────────────
		if (ImGui::TreeNodeEx("##val_general", ImGuiTreeNodeFlags_DefaultOpen, "%s", "\xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90" /* 조준점 */))
		{
			static const char *const kColorNames[9] = {
				"\xED\x9D\xB0\xEC\x83\x89" /* 흰색 */, "\xEC\xB4\x88\xEB\xA1\x9D" /* 초록 */, "\xEC\x97\xB0\xEB\x91\x90" /* 연두 */, "\xEB\x85\xB8\xEB\x9E\x91\xEC\x97\xB0\xEB\x91\x90" /* 노랑연두 */, "\xEB\x85\xB8\xEB\x9E\x91" /* 노랑 */,
				"\xEC\x8B\x9C\xEC\x95\x88" /* 시안 */, "\xEB\xB6\x84\xED\x99\x8D" /* 분홍 */, "\xEB\xB9\xA8\xEA\xB0\x95" /* 빨강 */, "\xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80" /* 커스텀 */ };

			const int ci_show = (VL.color_index >= 0 && VL.color_index <= 8) ? VL.color_index : 8;
			if (ImGui::BeginCombo("\xEC\x83\x89" /* 색 */, kColorNames[ci_show]))
			{
				for (int i = 0; i < 9; ++i)
				{
					ImGui::PushID(i);
					const xhr::rgb sw = (i == 8)
						? xhr::rgb { VL.custom_color.r, VL.custom_color.g, VL.custom_color.b }
						: xhr::preset_color(i);
					ImGui::ColorButton("##sw",
						ImVec4(sw.r / 255.0f, sw.g / 255.0f, sw.b / 255.0f, 1.0f),
						ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(16, 16));
					ImGui::SameLine();
					if (ImGui::Selectable(kColorNames[i], VL.color_index == i))
					{
						// §2.8 커스텀은 c;8 + b;1 이 한 세트다. 프리셋을 고르면 둘 다 되돌린다.
						VL.color_index = i;
						VL.use_custom_color = (i == 8);
						val_changed = true;
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
			if (xhr::uses_custom_color(VL))
			{
				float cc[3] = { VL.custom_color.r / 255.0f, VL.custom_color.g / 255.0f, VL.custom_color.b / 255.0f };
				if (ImGui::ColorEdit3("\xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEC\x83\x89" /* 커스텀 색 */, cc))
				{
					VL.custom_color.r = xhr::byte_from_unit(cc[0]);
					VL.custom_color.g = xhr::byte_from_unit(cc[1]);
					VL.custom_color.b = xhr::byte_from_unit(cc[2]);
					val_changed = true;
				}
			}

			val_changed |= ImGui::Checkbox("\xEC\x9C\xA4\xEA\xB3\xBD\xEC\x84\xA0" /* 윤곽선 */, &VL.has_outline);
			if (VL.has_outline)
			{
				val_changed |= ImGui::SliderInt("\xEC\x9C\xA4\xEA\xB3\xBD\xEC\x84\xA0 \xEB\x91\x90\xEA\xBB\x98" /* 윤곽선 두께 */, &VL.outline_thickness, xhr::kUiOutlineThickness.lo, xhr::kUiOutlineThickness.hi);
				val_changed |= ImGui::SliderFloat("\xEC\x9C\xA4\xEA\xB3\xBD\xEC\x84\xA0 \xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84" /* 윤곽선 투명도 */, &VL.outline_opacity, 0.0f, 1.0f, "%.2f");
				ImGui::TextDisabled("%s", "\xEC\x9C\xA4\xEA\xB3\xBD\xEC\x84\xA0 \xEC\x83\x89\xEC\x9D\x80 \xED\x95\xAD\xEC\x83\x81 \xEA\xB2\x80\xEC\xA0\x95\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4 \xE2\x80\x94 \xEA\xB3\xB5\xEC\x9C\xA0 \xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90 \xEC\x83\x89 \xED\x95\xAD\xEB\xAA\xA9\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94" /* 윤곽선 색은 항상 검정입니다 — 공유 코드에 색 항목이 없어요 */);
			}

			val_changed |= ImGui::Checkbox("\xEC\xA4\x91\xEC\x95\x99 \xEC\xA0\x90" /* 중앙 점 */, &VL.show_center_dot);
			if (VL.show_center_dot)
			{
				val_changed |= ImGui::SliderInt("\xEC\xA4\x91\xEC\x95\x99 \xEC\xA0\x90 \xED\x81\xAC\xEA\xB8\xB0" /* 중앙 점 크기 */, &VL.center_dot_size, xhr::kUiCenterDotSize.lo, xhr::kUiCenterDotSize.hi);
				val_changed |= ImGui::SliderFloat("\xEC\xA4\x91\xEC\x95\x99 \xEC\xA0\x90 \xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84" /* 중앙 점 투명도 */, &VL.center_dot_opacity, 0.0f, 1.0f, "%.2f");
				ImGui::TextDisabled("%s", "\xEC\x9B\x90\xEC\x9D\xB4 \xEC\x95\x84\xEB\x8B\x88\xEB\x9D\xBC \xEC\xA0\x95\xEC\x82\xAC\xEA\xB0\x81\xED\x98\x95\xEC\x9D\xB4\xEA\xB3\xA0, \xEA\xB0\x92\xEC\x9D\x80 \xED\x95\x9C \xEB\xB3\x80\xEC\x9D\x98 \xEA\xB8\xB8\xEC\x9D\xB4\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4" /* 원이 아니라 정사각형이고, 값은 한 변의 길이입니다 */);
			}

			val_changed |= ImGui::Checkbox("\xEC\x82\xAC\xEA\xB2\xA9 \xEC\xA4\x91 \xEC\x9C\x84\xEC\xAA\xBD \xED\x8C\x94 \xED\x9D\x90\xEB\xA6\xAC\xEA\xB2\x8C" /* 사격 중 위쪽 팔 흐리게 */, &VL.fade_with_firing_error);
			val_changed |= ImGui::Checkbox("\xED\x9C\xB4\xEC\xA7\x80 \xEC\x83\x81\xED\x83\x9C \xEA\xB0\x84\xEA\xB2\xA9 \xEB\xB3\xB4\xEC\xA0\x95 \xEB\x81\x84\xEA\xB8\xB0" /* 휴지 상태 간격 보정 끄기 */, &VL.fix_min_error);
			ImGui::TextDisabled("%s", "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8\xEB\x8A\x94 \xEA\xB0\x80\xEB\xA7\x8C\xED\x9E\x88 \xEC\x84\x9C \xEC\x9E\x88\xEC\x96\xB4\xEB\x8F\x84 \xEC\x84\xA4\xEC\xA0\x95\xEA\xB0\x92\xEB\xB3\xB4\xEB\x8B\xA4 4px \xEB\x8D\x94 \xEB\xB2\x8C\xEC\x96\xB4\xEC\xA0\xB8 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94. \xEC\x9D\xB4\xEA\xB1\xB8 \xEC\xBC\x9C\xEB\xA9\xB4 \xEC\x84\xA4\xEC\xA0\x95\xEA\xB0\x92 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEB\xB6\x99\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4" /* 발로란트는 가만히 서 있어도 설정값보다 4px 더 벌어져 있어요. 이걸 켜면 설정값 그대로 붙습니다 */);
			ImGui::TreePop();
		}

		// ── 안쪽선 / 바깥선 ───────────────────────────────────────────────
		// inner 와 outer 는 **범위가 다르다**(설계 §1.3). 같게 두면 발로란트 유저가 바로 알아챈다.
		const auto val_line_ui = [&](const char *id, xhr::line &Ln, const xhr::int_range &len_r, const xhr::int_range &off_r)
		{
			ImGui::PushID(id);
			val_changed |= ImGui::Checkbox("\xEC\x84\xA0 \xED\x91\x9C\xEC\x8B\x9C" /* 선 표시 */, &Ln.show_lines);
			if (Ln.show_lines)
			{
				val_changed |= ImGui::SliderInt("\xEB\x91\x90\xEA\xBB\x98" /* 두께 */, &Ln.thickness, xhr::kUiLineThickness.lo, xhr::kUiLineThickness.hi);
				val_changed |= ImGui::SliderInt("\xEA\xB8\xB8\xEC\x9D\xB4" /* 길이 */, &Ln.length, len_r.lo, len_r.hi);
				val_changed |= ImGui::Checkbox("\xEC\x84\xB8\xEB\xA1\x9C \xEA\xB8\xB8\xEC\x9D\xB4 \xEB\x94\xB0\xEB\xA1\x9C \xEC\xA7\x80\xEC\xA0\x95" /* 세로 길이 따로 지정 */, &Ln.allow_vert_scaling);
				if (Ln.allow_vert_scaling)
					val_changed |= ImGui::SliderInt("\xEC\x84\xB8\xEB\xA1\x9C \xEA\xB8\xB8\xEC\x9D\xB4" /* 세로 길이 */, &Ln.length_vertical, xhr::kUiLineLengthVertical.lo, xhr::kUiLineLengthVertical.hi);
				val_changed |= ImGui::SliderInt("\xEC\xA4\x91\xEC\x95\x99 \xEA\xB0\x84\xEA\xB2\xA9" /* 중앙 간격 */, &Ln.offset, off_r.lo, off_r.hi);
				val_changed |= ImGui::SliderFloat("\xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84" /* 투명도 */, &Ln.opacity, 0.0f, 1.0f, "%.2f");
				val_changed |= ImGui::Checkbox("\xEC\x9D\xB4\xEB\x8F\x99 \xEC\x98\xA4\xEC\xB0\xA8" /* 이동 오차 */, &Ln.show_movement_error);
				if (Ln.show_movement_error)
					val_changed |= ImGui::SliderFloat("\xEC\x9D\xB4\xEB\x8F\x99 \xEC\x98\xA4\xEC\xB0\xA8 \xEB\xB0\xB0\xEC\x9C\xA8" /* 이동 오차 배율 */, &Ln.movement_error_scale, xhr::kUiErrorScale.lo, xhr::kUiErrorScale.hi, "%.2f");
				val_changed |= ImGui::Checkbox("\xEB\xB0\x9C\xEC\x82\xAC \xEC\x98\xA4\xEC\xB0\xA8" /* 발사 오차 */, &Ln.show_shooting_error);
				if (Ln.show_shooting_error)
					val_changed |= ImGui::SliderFloat("\xEB\xB0\x9C\xEC\x82\xAC \xEC\x98\xA4\xEC\xB0\xA8 \xEB\xB0\xB0\xEC\x9C\xA8" /* 발사 오차 배율 */, &Ln.firing_error_scale, xhr::kUiErrorScale.lo, xhr::kUiErrorScale.hi, "%.2f");
			}
			ImGui::PopID();
		};

		if (ImGui::TreeNodeEx("##val_inner", ImGuiTreeNodeFlags_DefaultOpen, "%s", "\xEC\x95\x88\xEC\xAA\xBD\xEC\x84\xA0" /* 안쪽선 */))
		{
			val_line_ui("inner", VL.inner, xhr::kUiInnerLineLength, xhr::kUiInnerLineOffset);
			ImGui::TreePop();
		}
		if (ImGui::TreeNodeEx("##val_outer", ImGuiTreeNodeFlags_DefaultOpen, "%s", "\xEB\xB0\x94\xEA\xB9\xA5\xEC\x84\xA0" /* 바깥선 */))
		{
			val_line_ui("outer", VL.outer, xhr::kUiOuterLineLength, xhr::kUiOuterLineOffset);
			ImGui::TreePop();
		}

		// ── 오차 애니메이션 (§4) ───────────────────────────────────────────
		if (ImGui::TreeNodeEx("##val_error", ImGuiTreeNodeFlags_DefaultOpen, "%s", "\xEC\x98\xA4\xEC\xB0\xA8 \xEC\x95\xA0\xEB\x8B\x88\xEB\xA9\x94\xEC\x9D\xB4\xEC\x85\x98 (\xEC\x9B\x80\xEC\xA7\x81\xEC\x9E\x84/\xEC\x82\xAC\xEA\xB2\xA9)" /* 오차 애니메이션 (움직임/사격) */))
		{
			if (ImGui::Checkbox("\xEC\x98\xA4\xEC\xB0\xA8 \xEC\x95\xA0\xEB\x8B\x88\xEB\xA9\x94\xEC\x9D\xB4\xEC\x85\x98 \xEC\xBC\x9C\xEA\xB8\xB0" /* 오차 애니메이션 켜기 */, &_sherbet_val_err_on))
			{
				_sherbet_val_err = xhr::error_state(); // 껐다 켜면 옛 오차가 남지 않게
				modified = true;
			}
			ImGui::TextDisabled("%s", "\xEC\x96\xB4\xEB\x8A\x90 \xEC\x84\xA0\xEC\x97\x90 \xEC\xA0\x81\xEC\x9A\xA9\xED\x95\xA0\xEC\xA7\x80\xEB\x8A\x94 \xEC\x9C\x84\xEC\x9D\x98 \xEC\x95\x88\xEC\xAA\xBD\xEC\x84\xA0/\xEB\xB0\x94\xEA\xB9\xA5\xEC\x84\xA0 \xED\x95\xAD\xEB\xAA\xA9\xEC\x97\x90\xEC\x84\x9C \xEB\x94\xB0\xEB\xA1\x9C \xEC\xBC\xAD\xEB\x8B\x88\xEB\x8B\xA4" /* 어느 선에 적용할지는 위의 안쪽선/바깥선 항목에서 따로 켭니다 */);

			if (_sherbet_val_err_on)
			{
				// ⚠️ 감추지 않는다(설계 §5). 이동 오차는 *캐릭터 속도*의 함수인데 우리가 가진 건
				//    *키 입력*이라 물리량 자체가 다르다. 이걸 숨기면 "왜 안 벌어지죠?" 문의가 온다.
				ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  " "\xEC\x9D\xB4\xEB\x8F\x99 \xEC\x98\xA4\xEC\xB0\xA8\xEB\x8A\x94 \xED\x82\xA4 \xEC\x9E\x85\xEB\xA0\xA5\xEC\x9C\xBC\xEB\xA1\x9C \xED\x9D\x89\xEB\x82\xB4 \xEB\x82\xB8 \xEA\xB0\x92\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94" /* 이동 오차는 키 입력으로 흉내 낸 값이에요 */);
				ImGui::TextWrapped("%s", "\xEA\xB2\x8C\xEC\x9E\x84 \xEB\xA9\x94\xEB\xAA\xA8\xEB\xA6\xAC\xEB\xA5\xBC \xEC\x9D\xBD\xEC\xA7\x80 \xEC\x95\x8A\xEA\xB8\xB0 \xEB\x95\x8C\xEB\xAC\xB8\xEC\x97\x90 \xEC\x8B\xA4\xEC\xA0\x9C \xEC\xBA\x90\xEB\xA6\xAD\xED\x84\xB0 \xEC\x86\x8D\xEB\x8F\x84\xEB\xA5\xBC \xEC\x95\x8C \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEB\x84\x89\xEB\xB0\xB1\xC2\xB7\xEC\x8A\xAC\xEB\xA1\x9C\xEC\x9A\xB0\xC2\xB7\xEA\xB2\xBD\xEC\x82\xAC\xC2\xB7\xEC\xB0\xA8\xEB\x9F\x89\xC2\xB7\xEB\xAC\xBC\xEC\x86\x8D\xC2\xB7\xEC\x95\x89\xEA\xB8\xB0\xEB\x8A\x94 \xEB\xB0\x98\xEC\x98\x81\xEB\x90\x98\xEC\xA7\x80 \xEC\x95\x8A\xEA\xB3\xA0, \xED\x82\xA4\xEB\xA5\xBC \xEC\x95\x88 \xEB\x88\x8C\xEB\x9F\xAC\xEB\x8F\x84 \xEB\xB0\x80\xEB\xA0\xA4\xEB\x82\x98\xEB\x8A\x94 \xEC\x83\x81\xED\x99\xA9\xEC\x97\x90\xEC\x84\x9C\xEB\x8A\x94 \xEC\x98\xA4\xEC\xB0\xA8\xEA\xB0\x80 0 \xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB3\xB4\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4. \xEC\x82\xAC\xEA\xB2\xA9 \xEC\x98\xA4\xEC\xB0\xA8\xEB\x8A\x94 \xED\x81\xB4\xEB\xA6\xAD\xEC\x9D\x84 \xEC\xA0\x95\xED\x99\x95\xED\x9E\x88 \xEA\xB0\x90\xEC\xA7\x80\xED\x95\x98\xEB\xAF\x80\xEB\xA1\x9C \xED\x9B\xA8\xEC\x94\xAC \xEC\x9E\x98 \xEB\xA7\x9E\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4." /* 게임 메모리를 읽지 않기 때문에 실제 캐릭터 속도를 알 수 없어요. 넉백·슬로우·경사·차량·물속·앉기는 반영되지 않고, 키를 안 눌러도 밀려나는 상황에서는 오차가 0 으로 보입니다. 사격 오차는 클릭을 정확히 감지하므로 훨씬 잘 맞습니다. */);
				ImGui::Spacing();

				ImGui::SeparatorText("\xEC\x82\xAC\xEA\xB2\xA9" /* 사격 */);
				modified |= ImGui::SliderFloat("1\xEB\xB0\x9C\xEB\x8B\xB9 \xED\x99\x95\xEC\x9E\xA5(px)" /* 1발당 확장(px) */, &_sherbet_val_tune.fire_per_shot_px, 0.0f, 10.0f, "%.2f");
				modified |= ImGui::SliderFloat("\xEC\xB5\x9C\xEB\x8C\x80 \xED\x99\x95\xEC\x9E\xA5(px)" /* 최대 확장(px) */, &_sherbet_val_tune.fire_max_px, 0.0f, 60.0f, "%.1f");
				modified |= ImGui::SliderFloat("\xEA\xB0\x80\xEC\xA0\x95 \xEC\x97\xB0\xEC\x82\xAC \xEC\x86\x8D\xEB\x8F\x84(rpm)" /* 가정 연사 속도(rpm) */, &_sherbet_val_tune.fire_rate_rpm, 60.0f, 1200.0f, "%.0f");
				ImGui::TextDisabled("%s", "\xEB\x88\x84\xEB\xA5\xB4\xEA\xB3\xA0 \xEC\x9E\x88\xEC\x9D\x84 \xEB\x95\x8C \xEC\x9D\xB4 \xEC\x86\x8D\xEB\x8F\x84\xEB\xA1\x9C \xEC\x8F\x9C\xEB\x8B\xA4\xEA\xB3\xA0 \xEA\xB0\x80\xEC\xA0\x95\xED\x95\xA9\xEB\x8B\x88\xEB\x8B\xA4 \xE2\x80\x94 3\xEC\xA0\x90\xEC\x82\xAC\xEB\x82\x98 \xEB\xB0\x98\xEC\x9E\x90\xEB\x8F\x99 \xEC\x97\xB0\xED\x83\x80\xEB\x8A\x94 \xEC\x96\xB4\xEA\xB8\x8B\xEB\x82\x98\xEC\x9A\x94" /* 누르고 있을 때 이 속도로 쏜다고 가정합니다 — 3점사나 반자동 연타는 어긋나요 */);
				modified |= ImGui::SliderFloat("\xED\x9A\x8C\xEB\xB3\xB5 \xEC\x8B\x9C\xEA\xB0\x84(\xEC\xB4\x88)" /* 회복 시간(초) */, &_sherbet_val_tune.recovery_time, 0.05f, 2.0f, "%.3f");
				ImGui::TextDisabled("%s", "\xEB\xA7\x88\xEC\xA7\x80\xEB\xA7\x89 \xEB\xB0\x9C \xEC\x9D\xB4\xED\x9B\x84 \xED\x95\x9C \xEB\xB0\x9C \xEA\xB0\x84\xEA\xB2\xA9\xEC\x9D\xB4 \xEC\xA7\x80\xEB\x82\x98\xEB\xA9\xB4 \xEC\x9D\xB4 \xEC\x8B\x9C\xEA\xB0\x84\xEC\x97\x90 \xEA\xB1\xB8\xEC\xB3\x90 \xEC\x9B\x90\xEB\x9E\x98\xEB\x8C\x80\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4" /* 마지막 발 이후 한 발 간격이 지나면 이 시간에 걸쳐 원래대로 돌아옵니다 */);
				modified |= ImGui::SliderFloat("\xEC\x9C\x84\xEC\xAA\xBD \xED\x8C\x94 \xED\x9D\x90\xEB\xA0\xA4\xEC\xA7\x80\xEB\x8A\x94 \xEC\xA0\x95\xEB\x8F\x84" /* 위쪽 팔 흐려지는 정도 */, &_sherbet_val_tune.fade_depth, 0.0f, 1.0f, "%.2f");

				ImGui::SeparatorText("\xEC\x9D\xB4\xEB\x8F\x99" /* 이동 */);
				modified |= ImGui::SliderFloat("\xEA\xB1\xB7\xEA\xB8\xB0 \xEC\xB5\x9C\xEB\x8C\x80 \xED\x99\x95\xEC\x9E\xA5(px)" /* 걷기 최대 확장(px) */, &_sherbet_val_tune.walk_err_px, 0.0f, 40.0f, "%.1f");
				modified |= ImGui::SliderFloat("\xEB\x8B\xAC\xEB\xA6\xAC\xEA\xB8\xB0 \xEC\xB5\x9C\xEB\x8C\x80 \xED\x99\x95\xEC\x9E\xA5(px)" /* 달리기 최대 확장(px) */, &_sherbet_val_tune.run_err_px, 0.0f, 60.0f, "%.1f");
				modified |= ImGui::SliderFloat("\xEB\xAC\xB4\xEC\x8B\x9C \xEA\xB5\xAC\xEA\xB0\x84(deadzone)" /* 무시 구간(deadzone) */, &_sherbet_val_tune.deadzone, 0.0f, 0.9f, "%.3f");
				ImGui::TextDisabled("%s", "\xEB\x8B\xAC\xEB\xA6\xAC\xEA\xB8\xB0 \xEC\x86\x8D\xEB\x8F\x84\xEC\x9D\x98 27.5% \xEB\xAF\xB8\xEB\xA7\x8C\xEC\x9D\xB4\xEB\xA9\xB4 \xEC\x98\xA4\xEC\xB0\xA8 0 \xE2\x80\x94 \xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEA\xB3\xB5\xEC\x8B\x9D \xED\x8C\xA8\xEC\xB9\x98\xEB\x85\xB8\xED\x8A\xB8 \xEA\xB0\x92\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4" /* 달리기 속도의 27.5% 미만이면 오차 0 — 발로란트 공식 패치노트 값입니다 */);
				modified |= ImGui::SliderFloat("\xEA\xB0\x80\xEC\x86\x8D(1/\xEC\xB4\x88)" /* 가속(1/초) */, &_sherbet_val_tune.move_accel, 1.0f, 60.0f, "%.1f");
				modified |= ImGui::SliderFloat("\xEA\xB0\x90\xEC\x86\x8D(1/\xEC\xB4\x88)" /* 감속(1/초) */, &_sherbet_val_tune.move_decel, 1.0f, 60.0f, "%.1f");
				modified |= ImGui::SliderFloat("\xEA\xB1\xB7\xEA\xB8\xB0 \xEC\x86\x8D\xEB\x8F\x84 \xEB\xB9\x84\xEC\x9C\xA8" /* 걷기 속도 비율 */, &_sherbet_val_tune.walk_speed, 0.0f, 1.0f, "%.2f");

				ImGui::SeparatorText("\xED\x82\xA4 \xEC\x84\xA4\xEC\xA0\x95" /* 키 설정 */);
				modified |= imgui::key_input_box("\xEC\x95\x9E\xEC\x9C\xBC\xEB\xA1\x9C" /* 앞으로 */, _sherbet_val_key_fwd, *_input);
				modified |= imgui::key_input_box("\xEB\x92\xA4\xEB\xA1\x9C" /* 뒤로 */, _sherbet_val_key_back, *_input);
				modified |= imgui::key_input_box("\xEC\x99\xBC\xEC\xAA\xBD" /* 왼쪽 */, _sherbet_val_key_left, *_input);
				modified |= imgui::key_input_box("\xEC\x98\xA4\xEB\xA5\xB8\xEC\xAA\xBD" /* 오른쪽 */, _sherbet_val_key_right, *_input);
				modified |= imgui::key_input_box("\xEA\xB1\xB7\xEA\xB8\xB0/\xEB\x8B\xAC\xEB\xA6\xAC\xEA\xB8\xB0 \xEC\x88\x98\xEC\xA0\x95\xED\x82\xA4" /* 걷기/달리기 수정키 */, _sherbet_val_key_walk, *_input);
				modified |= ImGui::Checkbox("\xEC\x88\x98\xEC\xA0\x95\xED\x82\xA4\xEB\xA5\xBC \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEB\x8B\xAC\xEB\xA6\xAC\xEA\xB8\xB0 (GTA/FiveM \xEB\xB0\xA9\xEC\x8B\x9D)" /* 수정키를 누르면 달리기 (GTA/FiveM 방식) */, &_sherbet_val_tune.walk_key_means_run);
				ImGui::TextDisabled("%s", "\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8\xEB\x8A\x94 Shift \xEA\xB0\x80 \xEA\xB1\xB7\xEA\xB8\xB0, GTA \xEA\xB3\x84\xEC\x97\xB4\xEC\x9D\x80 Shift \xEA\xB0\x80 \xEB\x8B\xAC\xEB\xA6\xAC\xEA\xB8\xB0\xEB\x9D\xBC \xEC\x9D\x98\xEB\xAF\xB8\xEA\xB0\x80 \xEB\xB0\x98\xEB\x8C\x80\xEC\x98\x88\xEC\x9A\x94" /* 발로란트는 Shift 가 걷기, GTA 계열은 Shift 가 달리기라 의미가 반대예요 */);

				modified |= imgui::key_input_box("\xEC\x9D\xBC\xEC\x8B\x9C\xEC\xA0\x95\xEC\xA7\x80 \xED\x95\xAB\xED\x82\xA4" /* 일시정지 핫키 */, _sherbet_val_key_pause, *_input);
				ImGui::TextDisabled("%s", "\xEA\xB2\x8C\xEC\x9E\x84 \xEB\x82\xB4 \xEC\xB1\x84\xED\x8C\x85 \xEC\xA4\x91\xEC\x97\x90\xEB\x8A\x94 WASD \xEA\xB0\x80 \xEC\x9D\xB4\xEB\x8F\x99\xEC\x9C\xBC\xEB\xA1\x9C \xEC\x9E\xA1\xED\x98\x80\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB7\xB8\xEB\x95\x8C \xEC\x9D\xB4 \xED\x82\xA4\xEB\xA1\x9C \xEC\x9E\xA0\xEC\x8B\x9C \xEB\xA9\x88\xEC\xB6\x94\xEC\x84\xB8\xEC\x9A\x94" /* 게임 내 채팅 중에는 WASD 가 이동으로 잡혀요 — 그때 이 키로 잠시 멈추세요 */);
				if (_sherbet_val_err_paused)
					ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  " "\xEC\xA7\x80\xEA\xB8\x88 \xEC\x9D\xBC\xEC\x8B\x9C\xEC\xA0\x95\xEC\xA7\x80 \xEC\x83\x81\xED\x83\x9C\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4" /* 지금 일시정지 상태입니다 */);

				ImGui::Spacing();
				ImGui::TextDisabled("\xED\x98\x84\xEC\x9E\xAC \xED\x99\x95\xEC\x9E\xA5: \xEC\x82\xAC\xEA\xB2\xA9 %.1fpx \xC2\xB7 \xEC\x9D\xB4\xEB\x8F\x99 %.1fpx" /* 현재 확장: 사격 %.1fpx · 이동 %.1fpx */,
					static_cast<double>(_sherbet_val_err.fire_px),
					static_cast<double>(xhr::movement_error_px(_sherbet_val_err, _sherbet_val_tune)));
			}
			ImGui::TreePop();
		}

		// ── 고급 · 내보내기 전용 ──────────────────────────────────────────
		if (ImGui::TreeNode("##val_adv", "%s", "\xEA\xB3\xA0\xEA\xB8\x89 (\xEB\x82\xB4\xEB\xB3\xB4\xEB\x82\xB4\xEA\xB8\xB0 \xEC\xA0\x84\xEC\x9A\xA9)" /* 고급 (내보내기 전용) */))
		{
			val_changed |= ImGui::Checkbox("\xEC\xA0\x95\xEC\xA1\xB0\xEC\xA4\x80\xEC\x9D\xB4 \xEC\xA3\xBC \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x84 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEC\x82\xAC\xEC\x9A\xA9" /* 정조준이 주 조준점을 그대로 사용 */, &_sherbet_val_profile.ads_copies_primary);
			val_changed |= ImGui::Checkbox("\xEB\xAA\xA8\xEB\x93\xA0 \xEC\xA3\xBC\xEB\xAC\xB4\xEA\xB8\xB0 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xED\x86\xB5\xEC\x9D\xBC" /* 모든 주무기 조준점 통일 */, &_sherbet_val_profile.override_all_primary);
			val_changed |= ImGui::Checkbox("\xEA\xB3\xA0\xEA\xB8\x89 \xEC\x98\xB5\xEC\x85\x98 \xEC\x82\xAC\xEC\x9A\xA9" /* 고급 옵션 사용 */, &_sherbet_val_profile.advanced_options);
			ImGui::TextDisabled("%s", "\xEC\x9D\xB4 \xEC\x84\xB8 \xED\x95\xAD\xEB\xAA\xA9\xEA\xB3\xBC \xEC\xA0\x95\xEC\xA1\xB0\xEC\xA4\x80\xC2\xB7\xEC\xA0\x80\xEA\xB2\xA9 \xEC\xA1\xB0\xEC\xA4\x80\xEA\xB2\xBD \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\x80 \xED\x99\x94\xEB\xA9\xB4\xEC\x97\x90 \xEA\xB7\xB8\xEB\xA6\xAC\xEC\xA7\x80 \xEC\x95\x8A\xEC\xA7\x80\xEB\xA7\x8C, \xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\x82\xB4\xEB\xB3\xB4\xEB\x82\xBC \xEB\x95\x8C \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEC\x9C\xA0\xEC\xA7\x80\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4" /* 이 세 항목과 정조준·저격 조준경 설정은 화면에 그리지 않지만, 코드로 내보낼 때 그대로 유지됩니다 */);
			ImGui::TreePop();
		}

		// ── 클래식에서 가져오기 ───────────────────────────────────────────
		ImGui::Separator();
		const bool can_migrate = (_sherbet_crosshair_builtin != 0);
		if (!can_migrate)
			ImGui::BeginDisabled();
		if (ImGui::Button(ICON_FK_REFRESH "  " "\xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D \xEC\x84\xA4\xEC\xA0\x95\xEC\x97\x90\xEC\x84\x9C \xEA\xB0\x80\xEC\xA0\xB8\xEC\x98\xA4\xEA\xB8\xB0" /* 클래식 설정에서 가져오기 */))
		{
			xhr::classic_crosshair cc;
			cc.shape = _sherbet_crosshair_builtin;
			cc.size = _sherbet_crosshair_size;
			cc.thickness = _sherbet_crosshair_thick;
			cc.gap = _sherbet_crosshair_gap;
			cc.opacity = _sherbet_crosshair_opacity;
			for (int i = 0; i < 4; ++i)
				cc.color[i] = _sherbet_crosshair_col[i];
			VL = xhr::layer_from_classic(cc);
			val_changed = true;
			val_msg_kind = 3; val_msg_timer = 4.0f;
			val_msg = "\xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\x84 \xEA\xB0\x80\xEC\xA0\xB8\xEC\x99\x94\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x98\xA4\xEC\xB0\xA8 \xEC\x95\xA0\xEB\x8B\x88\xEB\xA9\x94\xEC\x9D\xB4\xEC\x85\x98\xEC\x9D\x80 \xEA\xBA\xBC\xEC\xA7\x84 \xEC\xB1\x84\xEB\xA1\x9C \xEB\x93\xA4\xEC\x96\xB4\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4(\xEC\x9B\x90\xEB\x9E\x98 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\xB4 \xEC\x9B\x80\xEC\xA7\x81\xEC\x9D\xB4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x98\xEC\x9C\xBC\xEB\xAF\x80\xEB\xA1\x9C). \xED\x95\x84\xEC\x9A\x94\xED\x95\x98\xEB\xA9\xB4 \xEC\x95\x84\xEB\x9E\x98\xEC\x97\x90\xEC\x84\x9C \xEC\xBC\x9C\xEC\x84\xB8\xEC\x9A\x94" /* 클래식 설정을 가져왔어요 — 오차 애니메이션은 꺼진 채로 들어옵니다(원래 조준점이 움직이지 않았으므로). 필요하면 아래에서 켜세요 */;
		}
		if (!can_migrate)
			ImGui::EndDisabled();
		if (!can_migrate)
			ImGui::TextDisabled("%s", "\xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x80 \xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEC\x82\xAC\xEC\x96\x91\xEC\x97\x90 \xEB\x8C\x80\xEC\x9D\x91\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEC\x93\xB0\xEC\x8B\x9C\xEB\xA0\xA4\xEB\xA9\xB4 \xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D \xEB\xAA\xA8\xEB\x93\x9C\xEB\xA1\x9C \xEB\x91\x90\xEC\x84\xB8\xEC\x9A\x94" /* 이미지 조준점은 발로란트 사양에 대응이 없어요 — 그대로 쓰시려면 클래식 모드로 두세요 */);
		else
			ImGui::TextDisabled("%s", "\xED\x99\x94\xEB\xA9\xB4 \xEC\x9C\x84\xEC\xB9\x98(X/Y) \xEC\x98\xA4\xED\x94\x84\xEC\x85\x8B\xEC\x9D\x80 \xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8\xEC\x97\x90 \xEB\x8C\x80\xEC\x9D\x91\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4 \xEA\xB0\x80\xEC\xA0\xB8\xEC\x98\xA4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4" /* 화면 위치(X/Y) 오프셋은 발로란트에 대응이 없어 가져오지 않습니다 */);

		// 슬라이더든 가져오기든 프로필이 바뀌면 공유 코드를 다시 만든다.
		// **문자열을 직접 편집하지 않는다** — generate_code() 를 거쳐야 미지 키와
		// 정조준·저격 섹션이 그대로 보존된다(설계 §2.7 규칙 8).
		if (val_changed)
		{
			_sherbet_val_code = xhr::generate_code(_sherbet_val_profile);
			val_code_fresh = false; // 입력 상자를 "Sherbet 이 이해한 코드"로 갱신
			modified = true;
			// 사용자가 직접 만진 코드는 더 이상 마켓 항목이 아니다. 이걸 알려 주지 않으면
			// 다음에 마켓 카드를 눌렀을 때 되돌리기 스냅샷이 갱신되지 않아 **방금 맞춘 값이
			// 사라진다**(설계: sherbet_xhmarket.hpp 의 session).
			sherbet::xhmarket::note_user_edit(_sherbet_xh_session);
		}
		ImGui::Spacing();
	}

	// ── 커스텀 조준점 (설정 탭에서 이동) ─────────────────────────────────────
	if (ImGui::CollapsingHeader("\xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 (\xEC\xA0\x90/\xEC\x8B\xAD\xEC\x9E\x90/\xEC\x9B\x90/\xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80)")) // "클래식 조준점 (점/십자/원/이미지)"
	{
		bool xh_changed = false;
		// 켜고 끄기는 위의 「조준점」 모드 선택이 담당한다 — 여기 체크박스가 또 있으면
		// 둘 중 무엇이 이기는지 알 수 없어진다.
		if (!_sherbet_crosshair_on)
			ImGui::TextDisabled("%s", "\xEC\xA7\x80\xEA\xB8\x88\xEC\x9D\x80 \xEC\x93\xB0\xEC\x9D\xB4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x84\xEC\x9A\x94 \xE2\x80\x94 \xEC\x9C\x84\xEC\x97\x90\xEC\x84\x9C '\xED\x81\xB4\xEB\x9E\x98\xEC\x8B\x9D'\xEC\x9D\x84 \xEA\xB3\xA0\xEB\xA5\xB4\xEB\xA9\xB4 \xEC\xA0\x81\xEC\x9A\xA9\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4"); // "지금은 쓰이지 않아요 — 위에서 「클래식」을 고르면 적용됩니다"

		const char *const xh_shapes[] = {
			"\xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80", // "커스텀 이미지"
			"\xEC\xA0\x90", "\xEC\x8B\xAD\xEC\x9E\x90", "\xEC\x9B\x90", "\xEC\x8B\xAD\xEC\x9E\x90+\xEC\xA0\x90" }; // 점/십자/원/십자+점
		const int prev_builtin = _sherbet_crosshair_builtin;
		if (ImGui::BeginCombo("\xEB\xAA\xA8\xEC\x96\x91", xh_shapes[ImClamp(_sherbet_crosshair_builtin, 0, 4)])) // "모양"
		{
			for (int i = 0; i < 5; ++i)
				if (ImGui::Selectable(xh_shapes[i], _sherbet_crosshair_builtin == i))
				{ _sherbet_crosshair_builtin = i; xh_changed = true; }
			ImGui::EndCombo();
		}
		if (_sherbet_crosshair_builtin != prev_builtin)
			_sherbet_crosshair_dirty = true;

		if (_sherbet_crosshair_builtin == 0) // 커스텀 이미지 모드
		{
			const std::filesystem::path xh_dir = _config_path.parent_path() / L"Sherbet-Crosshairs";
			static std::vector<std::string> xh_files;
			static bool xh_need_scan = true;
			if (xh_need_scan)
			{
				xh_need_scan = false;
				xh_files.clear();
				std::error_code ec;
				std::filesystem::create_directories(xh_dir, ec); // 없으면 폴더 생성
				for (std::filesystem::directory_iterator it(xh_dir, ec), end; it != end; it.increment(ec))
				{
					if (!it->is_regular_file(ec)) continue;
					std::filesystem::path ext = it->path().extension();
					std::string e = ext.u8string();
					std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga")
						xh_files.push_back(it->path().filename().u8string());
				}
			}

			if (ImGui::BeginCombo("\xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80 \xED\x8C\x8C\xEC\x9D\xBC", _sherbet_crosshair_file.empty() ? "-" : _sherbet_crosshair_file.c_str())) // "이미지 파일"
			{
				for (const std::string &f : xh_files)
					if (ImGui::Selectable(f.c_str(), f == _sherbet_crosshair_file))
					{ _sherbet_crosshair_file = f; _sherbet_crosshair_dirty = true; xh_changed = true; }
				ImGui::EndCombo();
			}
			// ⚠️ 세 안내는 **배타적**이다. 로딩 실패가 '폴더에 PNG 없음' 안내와 같이 뜨면
			//    무엇이 문제인지 더 헷갈린다.
			if (_sherbet_crosshair_load == 2)
				ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s",
					ICON_FK_CANCEL "  " "\xEC\x9D\xB4 \xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80\xEB\xA5\xBC \xEC\x9D\xBD\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\x8B\xA4\xEB\xA5\xB8 PNG \xEB\xA1\x9C \xEB\x8B\xA4\xEC\x8B\x9C \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xB4 \xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94"); // "이 이미지를 읽지 못했어요 — 다른 PNG 로 다시 저장해 보세요"
			else if (xh_files.empty())
				ImGui::TextDisabled("%s", "\xED\x8F\xB4\xEB\x8D\x94\xEC\x97\x90 PNG \xEC\x97\x86\xEC\x9D\x8C \xE2\x80\x94 '\xED\x8F\xB4\xEB\x8D\x94 \xEC\x97\xB4\xEA\xB8\xB0'\xEB\xA1\x9C \xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80\xEB\xA5\xBC \xEB\x84\xA3\xEC\x9C\xBC\xEC\x84\xB8\xEC\x9A\x94"); // 없음 안내
			else
				ImGui::TextDisabled("%s", "PNG(\xED\x88\xAC\xEB\xAA\x85 \xEB\xB0\xB0\xEA\xB2\xBD)\xEB\xA5\xBC \xED\x8F\xB4\xEB\x8D\x94\xEC\x97\x90 \xEB\x84\xA3\xEA\xB3\xA0 \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x97\x90\xEC\x84\x9C \xEA\xB3\xA0\xEB\xA5\xB4\xEC\x84\xB8\xEC\x9A\x94"); // 안내

			if (ImGui::Button("\xED\x8F\xB4\xEB\x8D\x94 \xEC\x97\xB4\xEA\xB8\xB0")) // "폴더 열기"
			{
				std::error_code ec;
				std::filesystem::create_directories(xh_dir, ec);
				utils::open_explorer(xh_dir);
				xh_need_scan = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("\xEC\x83\x88\xEB\xA1\x9C\xEA\xB3\xA0\xEC\xB9\xA8")) // "새로고침"
				xh_need_scan = true;
		}

		xh_changed |= ImGui::SliderFloat("\xED\x81\xAC\xEA\xB8\xB0", &_sherbet_crosshair_size, 4.0f, 256.0f, "%.0f"); // "크기"
		if (_sherbet_crosshair_builtin != 0)
		{
			xh_changed |= ImGui::SliderFloat("\xEB\x91\x90\xEA\xBB\x98", &_sherbet_crosshair_thick, 1.0f, 12.0f, "%.1f"); // "두께"
			if (_sherbet_crosshair_builtin == 2 || _sherbet_crosshair_builtin == 4)
				xh_changed |= ImGui::SliderFloat("\xEC\xA4\x91\xEC\x95\x99 \xEA\xB0\x84\xEA\xB2\xA9", &_sherbet_crosshair_gap, 0.0f, 40.0f, "%.0f"); // "중앙 간격"
			xh_changed |= ImGui::ColorEdit4("\xEC\x83\x89\xEC\x83\x81", _sherbet_crosshair_col, ImGuiColorEditFlags_AlphaBar); // "색상"
		}
		xh_changed |= ImGui::SliderFloat("\xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84", &_sherbet_crosshair_opacity, 0.0f, 1.0f, "%.2f"); // "투명도"
		xh_changed |= ImGui::SliderFloat2("\xEC\x9C\x84\xEC\xB9\x98 X/Y", _sherbet_crosshair_off, -400.0f, 400.0f, "%.0f"); // "위치 X/Y"

		if (xh_changed)
			modified = true;
		ImGui::Spacing();
	}

	if (modified)
		save_config();
}

// SHERBET: 「최적화」 탭의 한 줄 — 왼쪽에 라벨, 정렬된 자리에 값. 값 색으로만 상태를 말한다.
static void sherbet_stat_row(const char *label, float value_x, const ImVec4 &color, const char *value)
{
	ImGui::TextDisabled("%s", label);
	ImGui::SameLine(value_x);
	ImGui::TextColored(color, "%s", value);
}

// SHERBET: 「최적화」 탭 잠금 카드 — 탭 하나가 통째로 상품일 때의 진열대.
//
// 여기서도 말 대신 화면을 보여준다: 실제 탭과 **같은 sherbet_stat_row 줄 모양**으로
// 예시 수치를 늘어놓는다. 다만 그 값은 전부 sherbet::paid::demo_optimize_rows() 의
// 고정 문자열이다.
// ⚠️ 잠긴 사람에게 자기 PC 의 진짜 프레임·후처리 비용을 보여주면 그건 미리보기가 아니라
//    기능을 그냥 준 것이다. 이 함수는 런타임 필드를 **한 개도 읽지 않는다** — 그래서
//    실측 코드가 있는 draw_gui_optimize() 본문과 아예 다른 함수로 갈라 두었다.
void reshade::runtime::sherbet_draw_optimize_lock_card(const sherbet::paid::feature &f)
{
	static const char *const kLockExample = "\xEC\x98\x88\xEC\x8B\x9C"; // "예시"
	static const char *const kLockOptCaption = ICON_FK_ARROW_UP " \xEC\x98\x88\xEC\x8B\x9C \xEC\x88\x98\xEC\xB9\x98\xEC\x98\x88\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEB\xA9\xB4 \xEC\x9D\xB4 \xEC\x9E\x90\xEB\xA6\xAC\xEC\x97\x90 \xEB\x82\xB4 PC \xEC\x9D\x98 \xEC\x8B\xA4\xEC\xA0\x9C \xEA\xB0\x92\xEC\x9D\xB4 \xEB\x93\xA4\xEC\x96\xB4\xEC\x99\x80\xEC\x9A\x94"; // "↑ 예시 수치예요 — 구매하면 이 자리에 내 PC 의 실제 값이 들어와요"

	sherbet::begin_card("##optimize_lock");
	{
		sherbet_draw_lock_header(f);

		const sherbet::theme &t = sherbet::active_theme();
		ImDrawList *const dl = ImGui::GetWindowDrawList();
		const float value_x = 11.0f * ImGui::GetFontSize(); // 실제 탭과 같은 정렬 위치

		// 예시 패널 — 실제 카드보다 한 톤 죽여 "지금 내 값"으로 오해할 여지를 줄인다.
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float pw = ImMax(64.0f, ImGui::GetContentRegionAvail().x);
		const std::vector<sherbet::paid::stat_row> &rows = sherbet::paid::demo_optimize_rows();
		const float ph = ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(rows.size()) + 20.0f;
		dl->AddRectFilled(p0, ImVec2(p0.x + pw, p0.y + ph), sherbet::with_alpha(t.bg1, 200), 12.0f);

		ImGui::Dummy(ImVec2(pw, 8.0f));
		ImGui::Indent(10.0f);
		for (const sherbet::paid::stat_row &r : rows)
			sherbet_stat_row(r.label, value_x, ImGui::ColorConvertU32ToFloat4(sherbet::with_alpha(t.text, 190)), r.value);
		ImGui::Unindent(10.0f);
		ImGui::Dummy(ImVec2(pw, 4.0f));

		// 패널 우상단 '예시' 배지 — 잘라낸 스크린샷만 봐도 예시라는 게 보여야 한다.
		{
			const ImVec2 ts = ImGui::CalcTextSize(kLockExample);
			const ImVec2 b1(p0.x + pw - 10.0f, p0.y + 10.0f + ts.y + 6.0f);
			const ImVec2 b0(b1.x - (ts.x + 16.0f), p0.y + 10.0f);
			dl->AddRectFilled(b0, b1, sherbet::with_alpha(t.bg0, 225), (b1.y - b0.y) * 0.5f);
			dl->AddText(ImVec2(b0.x + 8.0f, b0.y + 3.0f), sherbet::with_alpha(t.accent2, 255), kLockExample);
		}
		ImGui::TextDisabled("%s", kLockOptCaption);

		sherbet_draw_lock_footer(f);
	}
	sherbet::end_card();
}

// SHERBET: 「최적화」 탭 — **읽기 전용 상태판**이다. 토글도 슬라이더도 없다.
// 최적화는 이미 출하된 프리셋·효과로 걸려 있고, 이 탭은 그게 지금 실제로 적용돼 있다는
// 것을 구매자에게 보여 줄 뿐이다.
//
// ⚠️ **여기 나오는 값은 한 줄도 빠짐없이 런타임의 실제 상태에서 읽는다.** 하드코딩된
//    "적용됨" 문구도, 지어낸 숫자도 없다. 얻을 수 없는 값은 그럴듯하게 채우지 않고 그 줄을
//    통째로 뺀다 — 무슨 일이 있어도 초록불인 표시는 아무것도 알려주지 않는 표시다.
//
// 비용: 이 함수는 탭이 열려 있을 때만 호출된다. 프레임 통계도 ImGui 가 이미 매 프레임
// 유지하는 링버퍼를 읽을 뿐이라 우리가 따로 프레임마다 하는 일은 없다.
void reshade::runtime::draw_gui_optimize()
{
	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.6f);
	ImGui::TextUnformatted(ICON_FK_DASHBOARD "  " "\xEC\xB5\x9C\xEC\xA0\x81\xED\x99\x94" /* 최적화 */);
	ImGui::PopFont();
	ImGui::Spacing();

	// ── 유료 기능 'optimize' ──────────────────────────────────────────────
	// 이 탭 전체가 상품이다. 잠겨 있으면 **레일 아이콘은 그대로 두고**(안 보이면 안 팔린다)
	// 탭 내용만 판매 카드로 바꾼다.
	// ⚠️ 잠긴 경로는 여기서 return 하므로 아래 실측 코드는 한 줄도 실행되지 않는다 —
	//    _gather_gpu_statistics 를 켜지도 않고(= GPU 타임스탬프 쿼리 비용도 안 낸다),
	//    프레임/후처리 비용을 계산하지도 않는다. "UI 만 가리고 일은 그대로"가 아니다.
	if (!sherbet::paid::show_real_stats(sherbet_feature_unlocked("optimize")))
	{
		if (const sherbet::paid::feature *const opt_f = sherbet::paid::find("optimize"))
		{
			sherbet_draw_optimize_lock_card(*opt_f);
			return;
		}
	}

	ImGui::TextWrapped("%s", "Sherbet \xEC\x9D\xB4 \xEC\xA7\x80\xEA\xB8\x88 \xEC\x8B\xA4\xEC\xA0\x9C\xEB\xA1\x9C \xEB\xAC\xB4\xEC\x97\x87\xEC\x9D\x84 \xEC\xA0\x81\xEC\x9A\xA9\xED\x95\x98\xEA\xB3\xA0 \xEC\x9E\x88\xEB\x8A\x94\xEC\xA7\x80 \xEB\xB3\xB4\xEC\x97\xAC\xEC\xA3\xBC\xEB\x8A\x94 \xED\x99\x94\xEB\xA9\xB4\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94. \xEC\x97\xAC\xEA\xB8\xB0\xEC\x84\x9C \xEB\xB0\x94\xEA\xBE\xB8\xEB\x8A\x94 \xEA\xB1\xB4 \xEC\x97\x86\xEA\xB3\xA0, \xEC\x95\x84\xEB\x9E\x98 \xEC\x88\xAB\xEC\x9E\x90\xEB\x8A\x94 \xEC\xA0\x84\xEB\xB6\x80 \xEC\xA7\x80\xEA\xB8\x88 \xEB\x8F\x8C\xEC\x95\x84\xEA\xB0\x80\xEB\x8A\x94 \xEC\x83\x81\xED\x83\x9C\xEC\x97\x90\xEC\x84\x9C \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEC\x9D\xBD\xEC\x96\xB4\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4." /* Sherbet 이 지금 실제로 무엇을 적용하고 있는지 보여주는 화면이에요. 여기서 바꾸는 건 없고, 아래 숫자는 전부 지금 돌아가는 상태에서 그대로 읽어옵니다. */);
	ImGui::Spacing();

	// 기존 탭들이 쓰는 상태색 그대로
	// 고정 파스텔이었다 — 라이트 테마(딸기)의 흰 카드 위에서 대비가 무너진다.
	const ImVec4 col_ok   = sherbet::status_color(sherbet::status::good);
	const ImVec4 col_bad  = sherbet::status_color(sherbet::status::bad);
	const ImVec4 col_warn = sherbet::status_color(sherbet::status::warn);
	const ImVec4 col_val = ImGui::ColorConvertU32ToFloat4(sherbet::active_theme().text);

	// 라벨 폭에 맞춘 고정 정렬 위치(카드 폭이 달라져도 흔들리지 않는다)
	const float value_x = 11.0f * ImGui::GetFontSize();
	char buf[256];

	// ── 지금 적용된 것 ────────────────────────────────────────────────────
	sherbet::begin_card("##opt_applied");
	{
		ImGui::TextUnformatted(ICON_FK_OK "  " "\xEC\xA7\x80\xEA\xB8\x88 \xEC\xA0\x81\xEC\x9A\xA9\xEB\x90\x9C \xEA\xB2\x83" /* 지금 적용된 것 */);
		ImGui::Spacing();

		// 프리셋 — _current_preset_path 그대로. 저장 안 된 변경이 있으면 그것도 표시한다.
		const std::string preset_name = _current_preset_path.stem().u8string();
		if (preset_name.empty())
		{
			sherbet_stat_row("\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B" /* 프리셋 */, value_x, col_warn, "\xEC\x84\xA0\xED\x83\x9D \xEC\x95\x88 \xEB\x90\xA8" /* 선택 안 됨 */);
		}
		else
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), _preset_is_modified ? "%s  " "(\xEC\xA0\x80\xEC\x9E\xA5 \xEC\x95\x88 \xEB\x90\x9C \xEB\xB3\x80\xEA\xB2\xBD \xEC\x9E\x88\xEC\x9D\x8C)" /* (저장 안 된 변경 있음) */ : "%s", preset_name.c_str());
			sherbet_stat_row("\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B" /* 프리셋 */, value_x, _preset_is_modified ? col_warn : col_ok, buf);
		}

		// 효과 전체 on/off — 이게 꺼져 있으면 아래 숫자가 다 무의미하므로 맨 위에 둔다.
		sherbet_stat_row("\xED\x9A\xA8\xEA\xB3\xBC" /* 효과 */, value_x, _effects_enabled ? col_ok : col_bad,
			_effects_enabled ? ICON_FK_OK "  " "\xEC\xBC\x9C\xEC\xA7\x90" /* 켜짐 */ : ICON_FK_CANCEL "  " "\xEA\xBA\xBC\xEC\xA7\x90" /* 꺼짐 */);

		// 활성 기법 수 — 목록에 실제로 보이는 것(숨김 아님 + 컴파일 성공)만 센다.
		size_t tech_total = 0, tech_on = 0;
		for (const technique &tech : _techniques)
		{
			if (tech.effect_index >= _effects.size())
				continue;
			if (tech.hidden || !_effects[tech.effect_index].compiled)
				continue;
			++tech_total;
			if (tech.enabled)
				++tech_on;
		}
		ImFormatString(buf, IM_ARRAYSIZE(buf), "%zu / %zu", tech_on, tech_total);
		sherbet_stat_row("\xED\x99\x9C\xEC\x84\xB1 \xED\x9A\xA8\xEA\xB3\xBC" /* 활성 효과 */, value_x, tech_on > 0 ? col_ok : col_warn, buf);

		// 효과 파일 — 로드된 개수, 건너뛴 개수(성능을 위해 미리 로드를 건너뛰는 옵션이 켜졌을 때)
		size_t fx_failed = 0, fx_skipped = 0;
		for (const effect &fx : _effects)
		{
			if (fx.skipped)
				++fx_skipped;
			else if (!fx.compiled)
				++fx_failed;
		}
		if (fx_skipped > 0)
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%zu\xEA\xB0\x9C \xEB\xA1\x9C\xEB\x93\x9C \xC2\xB7 %zu\xEA\xB0\x9C \xEA\xB1\xB4\xEB\x84\x88\xEB\x9C\x80" /* %zu개 로드 · %zu개 건너뜀 */, _effects.size() - fx_skipped, fx_skipped);
		else
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%zu\xEA\xB0\x9C \xEB\xA1\x9C\xEB\x93\x9C" /* %zu개 로드 */, _effects.size());
		sherbet_stat_row("\xED\x9A\xA8\xEA\xB3\xBC \xED\x8C\x8C\xEC\x9D\xBC" /* 효과 파일 */, value_x, col_val, buf);

		// 컴파일 상태 — 실패가 있으면 그걸 숨기지 않는다. 조용한 실패가 제일 나쁘다.
		if (is_loading())
		{
			sherbet_stat_row("\xEC\xBB\xB4\xED\x8C\x8C\xEC\x9D\xBC" /* 컴파일 */, value_x, col_warn, "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91" /* 불러오는 중 */);
		}
		else if (!_last_reload_successful || fx_failed > 0)
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), ICON_FK_CANCEL "  " "%zu\xEA\xB0\x9C \xEC\x8B\xA4\xED\x8C\xA8 \xE2\x80\x94 \xED\x99\x88 \xED\x83\xAD\xEC\x9D\x98 \xED\x9A\xA8\xEA\xB3\xBC \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x97\x90\xEC\x84\x9C \xED\x99\x95\xEC\x9D\xB8\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94" /* %zu개 실패 — 홈 탭의 효과 목록에서 확인하세요 */, fx_failed);
			sherbet_stat_row("\xEC\xBB\xB4\xED\x8C\x8C\xEC\x9D\xBC" /* 컴파일 */, value_x, col_bad, buf);
		}
		else
		{
			sherbet_stat_row("\xEC\xBB\xB4\xED\x8C\x8C\xEC\x9D\xBC" /* 컴파일 */, value_x, col_ok, ICON_FK_OK "  " "\xEC\xA0\x95\xEC\x83\x81" /* 정상 */);
		}

		// 성능 모드 — ReShade 자체 옵션이고 최적화 관점에서 의미가 있으므로 노출한다.
		sherbet_stat_row("\xEC\x84\xB1\xEB\x8A\xA5 \xEB\xAA\xA8\xEB\x93\x9C" /* 성능 모드 */, value_x, _performance_mode ? col_ok : col_val,
			_performance_mode ? "\xEC\xBC\x9C\xEC\xA7\x90" /* 켜짐 */ : "\xEA\xBA\xBC\xEC\xA7\x90" /* 꺼짐 */);
	}
	sherbet::end_card();
	ImGui::Spacing();

	// ── 성능 ──────────────────────────────────────────────────────────────
	sherbet::begin_card("##opt_perf");
	{
		ImGui::TextUnformatted(ICON_FK_CHART_LINE "  " "\xEC\x84\xB1\xEB\x8A\xA5" /* 성능 */);
		ImGui::Spacing();

		// 숫자가 매 프레임 바뀌면 읽을 수가 없다. 4Hz 로만 갱신한다.
		// (표시용 캐시일 뿐이라 함수 지역 static 으로 둔다 — 이 파일의 다른 탭들과 같은 방식)
		static float shown_fps = 0.0f, shown_ms = 0.0f, shown_worst_ms = 0.0f;
		static double last_sample = -1.0;
		const double now = ImGui::GetTime();
		if (last_sample < 0.0 || now - last_sample >= 0.25)
		{
			last_sample = now;
			shown_fps = _imgui_context->IO.Framerate;
			shown_ms = shown_fps > 0.0f ? 1000.0f / shown_fps : 0.0f;
			// 최악 프레임은 ImGui 가 **이미 매 프레임 유지하는** 60프레임 링을 그대로 읽는다.
			// 평균만 보면 끊김이 안 보이므로, 구매자에게는 이쪽이 더 정직한 숫자다.
			float worst = 0.0f;
			for (int i = 0; i < _imgui_context->FramerateSecPerFrameCount; ++i)
				if (_imgui_context->FramerateSecPerFrame[i] > worst)
					worst = _imgui_context->FramerateSecPerFrame[i];
			shown_worst_ms = worst * 1000.0f;
		}

		ImFormatString(buf, IM_ARRAYSIZE(buf), "%.0f fps  ·  %.2f ms", static_cast<double>(shown_fps), static_cast<double>(shown_ms));
		sherbet_stat_row("\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84" /* 프레임 */, value_x, col_val, buf);

		if (shown_worst_ms > 0.0f)
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%.2f ms", static_cast<double>(shown_worst_ms));
			sherbet_stat_row("\xEC\xB5\x9C\xEA\xB7\xBC 60\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84 \xEC\xB5\x9C\xEC\x95\x85" /* 최근 60프레임 최악 */, value_x, shown_worst_ms > shown_ms * 2.0f ? col_warn : col_val, buf);
		}

		// 후처리 비용 — 켜져 있는 기법들의 이동평균 합. 기법을 끄면 그 값은 clear() 되므로
		// 꺼진 기법이 합계를 부풀리지 않는다.
		uint64_t cpu_ns = 0, gpu_ns = 0;
		if (!is_loading() && _effects_enabled)
		{
			for (const technique &tech : _techniques)
			{
				cpu_ns += tech.average_cpu_duration;
				gpu_ns += tech.average_gpu_duration;
			}
		}
		ImFormatString(buf, IM_ARRAYSIZE(buf), "%.3f ms", cpu_ns * 1e-6);
		sherbet_stat_row("\xED\x9B\x84\xEC\xB2\x98\xEB\xA6\xAC \xEB\xB9\x84\xEC\x9A\xA9 (CPU)" /* 후처리 비용 (CPU) */, value_x, col_val, buf);

		// GPU 시간은 타임스탬프 쿼리를 켜야 나온다. 아래 한 줄이 그걸 켜는데, 이 탭이
		// 열려 있는 동안에만 켜진다(_gather_gpu_statistics 는 매 프레임 false 로 리셋된다).
		// 쿼리 결과가 도착하기 전에는 0 이므로, 그때는 **줄 자체를 내보내지 않는다** —
		// 0.000 ms 라고 적으면 "공짜"라는 거짓말이 된다.
		_gather_gpu_statistics = true;
		if (gpu_ns != 0)
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%.3f ms", gpu_ns * 1e-6);
			sherbet_stat_row("\xED\x9B\x84\xEC\xB2\x98\xEB\xA6\xAC \xEB\xB9\x84\xEC\x9A\xA9 (GPU)" /* 후처리 비용 (GPU) */, value_x, col_val, buf);
		}
	}
	sherbet::end_card();
	ImGui::Spacing();

	// ── 출력 ──────────────────────────────────────────────────────────────
	sherbet::begin_card("##opt_output");
	{
		ImGui::TextUnformatted(ICON_FK_ADJUST "  " "\xEC\xB6\x9C\xEB\xA0\xA5" /* 출력 */);
		ImGui::Spacing();

		if (_width != 0 && _height != 0)
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%u x %u", _width, _height);
			sherbet_stat_row("\xEC\xB6\x9C\xEB\xA0\xA5 \xED\x95\xB4\xEC\x83\x81\xEB\x8F\x84" /* 출력 해상도 */, value_x, col_val, buf);
		}

		// 효과가 실제로 처리되는 해상도가 출력과 다를 때만(해상도 스케일 등) 따로 보여준다.
		if (!_effect_permutations.empty() &&
			_effect_permutations[0].width != 0 &&
			(_effect_permutations[0].width != _width || _effect_permutations[0].height != _height))
		{
			ImFormatString(buf, IM_ARRAYSIZE(buf), "%u x %u", _effect_permutations[0].width, _effect_permutations[0].height);
			sherbet_stat_row("\xED\x9A\xA8\xEA\xB3\xBC \xEC\xB2\x98\xEB\xA6\xAC \xED\x95\xB4\xEC\x83\x81\xEB\x8F\x84" /* 효과 처리 해상도 */, value_x, col_val, buf);
		}

		if (_back_buffer_format != api::format::unknown)
		{
			const char *fmt_name = nullptr;
			switch (_back_buffer_format)
			{
			case api::format::r8g8b8a8_unorm: fmt_name = "R8G8B8A8"; break;
			case api::format::r8g8b8a8_unorm_srgb: fmt_name = "R8G8B8A8 sRGB"; break;
			case api::format::b8g8r8a8_unorm: fmt_name = "B8G8R8A8"; break;
			case api::format::b8g8r8a8_unorm_srgb: fmt_name = "B8G8R8A8 sRGB"; break;
			case api::format::r10g10b10a2_unorm: fmt_name = "R10G10B10A2"; break;
			case api::format::r16g16b16a16_float: fmt_name = "R16G16B16A16F"; break;
			default: break;
			}
			const bool hdr = _back_buffer_color_space == api::color_space::hdr10_pq || _back_buffer_format == api::format::r16g16b16a16_float;
			if (fmt_name != nullptr)
				ImFormatString(buf, IM_ARRAYSIZE(buf), hdr ? "%s (%u bpc) · HDR" : "%s (%u bpc)", fmt_name, api::format_bit_depth(_back_buffer_format));
			else
				ImFormatString(buf, IM_ARRAYSIZE(buf), hdr ? "Format %u (%u bpc) · HDR" : "Format %u (%u bpc)",
					static_cast<unsigned int>(_back_buffer_format), api::format_bit_depth(_back_buffer_format));
			sherbet_stat_row("\xEB\xB0\xB1\xEB\xB2\x84\xED\x8D\xBC \xED\x98\x95\xEC\x8B\x9D" /* 백버퍼 형식 */, value_x, col_val, buf);
		}

		const char *api_name = nullptr;
		switch (_device->get_api())
		{
		case api::device_api::d3d9: api_name = "Direct3D 9"; break;
		case api::device_api::d3d10: api_name = "Direct3D 10"; break;
		case api::device_api::d3d11: api_name = "Direct3D 11"; break;
		case api::device_api::d3d12: api_name = "Direct3D 12"; break;
		case api::device_api::opengl: api_name = "OpenGL"; break;
		case api::device_api::vulkan: api_name = "Vulkan"; break;
		}
		if (api_name != nullptr)
			sherbet_stat_row("\xEA\xB7\xB8\xEB\x9E\x98\xED\x94\xBD API" /* 그래픽 API */, value_x, col_val, api_name);
	}
	sherbet::end_card();
}

// SHERBET: 커스텀 사진 배경 잠금 카드 — 「설정」 탭.
//
// 가운데 미리보기는 **가짜 사진**이다(테마색 그라디언트 + 실제와 같은 어두운 스크림).
// 잠긴 사람에게 진짜 이미지 로딩 경로를 태우지 않는다 — 최적화 잠금 카드가 런타임 필드를
// 한 개도 읽지 않는 것과 같은 이유다. 보여주려는 건 사진 자체가 아니라 "창 배경이 사진으로
// 바뀐다" 는 사실이므로, 가짜 그라디언트로도 충분히 전달된다.
void reshade::runtime::sherbet_draw_bg_lock_card(const sherbet::paid::feature &f)
{
	static const char *const kBgLockCaption = ICON_FK_ARROW_UP " \xEC\x98\x88\xEC\x8B\x9C\xEC\x98\x88\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEB\xA9\xB4 \xEC\x9D\xB4 \xEC\x9E\x90\xEB\xA6\xAC\xEC\x97\x90 \xEB\x82\xB4 \xEC\x82\xAC\xEC\xA7\x84\xEC\x9D\xB4 \xEB\x93\xA4\xEC\x96\xB4\xEC\x99\x80\xEC\x9A\x94"; // "↑ 예시예요 — 구매하면 이 자리에 내 사진이 들어와요"
	static const char *const kBgLockPhoto = "\xEB\x82\xB4 \xEC\x82\xAC\xEC\xA7\x84"; // "내 사진"

	sherbet::begin_card("##bg_lock");
	{
		sherbet_draw_lock_header(f);

		const sherbet::theme &t = sherbet::active_theme();
		ImDrawList *const dl = ImGui::GetWindowDrawList();

		// 미니 오버레이 미리보기. 실제 배경 렌더와 **같은 규칙**으로 그린다:
		// 둥근 모서리 → 사진 → 가독성용 어두운 스크림 → 그 위에 UI.
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const float pw = ImMax(120.0f, ImGui::GetContentRegionAvail().x);
		const float ph = ImGui::GetTextLineHeightWithSpacing() * 4.0f + 24.0f;
		const ImVec2 p1(p0.x + pw, p0.y + ph);

		// '사진' 자리 — 테마 액센트에서 액센트2 로 흐르는 대각 그라디언트.
		dl->PushClipRect(p0, p1, true);
		dl->AddRectFilledMultiColor(p0, p1,
			sherbet::with_alpha(t.accent, 210), sherbet::with_alpha(t.accent2, 210),
			sherbet::with_alpha(t.bg1, 230), sherbet::with_alpha(t.accent, 160));
		// 실제 기능이 글씨 가독성을 위해 까는 것과 같은 스크림.
		dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 96));
		dl->PopClipRect();
		dl->AddRect(p0, p1, sherbet::with_alpha(t.border, 200), 12.0f, 0, 1.0f);

		// 스크림 위에 얹히는 가짜 UI — 제목 줄 하나 + 알약 두 개.
		const float pad = 12.0f;
		dl->AddText(ImVec2(p0.x + pad, p0.y + pad), sherbet::with_alpha(t.text, 235), "Sherbet");
		const float chip_y = p0.y + pad + ImGui::GetTextLineHeightWithSpacing() * 1.4f;
		float chip_x = p0.x + pad;
		for (int i = 0; i < 2; ++i)
		{
			const float cw = 52.0f + i * 18.0f, ch = ImGui::GetTextLineHeight() + 6.0f;
			dl->AddRectFilled(ImVec2(chip_x, chip_y), ImVec2(chip_x + cw, chip_y + ch),
				sherbet::with_alpha(i == 0 ? t.accent : t.chip, 220), ch * 0.5f);
			chip_x += cw + 8.0f;
		}
		// 우상단 '내 사진' 배지 — 잘라낸 스크린샷만 봐도 이 영역이 사진 자리임이 보인다.
		{
			const ImVec2 ts = ImGui::CalcTextSize(kBgLockPhoto);
			const ImVec2 b1(p1.x - 10.0f, p0.y + 10.0f + ts.y + 6.0f);
			const ImVec2 b0(b1.x - (ts.x + 16.0f), p0.y + 10.0f);
			dl->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 150), (b1.y - b0.y) * 0.5f);
			dl->AddText(ImVec2(b0.x + 8.0f, b0.y + 3.0f), sherbet::with_alpha(t.text, 220), kBgLockPhoto);
		}

		ImGui::Dummy(ImVec2(pw, ph));
		ImGui::TextDisabled("%s", kBgLockCaption);
		ImGui::Spacing();

		sherbet_draw_lock_footer(f);
	}
	sherbet::end_card();
}

void reshade::runtime::draw_gui_settings()
{
	if (ImGui::Button(ICON_FK_FOLDER " " + _("Open base folder in explorer"), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
		utils::open_explorer(_config_path);

	ImGui::Spacing();

	bool modified = false;
	bool modified_custom_style = false;

	// SHERBET: 커스텀 조준점 설정은 「에임」 탭으로 옮겼다(에임 관련 설정을 한 곳에 모으기 위해).
	// 기존 사용자가 "설정이 사라졌다"고 느끼지 않도록 원래 자리에 안내 한 줄을 남긴다.
	ImGui::TextDisabled("%s", ICON_FK_CROSSHAIRS " \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\x84\xA4\xEC\xA0\x95\xEC\x9D\x80 \xEC\x99\xBC\xEC\xAA\xBD \xEC\x97\x90\xEC\x9E\x84 \xED\x83\xAD\xEC\x9C\xBC\xEB\xA1\x9C \xEC\x98\xAE\xEA\xB2\xBC\xEC\x96\xB4\xEC\x9A\x94"); // "조준점 설정은 왼쪽 에임 탭으로 옮겼어요"
	ImGui::Spacing();

	// SHERBET: HUD 돋보기 — 화면의 한 조각(체력·방어구 막대 등)을 확대해 크게 보여준다.
	// 게임 상태를 읽지 않는다 — 그 자리 픽셀을 확대할 뿐이다.
	if (ImGui::CollapsingHeader(ICON_FK_SEARCH "  " "\xED\x99\x94\xEB\xA9\xB4 \xEB\x8F\x8B\xEB\xB3\xB4\xEA\xB8\xB0")) // "화면 돋보기"
	{
		if (ImGui::Checkbox("\xEB\x8F\x8B\xEB\xB3\xB4\xEA\xB8\xB0 \xEC\x82\xAC\xEC\x9A\xA9##mag", &_sherbet_mag_on)) // "돋보기 사용"
			modified = true;

		// ⚠️ 영역은 **사용자가 직접 잡는다.** 우리가 좌표를 박으면 서버마다·해상도마다 어긋난다
		//    (FiveM HUD 는 서버 리소스가 그리고, 서버가 HUD 를 바꾸면 그날로 깨진다).
		if (sherbet::pill_button(_sherbet_mag_picking
				? ICON_FK_CANCEL "  " "\xEC\x98\x81\xEC\x97\xAD \xEC\x9E\xA1\xEA\xB8\xB0 \xEC\xB7\xA8\xEC\x86\x8C"  // "영역 잡기 취소"
				: ICON_FK_SEARCH "  " "\xEC\x98\x81\xEC\x97\xAD \xEC\x9E\xA1\xEA\xB8\xB0", _sherbet_mag_picking)) // "영역 잡기"
			_sherbet_mag_picking = !_sherbet_mag_picking;
		ImGui::SameLine();
		if (sherbet::pill_button(ICON_FK_UNDO "  " "\xEC\xB4\x88\xEA\xB8\xB0\xED\x99\x94", false)) // "초기화"
		{
			_sherbet_mag_rect = sherbet::mag::rect(); // 기본값(우하단 근처)
			_sherbet_mag_anchor[0] = 0.5f; _sherbet_mag_anchor[1] = 0.30f;
			_sherbet_mag_cap_res[0] = _sherbet_mag_cap_res[1] = 0;
			modified = true;
		}

		if (_sherbet_mag_picking)
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s",
				ICON_FK_WARNING "  " "\xED\x99\x95\xEB\x8C\x80\xED\x95\xA0 \xEA\xB3\xB3\xEC\x9D\x84 \xEB\x93\x9C\xEB\x9E\x98\xEA\xB7\xB8\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94 \xC2\xB7 \xEC\xB7\xA8\xEC\x86\x8C\xEB\x8A\x94 \xEC\x9A\xB0\xED\x81\xB4\xEB\xA6\xAD/Esc"); // "확대할 곳을 드래그하세요 · 취소는 우클릭/Esc"

		// 해상도가 바뀌면 잡아 둔 영역이 어긋날 수 있다 — 서버 HUD 가 픽셀 고정이면 특히 그렇다.
		// 우리가 자동으로 고칠 수 없는 문제이므로(서버 구현에 달렸다) 정직하게 알리고 다시 잡게 한다.
		if (_sherbet_mag_cap_res[0] != 0 &&
			(_sherbet_mag_cap_res[0] != static_cast<int>(_width) || _sherbet_mag_cap_res[1] != static_cast<int>(_height)))
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s",
				ICON_FK_WARNING "  " "\xED\x95\xB4\xEC\x83\x81\xEB\x8F\x84\xEA\xB0\x80 \xEB\xB0\x94\xEB\x80\x8C\xEC\x97\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x98\x81\xEC\x97\xAD\xEC\x9D\x84 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x9E\xA1\xEC\x95\x84 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"); // "해상도가 바뀌었어요 — 영역을 다시 잡아 주세요"

		ImGui::SetNextItemWidth(ImMax(220.0f, 9.0f * ImGui::GetFontSize()));
		if (ImGui::SliderFloat("\xEB\xB0\xB0\xEC\x9C\xA8##mag", &_sherbet_mag_zoom, 1.0f, 10.0f, "%.1fx")) // "배율"
			modified = true;
		ImGui::SetNextItemWidth(ImMax(220.0f, 9.0f * ImGui::GetFontSize()));
		if (ImGui::SliderFloat("\xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84##mag", &_sherbet_mag_opacity, 0.2f, 1.0f, "%.2f")) // "투명도"
			modified = true;
		ImGui::SetNextItemWidth(ImMax(220.0f, 9.0f * ImGui::GetFontSize()));
		if (ImGui::SliderFloat2("\xED\x91\x9C\xEC\x8B\x9C \xEC\x9C\x84\xEC\xB9\x98##mag", _sherbet_mag_anchor, 0.0f, 1.0f, "%.2f")) // "표시 위치"
			modified = true;

		// ⚠️ 실패를 조용히 넘기지 않는다. "안 보여요" 신고를 받았을 때 원인을 즉시 가르는 줄이다
		//    (기능이 꺼짐 / 영역 미확정 / 캡처 자체가 안 됨 — 셋의 대응이 전부 다르다).
		if (_sherbet_mag_on)
		{
			if (_sherbet_mag_srv == 0)
				ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s",
					ICON_FK_CANCEL "  " "\xED\x99\x94\xEB\xA9\xB4\xEC\x9D\x84 \xEA\xB0\x80\xEC\xA0\xB8\xEC\x98\xA4\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xED\x8C\x90\xEB\xA7\xA4\xEC\x9E\x90\xEC\x97\x90\xEA\xB2\x8C \xEC\x95\x8C\xEB\xA0\xA4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"); // "화면을 가져오지 못했어요 — 판매자에게 알려 주세요"
			else
				ImGui::TextDisabled("%s  %dx%d \xE2\x86\x92 %.0fx%.0f",
					ICON_FK_OK, _sherbet_mag_tex_w, _sherbet_mag_tex_h,
					_sherbet_mag_tex_w * sherbet::mag::clamp_zoom(_sherbet_mag_zoom),
					_sherbet_mag_tex_h * sherbet::mag::clamp_zoom(_sherbet_mag_zoom));
		}

		sherbet_hint("\xED\x99\x94\xEB\xA9\xB4\xEC\x9D\x98 \xED\x95\x9C \xEA\xB3\xB3\xEC\x9D\x84 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xED\x81\xAC\xEA\xB2\x8C \xEB\xB3\xB4\xEC\x97\xAC\xEC\xA4\x8D\xEB\x8B\x88\xEB\x8B\xA4. \xEC\xB2\xB4\xEB\xA0\xA5\xC2\xB7\xEB\xB0\xA9\xEC\x96\xB4\xEA\xB5\xAC \xEB\xA7\x89\xEB\x8C\x80\xEC\xB2\x98\xEB\xB0\x8D \xEC\x9E\x91\xEC\x95\x84\xEC\x84\x9C \xEC\x95\x88 \xEB\xB3\xB4\xEC\x9D\xB4\xEB\x8A\x94 \xEA\xB2\x83\xEC\x9D\x84 \xED\x81\xAC\xEA\xB2\x8C \xEB\x9D\x84\xEC\x9A\xB8 \xEB\x95\x8C \xEC\x93\xB0\xEC\x84\xB8\xEC\x9A\x94."); // "화면의 한 곳을 그대로 크게 보여줍니다. 체력·방어구 막대처럼 작아서 안 보이는 것을 크게 띄울 때 쓰세요."
	}
	ImGui::Spacing();

	// SHERBET: 일일 알림 — 정한 시각에 화면에 한 줄 띄운다(상시 표시 아님, 몇 초 뒤 사라진다).
	// FiveM RP 는 알트탭이 곧 죽음이라 게임 밖 알림은 아무도 못 본다.
	if (ImGui::CollapsingHeader(ICON_FK_BELL "  " "\xEC\x9D\xBC\xEC\x9D\xBC \xEC\x95\x8C\xEB\xA6\xBC")) // "일일 알림"
	{
		if (ImGui::Checkbox("\xEC\x95\x8C\xEB\xA6\xBC \xEC\x82\xAC\xEC\x9A\xA9##alarm", &_sherbet_alarm_on)) // "알림 사용"
			modified = true;

		ImGui::SetNextItemWidth(ImMax(90.0f, 4.0f * ImGui::GetFontSize()));
		if (ImGui::DragInt("##alarmh", &_sherbet_alarm_hour, 0.1f, 0, 23, "%02d\xEC\x8B\x9C")) // "..시"
			modified = true;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImMax(90.0f, 4.0f * ImGui::GetFontSize()));
		if (ImGui::DragInt("##alarmm", &_sherbet_alarm_min, 0.1f, 0, 59, "%02d\xEB\xB6\x84")) // "..분"
			modified = true;
		sherbet::alarm::clamp_time(_sherbet_alarm_hour, _sherbet_alarm_min);

		ImGui::SetNextItemWidth(ImMax(220.0f, 9.0f * ImGui::GetFontSize()));
		char alarm_buf[128];
		std::snprintf(alarm_buf, sizeof(alarm_buf), "%s", _sherbet_alarm_text.c_str());
		if (ImGui::InputTextWithHint("##alarmtext",
				"\xEC\x9D\xBC\xEC\x9D\xBC\xEB\xB3\xB4\xEC\x83\x81\xEC\x9D\x84 \xEB\xB0\x9B\xEC\x95\x84\xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94!", // 기본 문구를 힌트로
				alarm_buf, sizeof(alarm_buf)))
		{
			_sherbet_alarm_text = alarm_buf;
			modified = true;
		}

		// ⚠️ 테스트 버튼 — 이게 없으면 판매자도 구매자도 **밤 11시 50분까지 기다려야만**
		//    알림이 어떻게 생겼는지 볼 수 있다. 실제 발화와 **같은 길**(remaining)로 띄운다.
		ImGui::SameLine();
		if (sherbet::pill_button(ICON_FK_BELL "  " "\xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8", false)) // "테스트"
			_sherbet_alarm.remaining = ImMax(1.0f, _sherbet_alarm_secs);

		sherbet_hint("\xEC\xA0\x95\xED\x95\x9C \xEC\x8B\x9C\xEA\xB0\x81\xEC\x97\x90 \xED\x99\x94\xEB\xA9\xB4 \xEC\x9C\x84\xEC\xAA\xBD\xEC\x97\x90 \xEC\x9E\xA0\xEC\x8B\x9C \xEB\x9C\xB0\xEB\x8B\xA4\xEA\xB0\x80 \xEC\x82\xAC\xEB\x9D\xBC\xEC\xA0\xB8\xEC\x9A\x94. \xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEB\x8B\xAB\xEC\x95\x84 \xEB\x91\x94 \xEC\xA4\x91\xEC\x97\x90\xEB\x8F\x84 \xEB\xB3\xB4\xEC\x97\xAC\xEC\x9A\x94."); // "정한 시각에 화면 위쪽에 잠시 떴다가 사라져요. 오버레이를 닫아 둔 중에도 보여요."
	}
	ImGui::Spacing();

	// SHERBET: 잠금 화면 미리보기 — 유료 기능이 잠겼을 때의 판매 화면을 눈으로 확인하는 스위치.
	// ⚠️ 이게 없으면 판매자는 **제일 중요한 화면을 한 번도 볼 수 없다.** sherbet::has_feature()
	//    는 auth 가 꺼진 개발/데모 빌드에서 무조건 true 이고, 온라인 인증 빌드라도 판매자
	//    본인은 역할을 전부 갖고 있어 언제나 열린 화면만 보게 된다.
	// 켜면 유료 기능은 권한과 무관하게 잠긴 것으로 취급되고(판매 카드 표시 + 기록/측정 정지),
	// 끄면 즉시 원래대로 돌아온다. 구매 내역과는 아무 관계가 없다.
	if (ImGui::CollapsingHeader(ICON_FK_LOCK "  " "\xEC\x9E\xA0\xEA\xB8\x88 \xED\x99\x94\xEB\xA9\xB4 \xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0")) // "잠금 화면 미리보기"
	{
		if (ImGui::Checkbox("\xEC\x9E\xA0\xEA\xB8\x88 \xED\x99\x94\xEB\xA9\xB4 \xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0##lockpreview", &_sherbet_lock_preview)) // "잠금 화면 미리보기"
			modified = true;
		sherbet_hint("\xEC\x9C\xA0\xEB\xA3\x8C \xEA\xB8\xB0\xEB\x8A\xA5\xEC\x9D\xB4 \xEC\x9E\xA0\xEA\xB2\xA8 \xEC\x9E\x88\xEC\x9D\x84 \xEB\x95\x8C \xEC\x96\xB4\xEB\x96\xBB\xEA\xB2\x8C \xEB\xB3\xB4\xEC\x9D\xB4\xEB\x8A\x94\xEC\xA7\x80 \xED\x99\x95\xEC\x9D\xB8\xED\x95\x98\xEB\x8A\x94 \xEC\x8A\xA4\xEC\x9C\x84\xEC\xB9\x98\xEC\x98\x88\xEC\x9A\x94. \xEC\xBC\x9C\xEB\x8F\x84 \xEA\xB5\xAC\xEB\xA7\xA4 \xEB\x82\xB4\xEC\x97\xAD\xEC\x97\x90\xEB\x8A\x94 \xEC\x98\x81\xED\x96\xA5\xEC\x9D\xB4 \xEC\x97\x86\xEA\xB3\xA0, \xEB\x81\x84\xEB\xA9\xB4 \xEB\xB0\x94\xEB\xA1\x9C \xEC\x9B\x90\xEB\x9E\x98\xEB\x8C\x80\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEC\x98\xB5\xEB\x8B\x88\xEB\x8B\xA4."); // "유료 기능이 잠겨 있을 때 어떻게 보이는지 확인하는 스위치예요. 켜도 구매 내역에는 영향이 없고, 끄면 바로 원래대로 돌아옵니다."
		if (_sherbet_lock_preview)
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", ICON_FK_WARNING "  \xEC\xA7\x80\xEA\xB8\x88 \xEC\x9E\xA0\xEA\xB8\x88 \xED\x99\x94\xEB\xA9\xB4 \xEB\xAF\xB8\xEB\xA6\xAC\xEB\xB3\xB4\xEA\xB8\xB0\xEA\xB0\x80 \xEC\xBC\x9C\xEC\xA0\xB8 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\x9C\xA0\xEB\xA3\x8C \xEA\xB8\xB0\xEB\x8A\xA5\xEC\x9D\xB4 \xEC\x9E\xA0\xEA\xB8\xB4 \xEA\xB2\x83\xEC\xB2\x98\xEB\x9F\xBC \xEB\xB3\xB4\xEC\x9D\xB4\xEA\xB3\xA0 \xEC\x8B\xA4\xEC\xA0\x9C\xEB\xA1\x9C \xEB\x8F\x99\xEC\x9E\x91\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4."); // "지금 잠금 화면 미리보기가 켜져 있어요 — 유료 기능이 잠긴 것처럼 보이고 실제로 동작하지 않습니다."
		ImGui::Spacing();
	}

	// SHERBET: 커스텀 배경 이미지 — 오버레이 배경을 내 사진으로.
	// ⚠️ 예전엔 has_feature 로 **헤더째 숨겼다.** 그래서 안 산 사람 화면에는 이 기능이
	//    아예 존재하지 않았고, 디스코드 역할까지 만들어 둔 상품을 아무도 볼 수 없었다.
	//    (sherbet_paid.hpp 머리말: "안 보이는 기능은 한 개도 안 팔린다.")
	//    이제 헤더는 **언제나** 보이고, 안쪽이 진열대냐 진짜 UI 냐만 갈린다 —
	//    스프레이/최적화 탭이 이미 쓰던 방식과 같다.
	if (ImGui::CollapsingHeader("\xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEB\xB0\xB0\xEA\xB2\xBD")) // "커스텀 배경"
	{
	if (!sherbet_feature_unlocked("custompicture"))
	{
		if (const sherbet::paid::feature *const bg_f = sherbet::paid::find("custompicture"))
			sherbet_draw_bg_lock_card(*bg_f);
		ImGui::Spacing();
	}
	else
	{
		bool bg_changed = false;
		bg_changed |= ImGui::Checkbox("\xEB\xB0\xB0\xEA\xB2\xBD \xEC\xBC\x9C\xEA\xB8\xB0", &_sherbet_bg_on); // "배경 켜기"
		if (bg_changed)
			_sherbet_bg_dirty = true;

		const std::filesystem::path bg_dir = _config_path.parent_path() / L"Sherbet-Backgrounds";
		static std::vector<std::string> bg_files;
		static bool bg_need_scan = true;
		if (bg_need_scan)
		{
			bg_need_scan = false;
			bg_files.clear();
			std::error_code ec;
			std::filesystem::create_directories(bg_dir, ec); // 없으면 폴더 생성
			for (std::filesystem::directory_iterator it(bg_dir, ec), end; it != end; it.increment(ec))
			{
				if (!it->is_regular_file(ec)) continue;
				std::string e = it->path().extension().u8string();
				std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga")
					bg_files.push_back(it->path().filename().u8string());
			}
		}

		if (ImGui::BeginCombo("\xEC\x9D\xB4\xEB\xAF\xB8\xEC\xA7\x80 \xED\x8C\x8C\xEC\x9D\xBC##bg", _sherbet_bg_file.empty() ? "-" : _sherbet_bg_file.c_str())) // "이미지 파일"
		{
			for (const std::string &f : bg_files)
				if (ImGui::Selectable(f.c_str(), f == _sherbet_bg_file))
				{ _sherbet_bg_file = f; _sherbet_bg_dirty = true; bg_changed = true; }
			ImGui::EndCombo();
		}
		// 실패했으면 안내 대신 이유를 말한다 — 예전엔 사진을 골라도 아무 일이 안 일어나고
		// 이유가 어디에도 없었다(stb_image 는 프로그레시브 JPEG 를 못 푼다).
		if (_sherbet_bg_load == 2)
			ImGui::TextColored(sherbet::status_color(sherbet::status::bad), "%s",
				ICON_FK_CANCEL "  " "\xEC\x9D\xB4 \xEC\x82\xAC\xEC\xA7\x84\xEC\x9D\x84 \xEC\x9D\xBD\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 PNG \xEB\x82\x98 \xEC\x9D\xBC\xEB\xB0\x98 JPG \xEB\xA1\x9C \xEB\x8B\xA4\xEC\x8B\x9C \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\xB4 \xEB\xB3\xB4\xEC\x84\xB8\xEC\x9A\x94"); // "이 사진을 읽지 못했어요 — PNG 나 일반 JPG 로 다시 저장해 보세요"
		else
			ImGui::TextDisabled("%s", "\xEC\x82\xAC\xEC\xA7\x84\xEC\x9D\x84 \xED\x8F\xB4\xEB\x8D\x94\xEC\x97\x90 \xEB\x84\xA3\xEA\xB3\xA0 \xEB\xAA\xA9\xEB\xA1\x9D\xEC\x97\x90\xEC\x84\x9C \xEA\xB3\xA0\xEB\xA5\xB4\xEC\x84\xB8\xEC\x9A\x94"); // 안내

		if (ImGui::Button("\xED\x8F\xB4\xEB\x8D\x94 \xEC\x97\xB4\xEA\xB8\xB0##bg")) // "폴더 열기"
		{
			std::error_code ec;
			std::filesystem::create_directories(bg_dir, ec);
			utils::open_explorer(bg_dir);
			bg_need_scan = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("\xEC\x83\x88\xEB\xA1\x9C\xEA\xB3\xA0\xEC\xB9\xA8##bg")) // "새로고침"
			bg_need_scan = true;

		bg_changed |= ImGui::SliderFloat("\xED\x88\xAC\xEB\xAA\x85\xEB\x8F\x84##bg", &_sherbet_bg_opacity, 0.0f, 1.0f, "%.2f"); // "투명도"
		bg_changed |= ImGui::SliderFloat("\xEC\x96\xB4\xEB\x91\xA1\xEA\xB2\x8C(\xEA\xB0\x80\xEB\x8F\x85\xEC\x84\xB1)", &_sherbet_bg_dim, 0.0f, 1.0f, "%.2f"); // "어둡게(가독성)"

		if (bg_changed)
			modified = true;
		ImGui::Spacing();
	}
	}

	if (ImGui::CollapsingHeader(_("General"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_input != nullptr)
		{
			std::string input_processing_mode_items = _(
				"Pass on all input\n"
				"Block input when cursor is on overlay\n"
				"Block all input when overlay is visible\n");
			std::replace(input_processing_mode_items.begin(), input_processing_mode_items.end(), '\n', '\0');
			modified |= ImGui::Combo(_("Input processing"), reinterpret_cast<int *>(&_input_processing_mode), input_processing_mode_items.c_str());

			modified |= imgui::key_input_box(_("Overlay key"), _overlay_key_data, *_input);

			modified |= imgui::key_input_box(_("Effect toggle key"), _effects_key_data, *_input);
			modified |= imgui::key_input_box(_("Effect reload key"), _reload_key_data, *_input);

			modified |= imgui::key_input_box(_("Previous preset key"), _prev_preset_key_data, *_input);
			modified |= imgui::key_input_box(_("Next preset key"), _next_preset_key_data, *_input);

			modified |= ImGui::SliderInt(_("Preset transition duration"), reinterpret_cast<int *>(&_preset_transition_duration), 0, 10 * 1000);
			ImGui::SetItemTooltip(_(
				"Make a smooth transition when switching presets, but only for floating point values.\n"
				"Recommended for multiple presets that contain the same effects, otherwise set this to zero.\n"
				"Values are in milliseconds."));

			ImGui::Spacing();
		}

		modified |= imgui::file_input_box(_("Start-up preset"), nullptr, _startup_preset_path, _file_selection_path, { L".ini", L".txt" });
		ImGui::SetItemTooltip(_("When not empty, reset the current preset to this file during reloads."));

		ImGui::Spacing();

		modified |= imgui::path_list(_("Effect search paths"), _effect_search_paths, _file_selection_path, g_reshade_base_path);
		ImGui::SetItemTooltip(_("List of directory paths to be searched for effect files (.fx).\nPaths that end in \"\\**\" are searched recursively."));
		modified |= imgui::path_list(_("Texture search paths"), _texture_search_paths, _file_selection_path, g_reshade_base_path);
		ImGui::SetItemTooltip(_("List of directory paths to be searched for image files used as source for textures.\nPaths that end in \"\\**\" are searched recursively."));

		if (ImGui::Checkbox(_("Load only enabled effects"), &_effect_load_skipping))
		{
			modified = true;

			// Force load all effects in case some where skipped after load skipping was disabled
			reload_effects(!_effect_load_skipping);
		}

		if (ImGui::Button(_("Clear effect cache"), ImVec2(ImGui::CalcItemWidth(), 0)))
			clear_effect_cache();
		ImGui::SetItemTooltip(_("Clear effect cache located in \"%s\"."), _effect_cache_path.u8string().c_str());
	}

	if (ImGui::CollapsingHeader(_("Screenshots"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_input != nullptr)
		{
			modified |= imgui::key_input_box(_("Screenshot key"), _screenshot_key_data, *_input);
		}

		modified |= imgui::directory_input_box(_("Screenshot path"), _screenshot_path, _file_selection_path);

		char name[260];
		name[_screenshot_name.copy(name, sizeof(name) - 1)] = '\0';
		if (ImGui::InputText(_("Screenshot name"), name, sizeof(name), ImGuiInputTextFlags_CallbackCharFilter, &is_invalid_path_element))
		{
			modified = true;
			_screenshot_name = name;

			// Strip any leading slashes, to avoid starting at drive root, rather than the screenshot path
			_screenshot_name = trim(_screenshot_name, " \t\\");
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip(_(
				"Macros you can add that are resolved during saving:\n"
				"  %%AppName%%         Name of the application (%s)\n"
				"  %%PresetName%%      File name without extension of the current preset file (%s)\n"
				"  %%BeforeAfter%%     Term describing the moment the screenshot was taken ('Before', 'After' or 'Overlay')\n"
				"  %%Date%%            Current date in format '%s'\n"
				"  %%DateYear%%        Year component of current date\n"
				"  %%DateMonth%%       Month component of current date\n"
				"  %%DateDay%%         Day component of current date\n"
				"  %%Time%%            Current time in format '%s'\n"
				"  %%TimeHour%%        Hour component of current time\n"
				"  %%TimeMinute%%      Minute component of current time\n"
				"  %%TimeSecond%%      Second component of current time\n"
				"  %%TimeMS%%          Milliseconds fraction of current time\n"
				"  %%Count%%           Number of screenshots taken this session\n"),
				g_target_executable_path.stem().u8string().c_str(),
				_current_preset_path.stem().u8string().c_str(),
				"yyyy-MM-dd",
				"HH-mm-ss");
		}

		// HDR screenshots have no alpha channel
		if (_back_buffer_format == api::format::r16g16b16a16_float || _back_buffer_color_space == api::color_space::hdr10_pq)
		{
			int hdr_screenshot_format = _screenshot_format == 3 ? 1 : 0;
			if (ImGui::Combo(_("Screenshot format"), reinterpret_cast<int *>(&hdr_screenshot_format), "Portable Network Graphics (*.png)\0JPEG XL Lossless (*.jxl)\0"))
			{
				_screenshot_format = hdr_screenshot_format == 1 ? 3 : 1;
				modified = true;
			}
		}
		else
		{
			modified |= ImGui::Combo(_("Screenshot format"), reinterpret_cast<int *>(&_screenshot_format), "Bitmap (*.bmp)\0Portable Network Graphics (*.png)\0JPEG (*.jpeg)\0JPEG XL Lossless (*.jxl)\0");

			if (_screenshot_format == 2)
				modified |= ImGui::SliderInt(_("JPEG quality"), reinterpret_cast<int *>(&_screenshot_jpeg_quality), 1, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
			else
				modified |= ImGui::Checkbox(_("Clear alpha channel"), &_screenshot_clear_alpha);
		}

		modified |= ImGui::Checkbox(_("Save current preset file"), &_screenshot_include_preset);
		modified |= ImGui::Checkbox(_("Save before and after images"), &_screenshot_save_before);
		modified |= ImGui::Checkbox(_("Save separate image with the overlay visible"), &_screenshot_save_gui);

		modified |= imgui::file_input_box(_("Screenshot sound"), "sound.wav", _screenshot_sound_path, _file_selection_path, { L".wav" });
		ImGui::SetItemTooltip(_("Audio file that is played when taking a screenshot."));

		modified |= imgui::file_input_box(_("Post-save command"), "command.bat", _screenshot_post_save_command, _file_selection_path, { L".exe", L".bat", L".cmd", L".ps1", L".py" });
		ImGui::SetItemTooltip(_(
			"Executable or script that is called after saving a screenshot.\n"
			"This can be used to perform additional processing on the image (e.g. compressing it with an image optimizer)."));

		char arguments[260];
		arguments[_screenshot_post_save_command_arguments.copy(arguments, sizeof(arguments) - 1)] = '\0';
		if (ImGui::InputText(_("Post-save command arguments"), arguments, sizeof(arguments)))
		{
			modified = true;
			_screenshot_post_save_command_arguments = arguments;
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			const char *extension = "";
			switch (_screenshot_format)
			{
			case 0:
				extension = ".bmp";
				break;
			case 1:
				extension = ".png";
				break;
			case 2:
				extension = ".jpg";
				break;
			case 3:
				extension = ".jxl";
				break;
			}

			ImGui::SetTooltip(_(
				"Macros you can add that are resolved during command execution:\n"
				"  %%AppName%%         Name of the application (%s)\n"
				"  %%PresetName%%      File name without extension of the current preset file (%s)\n"
				"  %%BeforeAfter%%     Term describing the moment the screenshot was taken ('Before', 'After' or 'Overlay')\n"
				"  %%Date%%            Current date in format '%s'\n"
				"  %%DateYear%%        Year component of current date\n"
				"  %%DateMonth%%       Month component of current date\n"
				"  %%DateDay%%         Day component of current date\n"
				"  %%Time%%            Current time in format '%s'\n"
				"  %%TimeHour%%        Hour component of current time\n"
				"  %%TimeMinute%%      Minute component of current time\n"
				"  %%TimeSecond%%      Second component of current time\n"
				"  %%TimeMS%%          Milliseconds fraction of current time\n"
				"  %%TargetPath%%      Full path to the screenshot file (%s)\n"
				"  %%TargetDir%%       Full path to the screenshot directory (%s)\n"
				"  %%TargetFileName%%  File name of the screenshot file (%s)\n"
				"  %%TargetExt%%       File extension of the screenshot file (%s)\n"
				"  %%TargetName%%      File name without extension of the screenshot file (%s)\n"
				"  %%Count%%           Number of screenshots taken this session\n"),
				g_target_executable_path.stem().u8string().c_str(),
				_current_preset_path.stem().u8string().c_str(),
				"yyyy-MM-dd",
				"HH-mm-ss",
				(_screenshot_path / (_screenshot_name + extension)).u8string().c_str(),
				_screenshot_path.u8string().c_str(),
				(_screenshot_name + extension).c_str(),
				extension,
				_screenshot_name.c_str());
		}

		modified |= imgui::directory_input_box(_("Post-save command working directory"), _screenshot_post_save_command_working_directory, _file_selection_path);
		modified |= ImGui::Checkbox(_("Hide post-save command window"), &_screenshot_post_save_command_hide_window);
	}

	if (ImGui::CollapsingHeader(_("Overlay & Styling"), ImGuiTreeNodeFlags_DefaultOpen))
	{
#if RESHADE_LOCALIZATION
		{
			std::vector<std::string> languages = resources::get_languages();

			int lang_index = 0;
			if (const auto it = std::find(languages.begin(), languages.end(), _selected_language); it != languages.end())
				lang_index = static_cast<int>(std::distance(languages.begin(), it) + 1);

			if (ImGui::Combo(_("Language"), &lang_index,
					[](void *data, int idx) -> const char * {
						return idx == 0 ? "System Default" : (*static_cast<const std::vector<std::string> *>(data))[idx - 1].c_str();
					}, &languages, static_cast<int>(languages.size() + 1)))
			{
				modified = true;
				if (lang_index == 0)
					_selected_language.clear();
				else
					_selected_language = languages[lang_index - 1];

				// Rebuild font atlas in case language needs a special font or glyph range
				_rebuild_font_atlas = true;
			}
		}
#endif

		if (ImGui::Button(_("Restart tutorial"), ImVec2(ImGui::CalcItemWidth(), 0)))
			_tutorial_index = 0;

		modified |= ImGui::Checkbox(_("Show screenshot message"), &_show_screenshot_message);

		ImGui::BeginDisabled(_preset_transition_duration == 0);
		modified |= ImGui::Checkbox(_("Show preset transition message"), &_show_preset_transition_message);
		ImGui::EndDisabled();
		if (_preset_transition_duration == 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
			ImGui::SetTooltip(_("Preset transition duration has to be non-zero for the preset transition message to show up."));

		if (_effect_load_skipping)
			modified |= ImGui::Checkbox(_("Show \"Force load all effects\" button"), &_show_force_load_effects_button);

		modified |= ImGui::Checkbox(_("Group effect files with tabs instead of a tree"), &_variable_editor_tabs);

		#pragma region Style
		// SHERBET: 스타일은 **테마가 소유한다.** draw_gui() 가 매 프레임 오버레이를 그리기 전에
		// sherbet::apply_style() 로 style.Colors 전체와 라운딩 7종을 덮어쓰므로, 아래 컨트롤들은
		// 값을 써도 다음 프레임 시작에 되돌려진다 — 콤보를 골라도 색이 안 바뀌고, 커스텀 색상
		// 편집기도 매 프레임 테마 색으로 리셋된다. **안 먹는 컨트롤을 노출하지 않는다.**
		// ⚠️ 아래 #endif 는 「Text editor style」 리전(#pragma region Editor Style) **앞**에서 닫는다.
		//    그쪽은 _editor_palette 를 채우는 살아 있는 코드고, load_custom_style() 정의도
		//    남겨 둬야 한다(_style_index 3/4 가 에디터 팔레트에 쓰인다).
		//    테마를 고르는 곳은 「마켓」 탭이다.
#if 0
		if (ImGui::Combo(_("Global style"), &_style_index, "Dark\0Light\0Default\0Custom Simple\0Custom Advanced\0Solarized Dark\0Solarized Light\0"))
		{
			modified = true;
			load_custom_style();
		}

		if (_style_index == 3) // Custom Simple
		{
			ImVec4 *const colors = _imgui_context->Style.Colors;

			if (ImGui::BeginChild("##colors", ImVec2(0, 105), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar))
			{
				ImGui::PushItemWidth(-160);
				modified_custom_style |= ImGui::ColorEdit3("Background", &colors[ImGuiCol_WindowBg].x);
				modified_custom_style |= ImGui::ColorEdit3("ItemBackground", &colors[ImGuiCol_FrameBg].x);
				modified_custom_style |= ImGui::ColorEdit3("Text", &colors[ImGuiCol_Text].x);
				modified_custom_style |= ImGui::ColorEdit3("ActiveItem", &colors[ImGuiCol_ButtonActive].x);
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();

			// Change all colors using the above as base
			if (modified_custom_style)
			{
				colors[ImGuiCol_PopupBg] = colors[ImGuiCol_WindowBg]; colors[ImGuiCol_PopupBg].w = 0.92f;

				colors[ImGuiCol_ChildBg] = colors[ImGuiCol_FrameBg]; colors[ImGuiCol_ChildBg].w = 0.00f;
				colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_FrameBg]; colors[ImGuiCol_MenuBarBg].w = 0.57f;
				colors[ImGuiCol_ScrollbarBg] = colors[ImGuiCol_FrameBg]; colors[ImGuiCol_ScrollbarBg].w = 1.00f;

				colors[ImGuiCol_TextDisabled] = colors[ImGuiCol_Text]; colors[ImGuiCol_TextDisabled].w = 0.58f;
				colors[ImGuiCol_Border] = colors[ImGuiCol_Text]; colors[ImGuiCol_Border].w = 0.30f;
				colors[ImGuiCol_Separator] = colors[ImGuiCol_Text]; colors[ImGuiCol_Separator].w = 0.32f;
				colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_Text]; colors[ImGuiCol_SeparatorHovered].w = 0.78f;
				colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_Text]; colors[ImGuiCol_SeparatorActive].w = 1.00f;
				colors[ImGuiCol_PlotLines] = colors[ImGuiCol_Text]; colors[ImGuiCol_PlotLines].w = 0.63f;
				colors[ImGuiCol_PlotHistogram] = colors[ImGuiCol_Text]; colors[ImGuiCol_PlotHistogram].w = 0.63f;

				colors[ImGuiCol_FrameBgHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_FrameBgHovered].w = 0.68f;
				colors[ImGuiCol_FrameBgActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_FrameBgActive].w = 1.00f;
				colors[ImGuiCol_TitleBg] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_TitleBg].w = 0.45f;
				colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_TitleBgCollapsed].w = 0.35f;
				colors[ImGuiCol_TitleBgActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_TitleBgActive].w = 0.58f;
				colors[ImGuiCol_ScrollbarGrab] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ScrollbarGrab].w = 0.31f;
				colors[ImGuiCol_ScrollbarGrabHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ScrollbarGrabHovered].w = 0.78f;
				colors[ImGuiCol_ScrollbarGrabActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ScrollbarGrabActive].w = 1.00f;
				colors[ImGuiCol_CheckMark] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_CheckMark].w = 0.80f;
				colors[ImGuiCol_SliderGrab] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_SliderGrab].w = 0.24f;
				colors[ImGuiCol_SliderGrabActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_SliderGrabActive].w = 1.00f;
				colors[ImGuiCol_Button] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_Button].w = 0.44f;
				colors[ImGuiCol_ButtonHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ButtonHovered].w = 0.86f;
				colors[ImGuiCol_Header] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_Header].w = 0.76f;
				colors[ImGuiCol_HeaderHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_HeaderHovered].w = 0.86f;
				colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_HeaderActive].w = 1.00f;
				colors[ImGuiCol_ResizeGrip] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ResizeGrip].w = 0.20f;
				colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ResizeGripHovered].w = 0.78f;
				colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_ResizeGripActive].w = 1.00f;
				colors[ImGuiCol_PlotLinesHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_PlotLinesHovered].w = 1.00f;
				colors[ImGuiCol_PlotHistogramHovered] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_PlotHistogramHovered].w = 1.00f;
				colors[ImGuiCol_TextSelectedBg] = colors[ImGuiCol_ButtonActive]; colors[ImGuiCol_TextSelectedBg].w = 0.43f;

				colors[ImGuiCol_Tab] = colors[ImGuiCol_Button];
				colors[ImGuiCol_TabSelected] = colors[ImGuiCol_ButtonActive];
				colors[ImGuiCol_TabSelectedOverline] = colors[ImGuiCol_TabSelected];
				colors[ImGuiCol_TabHovered] = colors[ImGuiCol_ButtonHovered];
				colors[ImGuiCol_TabDimmed] = ImLerp(colors[ImGuiCol_Tab], colors[ImGuiCol_TitleBg], 0.80f);
				colors[ImGuiCol_TabDimmedSelected] = ImLerp(colors[ImGuiCol_TabSelected], colors[ImGuiCol_TitleBg], 0.40f);
				colors[ImGuiCol_TabDimmedSelectedOverline] = colors[ImGuiCol_TabDimmedSelected];
				colors[ImGuiCol_DockingPreview] = colors[ImGuiCol_Header] * ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
				colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
			}
		}
		if (_style_index == 4) // Custom Advanced
		{
			if (ImGui::BeginChild("##colors", ImVec2(0, 300), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar))
			{
				ImGui::PushItemWidth(-160);
				for (ImGuiCol i = 0; i < ImGuiCol_COUNT; i++)
				{
					ImGui::PushID(i);
					modified_custom_style |= ImGui::ColorEdit4("##color", &_imgui_context->Style.Colors[i].x, ImGuiColorEditFlags_AlphaBar);
					ImGui::SameLine();
					ImGui::TextUnformatted(ImGui::GetStyleColorName(i));
					ImGui::PopID();
				}
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();
		}
#endif // SHERBET: 위 스타일 컨트롤은 apply_style() 이 매 프레임 덮어써서 동작하지 않는다
		#pragma endregion

		#pragma region Editor Style
		if (ImGui::Combo(_("Text editor style"), &_editor_style_index, "Dark\0Light\0Custom\0Solarized Dark\0Solarized Light\0"))
		{
			modified = true;
			load_custom_style();
		}

		if (_editor_style_index == 2)
		{
			if (ImGui::BeginChild("##editor_colors", ImVec2(0, 300), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_AlwaysVerticalScrollbar))
			{
				ImGui::PushItemWidth(-160);
				for (ImGuiCol i = 0; i < imgui::code_editor::color_palette_max; i++)
				{
					ImVec4 color = ImGui::ColorConvertU32ToFloat4(_editor_palette[i]);
					ImGui::PushID(i);
					modified_custom_style |= ImGui::ColorEdit4("##editor_color", &color.x, ImGuiColorEditFlags_AlphaBar);
					ImGui::SameLine();
					ImGui::TextUnformatted(imgui::code_editor::get_palette_color_name(i));
					ImGui::PopID();
					_editor_palette[i] = ImGui::ColorConvertFloat4ToU32(color);
				}
				ImGui::PopItemWidth();
			}
			ImGui::EndChild();
		}
		#pragma endregion

		if (imgui::font_input_box(_("Global font"), _default_font_path.empty() ? "ProggyClean.ttf" : _default_font_path.u8string().c_str(), _font_path, _file_selection_path, _font_size))
		{
			modified = true;
			_rebuild_font_atlas = true;
		}

		if (_imgui_context->IO.Fonts->Fonts[0]->Sources.Size > 2 && // Latin font + main font + icon font
			imgui::font_input_box(_("Latin font"), "ProggyClean.ttf", _latin_font_path, _file_selection_path, _font_size))
		{
			modified = true;
			_rebuild_font_atlas = true;
		}

		if (imgui::font_input_box(_("Text editor font"), _default_editor_font_path.empty() ? "ProggyClean.ttf" : _default_editor_font_path.u8string().c_str(), _editor_font_path, _file_selection_path, _editor_font_size))
		{
			modified = true;
			_rebuild_font_atlas = true;
		}

		if (float &alpha = _imgui_context->Style.Alpha; ImGui::SliderFloat(_("Global alpha"), &alpha, 0.1f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		{
			// Prevent user from setting alpha to zero
			alpha = std::max(alpha, 0.1f);
			modified = true;
		}

		// Only show on possible HDR swap chains
		if (_back_buffer_format == api::format::r16g16b16a16_float || _back_buffer_color_space == api::color_space::hdr10_pq)
		{
			if (ImGui::SliderFloat(_("HDR overlay brightness"), &_hdr_overlay_brightness, 20.f, 400.f, "%.0f nits", ImGuiSliderFlags_AlwaysClamp))
				modified = true;

			if (ImGui::Combo(_("Overlay color space"), reinterpret_cast<int *>(&_hdr_overlay_overwrite_color_space), "Auto\0SDR\0scRGB\0HDR10\0HLG\0"))
				modified = true;
		}

		// SHERBET: 「Frame rounding」 슬라이더 제거. apply_style() 이 매 프레임 style.FrameRounding
		// 을 10 으로 되돌려 놓기 때문에, 손잡이를 끌면 그 자리에서 즉시 되튀었다(값이 저장조차
		// 되지 않는다). 라운딩은 테마가 정한다. 위 「Global style」 리전과 같은 이유다.
#if 0
		if (float &rounding = _imgui_context->Style.FrameRounding; ImGui::SliderFloat(_("Frame rounding"), &rounding, 0.0f, 12.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
		{
			// Apply the same rounding to everything
			_imgui_context->Style.WindowRounding = rounding;
			_imgui_context->Style.ChildRounding = rounding;
			_imgui_context->Style.PopupRounding = rounding;
			_imgui_context->Style.ScrollbarRounding = rounding;
			_imgui_context->Style.GrabRounding = rounding;
			_imgui_context->Style.TabRounding = rounding;
			modified = true;
		}
#endif

		if (!_is_vr)
		{
			ImGui::Spacing();

			// SHERBET: OSD 표시를 켬/끔 2단계로 단순화(예전 3단계 tristate 제거 — 손님 혼동 방지)
			auto osd_toggle = [&](const char *label, unsigned int &v) {
				bool on = v != 0;
				if (ImGui::Checkbox(label, &on)) { v = on ? 1u : 0u; modified = true; }
			};
			ImGui::BeginGroup();
			osd_toggle(_("Show clock"), _show_clock);
			ImGui::SameLine(0, 10);
			osd_toggle(_("Show FPS"), _show_fps);
			ImGui::SameLine(0, 10);
			osd_toggle(_("Show frame time"), _show_frametime);
			osd_toggle(_("Show preset name"), _show_preset_name);
			ImGui::EndGroup();
			ImGui::SetItemTooltip(_("Check to always show on screen."));

			if (_input != nullptr)
			{
				modified |= imgui::key_input_box(_("FPS key"), _fps_key_data, *_input);
				modified |= imgui::key_input_box(_("Frame time key"), _frametime_key_data, *_input);
			}

			if (_show_clock)
				modified |= ImGui::Combo(_("Clock format"), reinterpret_cast<int *>(&_clock_format), "HH:mm\0HH:mm:ss\0yyyy-MM-dd HH:mm:ss\0");

			modified |= ImGui::SliderFloat(_("OSD text size"), &_fps_scale, 0.2f, 2.5f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			modified |= ImGui::ColorEdit4(_("OSD text color"), _fps_col, ImGuiColorEditFlags_AlphaBar);

			modified |= ImGui::SliderFloat("OSD X", &_sherbet_osd_x, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			modified |= ImGui::SliderFloat("OSD Y", &_sherbet_osd_y, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			modified |= ImGui::Checkbox("OSD \xEA\xB0\x80\xEB\xA1\x9C\xEB\xA1\x9C \xEB\xB0\xB0\xEC\xB9\x98", &_sherbet_osd_horizontal); // "OSD 가로로 배치"
			ImGui::SetItemTooltip("\xEC\x8B\x9C\xEA\xB3\x84\xC2\xB7" "FPS" "\xC2\xB7\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84\xED\x83\x80\xEC\x9E\x84\xC2\xB7\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B\xEB\xAA\x85\xEC\x9D\x84 \xEC\x84\xB8\xEB\xA1\x9C\xEA\xB0\x80 \xEC\x95\x84\xEB\x8B\x8C \xEA\xB0\x80\xEB\xA1\x9C \xED\x95\x9C \xEC\xA4\x84\xEB\xA1\x9C \xED\x91\x9C\xEC\x8B\x9C"); // 시계·FPS·프레임타임·프리셋명을 세로가 아닌 가로 한 줄로 표시
		}
	}

	if (modified)
		save_config();
	if (modified_custom_style)
		save_custom_style();
}
void reshade::runtime::draw_gui_statistics()
{
	unsigned int gpu_digits = 1;
	unsigned int cpu_digits = 1;
	uint64_t post_processing_time_cpu = 0;
	uint64_t post_processing_time_gpu = 0;

	if (!is_loading() && _effects_enabled)
	{
		for (const technique &tech : _techniques)
		{
			cpu_digits = std::max(cpu_digits, tech.average_cpu_duration >= 100'000'000 ? 3u : tech.average_cpu_duration >= 10'000'000 ? 2u : 1u);
			post_processing_time_cpu += tech.average_cpu_duration;
			gpu_digits = std::max(gpu_digits, tech.average_gpu_duration >= 100'000'000 ? 3u : tech.average_gpu_duration >= 10'000'000 ? 2u : 1u);
			post_processing_time_gpu += tech.average_gpu_duration;
		}
	}

	if (ImGui::CollapsingHeader(_("General"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		_gather_gpu_statistics = true;

		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		ImGui::PlotLines("##framerate",
			_imgui_context->FramerateSecPerFrame, static_cast<int>(std::size(_imgui_context->FramerateSecPerFrame)),
			_imgui_context->FramerateSecPerFrameIdx,
			nullptr,
			_imgui_context->FramerateSecPerFrameAccum / static_cast<int>(std::size(_imgui_context->FramerateSecPerFrame)) * 0.5f,
			_imgui_context->FramerateSecPerFrameAccum / static_cast<int>(std::size(_imgui_context->FramerateSecPerFrame)) * 1.5f,
			ImVec2(0, 50));

		const std::time_t t = std::chrono::system_clock::to_time_t(_current_time);
		struct tm tm; localtime_s(&tm, &t);

		ImGui::BeginGroup();

		ImGui::TextUnformatted(_("API:"));
		ImGui::TextUnformatted(_("Hardware:"));
		ImGui::TextUnformatted(_("Application:"));
		ImGui::TextUnformatted(_("Time:"));
		ImGui::TextUnformatted(_("Resolution:"));
		ImGui::Text(_("Frame %llu:"), _frame_count + 1);
		ImGui::TextUnformatted(_("Post-Processing:"));

		ImGui::EndGroup();
		ImGui::SameLine(ImGui::GetWindowWidth() * 0.33333333f);
		ImGui::BeginGroup();

		const char *api_name = "Unknown";
		switch (_device->get_api())
		{
		case api::device_api::d3d9:
			api_name = "D3D9";
			break;
		case api::device_api::d3d10:
			api_name = "D3D10";
			break;
		case api::device_api::d3d11:
			api_name = "D3D11";
			break;
		case api::device_api::d3d12:
			api_name = "D3D12";
			break;
		case api::device_api::opengl:
			api_name = "OpenGL";
			break;
		case api::device_api::vulkan:
			api_name = "Vulkan";
			break;
		}

		ImGui::TextUnformatted(api_name);
		if (_vendor_id != 0)
			ImGui::Text("VEN_%X", _vendor_id);
		else
			ImGui::TextUnformatted("Unknown");
		ImGui::TextUnformatted(g_target_executable_path.filename().u8string().c_str());
		ImGui::Text("%.4d-%.2d-%.2d %d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
		ImGui::Text("%ux%u", _effect_permutations[0].width, _effect_permutations[0].height);
		ImGui::Text("%.2f fps", _imgui_context->IO.Framerate);
		ImGui::Text("%*.3f ms CPU", cpu_digits + 4, post_processing_time_cpu * 1e-6f);

		ImGui::EndGroup();
		ImGui::SameLine(ImGui::GetWindowWidth() * 0.66666666f);
		ImGui::BeginGroup();

		ImGui::Text("0x%X", _renderer_id);
		if (_device_id != 0)
			ImGui::Text("DEV_%X", _device_id);
		else
			ImGui::TextUnformatted("Unknown");
		ImGui::Text("0x%X", static_cast<unsigned int>(std::hash<std::string>()(g_target_executable_path.stem().u8string()) & 0xFFFFFFFF));
		ImGui::Text("%.0f ms", std::chrono::duration_cast<std::chrono::nanoseconds>(_last_present_time - _start_time).count() * 1e-6f);
		ImGui::Text("Format %u (%u bpc)", static_cast<unsigned int>(_effect_permutations[0].color_format), api::format_bit_depth(_effect_permutations[0].color_format));
		ImGui::Text("%*.3f ms", gpu_digits + 4, _last_frame_duration.count() * 1e-6f);
		if (_gather_gpu_statistics && post_processing_time_gpu != 0)
			ImGui::Text("%*.3f ms GPU", gpu_digits + 4, (post_processing_time_gpu * 1e-6f));

		ImGui::EndGroup();
	}

	if (ImGui::CollapsingHeader(_("Techniques"), ImGuiTreeNodeFlags_DefaultOpen) && !is_loading() && _effects_enabled)
	{
		// Only need to gather GPU statistics if the statistics are actually visible
		_gather_gpu_statistics = true;

		ImGui::BeginGroup();

		size_t total_pass_count = 0;
		for (const technique &tech : _techniques)
			total_pass_count += tech.permutations[0].passes.size();
		std::vector<bool> long_technique_name(_techniques.size() + total_pass_count);

		total_pass_count = _techniques.size();

		for (size_t technique_index : _technique_sorting)
		{
			const reshade::technique &tech = _techniques[technique_index];

			if (!tech.enabled)
				continue;

			if (tech.permutations[0].passes.size() > 1)
				ImGui::Text("%s (%zu passes)", tech.name.c_str(), tech.permutations[0].passes.size());
			else
				ImGui::TextUnformatted(tech.name.c_str(), tech.name.c_str() + tech.name.size());

			long_technique_name[technique_index] = (ImGui::GetItemRectSize().x + 10.0f) > (ImGui::GetWindowWidth() * 0.33333333f);
			if (long_technique_name[technique_index])
				ImGui::NewLine();

			for (size_t pass_index = 0; pass_index < tech.permutations[0].passes.size(); ++pass_index, ++total_pass_count)
			{
				const reshade::technique::pass &pass = tech.permutations[0].passes[pass_index];

				if (pass.name.empty())
					ImGui::Text("  pass %-2zu", pass_index);
				else
					ImGui::Text("  pass %-2zu %s", pass_index, pass.name.c_str());

				long_technique_name[total_pass_count] = (ImGui::GetItemRectSize().x + 10.0f) > (ImGui::GetWindowWidth() * 0.66666666f);
				if (long_technique_name[total_pass_count])
					ImGui::NewLine();
			}
		}

		ImGui::EndGroup();
		ImGui::SameLine(ImGui::GetWindowWidth() * 0.33333333f);
		ImGui::BeginGroup();

		total_pass_count = _techniques.size();

		for (size_t technique_index : _technique_sorting)
		{
			const reshade::technique &tech = _techniques[technique_index];

			if (!tech.enabled)
				continue;

			if (long_technique_name[technique_index])
				ImGui::NewLine();

			if (tech.average_cpu_duration != 0)
				ImGui::Text("%*.3f ms CPU", cpu_digits + 4, tech.average_cpu_duration * 1e-6f);
			else
				ImGui::NewLine();

			for (size_t pass_index = 0; pass_index < tech.permutations[0].passes.size(); ++pass_index, ++total_pass_count)
			{
				ImGui::NewLine();

				if (long_technique_name[total_pass_count])
					ImGui::NewLine();
			}
		}

		ImGui::EndGroup();
		ImGui::SameLine(ImGui::GetWindowWidth() * 0.66666666f);
		ImGui::BeginGroup();

		total_pass_count = _techniques.size();

		for (size_t technique_index : _technique_sorting)
		{
			const reshade::technique &tech = _techniques[technique_index];

			if (!tech.enabled)
				continue;

			if (long_technique_name[technique_index])
				ImGui::NewLine();

			// GPU timings are not available for all APIs
			if (_gather_gpu_statistics && tech.average_gpu_duration != 0)
				ImGui::Text("%*.3f ms GPU", gpu_digits + 4, tech.average_gpu_duration * 1e-6f);
			else
				ImGui::NewLine();

			for (size_t pass_index = 0; pass_index < tech.permutations[0].passes.size(); ++pass_index, ++total_pass_count)
			{
				const reshade::technique::pass &pass = tech.permutations[0].passes[pass_index];

				if (long_technique_name[total_pass_count])
					ImGui::NewLine();

				if (_gather_gpu_statistics && pass.average_gpu_duration != 0)
					ImGui::Text("%*.3f ms GPU", gpu_digits + 4, pass.average_gpu_duration * 1e-6f);
				else
					ImGui::NewLine();
			}
		}

		ImGui::EndGroup();
	}

	if (ImGui::CollapsingHeader(_("Render Targets & Textures"), ImGuiTreeNodeFlags_DefaultOpen) && !is_loading())
	{
		struct texture_format_info
		{
			explicit texture_format_info(reshadefx::texture_format format)
			{
				switch (format)
				{
				default:
					assert(false);
					[[fallthrough]];
				case reshadefx::texture_format::unknown:
					name = "unknown";
					bytes_per_pixel = 0;
					components = 0;
					break;
				case reshadefx::texture_format::r8:
					name = "R8";
					bytes_per_pixel = 1;
					components = 1;
					break;
				case reshadefx::texture_format::r16f:
					name = "R16F";
					bytes_per_pixel = 2;
					components = 1;
					break;
				case reshadefx::texture_format::r16:
					name = "R16";
					bytes_per_pixel = 2;
					components = 1;
					break;
				case reshadefx::texture_format::r32f:
					name = "R32F";
					bytes_per_pixel = 4;
					components = 1;
					break;
				case reshadefx::texture_format::r32u:
					name = "R32U";
					bytes_per_pixel = 4;
					components = 1;
					break;
				case reshadefx::texture_format::r32i:
					name = "R32I";
					bytes_per_pixel = 4;
					components = 1;
					break;
				case reshadefx::texture_format::rg8:
					name = "RG8";
					bytes_per_pixel = 2;
					components = 2;
					break;
				case reshadefx::texture_format::rg16f:
					name = "RG16F";
					bytes_per_pixel = 4;
					components = 2;
					break;
				case reshadefx::texture_format::rg16:
					name = "RG16";
					bytes_per_pixel = 4;
					components = 2;
					break;
				case reshadefx::texture_format::rg32f:
					name = "RG32F";
					bytes_per_pixel = 8;
					components = 2;
					break;
				case reshadefx::texture_format::rgba8:
					name = "RGBA8";
					bytes_per_pixel = 4;
					components = 4;
					break;
				case reshadefx::texture_format::rgba16f:
					name = "RGBA16F";
					bytes_per_pixel = 8;
					components = 4;
					break;
				case reshadefx::texture_format::rgba16:
					name = "RGBA16";
					bytes_per_pixel = 8;
					components = 4;
					break;
				case reshadefx::texture_format::rgba32f:
					name = "RGBA32F";
					bytes_per_pixel = 16;
					components = 4;
					break;
				case reshadefx::texture_format::rgba32u:
					name = "RGBA32U";
					bytes_per_pixel = 16;
					components = 4;
					break;
				case reshadefx::texture_format::rgba32i:
					name = "RGBA32I";
					bytes_per_pixel = 16;
					components = 4;
					break;
				case reshadefx::texture_format::rgb10a2:
					name = "RGB10A2";
					bytes_per_pixel = 4;
					components = 4;
					break;
				case reshadefx::texture_format::rg11b10f:
					name = "RG11B10F";
					bytes_per_pixel = 4;
					components = 3;
					break;
				}
			}

			const char *name;
			int bytes_per_pixel;
			int components;
		};

		const float total_width = ImGui::GetContentRegionAvail().x;
		int texture_count = 0;
		const unsigned int num_columns = std::max(1u, static_cast<unsigned int>(std::ceil(total_width / (55.0f * ImGui::GetFontSize()))));
		const float single_image_width = (total_width / num_columns) - 5.0f;

		// Variables used to calculate memory size of textures
		size_t post_processing_memory_size = 0;
		const float memory_size_unit = 1024 * 1024;

		for (size_t texture_index = 0; texture_index < _textures.size(); ++texture_index)
		{
			const texture &tex = _textures[texture_index];

			if (tex.resource == 0 || !tex.semantic.empty() ||
				!std::any_of(tex.shared.cbegin(), tex.shared.cend(),
					[this](size_t effect_index) { return _effects[effect_index].rendering; }))
				continue;

			const texture_format_info format_info(tex.format);

			ImGui::PushID(texture_count);
			ImGui::BeginGroup();

			size_t memory_size = 0;

			for (uint32_t level = 0, width = tex.width, height = tex.height, depth = tex.depth; level < tex.levels; ++level, width /= 2, height /= 2, depth /= 2)
				memory_size += static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(depth) * texture_format_info(tex.format).bytes_per_pixel;

			post_processing_memory_size += memory_size;

			ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s%s", tex.unique_name.c_str(), tex.shared.size() > 1 ? " (pooled)" : "");
			switch (tex.type)
			{
			case reshadefx::texture_type::texture_1d:
				ImGui::Text("%u | %u mipmap(s) | %s | %.3f MiB",
					tex.width,
					tex.levels - 1,
					format_info.name,
					memory_size / memory_size_unit);
				break;
			case reshadefx::texture_type::texture_2d:
				ImGui::Text("%ux%u | %u mipmap(s) | %s | %.3f MiB",
					tex.width,
					tex.height,
					tex.levels - 1,
					format_info.name,
					memory_size / memory_size_unit);
				break;
			case reshadefx::texture_type::texture_3d:
				ImGui::Text("%ux%ux%u | %u mipmap(s) | %s | %.3f MiB",
					tex.width,
					tex.height,
					tex.depth,
					tex.levels - 1,
					format_info.name,
					memory_size / memory_size_unit);
				break;
			}

			size_t num_referenced_passes = 0;
			std::vector<std::pair<size_t, std::vector<std::string>>> references;
			for (const technique &tech : _techniques)
			{
				if (std::find(tex.shared.cbegin(), tex.shared.cend(), tech.effect_index) == tex.shared.cend())
					continue;

				std::pair<size_t, std::vector<std::string>> &reference = references.emplace_back();
				reference.first = tech.effect_index;

				for (size_t pass_index = 0; pass_index < tech.permutations[0].passes.size(); ++pass_index)
				{
					std::string pass_name = tech.permutations[0].passes[pass_index].name;
					if (pass_name.empty())
						pass_name = "pass " + std::to_string(pass_index);
					pass_name = tech.name + ' ' + pass_name;

					bool referenced = false;
					for (const reshadefx::texture_binding &binding : tech.permutations[0].passes[pass_index].texture_bindings)
					{
						if (_effects[tech.effect_index].permutations[0].module.samplers[binding.index].texture_name == tex.unique_name)
						{
							referenced = true;
							reference.second.emplace_back(pass_name + " (sampler)");
							break;
						}
					}

					for (const reshadefx::storage_binding &binding : tech.permutations[0].passes[pass_index].storage_bindings)
					{
						if (_effects[tech.effect_index].permutations[0].module.storages[binding.index].texture_name == tex.unique_name)
						{
							referenced = true;
							reference.second.emplace_back(pass_name + " (storage)");
							break;
						}
					}

					for (const std::string &render_target : tech.permutations[0].passes[pass_index].render_target_names)
					{
						if (render_target == tex.unique_name)
						{
							referenced = true;
							reference.second.emplace_back(pass_name + " (render target)");
							break;
						}
					}

					if (referenced)
						num_referenced_passes++;
				}
			}

			const bool supports_saving = (tex.type != reshadefx::texture_type::texture_3d) && (
				tex.format == reshadefx::texture_format::r8 ||
				tex.format == reshadefx::texture_format::rg8 ||
				tex.format == reshadefx::texture_format::rgba8 ||
				tex.format == reshadefx::texture_format::rgb10a2);

			const float button_size = ImGui::GetFrameHeight();
			const float button_spacing = _imgui_context->Style.ItemInnerSpacing.x;
			ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
			if (const std::string label = "Referenced by " + std::to_string(num_referenced_passes) + " pass(es) in " + std::to_string(tex.shared.size()) + " effect(s) ...";
				ImGui::ButtonEx(label.c_str(), ImVec2(single_image_width - (supports_saving ? button_spacing + button_size : 0), 0)))
				ImGui::OpenPopup("##references");
			if (supports_saving)
			{
				ImGui::SameLine(0, button_spacing);
				if (ImGui::Button(ICON_FK_FLOPPY, ImVec2(button_size, 0)))
					save_texture(tex);
				ImGui::SetItemTooltip(_("Save %s"), tex.unique_name.c_str());
			}
			ImGui::PopStyleVar();

			if (!references.empty() && ImGui::BeginPopup("##references"))
			{
				bool is_open = false;
				size_t effect_index = std::numeric_limits<size_t>::max();

				for (const std::pair<size_t, std::vector<std::string>> &reference : references)
				{
					if (reference.first != effect_index)
					{
						effect_index = reference.first;
						is_open = ImGui::TreeNodeEx(_effects[effect_index].source_file.filename().u8string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen);
					}

					if (is_open)
					{
						for (const std::string &pass : reference.second)
						{
							ImGui::Dummy(ImVec2(_imgui_context->Style.IndentSpacing, 0.0f));
							ImGui::SameLine(0.0f, 0.0f);
							ImGui::TextUnformatted(pass.c_str(), pass.c_str() + pass.size());
						}
					}
				}

				ImGui::EndPopup();
			}

			if (tex.type == reshadefx::texture_type::texture_2d)
			{
				if (bool check = _preview_texture == texture_index && _preview_size[0] == 0; ImGui::RadioButton(_("Preview scaled"), check))
				{
					_preview_size[0] = 0;
					_preview_size[1] = 0;
					_preview_texture = !check ? texture_index : std::numeric_limits<size_t>::max();
				}
				ImGui::SameLine();
				if (bool check = _preview_texture == texture_index && _preview_size[0] != 0; ImGui::RadioButton(_("Preview original"), check))
				{
					_preview_size[0] = tex.width;
					_preview_size[1] = tex.height;
					_preview_texture = !check ? texture_index : std::numeric_limits<size_t>::max();
				}

				bool r = (_preview_size[2] & 0x000000FF) != 0;
				bool g = (_preview_size[2] & 0x0000FF00) != 0;
				bool b = (_preview_size[2] & 0x00FF0000) != 0;
				ImGui::SameLine();
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(_imgui_context->Style.FramePadding.x, 0));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 0, 0, 1));
				imgui::toggle_button("R", r, 0.0f, ImGuiButtonFlags_AlignTextBaseLine);
				ImGui::PopStyleColor();
				if (format_info.components >= 2)
				{
					ImGui::SameLine(0, 1);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 1, 0, 1));
					imgui::toggle_button("G", g, 0.0f, ImGuiButtonFlags_AlignTextBaseLine);
					ImGui::PopStyleColor();
					if (format_info.components >= 3)
					{
						ImGui::SameLine(0, 1);
						ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 1, 1));
						imgui::toggle_button("B", b, 0.0f, ImGuiButtonFlags_AlignTextBaseLine);
						ImGui::PopStyleColor();
					}
				}
				ImGui::PopStyleVar();
				_preview_size[2] = (r ? 0x000000FF : 0) | (g ? 0x0000FF00 : 0) | (b ? 0x00FF0000 : 0) | 0xFF000000;

				const float aspect_ratio = static_cast<float>(tex.width) / static_cast<float>(tex.height);
				imgui::image_with_checkerboard_background(tex.srv[0].handle, ImVec2(single_image_width, single_image_width / aspect_ratio), _preview_size[2]);
			}

			ImGui::EndGroup();
			ImGui::PopID();

			if ((texture_count++ % num_columns) != (num_columns - 1))
				ImGui::SameLine(0.0f, 5.0f);
			else
				ImGui::Spacing();
		}

		if ((texture_count % num_columns) != 0)
			ImGui::NewLine(); // Reset ImGui::SameLine() so the following starts on a new line

		ImGui::Separator();

		ImGui::Text(_("Total memory usage: %.3f MiB"), post_processing_memory_size / memory_size_unit);
	}
}
void reshade::runtime::draw_gui_log()
{
	std::error_code ec;
	std::filesystem::path log_path = global_config().path();
	log_path.replace_extension(L".log");

	const bool filter_changed = imgui::search_input_box(_log_filter, sizeof(_log_filter), -(ImGui::GetFrameHeight() + 8.0f * ImGui::GetFontSize() + 2 * _imgui_context->Style.ItemSpacing.x));

	ImGui::SameLine();

	if (ImGui::Button(ICON_FK_FOLDER, ImVec2(ImGui::GetFrameHeight(), 0.0f)))
		utils::open_explorer(log_path);
	ImGui::SetItemTooltip(_("Open folder in explorer"));

	ImGui::SameLine();

	if (ImGui::Button(_("Clear Log"), ImVec2(8.0f * ImGui::GetFontSize(), 0.0f)))
		// Close and open the stream again, which will clear the file too
		log::open_log_file(log_path, ec);

	ImGui::Spacing();

	const uintmax_t file_size = std::filesystem::file_size(log_path, ec);
	// Defer log reloading during user interface interactions to avoid interfering with tab switching
	if (filter_changed || (_last_log_size != file_size && !ImGui::IsAnyItemActive() && !ImGui::IsAnyItemFocused() && !_log_editor.has_selection()))
	{
		_log_editor.set_readonly(true);

		if (FILE *const file = _wfsopen(log_path.c_str(), L"r", SH_DENYNO))
		{
			if (filter_changed || file_size <= _last_log_size)
				_log_editor.clear_text();
			else
				fseek(file, static_cast<long>(_last_log_size), SEEK_SET);

			char line_data[2048];
			while (fgets(line_data, sizeof(line_data), file))
			{
				const std::string_view line(line_data);
				if (string_contains(line, _log_filter))
				{
					if (line.back() != '\n')
						continue;

					const imgui::code_editor::text_pos line_pos_beg = _log_editor.get_text_end();
					_log_editor.append_text(line);
					const imgui::code_editor::text_pos line_pos_end = _log_editor.get_text_end();

					imgui::code_editor::color col = imgui::code_editor::color_default;
					/**/ if (line.find("ERROR |") != std::string_view::npos)
						col = imgui::code_editor::color_error_marker;
					else if (line.find("WARN  |") != std::string_view::npos)
						col = imgui::code_editor::color_warning_marker;
					else if (line.find("DEBUG |") != std::string_view::npos)
						col = imgui::code_editor::color_comment;
					else if (line.find("error") != std::string_view::npos)
						col = imgui::code_editor::color_error_marker;
					else if (line.find("warning") != std::string_view::npos)
						col = imgui::code_editor::color_warning_marker;

					_log_editor.colorize(line_pos_beg, line_pos_end, col);
				}
			}

			fclose(file);
		}

		_last_log_size = file_size;
	}

	uint32_t palette[imgui::code_editor::color_palette_max];
	std::copy_n(_editor_palette, imgui::code_editor::color_palette_max, palette);
	palette[imgui::code_editor::color_error_marker] = ImColor(COLOR_RED);
	palette[imgui::code_editor::color_warning_marker] = ImColor(COLOR_YELLOW);
	palette[imgui::code_editor::color_comment] = ImColor(100, 100, 255);

	_log_editor.render("##log", palette, true);
}
void reshade::runtime::draw_gui_about()
{
	// SHERBET 크레딧 블록
	sherbet::begin_card("##about_credit");
	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.9f);
	ImGui::Text("Sherbet %s", sherbet::active_theme().display_name);
	ImGui::PopFont();
	ImGui::TextUnformatted("\xEC\xA0\x95\xEB\xA0\xAC\xEC\x9D\xB4 \xEB\xA7\x8C\xEB\x93\xA0 \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEB\xA6\xAC\xEC\x89\x90\xEC\x9D\xB4\xEB\x93\x9C"); // "정렬이 만든 커스텀 리쉐이드"
	ImGui::Spacing();
	ImGui::TextLinkOpenURL(ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEC\xB0\xB8\xEC\x97\xAC", SHERBET_DISCORD_URL); // "디스코드 참여"
	const std::string about_owner = sherbet::auth::effective_owner_name(_sherbet_auth);
	if (!about_owner.empty())
	{
		ImGui::Spacing();
		ImGui::Text(ICON_FK_OK "  \xEB\x93\xB1\xEB\xA1\x9D \xEC\x86\x8C\xEC\x9C\xA0\xEC\x9E\x90: %s", about_owner.c_str()); // "등록 소유자 :"
		if (SHERBET_ORDER_NO[0] != '\0')
			ImGui::Text("   \xEC\xA3\xBC\xEB\xAC\xB8 #%s", SHERBET_ORDER_NO); // "주문 #"
	}
	sherbet::end_card();

	sherbet::begin_card("##about_warn");
	ImGui::PushStyleColor(ImGuiCol_Text, sherbet::status_color(sherbet::status::bad));
	ImGui::TextWrapped(ICON_FK_WARNING " \xEC\x9D\xB4 \xEB\xB9\x8C\xEB\x93\x9C\xEB\x8A\x94 \xEC\xA0\x95\xEB\xA0\xAC\xEC\x9D\xB4 \xEC\xA0\x9C\xEC\x9E\x91\xED\x95\x9C \xEA\xB5\xAC\xEB\xA7\xA4\xEC\x9E\x90 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB9\x8C\xEB\x93\x9C\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4. \xEB\xAC\xB4\xEB\x8B\xA8 \xEB\xB0\xB0\xED\x8F\xAC\xC2\xB7\xEA\xB3\xB5\xEC\x9C\xA0 \xEC\x8B\x9C \xEB\xB8\x94\xEB\x9E\x99\xEB\xA6\xAC\xEC\x8A\xA4\xED\x8A\xB8 \xEC\xB6\x94\xEA\xB0\x80 \xEB\xB0\x8F \xED\x8C\x8C\xEC\x9D\xBC \xEC\x9E\xA0\xEA\xB8\x88 \xEC\xA1\xB0\xEC\xB9\x98\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4.");
	ImGui::PopStyleColor();
	sherbet::end_card();
	ImGui::Spacing();

	ImGui::TextUnformatted("ReShade " VERSION_STRING_PRODUCT);

	ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("https://reshade.me").x, ImGui::GetStyle().ItemSpacing.x);
	ImGui::TextLinkOpenURL("https://reshade.me");

	ImGui::Separator();

	ImGui::PushTextWrapPos();

	ImGui::TextUnformatted(_("Developed and maintained by crosire."));
	ImGui::TextUnformatted(_("This project makes use of several open source libraries, licenses of which are listed below:"));

	if (ImGui::CollapsingHeader("ReShade", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_RESHADE);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("MinHook"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_MINHOOK);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("Dear ImGui"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_IMGUI);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("ImGuiColorTextEdit"))
	{
		ImGui::TextUnformatted("Copyright (C) 2017 BalazsJako\
\
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\
\
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\
\
THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.");
	}
	if (ImGui::CollapsingHeader("glad"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_GLAD);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("UTF8-CPP"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_UTFCPP);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("stb_image, stb_image_write"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_STB);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("DDS loading from SOIL"))
	{
		ImGui::TextUnformatted("Jonathan \"lonesock\" Dummer");
	}
	if (ImGui::CollapsingHeader("fpng"))
	{
		ImGui::TextUnformatted("Public Domain (https://github.com/richgel999/fpng)");
	}
	if (ImGui::CollapsingHeader("SPIR-V"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_SPIRV);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("Vulkan Memory Allocator"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_VMA);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("OpenVR"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_OPENVR);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("OpenXR"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_OPENXR);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}
	if (ImGui::CollapsingHeader("Solarized"))
	{
		ImGui::TextUnformatted("Copyright (C) 2011 Ethan Schoonover\
\
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\
\
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\
\
THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.");
	}
	if (ImGui::CollapsingHeader("Fork Awesome"))
	{
		ImGui::TextUnformatted("Copyright (C) 2018 Fork Awesome (https://forkawesome.github.io)\
\
This Font Software is licensed under the SIL Open Font License, Version 1.1. (http://scripts.sil.org/OFL)");
	}
	if (ImGui::CollapsingHeader("libjxl simple lossless encoder"))
	{
		const resources::data_resource resource = resources::load_data_resource(IDR_LICENSE_S_JXL);
		ImGui::TextUnformatted(static_cast<const char *>(resource.data), static_cast<const char *>(resource.data) + resource.data_size);
	}

	ImGui::PopTextWrapPos();
}

// SHERBET: 카드 한 줄짜리 라벨 — 주어진 폭을 넘으면 말줄임표로 자른다.
// 카드 안에서 이름을 **줄바꿈시키면 안 된다**: 두 줄이 되는 순간 아래 액션 줄과 겹친다
// (그 겹침이 2026-07-31 실물 스크린샷의 「적용」 버튼이 작성자 이름을 덮은 바로 그 증상이다).
// ⚠️ UTF-8 코드포인트 경계에서만 자른다 — 바이트로 자르면 한글이 깨진 조각으로 남는다.
static std::string sherbet_ellipsize(const char *text, float max_w)
{
	if (text == nullptr || *text == '\0' || ImGui::CalcTextSize(text).x <= max_w)
		return text != nullptr ? text : "";

	static const char *const kEllipsis = "\xE2\x80\xA6"; // …
	const float ellipsis_w = ImGui::CalcTextSize(kEllipsis).x;

	std::string fit;
	for (const char *p = text; *p != '\0'; )
	{
		const char *q = p + 1;
		while ((static_cast<unsigned char>(*q) & 0xC0) == 0x80) // 이어지는 바이트를 전부 삼킨다
			++q;

		const std::string next = fit + std::string(p, q);
		if (ImGui::CalcTextSize(next.c_str()).x + ellipsis_w > max_w)
			break;

		fit = next;
		p = q;
	}
	return fit + kEllipsis;
}

// SHERBET: 조준점 마켓 — 「마켓」 탭의 세 번째 세그먼트.
//
// 왜 새 탭이 아니라 마켓 안인가: 진열·역할 잠금·「내 전용 불러오기」가 테마/프리셋과
// 완전히 같은 물건이다. 새 탭을 만들면 같은 UI 를 두 벌 유지하게 된다.
//
// **판정은 이 함수에 한 줄도 없다.** 무엇이 적용 가능한지(잠김/코드 불량), 되돌리기가
// 무엇을 가리키는지는 전부 sherbet_xhmarket.hpp 가 정하고 tools/sherbet_xhmarket_test.cpp
// 가 검증한다. 여기는 그 결과를 그리고 클릭을 전달할 뿐이다 — 서버가 보낸 코드가
// 신뢰할 수 없는 입력이기 때문에, 판정이 UI 로 새면 맥에서 검증할 방법이 사라진다.
//
// 비용: 이 함수는 마켓 탭의 조준점 세그먼트가 열려 있을 때만 호출된다. 카드 미리보기는
// 카드마다 build_crosshair 를 한 번 부르는데(작은 사각형 수십 개) 버퍼를 재사용하므로
// 프레임당 할당이 없다. 서버 목록은 /content/me 도착 시 한 번 파싱된 것을 읽기만 하고,
// 로컬 목록은 슬롯이 바뀔 때만 다시 만든다.
void reshade::runtime::draw_gui_crosshair_market()
{
	namespace xm = sherbet::xhmarket;

	static const char *const kXhIntro = "\xEB\xA7\x88\xEC\x9D\x8C\xEC\x97\x90 \xEB\x93\x9C\xEB\x8A\x94 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x84 \xEA\xB3\xA8\xEB\x9D\xBC \xEB\xB0\x94\xEB\xA1\x9C \xEC\xA0\x81\xEC\x9A\xA9\xED\x95\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEA\xB3\xB5\xEC\x9C\xA0 \xEC\xBD\x94\xEB\x93\x9C \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4."; // "마음에 드는 조준점을 골라 바로 적용해요 — 발로란트 공유 코드 그대로입니다."
	static const char *const kXhUndoTitle = "\xEB\x90\x98\xEB\x8F\x8C\xEB\xA6\xAC\xEA\xB8\xB0"; // "되돌리기"
	static const char *const kXhUndoDesc = "\xEB\xA7\x88\xEC\xBC\x93\xEC\x97\x90\xEC\x84\x9C \xEC\xB2\x98\xEC\x9D\x8C \xEC\xA0\x81\xEC\x9A\xA9\xED\x95\x98\xEA\xB8\xB0 \xEC\xA7\x81\xEC\xA0\x84\xEC\x97\x90 \xEC\x93\xB0\xEB\x8D\x98 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x84 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEA\xB0\x96\xEA\xB3\xA0 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94. \xEC\xB9\xB4\xEB\x93\x9C\xEB\xA5\xBC \xEC\x95\x84\xEB\xAC\xB4\xEB\xA6\xAC \xEB\x88\x8C\xEB\x9F\xAC\xEB\xB4\x90\xEB\x8F\x84 \xEC\x9D\xB4 \xEA\xB0\x92\xEC\x9D\x80 \xEB\xB0\x94\xEB\x80\x8C\xEC\xA7\x80 \xEC\x95\x8A\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4."; // "마켓에서 처음 적용하기 직전에 쓰던 조준점을 그대로 갖고 있어요. 카드를 아무리 눌러봐도 이 값은 바뀌지 않습니다."
	static const char *const kXhUndoBtn = "\xEC\x9B\x90\xEB\x9E\x98 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA6\xAC\xEA\xB8\xB0"; // "원래 조준점으로 되돌리기"
	static const char *const kXhMine = "\xEB\x82\xB4 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90"; // "내 조준점"
	static const char *const kXhSaveBtn = "\xEC\xA7\x80\xEA\xB8\x88 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\xA0\x80\xEC\x9E\xA5"; // "지금 조준점 저장"
	static const char *const kXhNameHint = "\xEC\x9D\xB4\xEB\xA6\x84 (\xEB\xB9\x84\xEC\x9A\xB0\xEB\xA9\xB4 \xEC\x9E\x90\xEB\x8F\x99)"; // "이름 (비우면 자동)"
	static const char *const kXhMineEmpty = "\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x9C \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. '\xEC\x97\x90\xEC\x9E\x84' \xED\x83\xAD\xEC\x97\x90\xEC\x84\x9C \xEB\xA7\x9E\xEC\xB6\x98 \xEB\x92\xA4 \xEC\x97\xAC\xEA\xB8\xB0\xEC\x84\x9C \xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEB\xA9\xB4 \xEC\x96\xB8\xEC\xA0\x9C\xEB\x93\xA0 \xEB\x8F\x8C\xEC\x95\x84\xEC\x98\xAC \xEC\x88\x98 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94."; // "저장한 조준점이 없어요. 「에임」 탭에서 맞춘 뒤 여기서 저장하면 언제든 돌아올 수 있어요."
	static const char *const kXhServer = "\xEB\xB0\x9B\xEC\x9D\x80 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90"; // "받은 조준점"
	static const char *const kXhServerEmpty = "\xEB\xB0\x9B\xEC\x9D\x80 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 \xEC\x9C\x84 '\xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0'\xEB\xA1\x9C \xEB\xB0\x9B\xEC\x95\x84\xEC\x9A\x94."; // "받은 조준점이 없어요. 디스코드 로그인 후 위 「내 전용 불러오기」로 받아요."
	static const char *const kXhApply = "\xEC\xA0\x81\xEC\x9A\xA9"; // "적용"
	static const char *const kXhInUse = "\xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"; // "사용 중"
	static const char *const kXhLocked = "\xEC\x9E\xA0\xEA\xB9\x80"; // "잠김"
	static const char *const kXhBroken = "\xEC\x82\xAC\xEC\x9A\xA9 \xEB\xB6\x88\xEA\xB0\x80"; // "사용 불가"
	static const char *const kXhLockedTip = "\xEC\x9D\xB4 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x80 \xEC\x95\x84\xEC\xA7\x81 \xEC\x9E\xA0\xEA\xB2\xA8 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90\xEC\x84\x9C \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEB\xA9\xB4 \xEC\x97\xB4\xEB\xA6\xBD\xEB\x8B\x88\xEB\x8B\xA4"; // "이 조준점은 아직 잠겨 있어요 — 디스코드에서 구매하면 열립니다"
	static const char *const kXhBrokenTip = "\xEC\x9D\xB4 \xED\x95\xAD\xEB\xAA\xA9\xEC\x9D\x98 \xEC\xBD\x94\xEB\x93\x9C\xEB\xA5\xBC \xEC\x9D\xBD\xEC\x9D\x84 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xED\x8C\x90\xEB\xA7\xA4\xEC\x9E\x90\xEC\x97\x90\xEA\xB2\x8C \xEC\x95\x8C\xEB\xA0\xA4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "이 항목의 코드를 읽을 수 없어요 — 판매자에게 알려 주세요"
	static const char *const kXhSaveFail = "\xEC\xA0\x80\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEC\xA7\x80\xEA\xB8\x88 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\xBD\x94\xEB\x93\x9C\xEA\xB0\x80 \xEC\x98\xAC\xEB\xB0\x94\xEB\xA5\xB4\xEC\xA7\x80 \xEC\x95\x8A\xEA\xB1\xB0\xEB\x82\x98 \xEC\xB9\xB8\xEC\x9D\xB4 \xEA\xB0\x80\xEB\x93\x9D \xEC\xB0\xBC\xEC\x96\xB4\xEC\x9A\x94"; // "저장하지 못했어요 — 지금 조준점 코드가 올바르지 않거나 칸이 가득 찼어요"
	static const char *const kXhSaveOk = "\xEB\x82\xB4 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x97\x90 \xEC\xA0\x80\xEC\x9E\xA5\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94"; // "내 조준점에 저장했어요"
	static const char *const kXhApplied = "\xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9D\x84 \xEC\xA0\x81\xEC\x9A\xA9\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94 (\xEB\xB0\x9C\xEB\xA1\x9C\xEB\x9E\x80\xED\x8A\xB8 \xEB\xAA\xA8\xEB\x93\x9C\xEB\xA1\x9C \xEC\xBC\x9C\xEC\xA7\x91\xEB\x8B\x88\xEB\x8B\xA4)"; // "조준점을 적용했어요 (발로란트 모드로 켜집니다)"
	static const char *const kXhReverted = "\xEC\x9B\x90\xEB\x9E\x98 \xEC\x93\xB0\xEB\x8D\x98 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x90\x98\xEB\x8F\x8C\xEB\xA0\xB8\xEC\x96\xB4\xEC\x9A\x94"; // "원래 쓰던 조준점으로 되돌렸어요"
	static const char *const kXhDelTip = "\xEC\x9D\xB4 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEC\x82\xAD\xEC\xA0\x9C"; // "이 조준점 삭제"
	static const char *const kXhSlots = "\xEC\xB9\xB8"; // "칸"

	constexpr float kCardW = 172.0f;
	constexpr float kPreviewH = 96.0f;
	constexpr float kCardPad = 10.0f;

	// 카드 높이는 **폰트에서 계산한다**(예전엔 178px 고정이었다).
	// 담아야 하는 것: 여백 + 미리보기 + 이름 1줄 + 태그·작성자 1줄 + 액션 줄 + 여백.
	// 기본 폰트에서도 178px 로는 약 15px 이 모자랐고, 그래서 액션 줄을 카드 바닥에 놓는
	// SetCursorPosY 가 커서를 **위로** 되돌려 「적용」 버튼이 이미 그려둔 작성자 줄을 덮었다
	// ("Riot" → "ot", "Sherbet" → "bet" 으로 보이던 증상. 2026-07-31 실물 스크린샷).
	// ⚠️ 폰트 크기는 사용자가 설정에서 바꿀 수 있다 — 고정 픽셀로는 영영 못 맞춘다.
	const float kCardH = kCardPad * 2.0f + kPreviewH
		+ ImGui::GetStyle().ItemSpacing.y            // 미리보기 아래 간격
		+ ImGui::GetTextLineHeightWithSpacing() * 2.0f // 이름 줄 + 태그·작성자 줄
		+ ImGui::GetFrameHeight();                   // 액션 줄

	static int msg_kind = 0; // 0=없음 1=성공 2=실패
	// ⚠️ 함수 지역 static 은 탭을 떠나도, 오버레이를 닫았다 열어도 유지된다. 타이머가 없으면
	//    「저장했어요」 가 게임을 끌 때까지 화면에 붙어 있고, 더 나쁘게는 이미 해결된 빨간
	//    경고가 멀쩡한 새 코드 아래 그대로 남는다. _sherbet_content_done_timer 와 같은 규약.
	static float msg_timer = 0.0f;
	static std::string msg;
	static std::vector<sherbet::crosshair::quad> pv_quads; // 카드 미리보기 재사용 버퍼
	static char name_buf[64] = "";
	int pending_delete = -1; // 그리는 도중에 목록을 건드리지 않는다

	ImGui::TextWrapped("%s", kXhIntro);
	ImGui::Spacing();

	// 조준점 하나를 적용한다. **여기서만** 조준점이 바뀐다.
	auto apply_entry = [&](const xm::entry &e) {
		if (!e.applicable())
			return;
		sherbet::crosshair::profile tmp;
		// 매니페스트 파싱 때 이미 통과한 코드지만 적용 직전에 한 번 더 본다.
		// 실패하면 아무것도 바꾸지 않고 나간다 — '절반 적용' 이 구조적으로 불가능해진다.
		if (!sherbet::crosshair::parse_code(e.code, tmp).ok())
			return;
		std::string code;
		if (!xm::apply(_sherbet_xh_session, e, _sherbet_val_code, code))
			return;
		_sherbet_val_profile = tmp;
		_sherbet_val_code = code;
		_sherbet_val_on = true;         // 골랐으면 켜진다 — 한 번 클릭으로 끝나야 한다
		_sherbet_crosshair_on = false;  // 클래식과 같이 켜면 조준점이 두 개 겹쳐 보인다
		_sherbet_val_code_dirty = true; // 「에임」 탭 입력상자도 같은 코드를 보여주게
		msg_kind = 1; msg_timer = 4.0f;
		msg = kXhApplied;
		save_config();
	};

	// 카드 한 장. local_index >= 0 이면 삭제 버튼이 붙는다(내 조준점).
	auto draw_card = [&](const xm::entry &e, int local_index) {
		const sherbet::theme &t = sherbet::active_theme();
		const bool in_use = (_sherbet_xh_session.applied_id == e.id);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(t.panel));
		ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(in_use ? t.accent : t.border));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, in_use ? 2.0f : 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kCardPad, kCardPad));
		ImGui::BeginChild(e.id.c_str(), ImVec2(kCardW, kCardH), ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		{
			// ── 미리보기 ──
			// 공유 코드를 글자로 늘어놓으면 무엇을 고르는지 알 수 없다. 같은 기하 함수로
			// 카드마다 **실제 픽셀 크기** 그대로 그린다(배경은 게임처럼 어둡게).
			const float pw = ImGui::GetContentRegionAvail().x;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			const ImVec2 p1(p0.x + pw, p0.y + kPreviewH);
			ImGui::Dummy(ImVec2(pw, kPreviewH));
			ImDrawList *const dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, IM_COL32(24, 25, 30, 255), 10.0f);
			if (e.applicable())
			{
				sherbet::draw_crosshair_preview(dl, p0, p1, e.parsed.primary, pv_quads);
			}
			else
			{
				// 잠김/불량은 그릴 조준점이 없다 — 이유를 아이콘으로 말한다(빈 칸 금지).
				const char *const icon = (e.state() == xm::entry_state::locked) ? ICON_FK_LOCK : ICON_FK_WARNING;
				const ImVec2 ts = ImGui::CalcTextSize(icon);
				dl->AddText(ImVec2((p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f),
					sherbet::with_alpha(t.text_dim, 220), icon);
			}

			// 이름과 부제는 각각 **한 줄로 고정**한다(넘치면 말줄임표).
			// 줄바꿈을 허용하면 위에서 잡은 카드 높이 예산이 무너져 액션 줄과 겹친다.
			// 이름은 서버가 보내는 값이라 길이를 우리가 통제하지 못한다 — 여기서 막아야 한다.
			const float label_w = ImGui::GetContentRegionAvail().x;
			ImGui::TextUnformatted(sherbet_ellipsize(e.name.c_str(), label_w).c_str());
			if (!e.tag.empty() || !e.author.empty())
			{
				std::string meta = e.tag;
				if (!e.tag.empty() && !e.author.empty())
					meta += " \xC2\xB7 ";
				meta += e.author;
				ImGui::TextDisabled("%s", sherbet_ellipsize(meta.c_str(), label_w).c_str());
			}

			// 액션 줄은 카드 아래에 고정한다 — 이름 길이와 무관하게 버튼 위치가 흔들리지 않게.
			// ⚠️ ImMax 로 **뒤로는 절대 가지 않는다**. 위 내용이 예산을 넘겨도 커서를 되돌리지
			//    않으므로, 최악의 경우 버튼이 조금 내려갈 뿐 앞서 그린 글자를 덮어쓰지는 않는다.
			ImGui::SetCursorPosY(ImMax(ImGui::GetCursorPosY(), kCardH - kCardPad - ImGui::GetFrameHeight()));
			switch (e.state())
			{
			case xm::entry_state::ready:
				if (in_use)
					ImGui::TextDisabled("%s  %s", ICON_FK_OK, kXhInUse);
				else if (sherbet::pill_button(kXhApply, false))
					apply_entry(e);
				break;
			case xm::entry_state::locked:
				ImGui::TextDisabled("%s  %s", ICON_FK_LOCK, kXhLocked);
				ImGui::SetItemTooltip("%s", kXhLockedTip);
				break;
			case xm::entry_state::broken:
				ImGui::TextDisabled("%s  %s", ICON_FK_WARNING, kXhBroken);
				ImGui::SetItemTooltip("%s", kXhBrokenTip);
				break;
			}
			if (local_index >= 0)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FK_TRASH))
					pending_delete = local_index;
				ImGui::SetItemTooltip("%s", kXhDelTip);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(2);
	};

	// 카드를 창 폭에 맞춰 줄바꿈하며 깐다.
	auto draw_grid = [&](const std::vector<xm::entry> &list, bool local) {
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float avail = ImGui::GetContentRegionAvail().x;
		int per_row = static_cast<int>((avail + spacing) / (kCardW + spacing));
		if (per_row < 1)
			per_row = 1;
		for (std::size_t i = 0; i < list.size(); ++i)
		{
			if (static_cast<int>(i) % per_row != 0)
				ImGui::SameLine();
			draw_card(list[i], local ? static_cast<int>(i) : -1);
		}
	};

	// ── 되돌리기 ─────────────────────────────────────────────────────────
	// 카드를 눌러보다 자기 조준점을 잃는 것이 이 기능의 최악의 결과다. 스냅샷이 있는
	// 동안은 맨 위에 계속 보여 준다(어디에 숨겨 두면 없는 것과 같다).
	if (xm::can_revert(_sherbet_xh_session))
	{
		sherbet::begin_card("##xh_undo");
		ImGui::Text("%s  %s", ICON_FK_UNDO, kXhUndoTitle);
		ImGui::TextDisabled("%s", kXhUndoDesc);
		if (sherbet::pill_button(kXhUndoBtn, false))
		{
			sherbet::crosshair::profile tmp;
			// 스냅샷은 설정 파일에 저장돼 있어 손으로 고쳐졌을 수 있다.
			// 파싱에 성공할 때만 소비한다 — 실패하면 못 쓰는 스냅샷을 버리고 알린다.
			if (sherbet::crosshair::parse_code(_sherbet_xh_session.undo_code, tmp).ok())
			{
				std::string code;
				if (xm::revert(_sherbet_xh_session, code))
				{
					_sherbet_val_profile = tmp;
					_sherbet_val_code = code;
					_sherbet_val_code_dirty = true;
					msg_kind = 1; msg_timer = 4.0f;
					msg = kXhReverted;
					save_config();
				}
			}
			else
			{
				_sherbet_xh_session.undo_code.clear();
				msg_kind = 2; msg_timer = 4.0f;
				msg = kXhBrokenTip;
				save_config();
			}
		}
		sherbet::end_card();
		ImGui::Spacing();
	}

	// ── 내 조준점 ────────────────────────────────────────────────────────
	ImGui::Text("%s  %s", ICON_FK_FLOPPY, kXhMine);
	ImGui::SameLine();
	ImGui::TextDisabled("%d / %d %s", static_cast<int>(_sherbet_xh_locals.size()),
		static_cast<int>(xm::kMaxLocals), kXhSlots);
	// ⚠️ 고정 220px 이면 폰트 24 부터 힌트가 "이름 (비우면 자…" 로 잘린다. 폰트를 따라간다.
	ImGui::SetNextItemWidth(ImMax(220.0f,
		9.0f * ImGui::GetFontSize() + _imgui_context->Style.FramePadding.x * 2.0f));
	ImGui::InputTextWithHint("##xh_name", kXhNameHint, name_buf, sizeof(name_buf));
	ImGui::SameLine();
	if (ImGui::Button(kXhSaveBtn))
	{
		std::string nm = name_buf;
		if (nm.empty())
			nm = std::string(kXhMine) + " " + std::to_string(_sherbet_xh_locals.size() + 1);
		// add_local 이 코드 유효성과 칸 수를 함께 판정한다(UI 는 조건을 다시 쓰지 않는다).
		if (xm::add_local(_sherbet_xh_locals, nm, _sherbet_val_code))
		{
			name_buf[0] = '\0';
			_sherbet_xh_locals_dirty = true;
			msg_kind = 1; msg_timer = 4.0f;
			msg = kXhSaveOk;
			save_config();
		}
		else
		{
			msg_kind = 2; msg_timer = 4.0f;
			msg = kXhSaveFail;
		}
	}

	msg_timer -= _imgui_context->IO.DeltaTime;
	if (msg_timer <= 0.0f)
		{ msg_kind = 0; msg.clear(); }
	if (msg_kind != 0 && !msg.empty())
	{
		ImVec4 c = (msg_kind == 2) ? sherbet::status_color(sherbet::status::bad) : sherbet::status_color(sherbet::status::good);
		c.w = ImMin(1.0f, msg_timer); // 마지막 1초 페이드
		ImGui::PushStyleColor(ImGuiCol_Text, c);
		ImGui::TextWrapped("%s  %s", (msg_kind == 2) ? ICON_FK_CANCEL : ICON_FK_OK, msg.c_str());
		ImGui::PopStyleColor();
	}
	ImGui::Spacing();

	if (_sherbet_xh_locals_dirty)
	{
		_sherbet_xh_locals_dirty = false;
		_sherbet_xh_local_cards = xm::local_entries(_sherbet_xh_locals);
	}
	if (_sherbet_xh_local_cards.empty())
		ImGui::TextDisabled("%s", kXhMineEmpty);
	else
		draw_grid(_sherbet_xh_local_cards, true);
	ImGui::Spacing();

	// ── 받은 조준점(서버) ────────────────────────────────────────────────
	ImGui::Separator();
	ImGui::Text("%s  %s", ICON_FK_SHOPPING_CART, kXhServer);
	const std::vector<xm::entry> &server_list = sherbet::content_crosshairs();
	if (server_list.empty())
		ImGui::TextDisabled("%s", kXhServerEmpty);
	else
		draw_grid(server_list, false);

	// 그리기가 끝난 뒤에 삭제한다 — 그리는 도중에 목록을 줄이면 카드가 반쯤 그려진다.
	if (pending_delete >= 0 && xm::remove_local(_sherbet_xh_locals, static_cast<std::size_t>(pending_delete)))
	{
		_sherbet_xh_locals_dirty = true;
		save_config();
	}
}

// ── SHERBET: 에임 트레이너 배선 ──────────────────────────────────────────────
// 판정·배치·점수는 전부 sherbet_aim.hpp 에 있다(호스트 테스트로 검증됨).
// 여기 있는 것은 그것을 게임에 물리는 배선과 그리기뿐이다.

// 매 프레임. 스프레이 샘플링과 **같은 자리**에서 같은 raw 델타를 나눠 받는다.
void reshade::runtime::sherbet_aim_frame(float dt, int raw_dx, int raw_dy, bool fire)
{
	// 워커 결과 수거는 판 상태와 무관하게 매 프레임 한다 — 아래에서 early-out 하면
	// 판을 안 하는 동안 받아온 리더보드가 영영 화면에 안 뜬다.
	{
		bool was_submit = false, updated = false;
		if (_sherbet_aim_net.take_board(_sherbet_aim_board, was_submit, updated))
			_sherbet_aim_board_ok = true;
		std::string err;
		if (_sherbet_aim_net.take_error(err))
		{
			_sherbet_aim_msg = err;
			_sherbet_aim_msg_timer = 6.0f;
		}
	}
	if (_sherbet_aim_msg_timer > 0.0f)
		_sherbet_aim_msg_timer = ImMax(0.0f, _sherbet_aim_msg_timer - dt);

	const sherbet::aim::phase ph = _sherbet_aim.current_phase();
	if (ph == sherbet::aim::phase::idle)
		return;

	// 오버레이를 열면 게임이 마우스를 못 받아 판이 성립하지 않는다(화면이 아예 안 돈다).
	// 진행 중이었다면 중단한다. 끝난 판(finished)은 결과를 봐야 하므로 남긴다.
	if (_show_overlay && (ph == sherbet::aim::phase::countdown || ph == sherbet::aim::phase::running))
	{
		_sherbet_aim.stop();
		return;
	}
	if (ph == sherbet::aim::phase::finished)
		return;

	// 가상 카메라. **이 프레임에 들어온 입력만** 쓴다 — 화면 리드백은 몇 프레임 늦으므로
	// 여기 절대 끼우지 않는다. 스무딩·보간도 넣지 않는다(지연 0 이 이 기능의 생명이다).
	_sherbet_aim_cam_yaw = sherbet::aim::wrap_deg(
		_sherbet_aim_cam_yaw + static_cast<float>(raw_dx) * _sherbet_aim_dpc);
	// 마우스를 아래로 내리면(raw_dy +) 시야도 내려간다.
	_sherbet_aim_cam_pitch = sherbet::aim::clamp_pitch(
		_sherbet_aim_cam_pitch - static_cast<float>(raw_dy) * _sherbet_aim_dpc);

	// ⚠️ 사격을 tick 보다 **먼저** 판정한다. 사용자가 클릭한 순간 화면에 있던 표적은
	//    지난 프레임에 그려진 위치다 — 먼저 움직이고 판정하면 안 보이던 자리로 채점한다.
	if (fire)
		_sherbet_aim.shoot(_sherbet_aim_cam_yaw, _sherbet_aim_cam_pitch);

	_sherbet_aim.tick(dt, _sherbet_aim_cam_yaw, _sherbet_aim_cam_pitch);

	// 방금 끝났으면 개인 최고 기록 갱신 + 리더보드 제출.
	if (_sherbet_aim.current_phase() == sherbet::aim::phase::finished && !_sherbet_aim_trial)
	{
		const int li = static_cast<int>(_sherbet_aim.current_level());
		const int di = static_cast<int>(_sherbet_aim.current_duration());
		if (li >= 0 && li < sherbet::aim::kLevelCount && di >= 0 && di < sherbet::aim::kDurationCount)
			if (_sherbet_aim.result().hits > _sherbet_aim_best[li][di])
			{
				_sherbet_aim_best[li][di] = _sherbet_aim.result().hits;
				save_config();
			}

		// 순위에 올라가는 길이만 보낸다(10초는 연습용). 판당 정확히 한 번.
		if (!_sherbet_aim_submitted && sherbet::aim::ranked(_sherbet_aim.current_duration()) &&
			sherbet::auth::enabled() && _sherbet_auth.is_authed())
		{
			_sherbet_aim_submitted = true;
			_sherbet_aim_net.begin_submit(_sherbet_auth.token(), _sherbet_aim.current_level(),
				_sherbet_aim.current_duration(), _sherbet_aim.result().hits, _sherbet_aim.result().shots);
			// 제출 응답에 갱신된 표가 같이 오므로 화면 표를 이 조합으로 맞춰 둔다.
			_sherbet_aim_board_level = static_cast<int>(_sherbet_aim.current_level());
			_sherbet_aim_board_duration = static_cast<int>(_sherbet_aim.current_duration());
		}
	}
}

// 오버레이가 닫힌 채로 화면에 그린다 — 카운트다운, 표적, 좌측 HUD.
// 조준점·궤적과 같은 ForegroundDrawList 라 오버레이 게이트 바깥이다.
void reshade::runtime::draw_sherbet_aim_overlay()
{
	const sherbet::aim::phase ph = _sherbet_aim.current_phase();
	if (ph == sherbet::aim::phase::idle || _show_overlay)
		return;

	const ImGuiViewport *const vp = ImGui::GetMainViewport();
	ImDrawList *const dl = ImGui::GetForegroundDrawList();
	const sherbet::theme &t = sherbet::active_theme();
	const float W = vp->Size.x, H = vp->Size.y;

	// ── 카운트다운 ───────────────────────────────────────────────────────────
	if (ph == sherbet::aim::phase::countdown)
	{
		const int n = _sherbet_aim.countdown_display();
		if (n > 0)
		{
			char buf[8];
			snprintf(buf, sizeof(buf), "%d", n);
			// 숫자가 바뀌는 순간 크게, 1초에 걸쳐 잦아든다.
			const float frac = std::fmod(_sherbet_aim.countdown_remaining(), 1.0f);
			const float grow = 1.0f + 0.25f * frac;
			const float sz = ImMin(W, H) * 0.22f * grow;
			const ImVec2 ts = _sherbet_title_font->CalcTextSizeA(sz, FLT_MAX, 0.0f, buf);
			const ImVec2 p(vp->Pos.x + (W - ts.x) * 0.5f, vp->Pos.y + (H - ts.y) * 0.5f);
			dl->AddText(_sherbet_title_font, sz, ImVec2(p.x + 3.0f, p.y + 3.0f), IM_COL32(0, 0, 0, 150), buf);
			dl->AddText(_sherbet_title_font, sz, p, sherbet::with_alpha(t.accent, 240), buf);
		}
		return; // 카운트다운 중에는 표적을 미리 보여주지 않는다(반응 시간 측정이 무의미해진다)
	}

	// ── 표적 ─────────────────────────────────────────────────────────────────
	if (ph == sherbet::aim::phase::running)
	{
		const sherbet::aim::target &tg = _sherbet_aim.current_target();
		const float rel_yaw = sherbet::aim::wrap_deg(tg.yaw - _sherbet_aim_cam_yaw);
		const float rel_pitch = tg.pitch - _sherbet_aim_cam_pitch;

		float sx = 0.0f, sy = 0.0f;
		if (sherbet::aim::project(rel_yaw, rel_pitch, _sherbet_aim_fov, W, H, sx, sy))
		{
			// 화면 반지름 — 표적에서 반지름만큼 떨어진 점을 같이 투영해 픽셀 거리로 잰다.
			// 각도를 픽셀로 직접 환산하면 화면 가장자리에서 어긋난다(원근).
			const float rad = _sherbet_aim.current_tuning().radius_deg;
			float ey = 0.0f, ep = 0.0f, ex = 0.0f, eyy = 0.0f;
			sherbet::aim::direction_at(tg.yaw, tg.pitch, rad, 0.0f, ey, ep);
			float r_px = 8.0f;
			if (sherbet::aim::project(sherbet::aim::wrap_deg(ey - _sherbet_aim_cam_yaw),
					ep - _sherbet_aim_cam_pitch, _sherbet_aim_fov, W, H, ex, eyy))
			{
				const float dx = ex - sx, dy = eyy - sy;
				r_px = std::sqrt(dx * dx + dy * dy);
			}
			if (r_px < 3.0f) r_px = 3.0f;

			const ImVec2 c(vp->Pos.x + sx, vp->Pos.y + sy);
			dl->AddCircleFilled(c, r_px, sherbet::with_alpha(t.accent, 210), 32);
			dl->AddCircle(c, r_px, IM_COL32(255, 255, 255, 220), 32, 2.0f);
			dl->AddCircleFilled(c, ImMax(2.0f, r_px * 0.18f), IM_COL32(255, 255, 255, 235), 12);
		}
	}

	// ── 좌측 HUD ─────────────────────────────────────────────────────────────
	// 조준 중엔 눈이 표적에 있다. 왼쪽은 주변시라 글자를 못 읽는다 — 큰 숫자와 색으로
	// 안 읽고도 알 수 있어야 한다. 자세한 것은 판이 끝난 뒤 오버레이에서 본다.
	{
		const sherbet::aim::stats &st = _sherbet_aim.result();
		const float lh = ImGui::GetTextLineHeightWithSpacing();
		const float pad = 12.0f;
		const float bw = 190.0f;
		const ImVec2 p0(vp->Pos.x + 28.0f, vp->Pos.y + H * 0.30f);
		const float bh = lh * 6.5f + pad * 2.0f;
		dl->AddRectFilled(p0, ImVec2(p0.x + bw, p0.y + bh), IM_COL32(12, 12, 16, 175), 12.0f);

		float y = p0.y + pad;
		char buf[96];

		// 명중 수 — 제일 큰 글씨. 이게 곧 점수다.
		snprintf(buf, sizeof(buf), "%d", st.hits);
		const float big = ImGui::GetFontSize() * 2.2f;
		dl->AddText(_sherbet_title_font, big, ImVec2(p0.x + pad, y), sherbet::with_alpha(t.text, 245), buf);
		dl->AddText(ImVec2(p0.x + pad + _sherbet_title_font->CalcTextSizeA(big, FLT_MAX, 0.0f, buf).x + 6.0f,
			y + big * 0.45f), sherbet::with_alpha(t.text_dim, 220),
			"\xEB\xAA\x85\xEC\xA4\x91"); // "명중"
		y += big + 4.0f;

		// 남은 시간 + 막대. 숫자를 안 읽어도 줄어드는 게 보인다.
		const float total = sherbet::aim::duration_seconds(_sherbet_aim.current_duration());
		const float left = _sherbet_aim.time_left();
		snprintf(buf, sizeof(buf), "%d:%02d", static_cast<int>(left) / 60, static_cast<int>(left) % 60);
		dl->AddText(ImVec2(p0.x + pad, y), sherbet::with_alpha(t.text, 230), buf);
		y += lh;
		const float bar_w = bw - pad * 2.0f;
		const float frac = total > 0.0f ? ImClamp(left / total, 0.0f, 1.0f) : 0.0f;
		dl->AddRectFilled(ImVec2(p0.x + pad, y), ImVec2(p0.x + pad + bar_w, y + 6.0f), IM_COL32(255, 255, 255, 40), 3.0f);
		dl->AddRectFilled(ImVec2(p0.x + pad, y), ImVec2(p0.x + pad + bar_w * frac, y + 6.0f),
			sherbet::with_alpha(t.accent, 235), 3.0f);
		y += 6.0f + lh * 0.5f;

		// 내 최고기록 대비 — "잘하고 있나" 에 즉답하는 자리. 색만 봐도 안다.
		const int li = static_cast<int>(_sherbet_aim.current_level());
		const int di = static_cast<int>(_sherbet_aim.current_duration());
		const int best = (li >= 0 && li < sherbet::aim::kLevelCount && di >= 0 && di < sherbet::aim::kDurationCount)
			? _sherbet_aim_best[li][di] : 0;
		if (best > 0 && total > 0.0f)
		{
			const float done = 1.0f - frac;                       // 진행률
			const int pace = static_cast<int>(static_cast<float>(best) * done + 0.5f);
			const int diff = st.hits - pace;
			dl->AddText(ImVec2(p0.x + pad, y), sherbet::with_alpha(t.text_dim, 200),
				"\xEB\x82\xB4 \xEC\xB5\x9C\xEA\xB3\xA0\xEA\xB8\xB0\xEB\xA1\x9D\xEB\xB3\xB4\xEB\x8B\xA4"); // "내 최고기록보다"
			y += lh;
			ImVec4 cv = sherbet::status_color(diff >= 0 ? sherbet::status::good : sherbet::status::bad);
			snprintf(buf, sizeof(buf), "%s %d\xEA\xB0\x9C %s", diff >= 0 ? ICON_FK_ARROW_UP : ICON_FK_ARROW_DOWN,
				diff >= 0 ? diff : -diff,
				diff >= 0 ? "\xEC\x95\x9E\xEC\x84\xAC" : "\xEB\x92\xA4\xEC\xA7\x90"); // "앞섬"/"뒤짐"
			dl->AddText(ImVec2(p0.x + pad, y), ImGui::GetColorU32(cv), buf);
			y += lh * 1.2f;
		}

		// 정확도 + 최근 6발
		snprintf(buf, sizeof(buf), "%s %.0f%%", "\xEC\xA0\x95\xED\x99\x95\xEB\x8F\x84", sherbet::aim::accuracy(st) * 100.0f); // "정확도"
		dl->AddText(ImVec2(p0.x + pad, y), sherbet::with_alpha(t.text, 225), buf);
		y += lh;
		float dotx = p0.x + pad + 5.0f;
		for (int i = 0; i < 6 && i < _sherbet_aim.shot_history_count(); ++i)
		{
			const bool hit = _sherbet_aim.shot_history(i);
			if (hit)
				dl->AddCircleFilled(ImVec2(dotx, y + 6.0f), 4.0f, sherbet::with_alpha(t.accent, 235), 12);
			else
				dl->AddCircle(ImVec2(dotx, y + 6.0f), 4.0f, IM_COL32(200, 200, 200, 150), 12, 1.5f);
			dotx += 13.0f;
		}

		if (ph == sherbet::aim::phase::finished)
		{
			dl->AddText(ImVec2(p0.x, p0.y + bh + 8.0f), sherbet::with_alpha(t.text_dim, 215),
				"\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4\xEB\xA5\xBC \xEC\x97\xB4\xEB\xA9\xB4 \xEA\xB2\xB0\xEA\xB3\xBC\xEB\xA5\xBC \xEB\xB3\xBC \xEC\x88\x98 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94"); // "오버레이를 열면 결과를 볼 수 있어요"
		}
	}
}

// SHERBET: 「사격 훈련」 탭.
//
// 이 탭은 게임 메모리를 읽지 않는다. 마우스 입력으로 가상 카메라를 굴리고, 표적을
// 그 방향 공간에 놓고, 클릭한 순간의 각거리로 판정할 뿐이다. 총은 게임에서 실제로
// 나가지만 우리는 그 결과를 모른다 — 우리가 채점하는 것은 **조준**이지 킬이 아니다.
void reshade::runtime::draw_gui_aimlab()
{
	using namespace sherbet;

	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.6f);
	ImGui::TextUnformatted(ICON_FK_BULLSEYE "  \xEC\x82\xAC\xEA\xB2\xA9 \xED\x9B\x88\xEB\xA0\xA8"); // "사격 훈련"
	ImGui::PopFont();
	ImGui::Spacing();

	const bool unlocked = sherbet_feature_unlocked("aimlab");

	// 판 시작. 오버레이를 닫는다 — 열려 있으면 게임이 마우스를 못 받아 조준이 성립하지 않는다.
	auto start_run = [&](bool trial) {
		_sherbet_aim_trial = trial;
		_sherbet_aim_submitted = false; // 판마다 정확히 한 번만 보낸다
		_sherbet_aim_cam_yaw = 0.0f;
		_sherbet_aim_cam_pitch = 0.0f;
		const aim::level lv = static_cast<aim::level>(ImClamp(_sherbet_aim_level, 0, aim::kLevelCount - 1));
		const aim::duration du = trial
			? aim::duration::s10
			: static_cast<aim::duration>(ImClamp(_sherbet_aim_duration, 0, aim::kDurationCount - 1));
		// 시드는 매판 달라야 한다. 테스트에서는 주입하지만 실전에서는 시각으로 흩는다.
		const std::uint32_t seed = static_cast<std::uint32_t>(ImGui::GetTime() * 100000.0)
			^ (static_cast<std::uint32_t>(_sherbet_aim.result().shots) * 2654435761u) ^ 0xA5A5A5A5u;
		_sherbet_aim.start(lv, du, seed, 0.0f, 0.0f, _sherbet_aim_fov);
		_show_overlay = false;
	};

	// ── 잠금 상태: 판매 카드 + 맛보기 ────────────────────────────────────────
	if (!unlocked)
	{
		if (const paid::feature *const f = paid::find("aimlab"))
		{
			sherbet::begin_card("##aimlab_lock");
			sherbet_draw_lock_header(*f);
			ImGui::Spacing();
			// 말로 파는 대신 손에 쥐여 준다. 10초는 몸풀기 길이 그대로라, 정식이 60초라는
			// 걸 보면 차이가 바로 읽힌다. 횟수 제한은 두지 않는다 — 반복해도 난이도를
			// 못 고르고 기록도 안 남아서 "제대로 된 판" 이 되지 않는다.
			if (sherbet::pill_button(ICON_FK_PLAY "  \xEB\xA7\x9B\xEB\xB3\xB4\xEA\xB8\xB0 10\xEC\xB4\x88", true)) // "맛보기 10초"
				start_run(true);
			ImGui::Spacing();
			sherbet_draw_lock_footer(*f);
			sherbet::end_card();
		}
		ImGui::Spacing();
	}

	// ── 난이도 ───────────────────────────────────────────────────────────────
	ImGui::BeginDisabled(!unlocked);
	{
		static const char *const kLv[aim::kLevelCount] = {
			"\xEC\x89\xAC\xEC\x9B\x80",         // "쉬움"
			"\xEB\xB3\xB4\xED\x86\xB5",         // "보통"
			"\xEC\x96\xB4\xEB\xA0\xA4\xEC\x9B\x80", // "어려움"
			"\xED\x97\xAC",                     // "헬"
		};
		for (int i = 0; i < aim::kLevelCount; ++i)
		{
			if (i != 0) ImGui::SameLine();
			if (sherbet::pill_button(kLv[i], _sherbet_aim_level == i))
			{
				_sherbet_aim_level = i;
				save_config();
			}
		}
		ImGui::Spacing();

		// ── 판 길이 ──────────────────────────────────────────────────────────
		// 트로피가 붙은 것만 순위에 올라간다는 게 문구 없이 읽힌다.
		static const char *const kDu[aim::kDurationCount] = { "10\xEC\xB4\x88", "30\xEC\xB4\x88", "60\xEC\xB4\x88" };
		for (int i = 0; i < aim::kDurationCount; ++i)
		{
			if (i != 0) ImGui::SameLine();
			char lbl[48];
			if (aim::ranked(static_cast<aim::duration>(i)))
				snprintf(lbl, sizeof(lbl), "%s  " ICON_FK_TROPHY, kDu[i]);
			else
				snprintf(lbl, sizeof(lbl), "%s", kDu[i]);
			if (sherbet::pill_button(lbl, _sherbet_aim_duration == i))
			{
				_sherbet_aim_duration = i;
				save_config();
			}
		}
		ImGui::Spacing();

		if (!aim::ranked(static_cast<aim::duration>(ImClamp(_sherbet_aim_duration, 0, aim::kDurationCount - 1))))
			ImGui::TextDisabled("%s", "10\xEC\xB4\x88\xEB\x8A\x94 \xEC\x97\xB0\xEC\x8A\xB5\xEC\x9A\xA9\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\x80 30\xEC\xB4\x88\xEC\x99\x80 60\xEC\xB4\x88\xEB\xA7\x8C \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x80\xEC\x9A\x94"); // "10초는 연습용이에요 — 기록은 30초와 60초만 올라가요"

		ImGui::Spacing();
		if (sherbet::pill_button(ICON_FK_PLAY "  \xEC\x8B\x9C\xEC\x9E\x91", true)) // "시작"
			start_run(false);
	}
	ImGui::EndDisabled();

	// ── 결과 ─────────────────────────────────────────────────────────────────
	if (_sherbet_aim.current_phase() == aim::phase::finished)
	{
		const aim::stats &st = _sherbet_aim.result();
		ImGui::Spacing();
		sherbet::begin_card("##aimlab_result");
		ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.8f);
		ImGui::Text("%d", st.hits);
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", "\xEB\xAA\x85\xEC\xA4\x91"); // "명중"

		ImGui::Text("%s %.0f%%   (%d / %d)", "\xEC\xA0\x95\xED\x99\x95\xEB\x8F\x84", // "정확도"
			aim::accuracy(st) * 100.0f, st.hits, st.shots);
		ImGui::TextDisabled("%.2f / \xEC\xB4\x88", aim::per_second(st, _sherbet_aim.current_duration()));

		if (_sherbet_aim_trial)
			ImGui::TextDisabled("%s", "\xEC\x97\xB0\xEC\x8A\xB5 \xED\x8C\x90\xEC\x9D\xB4\xEB\x9D\xBC \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x97\x90 \xEC\x95\x88 \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x80\xEC\x9A\x94"); // "연습 판이라 기록에 안 올라가요"
		else if (!aim::ranked(_sherbet_aim.current_duration()))
			ImGui::TextDisabled("%s", "\xEC\x97\xB0\xEC\x8A\xB5 \xED\x8C\x90\xEC\x9D\xB4\xEB\x9D\xBC \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x97\x90 \xEC\x95\x88 \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x80\xEC\x9A\x94");

		ImGui::Spacing();
		if (sherbet::pill_button(ICON_FK_REFRESH "  \xEB\x8B\xA4\xEC\x8B\x9C \xED\x95\x98\xEA\xB8\xB0", true)) // "다시 하기"
			start_run(_sherbet_aim_trial);
		sherbet::end_card();
	}

	// ── 리더보드 ─────────────────────────────────────────────────────────────
	ImGui::Spacing();
	if (ImGui::CollapsingHeader(ICON_FK_USERS "  \xEB\xA6\xAC\xEB\x8D\x94\xEB\xB3\xB4\xEB\x93\x9C", ImGuiTreeNodeFlags_DefaultOpen)) // "리더보드"
	{
		const aim::duration cur_du = static_cast<aim::duration>(ImClamp(_sherbet_aim_duration, 0, aim::kDurationCount - 1));
		const aim::level cur_lv = static_cast<aim::level>(ImClamp(_sherbet_aim_level, 0, aim::kLevelCount - 1));

		if (!aim::ranked(cur_du))
		{
			// 조용히 빈 표를 보여주면 10초로 잘 나온 사람이 순위를 찾다가 헤맨다.
			ImGui::TextDisabled("%s", "10\xEC\xB4\x88\xEB\x8A\x94 \xEC\x97\xB0\xEC\x8A\xB5\xEC\x9A\xA9\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94 \xE2\x80\x94 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\x80 30\xEC\xB4\x88\xEC\x99\x80 60\xEC\xB4\x88\xEB\xA7\x8C \xEC\x98\xAC\xEB\x9D\xBC\xEA\xB0\x80\xEC\x9A\x94"); // "10초는 연습용이에요 — 기록은 30초와 60초만 올라가요"
		}
		else if (!(sherbet::auth::enabled() && _sherbet_auth.is_authed()))
		{
			ImGui::TextDisabled("%s", "\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8\xED\x95\x98\xEB\xA9\xB4 \xEC\x88\x9C\xEC\x9C\x84\xEB\xA5\xBC \xEB\xB3\xBC \xEC\x88\x98 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94"); // "로그인하면 순위를 볼 수 있어요"
		}
		else
		{
			// 고른 조합이 화면의 표와 다르면 자동으로 받아온다. 사용자가 새로고침을
			// 눌러야만 갱신되면, 난이도를 바꿔놓고 남의 표를 자기 것으로 착각한다.
			const bool stale = !_sherbet_aim_board_ok
				|| _sherbet_aim_board_level != static_cast<int>(cur_lv)
				|| _sherbet_aim_board_duration != static_cast<int>(cur_du);
			if (stale && !_sherbet_aim_net.active())
			{
				_sherbet_aim_board_level = static_cast<int>(cur_lv);
				_sherbet_aim_board_duration = static_cast<int>(cur_du);
				_sherbet_aim_board_ok = false;
				_sherbet_aim_net.begin_fetch(_sherbet_auth.token(), cur_lv, cur_du);
			}

			if (_sherbet_aim_net.active())
			{
				ImGui::TextDisabled("%s", "\xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91"); // "불러오는 중"
			}
			else if (sherbet::pill_button(ICON_FK_REFRESH "  \xEC\x83\x88\xEB\xA1\x9C\xEA\xB3\xA0\xEC\xB9\xA8", false)) // "새로고침"
			{
				_sherbet_aim_board_ok = false;
				_sherbet_aim_net.begin_fetch(_sherbet_auth.token(), cur_lv, cur_du);
			}

			if (_sherbet_aim_board_ok)
			{
				if (_sherbet_aim_board.top.empty())
				{
					// 빈 표를 '없음' 이 아니라 '자리가 비어 있다' 로 보여준다 —
					// 초기 공백이 약점이 아니라 유인이 된다.
					ImGui::TextDisabled("%s", "\xEC\x95\x84\xEC\xA7\x81 \xEC\x95\x84\xEB\xAC\xB4\xEB\x8F\x84 \xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 1\xEC\x9C\x84 \xEC\x9E\x90\xEB\xA6\xAC\xEA\xB0\x80 \xEB\xB9\x84\xEC\x96\xB4 \xEC\x9E\x88\xEC\x96\xB4\xEC\x9A\x94"); // "아직 아무도 기록이 없어요 — 1위 자리가 비어 있어요"
				}
				else
				{
					const float name_x = _imgui_context->Style.WindowPadding.x + 2.6f * ImGui::GetFontSize();
					const float hits_x = name_x + 9.0f * ImGui::GetFontSize();
					for (const aim::board_row &r : _sherbet_aim_board.top)
					{
						ImGui::Text("%d", r.rank);
						ImGui::SameLine(name_x);
						ImGui::TextUnformatted(r.name.c_str());
						ImGui::SameLine(hits_x);
						ImGui::Text("%d   %.0f%%", r.hits, r.accuracy * 100.0f);
					}
				}
				ImGui::Spacing();
				// 5등 밖도 자기 순위는 안다 — 아무것도 안 보이면 다시 안 한다.
				if (_sherbet_aim_board.my_rank > 0)
					ImGui::Text("%s  %d / %d%s", "\xEB\x82\xB4 \xEC\x88\x9C\xEC\x9C\x84", // "내 순위"
						_sherbet_aim_board.my_rank, _sherbet_aim_board.total, "\xEB\xAA\x85"); // "명"
				else
					ImGui::TextDisabled("%s", "\xEA\xB8\xB0\xEB\xA1\x9D \xEC\x97\x86\xEC\x9D\x8C"); // "기록 없음"
			}
		}

		if (_sherbet_aim_msg_timer > 0.0f && !_sherbet_aim_msg.empty())
			ImGui::TextColored(sherbet::status_color(sherbet::status::warn), "%s", _sherbet_aim_msg.c_str());
		ImGui::Spacing();
	}

	// ── 개인 최고 기록 ───────────────────────────────────────────────────────
	ImGui::Spacing();
	if (ImGui::CollapsingHeader(ICON_FK_TROPHY "  \xEC\xB5\x9C\xEA\xB3\xA0 \xEA\xB8\xB0\xEB\xA1\x9D")) // "최고 기록"
	{
		static const char *const kLvS[aim::kLevelCount] = {
			"\xEC\x89\xAC\xEC\x9B\x80", "\xEB\xB3\xB4\xED\x86\xB5", "\xEC\x96\xB4\xEB\xA0\xA4\xEC\x9B\x80", "\xED\x97\xAC" };
		static const char *const kDuS[aim::kDurationCount] = { "10\xEC\xB4\x88", "30\xEC\xB4\x88", "60\xEC\xB4\x88" };
		const float col = _imgui_context->Style.WindowPadding.x + 5.0f * ImGui::GetFontSize();
		for (int li = 0; li < aim::kLevelCount; ++li)
		{
			ImGui::TextUnformatted(kLvS[li]);
			for (int di = 0; di < aim::kDurationCount; ++di)
			{
				ImGui::SameLine(col + di * 5.0f * ImGui::GetFontSize());
				if (_sherbet_aim_best[li][di] > 0)
					ImGui::Text("%s %d", kDuS[di], _sherbet_aim_best[li][di]);
				else
					ImGui::TextDisabled("%s -", kDuS[di]);
			}
		}
		ImGui::Spacing();
	}

	// ── 손에 맞추기 ──────────────────────────────────────────────────────────
	if (ImGui::CollapsingHeader(ICON_FK_SLIDERS "  \xEC\x86\x90\xEC\x97\x90 \xEB\xA7\x9E\xEC\xB6\x94\xEA\xB8\xB0")) // "손에 맞추기"
	{
		ImGui::TextDisabled("%s", "\xED\x91\x9C\xEC\xA0\x81\xEC\x9D\xB4 \xEC\x86\x90\xEC\x97\x90 \xEC\x95\x88 \xEB\xB6\x99\xEC\x9C\xBC\xEB\xA9\xB4 \xEC\x9D\xB4 \xEA\xB0\x92\xEC\x9D\x84 \xEC\xA1\xB0\xEC\xA0\x88\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94"); // "표적이 손에 안 붙으면 이 값을 조절하세요"
		if (ImGui::SliderFloat("\xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4 1\xEC\xB9\xB4\xEC\x9A\xB4\xED\x8A\xB8\xEB\x8B\xB9 \xED\x9A\x8C\xEC\xA0\x84(\xEB\x8F\x84)##aimdpc", // "마우스 1카운트당 회전(도)"
				&_sherbet_aim_dpc, 0.002f, 0.200f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
			save_config();
		if (ImGui::SliderFloat("\xEC\x8B\x9C\xEC\x95\xBC\xEA\xB0\x81(FOV)##aimfov", // "시야각(FOV)"
				&_sherbet_aim_fov, 40.0f, 120.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
			save_config();
		ImGui::Spacing();
	}
}

// SHERBET: "내 전용 불러오기" 버튼 — 「마켓」 세 세그먼트와 「홈」 프리셋 줄 공용.
// 페치 중이면 클릭을 막고 로딩 표시로 바꿔, 되는지 안 되는지 헷갈려 연타하는 것을 방지한다.
// (원래 draw_gui_market 의 지역 람다였다. 「홈」 에서도 같은 버튼이 필요해져 멤버로 올렸다 —
//  복사하면 두 곳의 로딩/완료 표시가 언젠가 어긋난다.)
void reshade::runtime::draw_sherbet_fetch_button()
{
	if (!(sherbet::auth::enabled() && _sherbet_auth.is_authed()))
		return;
	if (_sherbet_auth.content_active())
	{
		const int dots = 1 + static_cast<int>(ImGui::GetTime() * 2.0) % 3; // 1~3, 애니메이션용
		char buf[64];
		snprintf(buf, sizeof(buf), ICON_FK_DOWNLOAD "  \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEB\x8A\x94 \xEC\xA4\x91%.*s", dots, "..."); // "불러오는 중"
		ImGui::BeginDisabled();
		sherbet::pill_button(buf, true);
		ImGui::EndDisabled();
	}
	else if (sherbet::pill_button(ICON_FK_DOWNLOAD "  \xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0", true)) // "내 전용 불러오기"
		_sherbet_auth.begin_fetch_content();
	// 완료 배너(마지막 1초 페이드) — 되는지 안 되는지 헷갈리지 않게 명확히 표시
	if (!_sherbet_auth.content_active() && _sherbet_content_done_timer > 0.0f)
	{
		const float a = ImMin(1.0f, _sherbet_content_done_timer);
		ImVec4 fetch_col = sherbet::status_color(sherbet::status::good); // 테마 명도에 맞는 초록
		fetch_col.w = a;                                                // 마지막 1초 페이드
		ImGui::TextColored(fetch_col, ICON_FK_OK "  \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0 \xEC\x99\x84\xEB\xA3\x8C\x21"); // "불러오기 완료!"
	}
	ImGui::Spacing();
}

// SHERBET: 프리셋 전환 공용 경로.
//
// ⚠️ set_current_preset_path() 만 부르면 **전환은 되는데 화면에 아무 일도 안 일어난다.**
//    「홈」 탭 프리셋 바의 전환 경로(draw_gui_home 의 reload_preset 블록)는 그 뒤에
//    스플래시와 전환 상태를 같이 세운다. 마켓 카드는 그걸 빠뜨려서, 적용해도 바뀐 티가
//    안 났다 — 프리셋 마켓이 안 쓰이던 이유 중 하나다. 두 호출부가 이 함수만 쓰게 한다.
void reshade::runtime::sherbet_apply_preset(const std::filesystem::path &preset_path)
{
	// 저장 → 경로 교체 → load_current_preset 까지는 여기가 다 한다.
	set_current_preset_path(preset_path.u8string().c_str());

	// 여기부터가 시각 피드백. 순서도 홈의 reload_preset 블록과 같다(로드 뒤에 세운다).
	_show_splash = true;
	_preset_is_modified = false;
	_last_preset_switching_time = _last_present_time;
	_is_in_preset_transition = true;
}

// SHERBET: 「홈」 탭 최상단 프리셋 줄.
//
// 왜 있는가: 홈만 쓰는 사람에게는 프리셋이라는 개념이 보이지 않았다. 홈 맨 위 프리셋 바는
// 스톡 리쉐이드 UI 라 **파일명**만 뜨고, 서버가 내려준 Sherbet-Presets 의 존재를 모른다.
// 그래서 다들 아래 이펙트 목록을 하나씩 켜고 껐다 — 「마켓」 탭까지 갈 이유가 없었다.
//
// 마켓과 **같은 소스**(sherbet::content_presets())와 **같은 상태 판정**(_current_preset_path
// 비교)을 쓴다. 그래서 한쪽에서 바꾸면 다른 쪽이 저절로 따라온다 — 동기화 코드가 따로 없다.
//
// 잠긴(안 산) 프리셋은 여기 진열하지 않는다. 판매 노출은 「마켓」 탭이 맡는다.
void reshade::runtime::draw_sherbet_preset_bar()
{
	const std::vector<sherbet::content_item> &presets = sherbet::content_presets();

	// 살 수 있는 것 말고 **지금 쓸 수 있는 것**만 센다. basename 규칙은 마켓·다운로드 워커와
	// 동일해야 카드가 실제 파일을 가리킨다(sherbet::safe_basename).
	std::size_t usable = 0;
	for (const sherbet::content_item &it : presets)
		if (it.unlocked && !sherbet::safe_basename(it.filename).empty())
			++usable;

	const bool can_fetch = sherbet::auth::enabled() && _sherbet_auth.is_authed();

	// 쓸 것도 없고 불러올 수단도 없으면 줄 자체를 띄우지 않는다 — 빈 카드는 소음이다.
	// (오프라인 빌드나 로그인 전에는 홈이 예전 그대로 보인다.)
	if (usable == 0 && !can_fetch)
		return;

	sherbet::begin_card("##sherbet_preset_bar");

	ImGui::TextUnformatted(ICON_FK_MAGIC "  \xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B \xE2\x80\x94 \xED\x95\x9C \xEB\xB2\x88\xEC\x97\x90 \xEB\xB0\x94\xEA\xBE\xB8\xEA\xB8\xB0"); // "프리셋 — 한 번에 바꾸기"
	ImGui::TextDisabled("%s", "\xEC\x95\x84\xEB\x9E\x98 \xEC\x9D\xB4\xED\x8E\x99\xED\x8A\xB8\xEB\xA5\xBC \xED\x95\x98\xEB\x82\x98\xEC\x94\xA9 \xEC\xBC\x9C\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x84\xEB\x8F\x84, \xEC\x97\xAC\xEA\xB8\xB0\xEC\x84\x9C \xEA\xB3\xA0\xEB\xA5\xB4\xEB\xA9\xB4 \xED\x95\x9C \xEC\x84\xB8\xED\x8A\xB8\xEA\xB0\x80 \xED\x86\xB5\xEC\xA7\xB8\xEB\xA1\x9C \xEB\xB0\x94\xEB\x80\x8C\xEC\x96\xB4\xEC\x9A\x94."); // "아래 이펙트를 하나씩 켜지 않아도, 여기서 고르면 한 세트가 통째로 바뀌어요."
	ImGui::Spacing();

	if (usable == 0)
	{
		ImGui::TextDisabled("%s", "\xEB\xB0\x9B\xEC\x9D\x80 \xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEC\x95\x84\xEB\x9E\x98 '\xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0'\xEB\xA5\xBC \xEB\x88\x8C\xEB\x9F\xAC \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94."); // "받은 프리셋이 없어요. 아래 '내 전용 불러오기'를 눌러 주세요."
		ImGui::Spacing();
		draw_sherbet_fetch_button();
		sherbet::end_card();
		return;
	}

	// 알약 줄바꿈 — ImGui::SameLine 은 창 폭을 모른다. pill_padding 으로 폭을 미리 재서
	// 넘칠 때만 줄을 바꾼다(마켓 세그먼트와 같은 방식, 상수도 같은 것을 쓴다).
	const float avail = ImGui::GetContentRegionAvail().x;
	const float spacing = _imgui_context->Style.ItemSpacing.x;
	float line_w = 0.0f;
	bool first_on_line = true;

	for (std::size_t i = 0; i < presets.size(); ++i)
	{
		const sherbet::content_item &it = presets[i];
		if (!it.unlocked)
			continue; // 잠긴 것은 「마켓」 탭에서만 진열한다
		const std::string base = sherbet::safe_basename(it.filename);
		if (base.empty())
			continue;

		const std::filesystem::path preset_path = _config_path.parent_path() / L"Sherbet-Presets" /
			std::filesystem::u8path(base);
		const bool active = _current_preset_path == preset_path;

		// 지금 쓰는 것에는 체크 표시를 붙인다 — 알약 색만으로는 "선택됨" 이 안 읽힌다.
		std::string label;
		if (active)
			label = ICON_FK_OK "  ";
		label += it.display_name;

		const float w = ImGui::CalcTextSize(label.c_str()).x + sherbet::pill_padding.x * 2.0f;

		if (!first_on_line && line_w + spacing + w > avail)
		{
			line_w = 0.0f;
			first_on_line = true;
		}
		if (!first_on_line)
		{
			ImGui::SameLine();
			line_w += spacing;
		}

		ImGui::PushID((int)i);
		if (sherbet::pill_button(label.c_str(), active) && !active)
			sherbet_apply_preset(preset_path);
		ImGui::PopID();

		line_w += w;
		first_on_line = false;
	}

	ImGui::Spacing();
	draw_sherbet_fetch_button();

	sherbet::end_card();
}

void reshade::runtime::draw_gui_market()
{
	ImGui::PushFont(_sherbet_title_font, _imgui_context->Style.FontSizeBase * 1.6f);
	ImGui::TextUnformatted(ICON_FK_SHOPPING_CART "  \xEB\xA7\x88\xEC\xBC\x93"); // "마켓" — 에임/최적화 탭과 표기 통일
	ImGui::PopFont();
	ImGui::Spacing();

	// 세그먼트: 테마 마켓 / 프리셋 마켓 / 조준점 마켓
	// ⚠️ 세그먼트 번호는 「에임」 탭의 「마켓에서 조준점 고르기」 버튼이 직접 세팅하므로
	//    (_sherbet_market_seg = 2) 번호를 재배치하지 않는다.
	int &seg = _sherbet_market_seg;
	if (sherbet::pill_button("\xED\x85\x8C\xEB\xA7\x88 \xEB\xA7\x88\xEC\xBC\x93", seg == 0)) seg = 0; // "테마 마켓"
	ImGui::SameLine();
	if (sherbet::pill_button("\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B \xEB\xA7\x88\xEC\xBC\x93", seg == 1)) seg = 1; // "프리셋 마켓"
	ImGui::SameLine();
	if (sherbet::pill_button("\xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90 \xEB\xA7\x88\xEC\xBC\x93", seg == 2)) seg = 2; // "조준점 마켓"
	ImGui::Spacing();

	if (seg == 0)
	{
		ImGui::TextUnformatted("\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4 \xED\x85\x8C\xEB\xA7\x88. \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x9C \xED\x85\x8C\xEB\xA7\x88\xEB\x8A\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\x84\xB8\xEC\x9A\x94."); // "오버레이 테마. 구매한 테마는 디스코드 로그인 후 불러오세요."
		ImGui::Spacing();

		draw_sherbet_fetch_button(); // /content/me 페치(비동기) — 로딩 중이면 클릭 막힘

		// 커스텀 사진 배경 구매자 안내 — 불러온 뒤 '설정' 탭에서 사진을 지정한다는 것을 알려줌
		if (sherbet::has_feature("custompicture"))
		{
			ImGui::TextWrapped("%s", ICON_FK_OK "  \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEC\x82\xAC\xEC\xA7\x84 \xEB\xB0\xB0\xEA\xB2\xBD\xEC\x9D\x80 '\xEC\x84\xA4\xEC\xA0\x95' \xED\x83\xAD > \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEB\xB0\xB0\xEA\xB2\xBD\xEC\x97\x90\xEC\x84\x9C \xEC\xA7\x80\xEC\xA0\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94"); // "커스텀 사진 배경은 '설정' 탭 > 커스텀 배경에서 지정하세요"
			ImGui::Spacing();
		}

		const std::vector<const sherbet::theme *> snapshot = sherbet::themes_snapshot();
		for (std::size_t i = 0; i < snapshot.size(); ++i)
		{
			const sherbet::theme &th = *snapshot[i];
			const bool unlocked = sherbet::is_unlocked(th.id);
			const bool active = std::strcmp(th.id, sherbet::active_theme_id()) == 0;
			ImGui::PushID((int)i);
			sherbet::begin_card("##theme_card");
			ImDrawList *dl = ImGui::GetWindowDrawList();
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const float bw = ImGui::GetContentRegionAvail().x;
			dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + 26.0f), th.bg1, th.accent, th.accent, th.bg1);
			ImGui::Dummy(ImVec2(bw, 30.0f));
			ImGui::Text("%s", th.display_name);
			if (active)
				ImGui::TextDisabled("%s", ICON_FK_OK " \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"); // "사용 중"
			else if (unlocked)
			{
				if (sherbet::pill_button(ICON_FK_OK "  \xEC\xA0\x81\xEC\x9A\xA9", false)) // "적용"
				{ sherbet::set_active_theme(th.id); save_config(); }
			}
			else
			{
				ImGui::TextDisabled("%s", ICON_FK_LOCK " \xEC\x9E\xA0\xEA\xB9\x80"); // "잠김"
			}
			sherbet::end_card();
			ImGui::PopID();
		}
	}
	else if (seg == 2)
	{
		// 조준점 마켓 — 항목이 공유 코드 한 줄이라 파일 다운로드가 없다.
		// 페치 버튼은 여기서도 같은 것을 쓴다(/content/me 하나로 테마·프리셋·조준점이 전부 온다).
		draw_sherbet_fetch_button();
		draw_gui_crosshair_market();
	}
	else
	{
		// 프리셋 마켓 — 서버가 내려준 "내 전용 프리셋"(디스코드 역할 기반)
		ImGui::TextUnformatted("\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xED\x9B\x84 '\xEB\x82\xB4 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEA\xB8\xB0'\xEB\xA1\x9C \xEB\xB0\x9B\xEC\x95\x84\xEC\x9A\x94."); // "디스코드 로그인 후 '내 전용 불러오기'로 받아요."
		ImGui::Spacing();

		draw_sherbet_fetch_button(); // /content/me 페치(비동기) — 로딩 중이면 클릭 막힘

		const std::vector<sherbet::content_item> &presets = sherbet::content_presets();
		if (presets.empty())
		{
			sherbet::begin_card("##preset_empty");
			ImGui::TextWrapped("\xEB\xB0\x9B\xEC\x9D\x80 \xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B\xEC\x9D\xB4 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94. \xEA\xB6\x8C\xED\x95\x9C\xEC\x9D\xB4 \xEC\x9E\x88\xEC\x9C\xBC\xEB\xA9\xB4 \xEC\x9C\x84 \xEB\xB2\x84\xED\x8A\xBC\xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB6\x88\xEB\x9F\xAC\xEC\x98\xA4\xEC\x84\xB8\xEC\x9A\x94."); // "받은 프리셋이 없어요. 권한이 있으면 위 버튼으로 불러오세요."
			sherbet::end_card();
		}
		for (std::size_t i = 0; i < presets.size(); ++i)
		{
			const sherbet::content_item &it = presets[i];
			ImGui::PushID((int)i);
			sherbet::begin_card("##preset_card");
			ImGui::Text("%s", it.display_name.c_str());

			// ⚠️ 잠긴 상품도 **진열한다.** 예전엔 서버가 권한 있는 것만 내려보내서, 안 산 사람
			//    화면에는 그 상품이 아예 존재하지 않았다 — 팔고 있는 물건을 아무도 몰랐다.
			//    잠긴 항목은 서버가 id(다운로드 키)를 빼고 보내므로 파일은 여전히 못 받는다.
			if (!it.unlocked)
			{
				// (sherbet_draw_lock_footer 의 kLockBuy 는 그 함수의 지역 static 이라 여기서 못 쓴다.
				//  같은 문구를 쓰되 문자열은 이 자리에 둔다.)
				ImGui::TextDisabled("%s", ICON_FK_LOCK "  \xEC\x9E\xA0\xEA\xB9\x80"); // "잠김"
				ImGui::TextLinkOpenURL(ICON_FK_SHOPPING_CART "  " "\xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEC\x97\x90\xEC\x84\x9C \xEA\xB5\xAC\xEB\xA7\xA4\xED\x95\x98\xEA\xB8\xB0", SHERBET_DISCORD_URL); // "디스코드에서 구매하기"
			}
			else
			{
				// 다운로드 경로와 동일하게 basename 만 사용(경로탈출/서브경로 파일명 거부).
				// 워커는 safe_basename 으로 기록하므로 UI 도 같은 규칙이어야 카드가 실제 파일을 가리킨다.
				const std::string base = sherbet::safe_basename(it.filename);
				if (!base.empty())
				{
					const std::filesystem::path preset_path = _config_path.parent_path() / L"Sherbet-Presets" /
						std::filesystem::u8path(base);
					const bool active = _current_preset_path == preset_path;
					if (active)
						ImGui::TextDisabled("%s", ICON_FK_OK " \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"); // "사용 중"
					else if (sherbet::pill_button(ICON_FK_OK "  \xEC\xA0\x81\xEC\x9A\xA9", false)) // "적용"
						sherbet_apply_preset(preset_path); // 「홈」 프리셋 줄과 같은 경로(전환 + 시각 피드백)
				}
			}
			sherbet::end_card();
			ImGui::PopID();
		}
	}
}
#if RESHADE_ADDON
void reshade::runtime::draw_gui_addons()
{
	ini_file &config = global_config();

#if RESHADE_ADDON == 1
	if (!addon_enabled)
	{
		ImGui::PushTextWrapPos();
		ImGui::TextColored(COLOR_YELLOW, _("High network activity discovered.\nAll add-ons are disabled to prevent exploitation."));
		ImGui::PopTextWrapPos();
		return;
	}

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(_("This build of ReShade has only limited add-on functionality."));
#else
	std::filesystem::path addon_search_path = L".\\";
	config.get("ADDON", "AddonPath", addon_search_path);
	if (imgui::directory_input_box(_("Add-on search path"), addon_search_path, _file_selection_path))
		config.set("ADDON", "AddonPath", addon_search_path);
#endif

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	imgui::search_input_box(_addons_filter, sizeof(_addons_filter));

	ImGui::Spacing();

	if (!addon_all_loaded)
	{
		ImGui::PushTextWrapPos();
#if RESHADE_ADDON == 1
		ImGui::TextColored(COLOR_YELLOW, _("Some add-ons were not loaded because this build of ReShade has only limited add-on functionality."));
#else
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
		ImGui::TextUnformatted(_("There were errors loading some add-ons."));
		ImGui::TextUnformatted(_("Check the log for more details."));
		ImGui::PopStyleColor();
#endif
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
	}

	if (ImGui::BeginChild("##addons", ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() + _imgui_context->Style.ItemSpacing.y)), ImGuiChildFlags_NavFlattened))
	{
		std::vector<std::string> disabled_addons;
		config.get("ADDON", "DisabledAddons", disabled_addons);
		std::vector<std::string> collapsed_or_expanded_addons;
		config.get("ADDON", "OverlayCollapsed", collapsed_or_expanded_addons);

		const float child_window_width = ImGui::GetContentRegionAvail().x;

		for (addon_info &info : addon_loaded_info)
		{
			if (!string_contains(info.name, _addons_filter))
				continue;

			ImGui::BeginChild(info.name.c_str(), ImVec2(child_window_width, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);

			const bool builtin = !info.external && info.file.empty();
			const std::string unique_name = builtin ? info.name : info.name + '@' + info.file;

			const auto collapsed_it = std::find(collapsed_or_expanded_addons.begin(), collapsed_or_expanded_addons.end(), unique_name);

			bool open = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##addon_open"), builtin ? collapsed_it == collapsed_or_expanded_addons.end() : collapsed_it != collapsed_or_expanded_addons.end());
			if (ImGui::ArrowButton("##addon_open", open ? ImGuiDir_Down : ImGuiDir_Right))
			{
				ImGui::GetStateStorage()->SetBool(ImGui::GetID("##addon_open"), open = !open);

				if (builtin ? open : !open)
				{
					if (collapsed_it != collapsed_or_expanded_addons.end())
						collapsed_or_expanded_addons.erase(collapsed_it);
				}
				else
				{
					if (collapsed_it == collapsed_or_expanded_addons.end())
						collapsed_or_expanded_addons.push_back(unique_name);
				}

				config.set("ADDON", "OverlayCollapsed", collapsed_or_expanded_addons);
			}

			ImGui::SameLine();

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(info.handle != nullptr ? ImGuiCol_Text : ImGuiCol_TextDisabled));

			const auto disabled_it = std::find_if(disabled_addons.begin(), disabled_addons.end(),
				[&info](const std::string_view addon_name) {
					const size_t at_pos = addon_name.find('@');
					if (at_pos == std::string_view::npos)
						return addon_name == info.name;
					return (at_pos == 0 || addon_name.substr(0, at_pos) == info.name) && addon_name.substr(at_pos + 1) == info.file;
				});

			bool enabled = (disabled_it == disabled_addons.end());
			if (ImGui::Checkbox(info.name.c_str(), &enabled))
			{
				if (enabled)
					disabled_addons.erase(disabled_it);
				else
					disabled_addons.push_back(unique_name);

				config.set("ADDON", "DisabledAddons", disabled_addons);
			}

			ImGui::PopStyleColor();

			if (enabled == (info.handle == nullptr))
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(enabled ? _("(will be enabled on next application restart)") : _("(will be disabled on next application restart)"));
			}

			if (open)
			{
				ImGui::Spacing();
				ImGui::BeginGroup();

				if (!builtin)
					ImGui::Text(_("File:"));
				if (!info.author.empty())
					ImGui::Text(_("Author:"));
				if (info.version.value)
					ImGui::Text(_("Version:"));
				if (!info.description.empty())
					ImGui::Text(_("Description:"));
				if (!info.website_url.empty())
					ImGui::Text(_("Website:"));
				if (!info.issues_url.empty())
					ImGui::Text(_("Issues:"));

				ImGui::EndGroup();
				ImGui::SameLine(ImGui::GetWindowWidth() * 0.25f);
				ImGui::BeginGroup();

				if (!builtin)
					ImGui::TextUnformatted(info.file.c_str(), info.file.c_str() + info.file.size());
				if (!info.author.empty())
					ImGui::TextUnformatted(info.author.c_str(), info.author.c_str() + info.author.size());
				if (info.version.value)
					ImGui::Text("%u.%u.%u.%u", info.version.number.major, info.version.number.minor, info.version.number.build, info.version.number.revision);
				if (!info.description.empty())
				{
					ImGui::PushTextWrapPos();
					ImGui::TextUnformatted(info.description.c_str(), info.description.c_str() + info.description.size());
					ImGui::PopTextWrapPos();
				}
				if (!info.website_url.empty())
					ImGui::TextLinkOpenURL(info.website_url.c_str());
				if (!info.issues_url.empty())
					ImGui::TextLinkOpenURL(info.issues_url.c_str());

				ImGui::EndGroup();

				if (info.settings_overlay_callback != nullptr)
				{
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();

					info.settings_overlay_callback(this);
				}
			}

			ImGui::EndChild();
		}
	}
	ImGui::EndChild();

	ImGui::Spacing();

	ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(_("Open developer documentation")).x) / 2);
	ImGui::TextLinkOpenURL(_("Open developer documentation"), "https://reshade.me/docs");
}
#endif

void reshade::runtime::draw_variable_editor()
{
	ImGui::BeginDisabled(_is_in_preset_transition);

	const ImVec2 popup_pos = ImGui::GetCursorScreenPos() + ImVec2(std::max(0.f, ImGui::GetContentRegionAvail().x * 0.5f - 200.0f), ImGui::GetFrameHeightWithSpacing());

	if (imgui::popup_button(_("Edit global preprocessor definitions"), ImGui::GetContentRegionAvail().x, ImGuiWindowFlags_NoMove))
	{
		ImGui::SetWindowPos(popup_pos);

		bool global_modified = false, preset_modified = false;
		float popup_height = (std::max(_global_preprocessor_definitions.size(), _preset_preprocessor_definitions[{}].size()) + 3) * ImGui::GetFrameHeightWithSpacing();
		popup_height = std::min(popup_height, ImGui::GetWindowViewport()->Size.y - popup_pos.y - 20.0f);
		popup_height = std::max(popup_height, 42.0f); // Ensure window always has a minimum height
		const float button_size = ImGui::GetFrameHeight();
		const float button_spacing = _imgui_context->Style.ItemInnerSpacing.x;

		ImGui::BeginChild("##definitions", ImVec2(30.0f * ImGui::GetFontSize(), popup_height));

		const float content_region_width = ImGui::GetContentRegionAvail().x;

		if (ImGui::BeginTabBar("##definition_types", ImGuiTabBarFlags_NoTooltip))
		{
			struct
			{
				std::string name;
				std::vector<std::pair<std::string, std::string>> &definitions;
				bool &modified;
			} const definition_types[] = {
				{ _("Global"), _global_preprocessor_definitions, global_modified },
				{ _("Current Preset"), _preset_preprocessor_definitions[{}], preset_modified },
			};

			for (const auto &type : definition_types)
			{
				if (ImGui::BeginTabItem(type.name.c_str()))
				{
					ImGui::Dummy(ImVec2());
					ImGui::SameLine(0, button_spacing);
					ImGui::TextUnformatted(_("Name"));
					ImGui::SameLine(content_region_width * 0.66666666f, button_spacing);
					ImGui::TextUnformatted(_("Value"));

					if (&type.modified == &preset_modified)
						ImGui::BeginDisabled(!_auto_save_preset);

					for (auto it = type.definitions.begin(); it != type.definitions.end();)
					{
						char name[128];
						name[it->first.copy(name, sizeof(name) - 1)] = '\0';
						char value[256];
						value[it->second.copy(value, sizeof(value) - 1)] = '\0';

						ImGui::PushID(static_cast<int>(std::distance(type.definitions.begin(), it)));

						ImGui::SetNextItemWidth(content_region_width * 0.66666666f - (button_spacing));
						type.modified |= ImGui::InputText("##name", name, sizeof(name), ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CallbackCharFilter,
							[](ImGuiInputTextCallbackData *data) -> int { return data->EventChar == '=' || (data->EventChar != '_' && !isalnum(data->EventChar)); }); // Filter out invalid characters

						ImGui::SameLine(0, button_spacing);

						ImGui::SetNextItemWidth(content_region_width * 0.33333333f - (button_spacing + button_size));
						type.modified |= ImGui::InputText("##value", value, sizeof(value));

						ImGui::SameLine(0, button_spacing);

						if (imgui::confirm_button(ICON_FK_MINUS, button_size, _("Do you really want to remove the preprocessor definition '%s'?"), name))
						{
							type.modified = true;
							it = type.definitions.erase(it);
						}
						else
						{
							if (type.modified)
							{
								it->first = name;
								it->second = value;
							}

							++it;
						}

						ImGui::PopID();
					}

					ImGui::Dummy(ImVec2());
					ImGui::SameLine(0, content_region_width - button_size);
					if (ImGui::Button(ICON_FK_PLUS, ImVec2(button_size, 0)))
						type.definitions.emplace_back();

					if (&type.modified == &preset_modified)
						ImGui::EndDisabled();

					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}

		ImGui::EndChild();

		const float apply_button_size = 8.0f * ImGui::GetFontSize();

		ImGui::Dummy(ImVec2());
		ImGui::SameLine(0, content_region_width - apply_button_size);
		if (ImGui::Button(ICON_FK_OK " " + _("Apply"), ImVec2(apply_button_size, 0)))
			ImGui::CloseCurrentPopup();

		if (global_modified)
			save_config();
		if (preset_modified)
			save_current_preset();
		if (global_modified || preset_modified)
			_was_preprocessor_popup_edited = true;

		ImGui::EndPopup();
	}
	else if (_was_preprocessor_popup_edited)
	{
		reload_effects();
		_was_preprocessor_popup_edited = false;
	}

	ImGui::BeginChild("##variables", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
	if (_variable_editor_tabs)
		ImGui::BeginTabBar("##variables", ImGuiTabBarFlags_TabListPopupButton | ImGuiTabBarFlags_FittingPolicyScroll);

	for (size_t effect_index = 0, id = 0; effect_index < _effects.size(); ++effect_index)
	{
		reshade::effect &effect = _effects[effect_index];

		// Hide variables that are not currently used in any of the active effects
		// Also skip showing this effect in the variable list if it doesn't have any uniform variables to show
		if (!effect.rendering || (effect.uniforms.empty() && effect.definitions.empty()))
			continue;
		assert(effect.compiled);

		bool force_reload_effect = false;
		const bool is_focused = _focused_effect == effect_index;
		const std::string effect_name = effect.source_file.filename().u8string();

		// Create separate tab for every effect file
		if (_variable_editor_tabs)
		{
			ImGuiTabItemFlags flags = 0;
			if (is_focused)
				flags |= ImGuiTabItemFlags_SetSelected;

			if (!ImGui::BeginTabItem(effect_name.c_str(), nullptr, flags))
				continue;
			// Begin a new child here so scrolling through variables does not move the tab itself too
			ImGui::BeginChild("##tab");
		}
		else
		{
			if (is_focused || _effects_expanded_state & 1)
				ImGui::SetNextItemOpen(is_focused || (_effects_expanded_state >> 1) != 0);

			if (!ImGui::TreeNodeEx(effect_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				continue; // Skip rendering invisible items
		}

		if (is_focused)
		{
			ImGui::SetScrollHereY(0.0f);
			_focused_effect = std::numeric_limits<size_t>::max();
		}

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(_imgui_context->Style.FramePadding.x, 0));
		if (imgui::confirm_button(
				ICON_FK_UNDO " " + _("Reset all to default"),
				_variable_editor_tabs ? ImGui::GetContentRegionAvail().x : ImGui::CalcItemWidth(),
				_("Do you really want to reset all values in '%s' to their defaults?"), effect_name.c_str()))
		{
			// Reset all uniform variables
			for (uniform &variable_it : effect.uniforms)
				if (variable_it.special == special_uniform::none && !variable_it.annotation_as_uint("noreset"))
					reset_uniform_value(variable_it);

			// Reset all preprocessor definitions
			if (const auto preset_it = _preset_preprocessor_definitions.find({});
				preset_it != _preset_preprocessor_definitions.end() && !preset_it->second.empty())
			{
				for (const std::pair<std::string, std::string> &definition : effect.definitions)
				{
					if (const auto it = std::remove_if(preset_it->second.begin(), preset_it->second.end(),
							[&definition](const std::pair<std::string, std::string> &preset_definition) { return preset_definition.first == definition.first; });
						it != preset_it->second.end())
					{
						preset_it->second.erase(it, preset_it->second.end());
						force_reload_effect = true; // Need to reload after changing preprocessor defines so to get accurate defaults again
					}
				}
			}

			if (const auto preset_it = _preset_preprocessor_definitions.find(effect_name);
				preset_it != _preset_preprocessor_definitions.end() && !preset_it->second.empty())
			{
				_preset_preprocessor_definitions.erase(preset_it);
				force_reload_effect = true;
			}

			if (_auto_save_preset)
				save_current_preset();
			else
				_preset_is_modified = true;
		}
		ImGui::PopStyleVar();

		bool category_closed = false;
		bool category_visible = true;
		std::string current_category;

		size_t active_variable = 0;
		size_t active_variable_index = std::numeric_limits<size_t>::max();
		size_t hovered_variable = 0;
		size_t hovered_variable_index = std::numeric_limits<size_t>::max();

		for (size_t variable_index = 0; variable_index < effect.uniforms.size(); ++variable_index)
		{
			reshade::uniform &variable = effect.uniforms[variable_index];

			// Skip hidden and special variables
			if (variable.annotation_as_int("hidden") || variable.special != special_uniform::none)
			{
				if (variable.special == special_uniform::overlay_active)
					active_variable_index = variable_index;
				else if (variable.special == special_uniform::overlay_hovered)
					hovered_variable_index = variable_index;
				continue;
			}

			if (const std::string_view category = variable.annotation_as_string("ui_category");
				category != current_category)
			{
				current_category = category;

				if (!current_category.empty())
				{
					std::string category_label(get_localized_annotation(variable, "ui_category", _current_language));
					if (!_variable_editor_tabs)
					{
						size_t num_spaces = 0;
						for (float x = 0, space_x = ImGui::CalcTextSize(" ").x, width = (ImGui::CalcItemWidth() - ImGui::CalcTextSize(category_label.data()).x - 45) / 2; x < width; x += space_x)
							num_spaces++;
						category_label.insert(0, num_spaces, ' ');
						// Ensure widget ID does not change with varying width
						category_label += "###" + current_category;
						// Append a unique value so that the context menu does not contain duplicated widgets when a category is made current multiple times
						category_label += std::to_string(variable_index);
					}

					if (category_visible = true;
						variable.annotation_as_uint("ui_category_toggle") != 0)
						get_uniform_value(variable, &category_visible);

					if (category_visible)
					{
						ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_NoTreePushOnOpen;
						if (!variable.annotation_as_int("ui_category_closed"))
							flags |= ImGuiTreeNodeFlags_DefaultOpen;

						category_closed = !ImGui::TreeNodeEx(category_label.c_str(), flags);

						if (ImGui::BeginPopupContextItem(category_label.c_str()))
						{
							char label[64] = "";
							ImFormatString(label, IM_ARRAYSIZE(label), ICON_FK_UNDO " " + _("Reset all in '%s' to default"), current_category.c_str());

							if (imgui::confirm_button(
									label,
									ImGui::GetContentRegionAvail().x,
									_("Do you really want to reset all values in '%s' to their defaults?"), current_category.c_str()))
							{
								for (uniform &variable_it : effect.uniforms)
									if (variable_it.special == special_uniform::none && !variable_it.annotation_as_uint("noreset") &&
										variable_it.annotation_as_string("ui_category") == category)
										reset_uniform_value(variable_it);

								if (_auto_save_preset)
									save_current_preset();
								else
									_preset_is_modified = true;
							}

							ImGui::EndPopup();
						}
					}
					else
					{
						category_closed = false;
					}
				}
				else
				{
					category_closed = false;
					category_visible = true;
				}
			}

			// Skip rendering invisible items
			if (category_closed || (!category_visible && !variable.annotation_as_uint("ui_category_toggle")))
				continue;

			bool modified = false;
			bool is_default_value = true;

			ImGui::PushID(static_cast<int>(id++));

			reshadefx::constant value;
			switch (variable.type.base)
			{
			case reshadefx::type::t_bool:
				get_uniform_value(variable, value.as_uint, variable.type.components());
				for (size_t i = 0; is_default_value && i < variable.type.components(); i++)
					is_default_value = (value.as_uint[i] != 0) == (variable.initializer_value.as_uint[i] != 0);
				break;
			case reshadefx::type::t_int:
			case reshadefx::type::t_uint:
				get_uniform_value(variable, value.as_int, variable.type.components());
				is_default_value = std::memcmp(value.as_int, variable.initializer_value.as_int, variable.type.components() * sizeof(int)) == 0;
				break;
			case reshadefx::type::t_float:
				const float threshold = variable.annotation_as_float("ui_step", 0, 0.001f) * 0.75f + FLT_EPSILON;
				get_uniform_value(variable, value.as_float, variable.type.components());
				for (size_t i = 0; is_default_value && i < variable.type.components(); i++)
					is_default_value = std::abs(value.as_float[i] - variable.initializer_value.as_float[i]) < threshold;
				break;
			}

#if RESHADE_ADDON
			if (invoke_addon_event<addon_event::reshade_overlay_uniform_variable>(this, api::effect_uniform_variable{ reinterpret_cast<uintptr_t>(&variable) }))
			{
				reshadefx::constant new_value;
				switch (variable.type.base)
				{
				case reshadefx::type::t_bool:
					get_uniform_value(variable, new_value.as_uint, variable.type.components());
					for (size_t i = 0; !modified && i < variable.type.components(); i++)
						modified = (new_value.as_uint[i] != 0) != (value.as_uint[i] != 0);
					break;
				case reshadefx::type::t_int:
				case reshadefx::type::t_uint:
					get_uniform_value(variable, new_value.as_int, variable.type.components());
					modified = std::memcmp(new_value.as_int, value.as_int, variable.type.components() * sizeof(int)) != 0;
					break;
				case reshadefx::type::t_float:
					get_uniform_value(variable, new_value.as_float, variable.type.components());
					for (size_t i = 0; !modified && i < variable.type.components(); i++)
						modified = std::abs(new_value.as_float[i] - value.as_float[i]) > FLT_EPSILON;
					break;
				}
			}
			else
#endif
			{
				// Add spacing before variable widget
				for (int i = 0, spacing = variable.annotation_as_int("ui_spacing"); i < spacing; ++i)
					ImGui::Spacing();

				// Add user-configurable text before variable widget
				if (const std::string_view text = get_localized_annotation(variable, "ui_text", _current_language);
					!text.empty())
				{
					ImGui::PushTextWrapPos();
					ImGui::TextUnformatted(text.data(), text.data() + text.size());
					ImGui::PopTextWrapPos();
				}

				ImGui::BeginDisabled(variable.annotation_as_uint("noedit") != 0);

				std::string_view label = get_localized_annotation(variable, "ui_label", _current_language);
				if (label.empty())
					label = variable.name;
				const std::string_view ui_type = variable.annotation_as_string("ui_type");

				switch (variable.type.base)
				{
				case reshadefx::type::t_bool:
					{
						if (ui_type == "button")
						{
							if (ImGui::Button(label.data(), ImVec2(ImGui::CalcItemWidth(), 0)))
							{
								value.as_uint[0] = 1;
								modified = true;
							}
							else if (value.as_uint[0] != 0)
							{
								// Reset value again next frame after button was pressed
								value.as_uint[0] = 0;
								modified = true;
							}
						}
						else if (ui_type == "combo")
							modified = imgui::combo_with_buttons(label.data(), reinterpret_cast<bool *>(&value.as_uint[0]));
						else
							modified = imgui::checkbox_list(label.data(), get_localized_annotation(variable, "ui_items", _current_language), value.as_uint, variable.type.components());

						if (modified)
							set_uniform_value(variable, value.as_uint, variable.type.components());
					}
					break;
				case reshadefx::type::t_int:
				case reshadefx::type::t_uint:
					{
						const int ui_min_val = variable.annotation_as_int("ui_min", 0, ui_type == "slider" ? 0 : std::numeric_limits<int>::lowest());
						const int ui_max_val = variable.annotation_as_int("ui_max", 0, ui_type == "slider" ? 1 : std::numeric_limits<int>::max());
						const int ui_stp_val = variable.annotation_as_int("ui_step", 0, 1);

						// Append units
						std::string format = "%d";
						format += get_localized_annotation(variable, "ui_units", _current_language);

						if (ui_type == "slider")
							modified = imgui::slider_with_buttons(label.data(), variable.type.is_signed() ? ImGuiDataType_S32 : ImGuiDataType_U32, value.as_int, variable.type.rows, &ui_stp_val, &ui_min_val, &ui_max_val, format.c_str());
						else if (ui_type == "drag")
							modified = variable.annotation_as_int("ui_step") == 0 ?
								ImGui::DragScalarN(label.data(), variable.type.is_signed() ? ImGuiDataType_S32 : ImGuiDataType_U32, value.as_int, variable.type.rows, 1.0f, &ui_min_val, &ui_max_val, format.c_str()) :
								imgui::drag_with_buttons(label.data(), variable.type.is_signed() ? ImGuiDataType_S32 : ImGuiDataType_U32, value.as_int, variable.type.rows, &ui_stp_val, &ui_min_val, &ui_max_val, format.c_str());
						else if (ui_type == "list")
							modified = imgui::list_with_buttons(label.data(), get_localized_annotation(variable, "ui_items", _current_language), &value.as_int[0]);
						else if (ui_type == "combo")
							modified = imgui::combo_with_buttons(label.data(), get_localized_annotation(variable, "ui_items", _current_language), &value.as_int[0]);
						else if (ui_type == "radio")
							modified = imgui::radio_list(label.data(), get_localized_annotation(variable, "ui_items", _current_language), &value.as_int[0]);
						else if (variable.type.is_matrix())
							for (unsigned int row = 0; row < variable.type.rows; ++row)
								modified |= ImGui::InputScalarN((std::string(label) + " [row " + std::to_string(row) + ']').c_str(), variable.type.is_signed() ? ImGuiDataType_S32 : ImGuiDataType_U32, &value.as_int[variable.type.cols * row], variable.type.cols) || modified;
						else
							modified = ImGui::InputScalarN(label.data(), variable.type.is_signed() ? ImGuiDataType_S32 : ImGuiDataType_U32, value.as_int, variable.type.rows);

						if (modified)
							set_uniform_value(variable, value.as_int, variable.type.components());
					}
					break;
				case reshadefx::type::t_float:
					{
						const float ui_min_val = variable.annotation_as_float("ui_min", 0, ui_type == "slider" ? 0.0f : std::numeric_limits<float>::lowest());
						const float ui_max_val = variable.annotation_as_float("ui_max", 0, ui_type == "slider" ? 1.0f : std::numeric_limits<float>::max());
						const float ui_stp_val = variable.annotation_as_float("ui_step", 0, 0.001f);

						// Calculate display precision based on step value
						std::string precision_format = "%.0f";
						for (float x = 1.0f; x * ui_stp_val < 1.0f && precision_format[2] < '9'; x *= 10.0f)
							++precision_format[2]; // This changes the text to "%.1f", "%.2f", "%.3f", ...

						// Append units
						precision_format += get_localized_annotation(variable, "ui_units", _current_language);

						if (ui_type == "slider")
							modified = imgui::slider_with_buttons(label.data(), ImGuiDataType_Float, value.as_float, variable.type.rows, &ui_stp_val, &ui_min_val, &ui_max_val, precision_format.c_str());
						else if (ui_type == "drag")
							modified = variable.annotation_as_float("ui_step") == 0.0f ?
								ImGui::DragScalarN(label.data(), ImGuiDataType_Float, value.as_float, variable.type.rows, ui_stp_val, &ui_min_val, &ui_max_val, precision_format.c_str()) :
								imgui::drag_with_buttons(label.data(), ImGuiDataType_Float, value.as_float, variable.type.rows, &ui_stp_val, &ui_min_val, &ui_max_val, precision_format.c_str());
						else if (ui_type == "color" && variable.type.rows == 1)
							modified = imgui::slider_for_alpha_value(label.data(), value.as_float);
						else if (ui_type == "color" && variable.type.rows == 3)
							modified = ImGui::ColorEdit3(label.data(), value.as_float, ImGuiColorEditFlags_NoOptions);
						else if (ui_type == "color" && variable.type.rows == 4)
							modified = ImGui::ColorEdit4(label.data(), value.as_float, ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_AlphaBar);
						else if (variable.type.is_matrix())
							for (unsigned int row = 0; row < variable.type.rows; ++row)
								modified |= ImGui::InputScalarN((std::string(label) + " [row " + std::to_string(row) + ']').c_str(), ImGuiDataType_Float, &value.as_float[variable.type.cols * row], variable.type.cols) || modified;
						else
							modified = ImGui::InputScalarN(label.data(), ImGuiDataType_Float, value.as_float, variable.type.rows);

						if (modified)
							set_uniform_value(variable, value.as_float, variable.type.components());
					}
					break;
				}

				ImGui::EndDisabled();

				// Display tooltip
				if (const std::string_view tooltip = get_localized_annotation(variable, "ui_tooltip", _current_language);
					!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
				{
					if (ImGui::BeginTooltip())
					{
						ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
						ImGui::EndTooltip();
					}
				}
			}

			if (ImGui::IsItemActive())
				active_variable = variable_index + 1;
			if (ImGui::IsItemHovered())
				hovered_variable = variable_index + 1;

			// Create context menu
			if (ImGui::BeginPopupContextItem("##context"))
			{
				ImGui::SetNextItemWidth(18.0f * ImGui::GetFontSize());
				if (variable.supports_toggle_key() &&
					_input != nullptr &&
					imgui::key_input_box("##toggle_key", variable.toggle_key_data, *_input))
					modified = true;

				if (ImGui::Button(ICON_FK_UNDO " " + _("Reset to default"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
				{
					modified = true;
					reset_uniform_value(variable);
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (!is_default_value && !variable.annotation_as_uint("noreset"))
			{
				ImGui::SameLine();
				if (ImGui::SmallButton(ICON_FK_UNDO))
				{
					modified = true;
					reset_uniform_value(variable);
				}
			}

			if (variable.toggle_key_data[0] != 0)
			{
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
				ImGui::TextDisabled("%s", input::key_name(variable.toggle_key_data).c_str());
			}

			ImGui::PopID();

			// A value has changed, so save the current preset
			if (modified && !variable.annotation_as_uint("nosave"))
			{
				if (_auto_save_preset)
					save_current_preset();
				else
					_preset_is_modified = true;
			}
		}

		if (active_variable_index < effect.uniforms.size())
			set_uniform_value(effect.uniforms[active_variable_index], static_cast<uint32_t>(active_variable));
		if (hovered_variable_index < effect.uniforms.size())
			set_uniform_value(effect.uniforms[hovered_variable_index], static_cast<uint32_t>(hovered_variable));

		// Draw preprocessor definition list after all uniforms of an effect file
		std::vector<std::pair<std::string, std::string>> &effect_definitions = _preset_preprocessor_definitions[effect_name];
		std::vector<std::pair<std::string, std::string>>::iterator modified_definition;

		if (!effect.definitions.empty())
		{
			std::string category_label = _("Preprocessor definitions");
			if (!_variable_editor_tabs)
			{
				for (float x = 0, space_x = ImGui::CalcTextSize(" ").x, width = (ImGui::CalcItemWidth() - ImGui::CalcTextSize(category_label.c_str()).x - 45) / 2; x < width; x += space_x)
					category_label.insert(0, " ");
				category_label += "###ppdefinitions";
			}

			if (ImGui::TreeNodeEx(category_label.c_str(), ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_DefaultOpen))
			{
				for (const std::pair<std::string, std::string> &definition : effect.definitions)
				{
					std::vector<std::pair<std::string, std::string>> *definition_scope = nullptr;
					std::vector<std::pair<std::string, std::string>>::iterator definition_it;

					char value[256];
					if (get_preprocessor_definition(effect_name, definition.first, 0b111, definition_scope, definition_it))
						value[definition_it->second.copy(value, sizeof(value) - 1)] = '\0';
					else
						value[0] = '\0';

					if (ImGui::InputTextWithHint(definition.first.c_str(), definition.second.c_str(), value, sizeof(value), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue))
					{
						if (value[0] == '\0') // An empty value removes the definition
						{
							if (definition_scope == &effect_definitions)
							{
								force_reload_effect = true;
								definition_scope->erase(definition_it);
							}
						}
						else
						{
							force_reload_effect = true;

							if (definition_scope == &effect_definitions)
							{
								definition_it->second = value;
								modified_definition = definition_it;
							}
							else
							{
								effect_definitions.emplace_back(definition.first, value);
								modified_definition = effect_definitions.end() - 1;
							}
						}
					}

					if (force_reload_effect) // Cannot compare iterators if definitions were just modified above
						continue;

					if (ImGui::BeginPopupContextItem())
					{
						if (ImGui::Button(ICON_FK_UNDO " " + _("Reset to default"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
						{
							if (definition_scope != nullptr)
							{
								force_reload_effect = true;
								definition_scope->erase(definition_it);
							}

							ImGui::CloseCurrentPopup();
						}

						ImGui::EndPopup();
					}

					if (definition_scope == &effect_definitions)
					{
						ImGui::PushID(definition_it->first.c_str());

						ImGui::SameLine();
						if (ImGui::SmallButton(ICON_FK_UNDO))
						{
							force_reload_effect = true;
							definition_scope->erase(definition_it);
						}

						ImGui::PopID();
					}
				}
			}
		}

		if (_variable_editor_tabs)
		{
			ImGui::EndChild();
			ImGui::EndTabItem();
		}
		else
		{
			ImGui::TreePop();
		}

		if (force_reload_effect)
		{
			save_current_preset();

			_preset_is_modified = false;

			const bool reload_successful_before = _last_reload_successful;

			// Reload current effect file
			if (!reload_effect(effect_index) && modified_definition != std::vector<std::pair<std::string, std::string>>::iterator())
			{
				// The preprocessor definition that was just modified caused the effect to not compile, so reset to default and try again
				effect_definitions.erase(modified_definition);

				if (reload_effect(effect_index))
				{
					_last_reload_successful = reload_successful_before;
					ImGui::OpenPopup("##pperror"); // Notify the user about this

					// Update preset again now, so that the removed preprocessor definition does not reappear on a reload
					// The preset is actually loaded again next frame to update the technique status (see 'update_effects'), so cannot use 'save_current_preset' here
					ini_file::load_cache(_current_preset_path).set(effect_name, "PreprocessorDefinitions", effect_definitions);
				}
			}

			// Reloading an effect file invalidates all textures, but the statistics window may already have drawn references to those, so need to reset it
			if (ImGuiWindow *const statistics_window = ImGui::FindWindowByName("###statistics"))
				statistics_window->DrawList->CmdBuffer.clear();
		}
	}

	if (ImGui::BeginPopup("##pperror"))
	{
		ImGui::TextColored(COLOR_RED, _("The effect failed to compile after this change, so reverted back to the default value."));
		ImGui::EndPopup();
	}

	if (_variable_editor_tabs)
		ImGui::EndTabBar();
	ImGui::EndChild();

	ImGui::EndDisabled();
}
void reshade::runtime::draw_technique_editor()
{
	if (_reload_count != 0 && _effects.empty())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, COLOR_YELLOW);
		ImGui::TextWrapped(_("No effect files (.fx) found in the configured effect search paths%c"), _effect_search_paths.empty() ? '.' : ':');
		for (const std::filesystem::path &search_path : _effect_search_paths)
			ImGui::Text("  %s", (g_reshade_base_path / search_path).lexically_normal().u8string().c_str());
		ImGui::Spacing();
		ImGui::TextWrapped(_("Go to the settings and configure the 'Effect search paths' option to point to the directory containing effect files, then hit 'Reload'!"));
		ImGui::PopStyleColor();
		return;
	}

	ImGui::BeginDisabled(_is_in_preset_transition);

	// SHERBET: 이펙트 필터 (전체 / 켜짐 / 즐겨찾기)
	{
		if (sherbet::pill_button("\xEC\xA0\x84\xEC\xB2\xB4", _sherbet_effect_filter == 0)) { _sherbet_effect_filter = 0; save_config(); } // "전체"
		ImGui::SameLine();
		if (sherbet::pill_button("\xEC\xBC\x9C\xEC\xA7\x90", _sherbet_effect_filter == 1)) { _sherbet_effect_filter = 1; save_config(); } // "켜짐"
		ImGui::SameLine();
		if (sherbet::pill_button(ICON_FK_STAR " \xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0", _sherbet_effect_filter == 2)) { _sherbet_effect_filter = 2; save_config(); } // "즐겨찾기"
		ImGui::Spacing();
	}

	if (!_last_reload_successful)
	{
		// Add fake items at the top for effects that failed to compile
		for (size_t effect_index = 0; effect_index < _effects.size(); ++effect_index)
		{
			const effect &effect = _effects[effect_index];

			if (effect.compiled || effect.skipped)
				continue;

			ImGui::PushID(static_cast<int>(_technique_sorting.size() + effect_index));

			ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);

			{
				char label[128] = "";
				ImFormatString(label, IM_ARRAYSIZE(label), _("[%s] failed to compile"), effect.source_file.filename().u8string().c_str());

				bool value = false;
				ImGui::Checkbox(label, &value);
			}

			ImGui::PopItemFlag();

			// Display tooltip
			if (!effect.errors.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_ForTooltip))
			{
				if (ImGui::BeginTooltip())
				{
					parse_errors(effect.errors,
						[](const std::string_view file, int line, const std::string_view message) {
							if (file.empty())
								ImGui::TextUnformatted(message.data(), message.data() + message.size());
							else
								ImGui::Text("%s(%d): %.*s", std::filesystem::path(file).filename().u8string().c_str(), line, message.size(), message.data());
						});
					ImGui::EndTooltip();
				}
			}

			ImGui::PopStyleColor();

			// Create context menu
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::OpenPopup("##context", ImGuiPopupFlags_MouseButtonRight);

			if (ImGui::BeginPopup("##context"))
			{
				if (ImGui::Button(ICON_FK_FOLDER " " + _("Open folder in explorer"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
					utils::open_explorer(effect.source_file);

				ImGui::Separator();

				if (imgui::popup_button(ICON_FK_PENCIL " " + _("Edit source code"), 18.0f * ImGui::GetFontSize()))
				{
					std::unordered_map<std::string_view, std::string> file_errors_lookup;
					parse_errors(effect.errors,
						[&file_errors_lookup](const std::string_view file, int line, const std::string_view message) {
							if (file.empty())
								return;
							file_errors_lookup[file] += std::string(file) + '(' + std::to_string(line) + "): " + std::string(message) + '\n';
						});

					const auto source_file_errors_it = file_errors_lookup.find(effect.source_file.u8string());
					if (source_file_errors_it != file_errors_lookup.end())
						ImGui::PushStyleColor(ImGuiCol_Text, source_file_errors_it->second.find("error") != std::string::npos ? COLOR_RED : COLOR_YELLOW);

					std::filesystem::path source_file;
					if (ImGui::MenuItem(effect.source_file.filename().u8string().c_str()))
						source_file = effect.source_file;

					if (source_file_errors_it != file_errors_lookup.end())
					{
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
						{
							if (ImGui::BeginTooltip())
							{
								ImGui::TextUnformatted(source_file_errors_it->second.c_str(), source_file_errors_it->second.c_str() + source_file_errors_it->second.size());
								ImGui::EndTooltip();
							}
						}

						ImGui::PopStyleColor();
					}

					if (!effect.included_files.empty())
					{
						ImGui::Separator();

						for (const std::filesystem::path &included_file : effect.included_files)
						{
							// Color file entries that contain warnings or errors
							const auto included_file_errors_it = file_errors_lookup.find(included_file.u8string());
							if (included_file_errors_it != file_errors_lookup.end())
								ImGui::PushStyleColor(ImGuiCol_Text, included_file_errors_it->second.find("error") != std::string::npos ? COLOR_RED : COLOR_YELLOW);

							std::filesystem::path display_path = included_file.lexically_relative(effect.source_file.parent_path());
							if (display_path.empty())
								display_path = included_file.filename();
							if (ImGui::MenuItem(display_path.u8string().c_str()))
								source_file = included_file;

							if (included_file_errors_it != file_errors_lookup.end())
							{
								if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
								{
									if (ImGui::BeginTooltip())
									{
										ImGui::TextUnformatted(included_file_errors_it->second.c_str(), included_file_errors_it->second.c_str() + included_file_errors_it->second.size());
										ImGui::EndTooltip();
									}
								}

								ImGui::PopStyleColor();
							}
						}
					}

					ImGui::EndPopup();

					if (!source_file.empty())
					{
						open_code_editor(effect_index, source_file);
						ImGui::CloseCurrentPopup();
					}
				}

				for (size_t permutation_index = 0; permutation_index < effect.permutations.size(); ++permutation_index)
				{
					std::string label = _("Show compiled results");
					if (effect.permutations.size() > 1)
						label += " (" + std::to_string(permutation_index) + ")";

					if (!effect.permutations[permutation_index].generated_code.empty() &&
						imgui::popup_button(label.c_str(), 18.0f * ImGui::GetFontSize()))
					{
						const bool open_generated_code = ImGui::MenuItem(_("Generated code"));

						ImGui::EndPopup();

						if (open_generated_code)
						{
							open_code_editor(effect_index, permutation_index, std::string());
							ImGui::CloseCurrentPopup();
						}
					}
				}

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}
	}

	size_t hovered_technique_index = std::numeric_limits<size_t>::max();

	for (size_t index = 0; index < _technique_sorting.size(); ++index)
	{
		const size_t technique_index = _technique_sorting[index];
		{
			technique &tech = _techniques[technique_index];
			const effect &effect = _effects[tech.effect_index];

			// Skip hidden techniques
			if (tech.hidden || !effect.compiled)
				continue;

			// SHERBET: 필터 적용 (PushID 이전이라 ID 스택 안전)
			if (_sherbet_effect_filter == 1 && !tech.enabled)
				continue;
			if (_sherbet_effect_filter == 2 && _sherbet_fav.count(tech.name) == 0)
				continue;

			bool modified = false;

			ImGui::PushID(static_cast<int>(index));

			// Draw border around the item if it is selected
			const bool draw_border = _selected_technique == index;
			if (draw_border)
				ImGui::Separator();

			// Prevent user from disabling the technique when it is set to always be enabled via annotation
			const bool force_enabled = tech.annotation_as_int("enabled");

#if RESHADE_ADDON
			if (bool was_enabled = tech.enabled;
				invoke_addon_event<addon_event::reshade_overlay_technique>(this, api::effect_technique { reinterpret_cast<uintptr_t>(&tech) }))
			{
				modified = tech.enabled != was_enabled;
			}
			else
#endif
			{
				ImGui::BeginDisabled(tech.annotation_as_uint("noedit") != 0);

				// Gray out disabled techniques
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(tech.enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));

				std::string label(get_localized_annotation(tech, "ui_label", _current_language));
				if (label.empty())
					label = tech.name;
				label += " [" + effect.source_file.filename().u8string() + ']';

				if (bool status = tech.enabled;
					sherbet::toggle(label.c_str(), &status) && !force_enabled)
				{
					modified = true;

					if (status)
						enable_technique(tech);
					else
						disable_technique(tech);
				}

				ImGui::PopStyleColor();

				ImGui::EndDisabled();

				// Display tooltip
				if (const std::string_view tooltip = get_localized_annotation(tech, "ui_tooltip", _current_language);
					!tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
				{
					if (ImGui::BeginTooltip())
					{
						ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
						ImGui::EndTooltip();
					}
				}
			}

			if (ImGui::IsItemActive())
				_selected_technique = index;
			if (ImGui::IsItemClicked())
				_focused_effect = tech.effect_index;
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly | ImGuiHoveredFlags_AllowWhenDisabled))
				hovered_technique_index = index;

			// Create context menu
			if (ImGui::BeginPopupContextItem("##context"))
			{
				ImGui::TextUnformatted(tech.name.c_str(), tech.name.c_str() + tech.name.size());
				ImGui::Separator();

				ImGui::SetNextItemWidth(18.0f * ImGui::GetFontSize());
				if (_input != nullptr && !force_enabled &&
					imgui::key_input_box("##toggle_key", tech.toggle_key_data, *_input))
				{
					if (_auto_save_preset)
						save_current_preset();
					else
						_preset_is_modified = true;
				}

				const bool is_not_top = index > 0;
				const bool is_not_bottom = index < _technique_sorting.size() - 1;

				if (is_not_top && ImGui::Button(_("Move to top"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
				{
					std::vector<size_t> technique_indices = _technique_sorting;
					technique_indices.insert(technique_indices.begin(), technique_indices[index]);
					technique_indices.erase(technique_indices.begin() + 1 + index);
					reorder_techniques(std::move(technique_indices));

					if (_auto_save_preset)
						save_current_preset();
					else
						_preset_is_modified = true;

					ImGui::CloseCurrentPopup();
				}
				if (is_not_bottom && ImGui::Button(_("Move to bottom"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
				{
					std::vector<size_t> technique_indices = _technique_sorting;
					technique_indices.push_back(technique_indices[index]);
					technique_indices.erase(technique_indices.begin() + index);
					reorder_techniques(std::move(technique_indices));

					if (_auto_save_preset)
						save_current_preset();
					else
						_preset_is_modified = true;

					ImGui::CloseCurrentPopup();
				}

				if (is_not_top || is_not_bottom || (_input != nullptr && !force_enabled))
					ImGui::Separator();

				// SHERBET: 즐겨찾기 토글
				{
					const bool is_fav = _sherbet_fav.count(tech.name) != 0;
					const char *fav_label = is_fav
						? ICON_FK_STAR "  \xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0 \xED\x95\xB4\xEC\xA0\x9C" // "즐겨찾기 해제"
						: ICON_FK_STAR "  \xEC\xA6\x90\xEA\xB2\xA8\xEC\xB0\xBE\xEA\xB8\xB0 \xEC\xB6\x94\xEA\xB0\x80"; // "즐겨찾기 추가"
					if (ImGui::Button(fav_label, ImVec2(18.0f * ImGui::GetFontSize(), 0)))
					{
						if (is_fav) _sherbet_fav.erase(tech.name); else _sherbet_fav.insert(tech.name);
						save_config();
						ImGui::CloseCurrentPopup();
					}
					ImGui::Separator();
				}

				if (ImGui::Button(ICON_FK_FOLDER " " + _("Open folder in explorer"), ImVec2(18.0f * ImGui::GetFontSize(), 0)))
					utils::open_explorer(effect.source_file);

				ImGui::Separator();

				if (imgui::popup_button(ICON_FK_PENCIL " " + _("Edit source code"), 18.0f * ImGui::GetFontSize()))
				{
					std::filesystem::path source_file;
					if (ImGui::MenuItem(effect.source_file.filename().u8string().c_str()))
						source_file = effect.source_file;

					if (!effect.preprocessed)
					{
						// Force preprocessor to run to update included files
						load_effect(effect.source_file, ini_file::load_cache(_current_preset_path), tech.effect_index, 0, true, true);
					}

					if (!effect.included_files.empty())
					{
						ImGui::Separator();

						for (const std::filesystem::path &included_file : effect.included_files)
						{
							std::filesystem::path display_path = included_file.lexically_relative(effect.source_file.parent_path());
							if (display_path.empty())
								display_path = included_file.filename();
							if (ImGui::MenuItem(display_path.u8string().c_str()))
								source_file = included_file;
						}
					}

					ImGui::EndPopup();

					if (!source_file.empty())
					{
						open_code_editor(tech.effect_index, source_file);
						ImGui::CloseCurrentPopup();
					}
				}

				for (size_t permutation_index = 0; permutation_index < effect.permutations.size(); ++permutation_index)
				{
					std::string label = _("Show compiled results");
					if (effect.permutations.size() > 1)
						label += " (" + std::to_string(permutation_index) + ")";

					if (!effect.permutations[permutation_index].generated_code.empty() &&
						imgui::popup_button(label.c_str(), 18.0f * ImGui::GetFontSize()))
					{
						const bool open_generated_code = ImGui::MenuItem(_("Generated code"));

						ImGui::Separator();

						std::string entry_point_name;
						for (const std::pair<std::string, reshadefx::shader_type> &entry_point : effect.permutations[permutation_index].module.entry_points)
							if (const auto assembly_it = effect.permutations[permutation_index].assembly.find(entry_point.first);
								assembly_it != effect.permutations[permutation_index].assembly.end() && ImGui::MenuItem(entry_point.first.c_str()))
								entry_point_name = entry_point.first;

						ImGui::EndPopup();

						if (open_generated_code || !entry_point_name.empty())
						{
							open_code_editor(tech.effect_index, permutation_index, entry_point_name);
							ImGui::CloseCurrentPopup();
						}
					}
				}

				ImGui::EndPopup();
			}

			if (tech.toggle_key_data[0] != 0)
			{
				ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.0f * ImGui::GetFontSize());
				ImGui::TextDisabled("%s", input::key_name(tech.toggle_key_data).c_str());
			}

			if (draw_border)
				ImGui::Separator();

			ImGui::PopID();

			if (modified)
			{
				if (_auto_save_preset)
					save_current_preset();
				else
					_preset_is_modified = true;
			}
		}
	}

	ImGui::EndDisabled();

	// Move the selected technique to the position of the mouse in the list
	if (_selected_technique < _technique_sorting.size() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		if (hovered_technique_index < _technique_sorting.size() && hovered_technique_index != _selected_technique)
		{
			std::vector<size_t> technique_indices = _technique_sorting;

			const auto move_technique = [this, &technique_indices](size_t from_index, size_t to_index) {
				if (to_index < from_index) // Up
					for (size_t i = from_index; to_index < i; --i)
						std::swap(technique_indices[i - 1], technique_indices[i]);
				else // Down
					for (size_t i = from_index; i < to_index; ++i)
						std::swap(technique_indices[i], technique_indices[i + 1]);
			};

			move_technique(_selected_technique, hovered_technique_index);

			// Pressing shift moves all techniques from the same effect file to the new location as well
			if (_imgui_context->IO.KeyShift)
			{
				for (size_t i = hovered_technique_index + 1, offset = 1; i < technique_indices.size(); ++i)
				{
					if (_techniques[technique_indices[i]].effect_index == _focused_effect)
					{
						if ((i - hovered_technique_index) > offset)
							move_technique(i, hovered_technique_index + offset);
						offset++;
					}
				}
				for (size_t i = hovered_technique_index - 1, offset = 0; i >= 0 && i != std::numeric_limits<size_t>::max(); --i)
				{
					if (_techniques[technique_indices[i]].effect_index == _focused_effect)
					{
						offset++;
						if ((hovered_technique_index - i) > offset)
							move_technique(i, hovered_technique_index - offset);
					}
				}
			}

			reorder_techniques(std::move(technique_indices));
			_selected_technique = hovered_technique_index;

			if (_auto_save_preset)
				save_current_preset();
			else
				_preset_is_modified = true;
		}
	}
	else
	{
		_selected_technique = std::numeric_limits<size_t>::max();
	}
}

void reshade::runtime::open_code_editor(size_t effect_index, size_t permutation_index, const std::string &entry_point)
{
	assert(effect_index < _effects.size());

	const std::filesystem::path &path = _effects[effect_index].source_file;

	if (const auto it = std::find_if(_editors.begin(), _editors.end(),
			[effect_index, permutation_index, &path, &entry_point](const editor_instance &instance) {
				return instance.effect_index == effect_index && instance.permutation_index == permutation_index && instance.file_path == path && instance.generated && instance.entry_point_name == entry_point;
			});
		it != _editors.end())
	{
		it->selected = true;
		open_code_editor(*it);
	}
	else
	{
		editor_instance instance { effect_index, permutation_index, path, entry_point, true, true };
		open_code_editor(instance);
		_editors.push_back(std::move(instance));
	}
}
void reshade::runtime::open_code_editor(size_t effect_index, const std::filesystem::path &path)
{
	assert(effect_index < _effects.size());

	if (const auto it = std::find_if(_editors.begin(), _editors.end(),
			[effect_index, &path](const editor_instance &instance) {
				return instance.effect_index == effect_index && instance.file_path == path && !instance.generated;
			});
		it != _editors.end())
	{
		it->selected = true;
		open_code_editor(*it);
	}
	else
	{
		editor_instance instance { effect_index, std::numeric_limits<size_t>::max(), path, std::string(), true, false };
		open_code_editor(instance);
		_editors.push_back(std::move(instance));
	}
}
void reshade::runtime::open_code_editor(editor_instance &instance) const
{
	const effect &effect = _effects[instance.effect_index];

	if (instance.generated)
	{
		const effect::permutation &permutation = effect.permutations[instance.permutation_index];

		if (instance.entry_point_name.empty())
			instance.editor.set_text(permutation.generated_code);
		else
			instance.editor.set_text(permutation.assembly.at(instance.entry_point_name));
		instance.editor.set_readonly(true);
		return; // Errors only apply to the effect source, not generated code
	}

	// Only update text if there is no undo history (in which case it can be assumed that the text is already up-to-date)
	if (!instance.editor.is_modified() && !instance.editor.can_undo())
	{
		if (FILE *const file = _wfsopen(instance.file_path.c_str(), L"rb", SH_DENYWR))
		{
			fseek(file, 0, SEEK_END);
			const size_t file_size = ftell(file);
			fseek(file, 0, SEEK_SET);

			std::string text(file_size, '\0');
			fread(text.data(), 1, file_size, file);

			fclose(file);

			instance.editor.set_text(text);
			instance.editor.set_readonly(false);
		}
	}

	instance.editor.clear_errors();

	parse_errors(effect.errors,
		[&instance](const std::string_view file, int line, const std::string_view message) {
			// Ignore errors that aren't in the current source file
			if (file.empty() || file != instance.file_path.u8string())
				return;

			instance.editor.add_error(line, message, message.find("error") == std::string::npos);
		});
}
void reshade::runtime::draw_code_editor(editor_instance &instance)
{
	if (!instance.generated &&
		(ImGui::Button(ICON_FK_FLOPPY " " + _("Save"), ImVec2(ImGui::GetContentRegionAvail().x, 0)) ||
			(_input != nullptr && _input->is_key_pressed('S', true, false, false))))
	{
		// Write current editor text to file
		if (FILE *const file = _wfsopen(instance.file_path.c_str(), L"wb", SH_DENYWR))
		{
			const std::string text = instance.editor.get_text();
			fwrite(text.data(), 1, text.size(), file);
			fclose(file);
		}

		if (!is_loading() && instance.effect_index < _effects.size())
		{
			// Clear modified flag, so that errors are updated next frame (see 'update_effects')
			instance.editor.clear_modified();

			reload_effect(instance.effect_index);

			// Reloading an effect file invalidates all textures, but the statistics window may already have drawn references to those, so need to reset it
			if (ImGuiWindow *const statistics_window = ImGui::FindWindowByName("###statistics"))
				statistics_window->DrawList->CmdBuffer.clear();
		}
	}

	instance.editor.render("##editor", _editor_palette, false, _imgui_context->IO.Fonts->Fonts[_imgui_context->IO.Fonts->Fonts.Size - 1], _editor_font_size);

	// Disable keyboard shortcuts when the window is focused so they don't get triggered while editing text
	const bool is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	_ignore_shortcuts |= is_focused;

	// Disable keyboard navigation starting with next frame when editor is focused so that the Alt key can be used without it switching focus to the menu bar
	if (is_focused)
		_imgui_context->IO.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
	else // Enable navigation again if focus is lost
		_imgui_context->IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

bool reshade::runtime::init_imgui_resources()
{
	// Adjust default font size based on the vertical resolution
	if (_font_size == 13.0f)
		_imgui_context->Style.FontScaleMain = _height >= 2160 ? 2.0f : _height >= 1440 ? 1.5f : 1.0f;

	const bool has_combined_sampler_and_view = _device->check_capability(api::device_caps::sampler_with_resource_view);

	if (_imgui_sampler_state == 0)
	{
		api::sampler_desc sampler_desc = {};
		sampler_desc.filter = api::filter_mode::min_mag_mip_linear;
		sampler_desc.address_u = api::texture_address_mode::clamp;
		sampler_desc.address_v = api::texture_address_mode::clamp;
		sampler_desc.address_w = api::texture_address_mode::clamp;

		if (!_device->create_sampler(sampler_desc, &_imgui_sampler_state))
		{
			log::message(log::level::error, "Failed to create ImGui sampler object!");
			return false;
		}
	}

	if (_imgui_pipeline_layout == 0)
	{
		uint32_t num_layout_params = 0;
		api::pipeline_layout_param layout_params[3];

		if (has_combined_sampler_and_view)
		{
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::sampler_with_resource_view }; // s0
		}
		else
		{
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::sampler }; // s0
			layout_params[num_layout_params++] = api::descriptor_range { 0, 0, 0, 1, api::shader_stage::pixel, 1, api::descriptor_type::shader_resource_view }; // t0
		}

		layout_params[num_layout_params++] = api::constant_range { 0, 0, 0, 18, api::shader_stage::vertex | api::shader_stage::pixel }; // b0

		if (!_device->create_pipeline_layout(num_layout_params, layout_params, &_imgui_pipeline_layout))
		{
			log::message(log::level::error, "Failed to create ImGui pipeline layout!");
			return false;
		}
	}

	if (_imgui_pipeline != 0)
		return true;

	const resources::data_resource vs_res = resources::load_data_resource(
		_renderer_id >= 0x20000 ? IDR_IMGUI_VS_SPIRV :
		_renderer_id >= 0x10000 ? IDR_IMGUI_VS_GLSL :
		_renderer_id >= 0x0a000 ? IDR_IMGUI_VS_4_0 : IDR_IMGUI_VS_3_0);
	api::shader_desc vs_desc;
	vs_desc.code = vs_res.data;
	vs_desc.code_size = vs_res.data_size;

	const resources::data_resource ps_res = resources::load_data_resource(
		_renderer_id >= 0x20000 ? IDR_IMGUI_PS_SPIRV :
		_renderer_id >= 0x10000 ? IDR_IMGUI_PS_GLSL :
		_renderer_id >= 0x0a000 ? IDR_IMGUI_PS_4_0 : IDR_IMGUI_PS_3_0);
	api::shader_desc ps_desc;
	ps_desc.code = ps_res.data;
	ps_desc.code_size = ps_res.data_size;

	std::vector<api::pipeline_subobject> subobjects;
	subobjects.push_back({ api::pipeline_subobject_type::vertex_shader, 1, &vs_desc });
	subobjects.push_back({ api::pipeline_subobject_type::pixel_shader, 1, &ps_desc });

	const api::input_element input_layout[3] = {
		{ 0, "POSITION", 0, api::format::r32g32_float,   0, offsetof(ImDrawVert, pos), sizeof(ImDrawVert), 0 },
		{ 1, "TEXCOORD", 0, api::format::r32g32_float,   0, offsetof(ImDrawVert, uv ), sizeof(ImDrawVert), 0 },
		{ 2, "COLOR",    0, api::format::r8g8b8a8_unorm, 0, offsetof(ImDrawVert, col), sizeof(ImDrawVert), 0 }
	};
	subobjects.push_back({ api::pipeline_subobject_type::input_layout, 3, (void *)input_layout });

	api::primitive_topology topology = api::primitive_topology::triangle_list;
	subobjects.push_back({ api::pipeline_subobject_type::primitive_topology, 1, &topology });

	api::blend_desc blend_state;
	blend_state.blend_enable[0] = true;
	blend_state.source_color_blend_factor[0] = api::blend_factor::source_alpha;
	blend_state.dest_color_blend_factor[0] = api::blend_factor::one_minus_source_alpha;
	blend_state.color_blend_op[0] = api::blend_op::add;
	blend_state.source_alpha_blend_factor[0] = api::blend_factor::one;
	blend_state.dest_alpha_blend_factor[0] = api::blend_factor::one_minus_source_alpha;
	blend_state.alpha_blend_op[0] = api::blend_op::add;
	blend_state.render_target_write_mask[0] = 0xF;
	subobjects.push_back({ api::pipeline_subobject_type::blend_state, 1, &blend_state });

	api::rasterizer_desc rasterizer_state;
	rasterizer_state.cull_mode = api::cull_mode::none;
	rasterizer_state.scissor_enable = true;
	subobjects.push_back({ api::pipeline_subobject_type::rasterizer_state, 1, &rasterizer_state });

	api::depth_stencil_desc depth_stencil_state;
	depth_stencil_state.depth_enable = false;
	depth_stencil_state.stencil_enable = false;
	subobjects.push_back({ api::pipeline_subobject_type::depth_stencil_state, 1, &depth_stencil_state });

	// Always choose non-sRGB format variant, since 'render_imgui_draw_data' is called with the non-sRGB render target (see 'draw_gui')
	api::format render_target_format = api::format_to_default_typed(_back_buffer_format, 0);
	subobjects.push_back({ api::pipeline_subobject_type::render_target_formats, 1, &render_target_format });

	if (!_device->create_pipeline(_imgui_pipeline_layout, static_cast<uint32_t>(subobjects.size()), subobjects.data(), &_imgui_pipeline))
	{
		log::message(log::level::error, "Failed to create ImGui pipeline!");
		return false;
	}

	return true;
}
void reshade::runtime::render_imgui_draw_data(api::command_list *cmd_list, ImDrawData *draw_data, api::resource_view rtv)
{
	assert(draw_data->Textures != nullptr);
	for (ImTextureData *const texture_data : *draw_data->Textures)
	{
		if (texture_data->Status == ImTextureStatus_OK)
			continue;

		if (texture_data->Status == ImTextureStatus_WantCreate)
		{
			assert(texture_data->GetTexID() == ImTextureID_Invalid);

			api::format format = api::format::unknown;
			switch (texture_data->Format)
			{
			case ImTextureFormat_RGBA32:
				format = api::format::r8g8b8a8_unorm;
				break;
			case ImTextureFormat_Alpha8:
				format = api::format::r8_unorm;
				break;
			}

			const api::subresource_data initial_data = { texture_data->GetPixels(), static_cast<uint32_t>(texture_data->GetPitch()), static_cast<uint32_t>(texture_data->GetSizeInBytes()) };

			api::resource imgui_tex;
			if (!_device->create_resource(
					api::resource_desc(texture_data->Width, texture_data->Height, 1, 1, format, 1, api::memory_heap::default_, api::resource_usage::shader_resource | api::resource_usage::copy_dest),
					&initial_data, api::resource_usage::shader_resource, &imgui_tex))
			{
				log::message(log::level::error, "Failed to create imgui texture resource!");

				texture_data->SetStatus(ImTextureStatus_Destroyed);
				continue;
			}

			api::resource_view imgui_srv;
			if (!_device->create_resource_view(imgui_tex, api::resource_usage::shader_resource, api::resource_view_desc(format), &imgui_srv))
			{
				log::message(log::level::error, "Failed to create imgui texture resource view!");

				_device->destroy_resource(imgui_tex);

				texture_data->SetStatus(ImTextureStatus_Destroyed);
				continue;
			}

			texture_data->SetTexID(imgui_srv.handle);
			texture_data->SetStatus(ImTextureStatus_OK);
			continue;
		}

		if (texture_data->Status == ImTextureStatus_WantUpdates)
		{
			const auto imgui_srv = api::resource_view { texture_data->GetTexID() };
			const auto imgui_tex = _device->get_resource_from_view(imgui_srv);

			cmd_list->barrier(imgui_tex, api::resource_usage::shader_resource, api::resource_usage::copy_dest);
			for (const ImTextureRect &update_rect : texture_data->Updates)
			{
				api::subresource_box box;
				box.left = update_rect.x;
				box.top = update_rect.y;
				box.front = 0;
				box.right = update_rect.x + update_rect.w;
				box.bottom = update_rect.y + update_rect.h;
				box.back = 1;

				cmd_list->update_texture_region(
					api::subresource_data { texture_data->GetPixelsAt(update_rect.x, update_rect.y), static_cast<uint32_t>(texture_data->GetPitch()), static_cast<uint32_t>(texture_data->GetSizeInBytes()) },
					imgui_tex,
					0,
					&box);
			}
			cmd_list->barrier(imgui_tex, api::resource_usage::copy_dest, api::resource_usage::shader_resource);

			texture_data->SetStatus(ImTextureStatus_OK);
			continue;
		}

		if (texture_data->Status == ImTextureStatus_WantDestroy && texture_data->UnusedFrames > 8)
		{
			const auto imgui_srv = api::resource_view { texture_data->GetTexID() };
			const auto imgui_tex = _device->get_resource_from_view(imgui_srv);

			_device->destroy_resource_view(imgui_srv);
			_device->destroy_resource(imgui_tex);

			texture_data->SetTexID(ImTextureID_Invalid);
			texture_data->SetStatus(ImTextureStatus_Destroyed);
			continue;
		}
	}

	// Need to multi-buffer vertex data so not to modify data below when the previous frame is still in flight
	const size_t buffer_index = _frame_count % (_renderer_id & 0x20000 ? 8 : 4);
	assert(buffer_index < std::size(_imgui_vertices));

	// Create and grow vertex/index buffers if needed
	if (_imgui_num_indices[buffer_index] < draw_data->TotalIdxCount)
	{
		if (_imgui_indices[buffer_index] != 0)
		{
			_graphics_queue->wait_idle(); // Be safe and ensure nothing still uses this buffer

			_device->destroy_resource(_imgui_indices[buffer_index]);
		}

		const int new_size = draw_data->TotalIdxCount + 10000;
		if (!_device->create_resource(api::resource_desc(new_size * sizeof(ImDrawIdx), api::memory_heap::upload, api::resource_usage::index_buffer), nullptr, api::resource_usage::cpu_access, &_imgui_indices[buffer_index]))
		{
			log::message(log::level::error, "Failed to create ImGui index buffer!");
			return;
		}

		_device->set_resource_name(_imgui_indices[buffer_index], "ImGui index buffer");

		_imgui_num_indices[buffer_index] = new_size;
	}
	if (_imgui_num_vertices[buffer_index] < draw_data->TotalVtxCount)
	{
		if (_imgui_vertices[buffer_index] != 0)
		{
			_graphics_queue->wait_idle();

			_device->destroy_resource(_imgui_vertices[buffer_index]);
		}

		const int new_size = draw_data->TotalVtxCount + 5000;
		if (!_device->create_resource(api::resource_desc(new_size * sizeof(ImDrawVert), api::memory_heap::upload, api::resource_usage::vertex_buffer), nullptr, api::resource_usage::cpu_access, &_imgui_vertices[buffer_index]))
		{
			log::message(log::level::error, "Failed to create ImGui vertex buffer!");
			return;
		}

		_device->set_resource_name(_imgui_vertices[buffer_index], "ImGui vertex buffer");

		_imgui_num_vertices[buffer_index] = new_size;
	}

#ifndef NDEBUG
	cmd_list->begin_debug_event("ReShade overlay");
#endif

	if (ImDrawIdx *idx_dst;
		_device->map_buffer_region(_imgui_indices[buffer_index], 0, UINT64_MAX, api::map_access::write_only, reinterpret_cast<void **>(&idx_dst)))
	{
		for (int n = 0; n < draw_data->CmdListsCount; ++n)
		{
			const ImDrawList *const draw_list = draw_data->CmdLists[n];
			std::memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			idx_dst += draw_list->IdxBuffer.Size;
		}

		_device->unmap_buffer_region(_imgui_indices[buffer_index]);
	}
	if (ImDrawVert *vtx_dst;
		_device->map_buffer_region(_imgui_vertices[buffer_index], 0, UINT64_MAX, api::map_access::write_only, reinterpret_cast<void **>(&vtx_dst)))
	{
		for (int n = 0; n < draw_data->CmdListsCount; ++n)
		{
			const ImDrawList *const draw_list = draw_data->CmdLists[n];
			std::memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
			vtx_dst += draw_list->VtxBuffer.Size;
		}

		_device->unmap_buffer_region(_imgui_vertices[buffer_index]);
	}

	api::render_pass_render_target_desc render_target = {};
	render_target.view = rtv;

	cmd_list->begin_render_pass(1, &render_target, nullptr);

	// Setup render state
	cmd_list->bind_pipeline(api::pipeline_stage::all_graphics, _imgui_pipeline);

	cmd_list->bind_index_buffer(_imgui_indices[buffer_index], 0, sizeof(ImDrawIdx));
	cmd_list->bind_vertex_buffer(0, _imgui_vertices[buffer_index], 0, sizeof(ImDrawVert));

	const api::viewport viewport = { 0, 0, draw_data->DisplaySize.x, draw_data->DisplaySize.y, 0.0f, 1.0f };
	cmd_list->bind_viewports(0, 1, &viewport);

	// Setup orthographic projection matrix
	const bool flip_y = (_renderer_id & 0x10000) != 0 && !_is_vr;
	const bool adjust_half_pixel = _renderer_id < 0xa000; // Bake half-pixel offset into matrix in D3D9
	const bool depth_clip_zero_to_one = (_renderer_id & 0x10000) == 0;

	const struct {
		float ortho_projection[16];
		api::color_space color_space;
		float hdr_overlay_brightness;
	} push_constants = {
		{
			2.0f / draw_data->DisplaySize.x, 0.0f, 0.0f, 0.0f,
			0.0f, (flip_y ? 2.0f : -2.0f) / draw_data->DisplaySize.y, 0.0f, 0.0f,
			0.0f,                            0.0f, depth_clip_zero_to_one ? 0.5f : -1.0f, 0.0f,
							   -(2 * draw_data->DisplayPos.x + draw_data->DisplaySize.x + (adjust_half_pixel ? 1.0f : 0.0f)) / draw_data->DisplaySize.x,
			(flip_y ? -1 : 1) * (2 * draw_data->DisplayPos.y + draw_data->DisplaySize.y + (adjust_half_pixel ? 1.0f : 0.0f)) / draw_data->DisplaySize.y, depth_clip_zero_to_one ? 0.5f : 0.0f, 1.0f,
		},
		_hdr_overlay_overwrite_color_space != api::color_space::unknown ? _hdr_overlay_overwrite_color_space : _back_buffer_color_space,
		_hdr_overlay_brightness
	};

	const bool has_combined_sampler_and_view = _device->check_capability(api::device_caps::sampler_with_resource_view);

	cmd_list->push_constants(api::shader_stage::vertex | api::shader_stage::pixel, _imgui_pipeline_layout, has_combined_sampler_and_view ? 1 : 2, 0, (_renderer_id != 0x9000 ? sizeof(push_constants) : sizeof(push_constants.ortho_projection)) / 4, &push_constants);
	if (!has_combined_sampler_and_view)
		cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 0, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::sampler, &_imgui_sampler_state });

	int vtx_offset = 0, idx_offset = 0;
	for (int n = 0; n < draw_data->CmdListsCount; ++n)
	{
		const ImDrawList *const draw_list = draw_data->CmdLists[n];

		for (const ImDrawCmd &cmd : draw_list->CmdBuffer)
		{
			if (cmd.UserCallback != nullptr)
			{
				cmd.UserCallback(draw_list, &cmd);
				continue;
			}

			const api::rect scissor_rect = {
				static_cast<int32_t>(cmd.ClipRect.x - draw_data->DisplayPos.x),
				flip_y ? static_cast<int32_t>(_height - cmd.ClipRect.w + draw_data->DisplayPos.y) : static_cast<int32_t>(cmd.ClipRect.y - draw_data->DisplayPos.y),
				static_cast<int32_t>(cmd.ClipRect.z - draw_data->DisplayPos.x),
				flip_y ? static_cast<int32_t>(_height - cmd.ClipRect.y + draw_data->DisplayPos.y) : static_cast<int32_t>(cmd.ClipRect.w - draw_data->DisplayPos.y)
			};

			cmd_list->bind_scissor_rects(0, 1, &scissor_rect);

			const api::resource_view srv = { cmd.GetTexID() };
			if (has_combined_sampler_and_view)
			{
				api::sampler_with_resource_view sampler_and_view = { _imgui_sampler_state, srv };
				cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 0, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::sampler_with_resource_view, &sampler_and_view });
			}
			else
			{
				cmd_list->push_descriptors(api::shader_stage::pixel, _imgui_pipeline_layout, 1, api::descriptor_table_update { {}, 0, 0, 1, api::descriptor_type::shader_resource_view, &srv });
			}

			cmd_list->draw_indexed(cmd.ElemCount, 1, cmd.IdxOffset + idx_offset, cmd.VtxOffset + vtx_offset, 0);
		}

		idx_offset += draw_list->IdxBuffer.Size;
		vtx_offset += draw_list->VtxBuffer.Size;
	}

	cmd_list->end_render_pass();

#ifndef NDEBUG
	cmd_list->end_debug_event();
#endif
}
void reshade::runtime::destroy_imgui_resources()
{
	ImFontAtlas *const atlas = _imgui_context->IO.Fonts;
	atlas->Clear();

	// Have to rebuild font atlas next time it is used again, since it is being destroyed here
	_rebuild_font_atlas = true;

	for (ImTextureData *const texture_data : _imgui_context->PlatformIO.Textures)
	{
		if (texture_data->Status == ImTextureStatus_Destroyed || (texture_data->Status == ImTextureStatus_WantCreate && !texture_data->WantDestroyNextFrame))
			continue;

		assert(texture_data->RefCount == 1);

		const auto imgui_srv = api::resource_view { texture_data->GetTexID() };
		const auto imgui_tex = _device->get_resource_from_view(imgui_srv);

		_device->destroy_resource_view(imgui_srv);
		_device->destroy_resource(imgui_tex);

		texture_data->SetTexID(ImTextureID_Invalid);
		texture_data->SetStatus(ImTextureStatus_Destroyed);
	}

	// Remove texture from font atlas, since it was destroyed among all texture above
	atlas->TexList.clear_delete();
	atlas->TexData = nullptr;
	// Also remove from the platform texture list, so that the now deleted font atlas texture data is not accessed again
	_imgui_context->PlatformIO.Textures.clear();

	for (size_t i = 0; i < std::size(_imgui_vertices); ++i)
	{
		_device->destroy_resource(_imgui_indices[i]);
		_imgui_indices[i] = {};
		_imgui_num_indices[i] = 0;
		_device->destroy_resource(_imgui_vertices[i]);
		_imgui_vertices[i] = {};
		_imgui_num_vertices[i] = 0;
	}

	_device->destroy_sampler(_imgui_sampler_state);
	_imgui_sampler_state = {};
	_device->destroy_pipeline(_imgui_pipeline);
	_imgui_pipeline = {};
	_device->destroy_pipeline_layout(_imgui_pipeline_layout);
	_imgui_pipeline_layout = {};
}

bool reshade::runtime::open_overlay(bool open, api::input_source source)
{
#if RESHADE_ADDON
	if (invoke_addon_event<addon_event::reshade_open_overlay>(this, open, source))
		return false;
#endif

	_show_overlay = open;

	if (open)
		_imgui_context->NavInputSource = static_cast<ImGuiInputSource>(source);

	return true;
}

#endif
