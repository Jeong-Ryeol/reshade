/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — 순수 로직(플랫폼 비의존).
// WinInet/스레드/ImGui/std::filesystem 은 여기 넣지 않는다. sherbet_update.cpp 가 글루를 맡는다.
// 판정 로직 100% 를 여기 몰아넣어 맥 clang 으로 실제 단위테스트한다(tools/sherbet_update_test.cpp).
#pragma once

#include <string>
#include <cstddef>

namespace sherbet
{
	namespace update
	{
		struct version3 { unsigned major = 0, minor = 0, patch = 0; };

		// "major.minor.patch" 정확히 3필드. 공백·접두사·잔여물 전부 거부.
		// 실패하면 false 를 반환하고 out 을 건드리지 않는다(호출자가 '업데이트 없음' 으로 처리).
		inline bool parse_version(const std::string &s, version3 &out)
		{
			version3 v;
			unsigned *field[3] = { &v.major, &v.minor, &v.patch };
			std::size_t i = 0;
			for (int f = 0; f < 3; ++f)
			{
				if (f > 0)
				{
					if (i >= s.size() || s[i] != '.') return false;
					++i;
				}
				const std::size_t start = i;
				unsigned long long acc = 0;
				while (i < s.size() && s[i] >= '0' && s[i] <= '9')
				{
					acc = acc * 10 + static_cast<unsigned>(s[i] - '0');
					if (acc > 999999ULL) return false; // 거대 정수 거부(오버플로 방지)
					++i;
				}
				if (i == start) return false;          // 숫자가 하나도 없음
				*field[f] = static_cast<unsigned>(acc);
			}
			if (i != s.size()) return false;           // 뒤에 잔여물(공백 포함)
			out = v;
			return true;
		}

		// 정수 3필드 사전식. 문자열 비교를 쓰면 "1.10.0" < "1.9.0" 이 되므로 반드시 이걸 쓴다.
		inline int version_cmp(const version3 &a, const version3 &b)
		{
			if (a.major != b.major) return a.major < b.major ? -1 : 1;
			if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
			if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
			return 0;
		}
	}
}
