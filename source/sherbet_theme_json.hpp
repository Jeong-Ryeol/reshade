/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 원격 테마 JSON 파서 — 순수 로직(플랫폼 비의존, WinInet/스레드 없음).
// imgui.h(IM_COL32/ImU32) + sherbet_theme.hpp(particle) 만 의존 → host 테스트 가능.
#pragma once

#include "sherbet_theme.hpp"
#include "sherbet_json.hpp" // detail::json_str / json_bool / split_top_level_objects (imgui 비의존)
#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
	struct parsed_theme
	{
		std::string id, display_name;
		ImU32 bg0 = 0, bg1 = 0, bg2 = 0, panel = 0, panel_alt = 0, chip = 0, border = 0;
		ImU32 text = 0, text_dim = 0, accent = 0, accent2 = 0, glow = 0;
		particle particle_shape = particle::spark;
		bool hue_cycle = false;
		bool unlocked = true; // 서버가 이 유저에게 해제해줬는지. false면 마켓에 '잠김'으로 진열
		bool ok = false; // id + 12색 모두 유효할 때만 true
	};

	// "#rrggbbaa"(9자) → IM_COL32(r,g,b,a). 형식 불량 → false(out 미변경).
	inline bool parse_hex_color(const std::string &s, ImU32 &out)
	{
		if (s.size() != 9 || s[0] != '#') return false;
		int v[8];
		for (int i = 0; i < 8; ++i)
		{
			const char c = s[i + 1];
			if (c >= '0' && c <= '9') v[i] = c - '0';
			else if (c >= 'a' && c <= 'f') v[i] = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v[i] = c - 'A' + 10;
			else return false;
		}
		const int r = v[0] * 16 + v[1], g = v[2] * 16 + v[3];
		const int b = v[4] * 16 + v[5], a = v[6] * 16 + v[7];
		out = IM_COL32(r, g, b, a);
		return true;
	}

	inline particle parse_particle(const std::string &s)
	{
		if (s == "heart") return particle::heart;
		if (s == "leaf") return particle::leaf;
		if (s == "petal") return particle::petal;
		return particle::spark;
	}

	namespace detail
	{
		// json_str / json_bool / split_top_level_objects 는 sherbet_json.hpp 로 옮겼다
		// (조준점 마켓 파서가 imgui 없이 같은 헬퍼를 쓰기 위해서다 — 동작은 그대로).

		// obj 안 "key":"#rrggbbaa" → dst. 실패 시 false.
		inline bool color_field(const std::string &obj, const char *key, ImU32 &dst)
		{
			std::string hex;
			return json_str(obj, key, hex) && parse_hex_color(hex, dst);
		}
	}

	inline std::vector<parsed_theme> parse_themes_manifest(const std::string &body)
	{
		std::vector<parsed_theme> out;
		for (const std::string &obj : detail::split_top_level_objects(body, "themes"))
		{
			parsed_theme pt;
			detail::json_str(obj, "id", pt.id);
			if (!detail::json_str(obj, "display_name", pt.display_name))
				pt.display_name = pt.id;
			std::string ps;
			if (detail::json_str(obj, "particle", ps)) pt.particle_shape = parse_particle(ps);
			pt.hue_cycle = detail::json_bool(obj, "hue_cycle", false);
			pt.unlocked = detail::json_bool(obj, "unlocked", true); // 서버 미표기(구버전)면 해제로 간주(하위호환)
			const bool colors_ok =
				detail::color_field(obj, "bg0", pt.bg0) & detail::color_field(obj, "bg1", pt.bg1) &
				detail::color_field(obj, "bg2", pt.bg2) & detail::color_field(obj, "panel", pt.panel) &
				detail::color_field(obj, "panel_alt", pt.panel_alt) & detail::color_field(obj, "chip", pt.chip) &
				detail::color_field(obj, "border", pt.border) & detail::color_field(obj, "text", pt.text) &
				detail::color_field(obj, "text_dim", pt.text_dim) & detail::color_field(obj, "accent", pt.accent) &
				detail::color_field(obj, "accent2", pt.accent2) & detail::color_field(obj, "glow", pt.glow);
			pt.ok = !pt.id.empty() && colors_ok;
			out.push_back(std::move(pt));
		}
		return out;
	}
}
