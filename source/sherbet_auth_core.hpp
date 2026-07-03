/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 온라인 인증 — 순수 로직(플랫폼 비의존). WinInet/스레드/ImGui 등은 여기 넣지 않는다.
#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
	namespace auth
	{
		struct start_result { bool ok = false; std::string state; std::string authorize_url; };
		struct poll_result { enum status_t { pending, ready, denied, error } status = error; std::string token; std::string reason; };
		struct verify_result { bool ok = false; bool upstream_down = false; std::string sub; std::vector<std::string> roles; };

		// "key" 다음의 문자열 값을 찾는다. 매우 단순: `"key"` 뒤 첫 `"`쌍의 내용을 읽되 \" \\ 만 언이스케이프.
		inline bool json_string(const std::string &body, const char *key, std::string &out)
		{
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return false;
			std::size_t colon = body.find(':', k + needle.size());
			if (colon == std::string::npos) return false;
			// 값 시작(공백 스킵)
			std::size_t i = colon + 1;
			while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
			if (i >= body.size() || body[i] != '"') return false; // 문자열 아님
			++i;
			std::string v;
			for (; i < body.size(); ++i) {
				char c = body[i];
				if (c == '\\' && i + 1 < body.size()) { char n = body[++i]; v += (n == 'n' ? '\n' : n); continue; }
				if (c == '"') { out = v; return true; }
				v += c;
			}
			return false;
		}

		// 1=true, 0=false, -1=null 또는 없음/불리언 아님.
		inline int json_bool_or_null(const std::string &body, const char *key)
		{
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return -1;
			std::size_t colon = body.find(':', k + needle.size());
			if (colon == std::string::npos) return -1;
			std::size_t i = colon + 1;
			while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) ++i;
			if (body.compare(i, 4, "true") == 0) return 1;
			if (body.compare(i, 5, "false") == 0) return 0;
			return -1; // null 포함
		}

		// "key":["a","b"] → {"a","b"}
		inline std::vector<std::string> json_string_array(const std::string &body, const char *key)
		{
			std::vector<std::string> out;
			std::string needle = std::string("\"") + key + "\"";
			std::size_t k = body.find(needle);
			if (k == std::string::npos) return out;
			std::size_t lb = body.find('[', k);
			if (lb == std::string::npos) return out;
			std::size_t rb = body.find(']', lb);
			if (rb == std::string::npos) return out;
			for (std::size_t i = lb + 1; i < rb; ) {
				std::size_t q1 = body.find('"', i);
				if (q1 == std::string::npos || q1 >= rb) break;
				std::size_t q2 = body.find('"', q1 + 1);
				if (q2 == std::string::npos || q2 > rb) break;
				out.push_back(body.substr(q1 + 1, q2 - q1 - 1));
				i = q2 + 1;
			}
			return out;
		}

		inline start_result parse_start(const std::string &body)
		{
			start_result r;
			r.ok = json_string(body, "state", r.state) && json_string(body, "authorize_url", r.authorize_url);
			return r;
		}

		inline poll_result parse_poll(const std::string &body)
		{
			poll_result r;
			std::string status;
			if (!json_string(body, "status", status)) { r.status = poll_result::error; return r; }
			if (status == "pending") r.status = poll_result::pending;
			else if (status == "ready") { r.status = poll_result::ready; json_string(body, "token", r.token); }
			else if (status == "denied") { r.status = poll_result::denied; json_string(body, "reason", r.reason); }
			else r.status = poll_result::error;
			return r;
		}

		inline verify_result parse_verify(const std::string &body, int http_status)
		{
			verify_result r;
			if (http_status == 0 || http_status >= 500) { r.upstream_down = true; return r; } // 전송 실패 또는 503/5xx
			const int v = json_bool_or_null(body, "valid");
			if (v == 1) {
				r.ok = true;
				json_string(body, "sub", r.sub);
				r.roles = json_string_array(body, "roles");
			}
			return r; // v==0(구매자 아님) 또는 v==-1 → ok=false, upstream_down=false
		}
	}
}
