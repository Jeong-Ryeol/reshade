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
	// unlocked=false 인 항목은 **진열 전용**이다 — 서버가 id(다운로드 키)를 빼고 내려보내므로
	// 이름만 있고 받을 수 없다. 마켓에 '잠김' 카드로 보여 주기 위해 목록에는 남긴다.
	struct content_item { std::string id, filename, display_name; bool unlocked = true; };

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
			it.unlocked = detail::json_bool(obj, "unlocked", true); // 미표기(구버전 서버)면 열림
			// 열린 항목인데 id 가 없으면 받을 방법이 없다 — 버린다(기존 동작).
			// 잠긴 항목은 id 가 없는 것이 정상이므로 진열용으로 남기되, 이름조차 없으면 버린다.
			if (it.unlocked ? it.id.empty() : (it.display_name.empty() && it.filename.empty()))
				continue;
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
				// ⚠️ 잠긴 항목은 **절대 받지 않는다.** 서버가 id 를 안 주므로 아래 id.empty() 로도
				//    걸리지만, 의도를 코드에 남긴다 — 이게 뚫리면 잠긴 상품마다 403 요청이 나가고
				//    그때마다 서버가 디스코드에 역할을 물어본다(요청 폭증).
				if (!it.unlocked) continue;
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
