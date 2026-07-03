/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 원격 콘텐츠(프리셋/이펙트) 매니페스트 파서 — 순수 로직.
// sherbet_theme_json.hpp 의 JSON 헬퍼 재사용. WinInet/스레드 없음 → host 테스트 가능.
#pragma once

#include "sherbet_theme_json.hpp"
#include <string>
#include <vector>

namespace sherbet
{
	struct content_item { std::string id, filename, display_name; };

	inline std::vector<content_item> parse_content_items(const std::string &body, const char *key)
	{
		std::vector<content_item> out;
		for (const std::string &obj : detail::split_top_level_objects(body, key))
		{
			content_item it;
			detail::json_str(obj, "id", it.id);
			detail::json_str(obj, "filename", it.filename);
			if (!detail::json_str(obj, "display_name", it.display_name))
				it.display_name = it.filename;
			if (it.id.empty()) continue;
			out.push_back(std::move(it));
		}
		return out;
	}

	// 경로 구분자 뒤 마지막 요소만. ".." 포함 요소는 거부(빈 문자열).
	inline std::string safe_basename(const std::string &filename)
	{
		std::size_t slash = filename.find_last_of("/\\");
		std::string base = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
		if (base.empty() || base == "." || base == "..") return "";
		if (filename.find("..") != std::string::npos) return ""; // 경로 어디에든 .. 있으면 거부
		return base;
	}

	struct download_target { std::string id, dest_path; };

	inline std::vector<download_target> content_download_targets(
		const std::string &body, const std::string &presets_dir, const std::string &effects_dir)
	{
		std::vector<download_target> out;
		auto add = [&out](const std::vector<content_item> &items, const std::string &dir) {
			for (const content_item &it : items)
			{
				const std::string base = safe_basename(it.filename);
				if (it.id.empty() || base.empty()) continue;
				out.push_back({ it.id, dir + "/" + base });
			}
		};
		add(parse_content_items(body, "presets"), presets_dir);
		add(parse_content_items(body, "effects"), effects_dir);
		return out;
	}
}
