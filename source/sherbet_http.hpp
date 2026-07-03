/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <string>

namespace sherbet
{
	namespace http
	{
		// HTTPS(443). 반환=HTTP status(성공) 또는 0(전송 실패). out=응답 바디.
		int post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer = nullptr);
		int get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer = nullptr);
	}
}
