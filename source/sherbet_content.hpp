/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <string>

namespace sherbet
{
	namespace content
	{
		// /content/me 를 Bearer 로 GET. 200+비어있지 않은 바디면 out_body 설정 + 캐시 파일 기록 후 true.
		bool fetch(const std::string &bearer, const std::string &config_dir_utf8, std::string &out_body);
		// 캐시 파일(sherbet.themes) 로드. 성공 시 out_body 설정 + true.
		bool load_cached(const std::string &config_dir_utf8, std::string &out_body);
		// /content/file/<id> 를 Bearer 로 GET. 200+비어있지 않으면 dest_path 에 바이트 기록 후 true.
		bool fetch_file(const std::string &bearer, const std::string &item_id, const std::string &dest_path_utf8);
	}
}
