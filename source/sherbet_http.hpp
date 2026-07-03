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
		// HTTPS(443). 반환=HTTP 상태코드. 0=전송 실패 또는 상태코드 판독 불가(둘 다 '서버 사용 불가'로 취급).
		// 호출자는 0과 5xx를 재시도/오프라인 신호로 다뤄야 한다. out=응답 바디.
		int post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer = nullptr);
		int get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer = nullptr);
	}
}
