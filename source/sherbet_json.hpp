/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 서버 매니페스트용 **평면 substring JSON 헬퍼** — 순수 로직, 의존성은 std 뿐이다.
//
// 원래 sherbet_theme_json.hpp 안에 있던 detail:: 세 함수를 그대로 옮겨 왔다(동작 무변경).
// 옮긴 이유: 테마 파서는 ImU32 때문에 imgui.h 를 필요로 하는데, 조준점 마켓 파서는
// 색을 다루지 않으므로 imgui 없이 컴파일돼야 서버 테스트(deps/imgui 서브모듈이 없는
// 잡)에서도 **진짜 클라 파서**로 라우터 응답을 크로스체크할 수 있다.
//
// ⚠️ 이것은 JSON 파서가 아니다. 중첩 객체를 모르고, 값은 **따옴표로 감싼 문자열**만
//    읽는다(따옴표 없는 숫자/불리언은 json_str 이 false 를 낸다 — 조용히 기본값이 된다).
//    새 페이로드를 설계할 때의 계약은 docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md §3.3.
#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
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
}
