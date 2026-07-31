/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "reshade_api.hpp"
#include "state_block.hpp"
#include "imgui_code_editor.hpp"
#include "sherbet_auth.hpp"
#include "sherbet_alarm.hpp"
#include "sherbet_magnifier.hpp"
#include "sherbet_spray.hpp"
#include "sherbet_crosshair.hpp"
#include "sherbet_xhmarket.hpp"
#include "sherbet_motion.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>
#include <mutex>
#include <set>
#include <shared_mutex>

// SHERBET: 유료 기능 카탈로그. 헤더를 통째로 끌어오지 않고 앞선언만 한다
// (sherbet_paid.hpp 는 runtime_gui.cpp 만 필요로 한다).
namespace sherbet { namespace paid { struct feature; } }

namespace reshade
{
	struct effect;
	struct uniform;
	struct texture;
	struct technique;

	/// <summary>
	/// The main ReShade post-processing effect runtime.
	/// </summary>
	class __declspec(uuid("77FF8202-5BEC-42AD-8CE0-397F3E84EAA6")) runtime : public api::effect_runtime
	{
	public:
		runtime(api::swapchain *swapchain, api::command_queue *graphics_queue, const std::filesystem::path &config_path, bool is_vr);
		~runtime();

		bool on_init();
		void on_reset();
		void on_present();

		uint64_t get_native() const final { return _swapchain->get_native(); }

		void get_private_data(const uint8_t guid[16], uint64_t *data) const final { return _swapchain->get_private_data(guid, data); }
		void set_private_data(const uint8_t guid[16], const uint64_t data)  final { return _swapchain->set_private_data(guid, data); }

		api::device *get_device() final { return _device; }
		api::swapchain *get_swapchain() { return _swapchain; }
		api::command_queue *get_command_queue() final { return _graphics_queue; }

		void *get_hwnd() const final { return _swapchain->get_hwnd(); }

		api::resource get_back_buffer(uint32_t index) final { return _swapchain->get_back_buffer(index); }
		uint32_t get_back_buffer_count() const final { return _swapchain->get_back_buffer_count(); }
		uint32_t get_current_back_buffer_index() const final { return _swapchain->get_current_back_buffer_index(); }

		/// <summary>
		/// Gets the path to the configuration file used by this effect runtime.
		/// </summary>
		const std::filesystem::path &get_config_path() const { return _config_path; }

		/// <summary>
		/// Gets a boolean indicating whether effects are being loaded.
		/// </summary>
		bool is_loading() const { return _reload_remaining_effects != std::numeric_limits<size_t>::max() || !_reload_create_queue.empty(); }

		void render_effects(api::command_list *cmd_list, api::resource_view rtv, api::resource_view rtv_srgb) final;
		void render_technique(api::effect_technique handle, api::command_list *cmd_list, api::resource_view rtv, api::resource_view rtv_srgb) final;

		/// <summary>
		/// Captures a screenshot of the current back buffer resource and writes it to an image file on disk.
		/// </summary>
		void save_screenshot(const char *postfix) final;
		bool capture_screenshot(void *pixels) final { return get_texture_data(_back_buffer_resolved != 0 ? _back_buffer_resolved : _swapchain->get_current_back_buffer(), _back_buffer_resolved != 0 ? api::resource_usage::render_target : api::resource_usage::present, static_cast<uint8_t *>(pixels), _back_buffer_format); }

		void get_screenshot_width_and_height(uint32_t *out_width, uint32_t *out_height) const final { *out_width = _width; *out_height = _height; }

		bool is_key_down(uint32_t keycode) const final;
		bool is_key_pressed(uint32_t keycode) const final;
		bool is_key_released(uint32_t keycode) const final;
		bool is_mouse_button_down(uint32_t button) const final;
		bool is_mouse_button_pressed(uint32_t button) const final;
		bool is_mouse_button_released(uint32_t button) const final;

		uint32_t last_key_pressed() const final;
		uint32_t last_key_released() const final;

		void get_mouse_cursor_position(uint32_t *out_x, uint32_t *out_y, int16_t *out_wheel_delta) const final;

		void block_input_next_frame() final;

		void enumerate_uniform_variables(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_uniform_variable variable, void *user_data), void *user_data) final;

		api::effect_uniform_variable find_uniform_variable(const char *effect_name, const char *variable_name) const final;

		void get_uniform_variable_type(api::effect_uniform_variable variable, api::format *out_base_type, uint32_t *out_rows, uint32_t *out_columns, uint32_t *out_array_length) const final;

		void get_uniform_variable_name(api::effect_uniform_variable variable, char *name, size_t *name_size) const final;
		void get_uniform_variable_effect_name(api::effect_uniform_variable variable, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_uniform_variable(api::effect_uniform_variable variable, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_uniform_variable(api::effect_uniform_variable variable, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_uniform_variable(api::effect_uniform_variable variable, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_uniform_variable(api::effect_uniform_variable variable, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_uniform_variable(api::effect_uniform_variable variable, const char *name, char *value, size_t *value_size) const final;

		void reset_uniform_value(api::effect_uniform_variable variable);

		void get_uniform_value_bool(api::effect_uniform_variable variable, bool *values, size_t count, size_t array_index) const final;
		void get_uniform_value_float(api::effect_uniform_variable variable, float *values, size_t count, size_t array_index) const final;
		void get_uniform_value_int(api::effect_uniform_variable variable, int32_t *values, size_t count, size_t array_index) const final;
		void get_uniform_value_uint(api::effect_uniform_variable variable, uint32_t *values, size_t count, size_t array_index) const final;

		void set_uniform_value_bool(api::effect_uniform_variable variable, const bool *values, size_t count, size_t array_index) final;
		void set_uniform_value_float(api::effect_uniform_variable variable, const float *values, size_t count, size_t array_index) final;
		void set_uniform_value_int(api::effect_uniform_variable variable, const int32_t *values, size_t count, size_t array_index) final;
		void set_uniform_value_uint(api::effect_uniform_variable variable, const uint32_t *values, size_t count, size_t array_index) final;

		void enumerate_texture_variables(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_texture_variable variable, void *user_data), void *user_data) final;

		api::effect_texture_variable find_texture_variable(const char *effect_name, const char *variable_name) const final;

		void get_texture_variable_name(api::effect_texture_variable variable, char *name, size_t *name_size) const final;
		void get_texture_variable_effect_name(api::effect_texture_variable variable, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_texture_variable(api::effect_texture_variable variable, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_texture_variable(api::effect_texture_variable variable, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_texture_variable(api::effect_texture_variable variable, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_texture_variable(api::effect_texture_variable variable, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_texture_variable(api::effect_texture_variable variable, const char *name, char *value, size_t *value_size) const final;

		void update_texture(api::effect_texture_variable variable, const uint32_t width, const uint32_t height, const void *pixels) final;

		void get_texture_binding(api::effect_texture_variable variable, api::resource_view *out_srv, api::resource_view *out_srv_srgb) const final;

		void update_texture_bindings(const char *semantic, api::resource_view srv, api::resource_view srv_srgb) final;

		void enumerate_techniques(const char *effect_name, void(*callback)(effect_runtime *runtime, api::effect_technique technique, void *user_data), void *user_data) final;

		api::effect_technique find_technique(const char *effect_name, const char *technique_name) final;

		void get_technique_name(api::effect_technique technique, char *name, size_t *name_size) const final;
		void get_technique_effect_name(api::effect_technique technique, char *effect_name, size_t *effect_name_size) const final;

		bool get_annotation_bool_from_technique(api::effect_technique technique, const char *name, bool *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_float_from_technique(api::effect_technique technique, const char *name, float *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_int_from_technique(api::effect_technique technique, const char *name, int32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_uint_from_technique(api::effect_technique technique, const char *name, uint32_t *values, size_t count, size_t array_index = 0) const final;
		bool get_annotation_string_from_technique(api::effect_technique technique, const char *name, char *value, size_t *value_size) const final;

		bool get_technique_state(api::effect_technique technique) const final;
		void set_technique_state(api::effect_technique technique, bool enabled) final;

		bool get_preprocessor_definition(const char *name, char *value, size_t *value_size) const final;
		bool get_preprocessor_definition_for_effect(const char *effect_name, const char *name, char *value, size_t *value_size) const final;
		void set_preprocessor_definition(const char *name, const char *value) final;
		void set_preprocessor_definition_for_effect(const char *effect_name, const char *name, const char *value) final;

		bool get_effects_state() const final;
		void set_effects_state(bool enabled) final;

		void save_current_preset() const final;
		void export_current_preset(const char *path) const final;

		void get_current_preset_path(char *path, size_t *path_size) const final;
		void set_current_preset_path(const char *path) final;

		void reorder_techniques(size_t count, const api::effect_technique *techniques) final;

		bool open_overlay(bool open, api::input_source source) final;

		void set_color_space(api::color_space color_space) final;

		void reload_effect_next_frame(const char *effect_name) final;

		void load_config();
		void save_config() const;

	private:
		static void check_for_update();

		void load_current_preset();
		void save_current_preset(class ini_file &preset) const;

		bool switch_to_next_preset(std::filesystem::path filter_path, bool reversed = false);

		bool load_effect(const std::filesystem::path &source_file, const class ini_file &preset, size_t effect_index, size_t permutation_index, bool force_load = false, bool preprocess_required = false);
		bool create_effect(size_t effect_index, size_t permutation_index);
		void destroy_effect(size_t effect_index, bool unload = true);

		void load_textures(size_t effect_index);
		bool create_texture(texture &texture);
		void destroy_texture(texture &texture);

		void enable_technique(technique &technique);
		void disable_technique(technique &technique);

		void reorder_techniques(std::vector<size_t> &&technique_indices);

		void load_effects(bool force_load_all = false);
		bool reload_effect(size_t effect_index);
		void reload_effects(bool force_load_all = false);
		void destroy_effects();

		bool load_effect_cache(const std::string &id, const std::string &type, std::string &data) const;
		bool save_effect_cache(const std::string &id, const std::string &type, const std::string &data) const;
		void clear_effect_cache();

		auto add_effect_permutation(uint32_t width, uint32_t height, api::format color_format, api::format stencil_format, api::color_space color_space) -> size_t;

		void update_effects();
		void render_technique(technique &technique, api::command_list *cmd_list, api::resource back_buffer_resource, api::resource_view back_buffer_rtv, api::resource_view back_buffer_rtv_srgb, size_t permutation_index);

		void save_texture(const texture &texture);
		void update_texture(texture &texture, uint32_t width, uint32_t height, uint32_t depth, const void *pixels);

		void reset_uniform_value(uniform &variable);

		void get_uniform_value_data(const uniform &variable, uint8_t *data, size_t size, size_t base_index) const;
		template <typename T>
		std::enable_if_t<std::is_same_v<T, bool> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float>>
		get_uniform_value(const uniform &variable, T *values, size_t count = 1, size_t array_index = 0) const;

		void set_uniform_value_data(uniform &variable, const uint8_t *data, size_t size, size_t base_index);
		template <typename T>
		std::enable_if_t<std::is_same_v<T, bool> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float>>
		set_uniform_value(uniform &variable, const T *values, size_t count = 1, size_t array_index = 0);
		template <typename T>
		std::enable_if_t<std::is_same_v<T, bool> || std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t> || std::is_same_v<T, float>>
		set_uniform_value(uniform &variable, T x, T y = T(0), T z = T(0), T w = T(0))
		{
			const T values[4] = { x, y, z, w };
			set_uniform_value(variable, values, 4, 0);
		}

		bool get_preprocessor_definition(const std::string &effect_name, const std::string &name, int scope_mask, std::vector<std::pair<std::string, std::string>> *&scope, std::vector<std::pair<std::string, std::string>>::iterator &value) const;

		bool get_texture_data(api::resource resource, api::resource_usage state, uint8_t *pixels, api::format quantization_format);

		bool execute_screenshot_post_save_command(const std::filesystem::path &screenshot_path, unsigned int screenshot_count, std::string_view postfix);

		// SHERBET(실험): 화면 이동 추정. render_effects() 안, 효과가 적용되기 **전**의
		// 렌더 타깃에서 가운데를 잘라 리드백 링에 복사하고, 두 프레임 전 것을 매핑해 계산한다.
		// _sherbet_motion_on 이 꺼져 있으면 즉시 반환하며 리소스도 만들지 않는다.
		void sherbet_motion_tick(api::command_list *cmd_list, api::resource_view rtv);
		// SHERBET: HUD 돋보기 — 잡은 영역만 작은 텍스처로 복사한다(on_present 에서 매 프레임).
		// ⚠️ render_effects 안이 아니라 on_present 본문에서 부른다 — 효과를 안 쓰는 구매자도
		//    동작해야 하기 때문이다. 자세한 이유는 호출부 주석 참고.
		void sherbet_magnifier_capture(api::command_list *cmd_list, api::resource back_buffer);
		void sherbet_magnifier_release();
		void sherbet_motion_release(); // 리드백 링 해제(on_reset / 초기화 실패 경로)

		api::swapchain *const _swapchain;
		api::device *const _device;
		api::command_queue *const _graphics_queue;
		unsigned int _width = 0;
		unsigned int _height = 0;
		unsigned int _vendor_id = 0;
		unsigned int _device_id = 0;
		unsigned int _renderer_id = 0;
		uint16_t _back_buffer_samples = 1;
		api::format _back_buffer_format = api::format::unknown;
		api::color_space _back_buffer_color_space = api::color_space::unknown;
		bool _is_vr = false;

#if RESHADE_ADDON
		bool _is_in_present_call = false;
#endif

		#pragma region Status
		static unsigned int s_latest_version[3];

		bool _is_initialized = false;
		bool _preset_is_incomplete = false;
		bool _preset_save_successful = true;
		std::filesystem::path _config_path;

		bool _ignore_shortcuts = false;
		bool _force_shortcut_modifiers = true;
		bool _primary_input_handler = false;
		std::shared_ptr<class input> _input;
		std::shared_ptr<class input_gamepad> _input_gamepad;

		bool _effects_enabled = true;
		bool _effects_rendered_this_frame = false;
		unsigned int _effects_key_data[4] = {};

		std::chrono::system_clock::time_point _current_time;
		uint64_t _frame_count = 0;
		std::chrono::high_resolution_clock::duration _last_frame_duration;
		std::chrono::high_resolution_clock::time_point _start_time, _last_present_time;
		#pragma endregion

		#pragma region Effect Loading
		bool _no_debug_info = true;
		bool _no_effect_cache = false;
		bool _no_reload_on_init = false;
		bool _performance_mode = false;
		bool _effect_load_skipping = false;
		unsigned int _reload_key_data[4] = {};

		std::vector<std::pair<std::string, std::string>> _global_preprocessor_definitions;
		std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> _preset_preprocessor_definitions;
		std::vector<std::pair<size_t, size_t>> _reload_required_effects;

		std::filesystem::path _effect_cache_path;
		std::vector<std::filesystem::path> _effect_search_paths;
		std::vector<std::filesystem::path> _texture_search_paths;

		std::atomic<bool> _last_reload_successful = true;
		std::shared_mutex _reload_mutex;
		std::vector<std::pair<size_t, size_t>> _reload_create_queue;
		std::atomic<size_t> _reload_remaining_effects = std::numeric_limits<size_t>::max();

		std::vector<effect> _effects;
		std::vector<texture> _textures;
		std::vector<technique> _techniques;
		std::vector<size_t> _technique_sorting;

		std::vector<std::thread> _worker_threads;
		std::chrono::high_resolution_clock::time_point _last_reload_time;
		#pragma endregion

		#pragma region Effect Rendering
		struct effect_permutation
		{
			unsigned int width = 0;
			unsigned int height = 0;
			api::color_space color_space = api::color_space::unknown;
			api::format color_format = api::format::unknown;
			api::resource color_tex = {};
			api::resource_view color_srv[2] = {};
			api::format stencil_format = api::format::unknown;
			api::resource stencil_tex = {};
			api::resource_view stencil_dsv = {};
		};
		std::vector<effect_permutation> _effect_permutations;

		api::resource _empty_tex = {};
		api::resource_view _empty_srv = {};

		std::unordered_map<size_t, api::sampler> _effect_sampler_states;
		std::unordered_map<std::string, std::pair<api::resource_view, api::resource_view>> _texture_semantic_bindings;
#if RESHADE_ADDON == 1
		std::unordered_map<std::string, std::pair<api::resource_view, api::resource_view>> _backup_texture_semantic_bindings;
#endif
		api::pipeline _copy_pipeline = {};
		api::pipeline_layout _copy_pipeline_layout = {};
		api::sampler  _copy_sampler_state = {};

		api::resource _back_buffer_resolved = {};
		api::resource_view _back_buffer_resolved_srv = {};
		std::vector<api::resource_view> _back_buffer_targets;

		// SHERBET: 효과 적용 전/후 반반 비교 슬라이더.
		// _sherbet_before_tex 에 '효과 적용 전' 프레임을 스냅샷해두고, 오버레이(ImGui) 위에
		// 왼쪽 절반을 겹쳐 그린다. 렌더 타깃 상태머신은 건드리지 않아 안전하며,
		// _sherbet_compare_active 가 꺼져 있으면(기본) 스냅샷/합성 모두 완전히 스킵된다.
		api::resource _sherbet_before_tex = {};
		api::resource_view _sherbet_before_srv = {};
		bool _sherbet_compare_active = false;
		float _sherbet_compare_split = 0.5f; // 0..1, 분할선 x 위치 비율
		bool _sherbet_compare_dragging = false;

		// SHERBET: 커스텀 조준점(크로스헤어). Sherbet-Crosshairs 폴더의 PNG 를 골라 화면 중앙에
		// 표시하거나, 내장 도형(점/십자/원/십자+점)을 그린다. ImGui 오버레이라 렌더 파이프라인 무영향.
		api::resource _sherbet_crosshair_tex = {};
		api::resource_view _sherbet_crosshair_srv = {};
		int _sherbet_crosshair_w = 0, _sherbet_crosshair_h = 0;
		bool _sherbet_crosshair_on = false;
		int _sherbet_crosshair_builtin = 1;      // 0=커스텀이미지, 1=점, 2=십자, 3=원, 4=십자+점
		std::string _sherbet_crosshair_file;     // 커스텀 이미지 파일명(폴더 기준)
		float _sherbet_crosshair_size = 24.0f;   // 픽셀 크기(도형 지름 / 이미지 폭)
		float _sherbet_crosshair_thick = 2.0f;   // 도형 선 두께
		float _sherbet_crosshair_gap = 6.0f;     // 십자 중앙 간격
		float _sherbet_crosshair_opacity = 1.0f; // 0..1
		float _sherbet_crosshair_off[2] = { 0.0f, 0.0f };            // 중앙 기준 오프셋(px)
		float _sherbet_crosshair_col[4] = { 1.0f, 0.36f, 0.56f, 1.0f }; // 도형 색(딸기 핑크 기본)
		bool _sherbet_crosshair_dirty = false;   // 이미지 재로딩 필요(선택 변경/디바이스 리셋 후)
		// 0=미시도(끔/내장도형/미선택) 1=성공 2=실패. 실패를 조용히 넘기면 조준점이 화면에
		// 안 그려지는데 이유를 알 수 없다(콤보는 파일명만 보고 선택됨처럼 그린다).
		int _sherbet_crosshair_load = 0;

		// SHERBET: 발로란트급 조준점(신규). 위의 「클래식」 조준점과 **완전히 별개 토글**이다 —
		// 이미지 조준점을 쓰던 기존 구매자의 설정이 깨지면 안 되므로 기존 멤버는 그대로 둔다.
		// UI 는 아직 없다(태스크 3). 지금은 ReShade.ini 의 SHERBET/ValOn·ValCode 로만 켠다.
		//
		// 설정을 개별 키가 아니라 **공유 코드 문자열 1개**로 저장한다(설계 §3.8):
		// ini 가 안 붐비고, 유저가 ini 를 그대로 주고받을 수 있으며, 무엇보다 범위 밖 값과
		// 미지 키를 원문 그대로 보존한다(개별 키로 쪼개면 그 둘이 조용히 사라진다).
		bool _sherbet_val_on = false;
		std::string _sherbet_val_code = "0";              // 원문. 이것이 설정의 정본이다
		sherbet::crosshair::profile _sherbet_val_profile; // _sherbet_val_code 를 파싱한 결과
		std::vector<sherbet::crosshair::quad> _sherbet_val_quads; // 매 프레임 재사용(할당 회피)

		// SHERBET: 발로란트 조준점의 오차 애니메이션(설계 §4). 게임 메모리는 읽지 않고
		// 이미 후킹 중인 입력(WASD·좌클릭)만 본다 — 그래서 **근사**이고, 그 한계는 §5 에 있다.
		// 끄면(_sherbet_val_err_on = false) 정적 조준점과 완전히 같은 결과가 나온다.
		bool _sherbet_val_err_on = false;
		bool _sherbet_val_err_paused = false;  // 일시정지 핫키 토글(§4.4 #2 — 게임 내 채팅 방어)
		bool _sherbet_val_pause_prev = false;  // 핫키 상승 엣지
		sherbet::crosshair::error_tuning _sherbet_val_tune;
		sherbet::crosshair::error_state _sherbet_val_err;
		// 이동 키는 게임마다 다르므로 전부 재바인딩 가능하게 둔다(§4.4 #3).
		unsigned int _sherbet_val_key_fwd[4] = { 'W', 0, 0, 0 };
		unsigned int _sherbet_val_key_back[4] = { 'S', 0, 0, 0 };
		unsigned int _sherbet_val_key_left[4] = { 'A', 0, 0, 0 };
		unsigned int _sherbet_val_key_right[4] = { 'D', 0, 0, 0 };
		unsigned int _sherbet_val_key_walk[4] = { 0x10, 0, 0, 0 }; // VK_SHIFT
		unsigned int _sherbet_val_key_pause[4] = { 0, 0, 0, 0 };   // 기본 없음

		// SHERBET: 조준점 마켓(「마켓」 탭 세 번째 세그먼트). 서버 항목은 sherbet::content_crosshairs()
		// 가 들고 있고, 여기 있는 것은 **내 것** 과 **되돌리기** 뿐이다.
		//
		// 되돌리기 규칙(sherbet_xhmarket.hpp): 마켓에서 처음 적용할 때 그 직전의 코드를
		// 한 번만 스냅샷한다. 카드를 몇 장을 눌러도 undo 는 "내가 튜닝하던 것" 을 가리킨다.
		// 스냅샷과 '지금 적용 중인 항목' 은 설정에 저장돼 재시작 후에도 되돌릴 수 있다.
		sherbet::xhmarket::session _sherbet_xh_session;
		std::vector<sherbet::xhmarket::local_slot> _sherbet_xh_locals; // 내 조준점 슬롯
		// 카드로 만든 로컬 슬롯(코드 파싱 결과 포함). 슬롯이 바뀔 때만 다시 만든다 —
		// 매 프레임 parse_code 를 64번 돌릴 이유가 없다.
		std::vector<sherbet::xhmarket::entry> _sherbet_xh_local_cards;
		bool _sherbet_xh_locals_dirty = true;
		// 마켓에서 코드를 적용하면 「에임」 탭의 공유 코드 입력상자도 갱신돼야 한다
		// (두 탭이 서로 다른 코드를 보여 주면 어느 쪽이 진짜인지 알 수 없다).
		bool _sherbet_val_code_dirty = false;
		int _sherbet_market_seg = 0; // 0=테마 1=프리셋 2=조준점 (저장 안 함 — 세션 한정)

		// SHERBET: 커스텀 배경 이미지 — 오버레이 창 배경을 사용자 사진으로. 'custompicture' 기능 구매자 전용.
		api::resource _sherbet_bg_tex = {};
		api::resource_view _sherbet_bg_srv = {};
		int _sherbet_bg_w = 0, _sherbet_bg_h = 0;
		bool _sherbet_bg_on = false;
		std::string _sherbet_bg_file;            // Sherbet-Backgrounds 폴더 기준 이미지 파일명
		float _sherbet_bg_opacity = 0.9f;        // 이미지 불투명도 0..1
		float _sherbet_bg_dim = 0.5f;            // 가독성용 어두운 스크림 강도 0..1
		bool _sherbet_bg_dirty = false;          // 이미지 재로딩 필요(선택 변경/디바이스 리셋 후)
		int _sherbet_bg_load = 0;                // 0=미시도 1=성공 2=실패 (위와 같은 이유)

		// SHERBET: 일일 알림(기본 밤 11:50). 게임 안에서 보여야 의미가 있다 — RP 는 알트탭이
		// 곧 죽음이라 창 밖 알림은 아무도 못 본다.
		// ⚠️ _sherbet_alarm_on 은 draw_gui() 의 early-out 조건에도 들어간다. 빼먹으면
		//    오버레이가 닫힌 시각(=알림이 떠야 할 바로 그 시각)에 판정이 한 번도 안 돈다.
		bool _sherbet_alarm_on = false;
		int  _sherbet_alarm_hour = 23;
		int  _sherbet_alarm_min = 50;
		float _sherbet_alarm_secs = 8.0f;      // 화면에 띄워 두는 시간
		std::string _sherbet_alarm_text;       // 비우면 기본 문구
		sherbet::alarm::state _sherbet_alarm;  // 세션 상태(디스크에 안 남긴다)

		// SHERBET: HUD 돋보기 — 화면의 한 사각형(체력·방어구 막대 등)을 확대해 크게 그린다.
		// **게임 상태를 읽지 않는다.** 픽셀을 확대할 뿐이다(메모리 접근 0, 상태 판정 0).
		// ⚠️ 좌표는 전부 **화면 대비 정규화(0~1)** — 반반 비교 분할선과 같은 관례.
		//    OSD 의 정규화는 '화면-위젯' 대비라 관례가 다르니 베끼지 말 것.
		bool _sherbet_mag_on = false;
		bool _sherbet_mag_picking = false;      // 영역 잡는 중
		float _sherbet_mag_drag[2] = { 0, 0 };  // 드래그 시작점(정규화)
		sherbet::mag::rect _sherbet_mag_rect;   // 잡은 영역(정규화)
		float _sherbet_mag_anchor[2] = { 0.5f, 0.30f }; // 확대창 **중심** 위치(정규화)
		float _sherbet_mag_zoom = 3.0f;
		float _sherbet_mag_opacity = 1.0f;
		int _sherbet_mag_cap_res[2] = { 0, 0 };  // 영역을 잡을 당시 해상도(바뀌면 안내)
		// 잘라낸 영역을 담는 텍스처. 영역 크기가 바뀌면 다시 만든다.
		api::resource _sherbet_mag_tex = {};
		api::resource_view _sherbet_mag_srv = {};
		int _sherbet_mag_tex_w = 0, _sherbet_mag_tex_h = 0;

		// SHERBET: 스프레이 트레이너 입력 진단(「에임」 탭). 게임 메모리·화면 픽셀은 읽지 않고
		// 이미 후킹 중인 입력만 관찰한다. 오버레이가 열려 있는 동안은 세지 않는다 —
		// UI 를 조작하는 클릭·이동은 사격이 아니다.
		unsigned int _sherbet_spray_clicks = 0;        // 좌클릭 상승 엣지 세션 누적
		bool _sherbet_spray_lmb_prev = false;          // 직전 프레임 좌클릭 상태(상승 엣지 계산용)
		unsigned long long _sherbet_spray_move_total = 0; // raw 이동량 절대값 세션 누적(단조 증가)

		// SHERBET: 스프레이 기록기와 설정. **두 토글은 독립이고 기본은 둘 다 꺼짐**이다 —
		// PVP 유저에게 프레임은 실력이라, 켜지 않은 구매자는 비용을 한 푼도 내지 않아야 한다.
		// 기록은 세션 한정(파일을 만들지 않는다).
		sherbet::spray::recorder _sherbet_spray;
		bool _sherbet_spray_live = false;   // 화면에 실시간 궤적
		bool _sherbet_spray_chart = false;  // 오버레이 에임 탭에 차트
		// ⚠️ 같은 카드의 다른 토글 넷은 전부 ini 왕복하는데 이것만 함수 지역 static 이라
		//    재시작하면 혼자 꺼졌다 — "설정이 저장이 안 되나?" 하는 인상을 준다.
		bool _sherbet_spray_overlay5 = false; // 최근 5개 겹쳐보기
		float _sherbet_spray_scale = 1.0f;  // raw → 픽셀 배율(감도가 사람마다 달라 필수)
		int _sherbet_spray_gap_ms = 400;    // 구간 나누기 임계값
		float _sherbet_spray_fade = 0.0f;   // 마지막 발사 후 남은 표시 시간(초). 2초에서 0 으로

		// SHERBET: 잠금 화면 미리보기(판매자 확인용). 켜면 유료 기능이 **권한과 무관하게**
		// 잠긴 것으로 취급된다 — 판매 카드도 뜨고, 잠긴 기능의 일도 실제로 멈춘다.
		// 이 스위치가 필요한 이유: sherbet::has_feature() 는 auth 가 꺼진 빌드에서 무조건
		// true 라, 판매자가 정작 제일 중요한 화면(잠긴 상태)을 한 번도 볼 수 없다.
		// 구매 내역과는 무관하며 끄면 즉시 원래대로 돌아온다.
		bool _sherbet_lock_preview = false;

		// SHERBET(실험): 화면 이동 추정 스파이크. 백버퍼 가운데를 잘라 CPU 로 리드백하고
		// 프레임 간 이동을 1D 투영 매칭으로 재서, 마우스 이동을 빼 **게임의 실제 반동**을
		// 추정한다(화면 = 반동 + 마우스 → 반동 = 화면 − 마우스).
		// ⚠️ 스프레이 트레이너와 달리 **화면 픽셀을 읽는다.** 그래서 기본이 꺼짐이고,
		//    꺼져 있으면 리소스 생성도 복사도 매핑도 전부 일어나지 않는다(렌더 경로 무변화).
		//    이 토글은 실험용이며, 결과가 쓸 만한지 판정한 뒤에 정식 기능 여부를 정한다.
		static constexpr unsigned int kSherbetMotionCrop = 512; // 가운데 크롭 한 변(px)
		static constexpr int kSherbetMotionStep = 4;            // 프로파일 만들 때 샘플 간격
		static constexpr int kSherbetMotionSearch = 32;         // 시프트 탐색 범위(±px)
		static constexpr int kSherbetMotionSlots = 3;           // 리드백 링(GPU 를 기다리지 않으려는 지연)
		static constexpr int kSherbetMotionMouseRing = 8;       // 프레임별 마우스 이동 보관
		static constexpr int kSherbetMotionMaxAge = 4;          // 이보다 오래된 슬롯은 짝지을 마우스 값이 없다

		bool _sherbet_motion_on = false;    // 메인 토글(기본 꺼짐)
		bool _sherbet_motion_hud = true;    // 화면에 작은 숫자판(메인 토글이 켜져 있을 때만)
		int _sherbet_motion_lag = 0;        // 화면 ↔ 마우스 정렬 보정(프레임). 엔진 입력 지연만큼 어긋난다
		api::resource _sherbet_motion_stage[kSherbetMotionSlots] = {};
		bool _sherbet_motion_slot_full[kSherbetMotionSlots] = {};
		uint64_t _sherbet_motion_slot_tag[kSherbetMotionSlots] = {};
		unsigned int _sherbet_motion_w = 0, _sherbet_motion_h = 0;
		api::format _sherbet_motion_format = api::format::unknown;
		int _sherbet_motion_kind = 0;       // sherbet::motion::pixel_kind 값
		// 0=아직 1회도 안 돌음 · 1=동작중 · 2=백버퍼 포맷 미지원 · 3=리드백 리소스 생성 실패
		// 2/3 은 매 프레임 재시도하지 않는다(실패가 확정된 상태다). 화면에 이유를 적는다.
		int _sherbet_motion_state = 0;
		uint64_t _sherbet_motion_frame = 0; // 이 기능 전용 프레임 카운터(draw_gui 에서 증가)
		// 마지막 복사를 건 시점의 _frame_count. 토글을 끄면 링(3 MiB)을 놓아주는데,
		// GPU 가 아직 그 복사를 처리 중일 수 있으므로 몇 프레임 지난 뒤에 놓는다.
		// (_frame_count 는 토글과 무관하게 항상 증가하므로 여기 기준으로 쓴다)
		uint64_t _sherbet_motion_last_write = 0;
		float _sherbet_motion_mouse[kSherbetMotionMouseRing][2] = {};
		std::vector<float> _sherbet_motion_vprof, _sherbet_motion_hprof;
		sherbet::motion::tracker _sherbet_motion;
		float _sherbet_motion_cpu_ms = 0.0f;      // 리드백+계산 CPU 시간(지수 이동평균, ms)
		unsigned int _sherbet_motion_samples = 0; // 누적 추정 수(살아 있는지 확인용)

		// SHERBET: 온라인 인증 컨트롤러. 캐시 토큰 로드 + 비동기 시작 검증을 담당하며,
		// update_effects()에서 인증 전 이펙트 컴파일/적용을 막는 게이트로 쓰인다.
		sherbet::auth::controller _sherbet_auth;
		bool _sherbet_auth_was_locked = false; // 인증 잠금 상태였는지(잠금→인증 전환 시 최초-로드 원샷 재무장용)
		bool _sherbet_content_was_active = false; // 직전 프레임 콘텐츠 페치 진행중이었는지(완료 엣지 감지용)
		float _sherbet_content_done_timer = 0.0f; // "불러오기 완료" 배너 남은 표시 시간(초)

		api::state_block _app_state = {};
		#pragma endregion

		#pragma region Screenshot
		bool _screenshot_save_before = false;
		bool _screenshot_include_preset = false;
#if RESHADE_GUI
		bool _screenshot_save_gui = false;
#endif
		bool _screenshot_clear_alpha = true;
		unsigned int _screenshot_count = 0;
		unsigned int _screenshot_format = 1;
		unsigned int _screenshot_jpeg_quality = 90;
		unsigned int _screenshot_key_data[4] = {};
		std::filesystem::path _screenshot_sound_path;
		std::filesystem::path _screenshot_path;
		std::string _screenshot_name;
		std::filesystem::path _screenshot_post_save_command;
		std::string _screenshot_post_save_command_arguments;
		std::filesystem::path _screenshot_post_save_command_working_directory;
		bool _screenshot_post_save_command_hide_window = false;

		bool _should_save_screenshot = false;
		std::atomic<bool> _last_screenshot_save_successful = true;
		bool _screenshot_directory_creation_successful = true;
		std::filesystem::path _last_screenshot_file;
		std::chrono::high_resolution_clock::time_point _last_screenshot_time;
		#pragma endregion

		#pragma region Preset Switching
		unsigned int _prev_preset_key_data[4] = {};
		unsigned int _next_preset_key_data[4] = {};
		unsigned int _preset_transition_duration = 1000;
		std::filesystem::path _startup_preset_path;
		std::filesystem::path _current_preset_path;

		bool _is_in_preset_transition = false;
		std::chrono::high_resolution_clock::time_point _last_preset_switching_time;

		struct preset_shortcut
		{
			std::filesystem::path preset_path;
			unsigned int key_data[4] = {};
		};
		std::vector<preset_shortcut> _preset_shortcuts;
		#pragma endregion

#if RESHADE_GUI
		void init_gui();
		bool init_gui_vr();
		void deinit_gui();
		void deinit_gui_vr();
		void build_font_atlas();

		void load_config_gui(const class ini_file &config);
		void save_config_gui(class ini_file &config) const;

		void load_custom_style();
		void save_custom_style() const;

		void draw_gui();
		void draw_gui_vr();

		// SHERBET: 자동 업데이트 배너(스펙 §6 '배너 배치'). 상태에 따라 제안/진행률/완료/
		// 롤백/필수를 그린다. 그릴 것이 없으면 아무것도 그리지 않으므로 호출부는 조건을
		// 몰라도 된다. compact=true 는 스플래시용 한 줄.
		// ⚠️ 오버레이 안에만 두면 Home 키를 안 누르는 구매자에게 영영 도달하지 않는다 —
		//    홈 탭 · 인증 게이트 패널 · 스플래시 **세 곳**에서 부른다.
		void draw_sherbet_update_card(bool compact);
		void draw_gui_home();
		void draw_gui_settings();
		// SHERBET: 「에임」 탭 — 입력 진단 + 커스텀 조준점(설정 탭에서 이전).
		void draw_gui_aim();
		// SHERBET: 「에임」 탭 안의 스프레이 트레이너 구획(유료 기능 'spray').
		// 잠겨 있으면 호출되지 않고 sherbet_draw_spray_lock_card() 가 대신 그린다.
		// 설정이 바뀌었으면 true 를 돌려준다.
		bool draw_gui_spray_trainer();
		void sherbet_draw_spray_lock_card(const sherbet::paid::feature &f);
		void draw_gui_optimize();
		// SHERBET: 「최적화」 탭이 통째로 잠겼을 때의 판매 카드. 런타임 실측 필드를 한 개도
		// 읽지 않는다 — 실측 코드와 다른 함수로 갈라 둔 것이 그 규칙의 구조적 보장이다.
		void sherbet_draw_optimize_lock_card(const sherbet::paid::feature &f);

		// SHERBET: 유료 기능 잠금 판정의 **호출부 단일 입구**.
		// sherbet::paid::unlocked() 에 서버 엔타이틀(has_feature)과 판매자 미리보기 스위치를
		// 함께 먹인다. UI 도 기록기도 전부 이 함수만 부른다 — 두 곳이 각자 다른 식으로
		// 판정하면 "화면은 열렸는데 일은 안 도는" 상태가 조용히 생긴다.
		bool sherbet_feature_unlocked(const char *id) const;
		// SHERBET: 잠긴 유료 기능의 판매 카드. 머리(이름·소개)와 발(사면 생기는 것·구매 동선)
		// 사이에 호출부가 자기 미리보기를 그린다 — 보여줄 그림은 기능마다 다르기 때문.
		void sherbet_draw_lock_header(const sherbet::paid::feature &f);
		void sherbet_draw_lock_footer(const sherbet::paid::feature &f);
		void draw_gui_statistics();
		void draw_gui_log();
		void draw_gui_about();
		void draw_gui_market();
		void draw_gui_crosshair_market(); // 「마켓」 탭의 조준점 세그먼트
#if RESHADE_ADDON
		void draw_gui_addons();
#endif
		void draw_variable_editor();
		void draw_technique_editor();
		void sherbet_load_crosshair(); // 선택된 커스텀 조준점 이미지를 텍스처로 로딩(파일 없으면 해제)
		void sherbet_load_background(); // 선택된 커스텀 배경 이미지를 텍스처로 로딩(파일 없으면 해제)

		bool init_imgui_resources();
		void render_imgui_draw_data(api::command_list *cmd_list, ImDrawData *draw_data, api::resource_view rtv);
		void destroy_imgui_resources();

		#pragma region Overlay
		ImGuiContext *_imgui_context = nullptr;
		ImFont *_sherbet_title_font = nullptr;
		int _sherbet_tab = 0; // 0=Home 1=Market 2=Settings 3=About
		int _sherbet_effect_filter = 0; // 이펙트 필터: 0=전체 1=켜짐 2=즐겨찾기
		std::set<std::string> _sherbet_fav; // 즐겨찾기 이펙트(테크닉 이름)

		bool _show_splash = true;
		bool _show_overlay = false;
		unsigned int _show_fps = 2;
		unsigned int _show_clock = false;
		unsigned int _show_frametime = false;
		unsigned int _show_preset_name = false;
		bool _show_screenshot_message = true;
		bool _show_preset_transition_message = true;
		unsigned int _reload_count = 0;

		bool _is_font_scaling = false;
		bool _no_font_scaling = false;
		bool _block_input_next_frame = false;
		bool _rebuild_font_atlas = true;
		unsigned int _overlay_key_data[4];
		unsigned int _fps_key_data[4] = {};
		unsigned int _frametime_key_data[4] = {};
		unsigned int _fps_pos = 1;
		unsigned int _clock_format = 0;
		unsigned int _input_processing_mode = 2;

		api::pipeline _imgui_pipeline = {};
		api::pipeline_layout _imgui_pipeline_layout = {};
		api::sampler  _imgui_sampler_state = {};

		int _imgui_num_indices[8] = {};
		api::resource _imgui_indices[8] = {};
		int _imgui_num_vertices[8] = {};
		api::resource _imgui_vertices[8] = {};

		api::resource _vr_overlay_tex = {};
		api::resource_view _vr_overlay_target = {};
		#pragma endregion

		#pragma region Overlay Home
		char _effect_filter[32] = {};
		bool _variable_editor_tabs = false;
		bool _auto_save_preset = true;
		bool _preset_is_modified = false;
		bool _inherit_current_preset = false;
		std::filesystem::path _template_preset_path;
		bool _was_preprocessor_popup_edited = false;
		size_t _focused_effect = std::numeric_limits<size_t>::max();
		size_t _selected_technique = std::numeric_limits<size_t>::max();
		unsigned int _tutorial_index = 0;
		unsigned int _effects_expanded_state = 2;
		float _variable_editor_height = 200.0f;
		#pragma endregion

		#pragma region Overlay Add-ons
		char _addons_filter[32] = {};
		#pragma endregion

		#pragma region Overlay Settings
		std::string _selected_language, _current_language;
		float _font_size = 13.0f;
		float _editor_font_size = 13.0f;
		int _style_index = 2;
		int _editor_style_index = 0;
		std::filesystem::path _font_path, _default_font_path;
		std::filesystem::path _latin_font_path;
		std::filesystem::path _editor_font_path, _default_editor_font_path;
		std::filesystem::path _file_selection_path;
		float _fps_col[4] = { 1.0f, 1.0f, 0.784314f, 1.0f };
		float _fps_scale = 1.0f;
		float _sherbet_osd_x = 0.02f; // OSD 가로 위치 (0=왼쪽,1=오른쪽)
		float _sherbet_osd_y = 0.02f; // OSD 세로 위치 (0=위,1=아래)
		bool _sherbet_osd_horizontal = false; // OSD 항목 가로 배치(끄면 세로 쌓기)
		float _hdr_overlay_brightness = 203.f; // HDR reference white as per BT.2408
		api::color_space _hdr_overlay_overwrite_color_space = api::color_space::unknown;
		bool  _show_force_load_effects_button = true;
		#pragma endregion

		#pragma region Overlay Statistics
		bool _gather_gpu_statistics = false;
		size_t _preview_texture = std::numeric_limits<size_t>::max();
		unsigned int _preview_size[3] = { 0, 0, 0xFFFFFFFF };
		uint64_t _timestamp_frequency = 0;
		#pragma endregion

		#pragma region Overlay Log
		char _log_filter[32] = {};
		uintmax_t _last_log_size = 0;
		imgui::code_editor _log_editor;
		#pragma endregion

		#pragma region Overlay Code Editor
		struct editor_instance
		{
			size_t effect_index;
			size_t permutation_index;
			std::filesystem::path file_path;
			std::string entry_point_name;
			bool selected = false;
			bool generated = false;
			imgui::code_editor editor;
		};

		void open_code_editor(size_t effect_index, size_t permutation_index, const std::string &entry_point);
		void open_code_editor(size_t effect_index, const std::filesystem::path &path);
		void open_code_editor(editor_instance &instance) const;
		void draw_code_editor(editor_instance &instance);

		std::vector<editor_instance> _editors;
		uint32_t _editor_palette[imgui::code_editor::color_palette_max];
		#pragma endregion
#endif
	};

	template <> void runtime::get_uniform_value<bool>(const uniform &variable, bool *values, size_t count, size_t array_index) const;
	template <> void runtime::get_uniform_value<float>(const uniform &variable, float *values, size_t count, size_t array_index) const;
	template <> void runtime::get_uniform_value<int32_t>(const uniform &variable, int32_t *values, size_t count, size_t array_index) const;
	template <> void runtime::get_uniform_value<uint32_t>(const uniform &variable, uint32_t *values, size_t count, size_t array_index) const;

	template <> void runtime::set_uniform_value<bool>(uniform &variable, const bool *values, size_t count, size_t array_index);
	template <> void runtime::set_uniform_value<float>(uniform &variable, const float *values, size_t count, size_t array_index);
	template <> void runtime::set_uniform_value<int32_t>(uniform &variable, const int32_t *values, size_t count, size_t array_index);
	template <> void runtime::set_uniform_value<uint32_t>(uniform &variable, const uint32_t *values, size_t count, size_t array_index);
}
