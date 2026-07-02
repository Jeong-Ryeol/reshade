/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */

// SHERBET 통합 오프라인 코드 검증 모듈.
// 테마 언락 / 프리셋 언락 / 노드락 서명이 하나의 FNV-1a 공식을 공유한다.
// DLL 본체와 tools/sherbet_codegen 이 같은 헤더를 include 하므로,
// 여기서 만든 코드는 항상 게임 내 검증과 일치한다.
//
// 형식:
//   테마   : SHRB-XXXX-XXXX  (kind="theme")
//   프리셋 : PRE-XXXX-XXXX   (kind="preset")
//   노드락 : LIC-XXXX-XXXX   (kind="hwid", sherbet.lic 에 저장되는 서명)
//
// 주의: FNV-1a 는 암호학적 서명이 아니라 난독화 수준의 보호다.
// 오프라인 소규모 판매용으로 "복붙 재사용/손쉬운 위조 방지" 목적에 충분하다.

#pragma once

#include <cstdio>

namespace sherbet
{
	namespace license
	{
		// 이 값이 바뀌면 기존에 발급한 모든 코드가 무효가 된다. 함부로 변경 금지.
		inline const char *secret() { return "sherbet-by-jeongryeol-2026"; }

		inline unsigned int fnv1a(const char *s)
		{
			unsigned int h = 2166136261u;
			for (; *s != '\0'; ++s)
			{
				h ^= static_cast<unsigned char>(*s);
				h *= 16777619u;
			}
			return h;
		}

		// prefix: "SHRB"/"PRE"/"LIC", kind: "theme"/"preset"/"hwid".
		// out 은 최소 24바이트. 결과 예: "SHRB-1A2B-3C4D".
		inline void make_code(const char *prefix, const char *kind, const char *id, char *out, unsigned int out_size)
		{
			char buf[192];
			std::snprintf(buf, sizeof(buf), "%s:%s:%s", secret(), kind, id);
			const unsigned int h = fnv1a(buf);
			std::snprintf(out, out_size, "%s-%04X-%04X", prefix, (h >> 16) & 0xFFFF, h & 0xFFFF);
		}

		// 대소문자 무시 문자열 비교(공백/하이픈 포함 그대로 비교).
		inline bool iequals(const char *a, const char *b)
		{
			if (a == nullptr || b == nullptr)
				return false;
			for (int i = 0; a[i] != '\0' || b[i] != '\0'; ++i)
			{
				char x = a[i], y = b[i];
				if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
				if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
				if (x != y)
					return false;
			}
			return true;
		}

		inline bool verify(const char *prefix, const char *kind, const char *id, const char *code)
		{
			if (id == nullptr || code == nullptr)
				return false;
			char expect[24];
			make_code(prefix, kind, id, expect, sizeof(expect));
			return iequals(expect, code);
		}

		// 테마 언락코드 검증 (기존 SHRB-XXXX-XXXX 와 100% 호환).
		inline bool verify_theme(const char *id, const char *code) { return verify("SHRB", "theme", id, code); }
		// 프리셋 언락코드 검증.
		inline bool verify_preset(const char *id, const char *code) { return verify("PRE", "preset", id, code); }

		// 노드락 서명: sherbet.lic 에 저장/비교할 HWID 서명 토큰을 만든다.
		inline void sign_hwid(const char *hwid, char *out, unsigned int out_size)
		{
			make_code("LIC", "hwid", hwid, out, out_size);
		}
	}
}
