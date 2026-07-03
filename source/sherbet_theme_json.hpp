/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 원격 테마 JSON 파서 — 순수 로직(플랫폼 비의존, WinInet/스레드 없음).
// imgui.h(IM_COL32/ImU32) + sherbet_theme.hpp(particle) 만 의존 → host 테스트 가능.
#pragma once

#include "sherbet_theme.hpp"
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
		// obj 안에서 "key":"value" 문자열 값 추출(단순, \" \\ 언이스케이프). 없으면 false.
		inline bool json_str(const std::string &obj, const char *key, std::string &out)
		{
			const std::string needle = std::string("\"") + key + "\"";
			std::size_t k = obj.find(needle);
			if (k == std::string::npos) return false;
			std::size_t colon = obj.find(':', k + needle.size());
			if (colon == std::string::npos) return false;
			std::size_t i = colon + 1;
			while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
			if (i >= obj.size() || obj[i] != '"') return false;
			++i;
			std::string val;
			for (; i < obj.size(); ++i)
			{
				const char c = obj[i];
				if (c == '\\' && i + 1 < obj.size()) { val += obj[++i]; continue; }
				if (c == '"') { out = val; return true; }
				val += c;
			}
			return false;
		}

		// obj 안에서 "key":true/false. 없거나 불리언 아니면 def.
		inline bool json_bool(const std::string &obj, const char *key, bool def)
		{
			const std::string needle = std::string("\"") + key + "\"";
			std::size_t k = obj.find(needle);
			if (k == std::string::npos) return def;
			std::size_t colon = obj.find(':', k + needle.size());
			if (colon == std::string::npos) return def;
			std::size_t i = colon + 1;
			while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
			if (obj.compare(i, 4, "true") == 0) return true;
			if (obj.compare(i, 5, "false") == 0) return false;
			return def;
		}

		// obj 안 "key":"#rrggbbaa" → dst. 실패 시 false.
		inline bool color_field(const std::string &obj, const char *key, ImU32 &dst)
		{
			std::string hex;
			return json_str(obj, key, hex) && parse_hex_color(hex, dst);
		}

		// body 의 "key":[ {..}, {..} ] 배열에서 최상위 객체 문자열들을 brace 매칭으로 잘라 반환.
		inline std::vector<std::string> split_top_level_objects(const std::string &body, const char *key)
		{
			std::vector<std::string> out;
			std::string needle = std::string("\"") + key + "\"";
			std::size_t tk = body.find(needle);
			if (tk == std::string::npos) return out;
			std::size_t lb = body.find('[', tk);
			if (lb == std::string::npos) return out;
			std::size_t i = lb + 1;
			while (i < body.size())
			{
				while (i < body.size() && body[i] != '{' && body[i] != ']') ++i;
				if (i >= body.size() || body[i] == ']') break;
				const std::size_t start = i;
				int depth = 0;
				bool in_str = false;
				for (; i < body.size(); ++i)
				{
					const char c = body[i];
					if (in_str) { if (c == '\\') { ++i; continue; } if (c == '"') in_str = false; continue; }
					if (c == '"') in_str = true;
					else if (c == '{') ++depth;
					else if (c == '}') { if (--depth == 0) { ++i; break; } }
				}
				out.push_back(body.substr(start, i - start));
			}
			return out;
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
