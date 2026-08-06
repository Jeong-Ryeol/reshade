/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 사격 훈련 리더보드 응답 파서 — 순수 로직(플랫폼 비의존).
//
// 서버(server/app/aim.py)의 /aim/leaderboard · /aim/score 응답을 읽는다.
// imgui 도 Windows 도 필요 없어서 호스트 테스트로 전수 검증하고, 서버 테스트가
// **이 파서에 라우터 응답을 직접 먹여** 계약을 크로스체크할 수 있다.
//
// ⚠️ 서버는 **모든 숫자를 따옴표 문자열로** 낸다. sherbet_json.hpp 의 평면 헬퍼가
//    따옴표 있는 값만 읽기 때문이다. 숫자로 오면 여기서 0 이 되고, 리더보드가
//    조용히 전부 '0개' 로 보인다 — 아무 데도 오류가 나지 않는다. 그래서 서버 쪽에
//    같은 경고가 붙어 있고 pytest 가 문자열인지 단언한다.
#pragma once

#include "sherbet_json.hpp"

#include <string>
#include <vector>

namespace sherbet
{
	namespace aim
	{
		struct board_row
		{
			int rank = 0;
			std::string name;
			int hits = 0;
			float accuracy = 0.0f; // 0~1
		};

		struct board
		{
			std::vector<board_row> top;
			int my_rank = 0;  // 0 = 기록 없음
			int my_hits = 0;
			int total = 0;    // 이 표에 기록을 낸 전체 인원
		};

		namespace detail
		{
			// 따옴표 문자열로 온 정수. 없거나 숫자가 아니면 def.
			// ⚠️ atoi 계열을 쓰지 않는다 — 서버가 이상한 값을 보내도 여기서 멈춰야지,
			//    로캘이나 오버플로에 기대면 안 된다.
			inline int json_int(const std::string &obj, const char *key, int def = 0)
			{
				std::string s;
				if (!sherbet::detail::json_str(obj, key, s) || s.empty())
					return def;
				bool neg = false;
				std::size_t i = 0;
				if (s[0] == '-') { neg = true; i = 1; }
				if (i >= s.size())
					return def;
				long long v = 0;
				for (; i < s.size(); ++i)
				{
					if (s[i] < '0' || s[i] > '9')
						return def;
					v = v * 10 + (s[i] - '0');
					if (v > 100000000LL) // 사람이 낼 수 있는 값을 한참 넘었다 — 더 읽지 않는다
						return def;
				}
				return static_cast<int>(neg ? -v : v);
			}

			// 따옴표 문자열로 온 0~1 실수. 소수점 이하만 다루면 충분하다(정확도).
			// std::stof 를 쓰지 않는 이유: 로캘에 따라 소수점이 ',' 인 환경에서 조용히
			// 잘못 읽는다(한국 윈도우는 '.' 이지만 구매자 환경을 가정하지 않는다).
			inline float json_unit(const std::string &obj, const char *key, float def = 0.0f)
			{
				std::string s;
				if (!sherbet::detail::json_str(obj, key, s) || s.empty())
					return def;
				std::size_t i = 0;
				long long whole = 0;
				for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
				{
					whole = whole * 10 + (s[i] - '0');
					if (whole > 10) return def;
				}
				if (i == 0)
					return def; // 정수부가 아예 없다
				float v = static_cast<float>(whole);
				if (i < s.size() && s[i] == '.')
				{
					++i;
					float scale = 0.1f;
					int digits = 0;
					for (; i < s.size() && s[i] >= '0' && s[i] <= '9' && digits < 8; ++i, ++digits)
					{
						v += static_cast<float>(s[i] - '0') * scale;
						scale *= 0.1f;
					}
				}
				if (i != s.size())
					return def; // 잔여물이 있다 — 통째로 거부한다
				if (v < 0.0f) v = 0.0f;
				if (v > 1.0f) v = 1.0f;
				return v;
			}
		}

		// 리더보드 응답 파싱. 형식이 아니면 false 를 돌려주고 out 은 건드리지 않는다.
		// "기록이 아직 없음"(top 이 빈 배열)은 **성공**이다 — 새 표는 원래 비어 있다.
		inline bool parse_board(const std::string &body, board &out)
		{
			if (body.empty())
				return false;
			// total 이 없으면 우리 응답이 아니다(오류 페이지·프록시 안내문 방어).
			std::string probe;
			if (!sherbet::detail::json_str(body, "total", probe))
				return false;

			board b;
			// ⚠️ 최상위 키가 `top` 줄의 키와 겹치면 안 된다. 평면 헬퍼는 중첩을 모르고
			//    body 전체에서 첫 "key" 를 찾으므로, 최상위를 "rank" 로 두면 top[0] 의
			//    "rank" 가 먼저 걸려 내 순위가 항상 1위로 읽힌다 — 조용히 틀린다.
			//    그래서 서버가 my_rank / my_hits 로 낸다(호스트 테스트가 잡아낸 건이다).
			b.total = detail::json_int(body, "total");
			b.my_rank = detail::json_int(body, "my_rank");
			b.my_hits = detail::json_int(body, "my_hits");

			for (const std::string &obj : sherbet::detail::split_top_level_objects(body, "top"))
			{
				board_row r;
				r.rank = detail::json_int(obj, "rank");
				r.hits = detail::json_int(obj, "hits");
				r.accuracy = detail::json_unit(obj, "accuracy");
				sherbet::detail::json_str(obj, "name", r.name);
				// 이름 없는 줄은 버린다. 화면에 빈 칸만 뜨느니 없는 게 낫다.
				if (r.rank > 0 && !r.name.empty())
					b.top.push_back(r);
			}

			out = b;
			return true;
		}

		// 기록 제출 응답(/aim/score)도 같은 모양이라 같은 파서를 쓴다.
		// updated 만 따로 읽는다 — 진짜 불리언이다(숫자와 달리 따옴표가 없다).
		inline bool parse_submit(const std::string &body, board &out, bool &updated)
		{
			if (!parse_board(body, out))
				return false;
			updated = sherbet::detail::json_bool(body, "updated", false);
			return true;
		}
	}
}
