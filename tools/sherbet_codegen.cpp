/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */

// SHERBET 언락코드 생성기 (판매자 전용 오프라인 툴).
//
// DLL 본체와 동일한 sherbet_license.hpp 를 공유하므로, 여기서 발급한 코드는
// 게임 내 검증(테마/프리셋 언락)과 항상 일치한다.
//
// 빌드:
//   Windows (MSVC 개발자 프롬프트):  cl /EHsc /I..\source sherbet_codegen.cpp
//   Windows (MinGW/clang):           g++ -std=c++17 -I../source sherbet_codegen.cpp -o sherbet_codegen
//   macOS/Linux:                     clang++ -std=c++17 -I../source sherbet_codegen.cpp -o sherbet_codegen
//
// 사용:
//   sherbet_codegen theme  mint          →  SHRB-XXXX-XXXX   (테마 "mint" 언락코드)
//   sherbet_codegen preset ORD-1042      →  PRE-XXXX-XXXX    (주문번호 ORD-1042 전용 프리셋 언락코드)
//   sherbet_codegen hwid   <hwid문자열>  →  LIC-XXXX-XXXX    (노드락 서명값 — 필요 시 수동 sherbet.lic)
//
// 인자를 생략하면 전체 테마 목록의 코드를 한 번에 뽑아준다.

#include "sherbet_license.hpp"

#include <cstdio>
#include <cstring>

int main(int argc, char **argv)
{
	char code[24];

	if (argc >= 3)
	{
		const char *kind = argv[1];
		const char *id = argv[2];

		if (std::strcmp(kind, "theme") == 0)
			sherbet::license::make_code("SHRB", "theme", id, code, sizeof(code));
		else if (std::strcmp(kind, "preset") == 0)
			sherbet::license::make_code("PRE", "preset", id, code, sizeof(code));
		else if (std::strcmp(kind, "hwid") == 0)
			sherbet::license::sign_hwid(id, code, sizeof(code));
		else
		{
			std::fprintf(stderr, "unknown kind '%s' (theme|preset|hwid)\n", kind);
			return 2;
		}

		std::printf("%s\n", code);
		return 0;
	}

	// 인자 없음: 자주 쓰는 테마 목록 코드 일괄 출력
	static const char *themes[] = { "mint", "peach", "pink", "rainbow", "lavender", "noir" };
	std::printf("== 테마 언락코드 (SHRB) ==\n");
	for (const char *t : themes)
	{
		sherbet::license::make_code("SHRB", "theme", t, code, sizeof(code));
		std::printf("  %-8s  %s\n", t, code);
	}
	std::printf("\n프리셋 코드:  sherbet_codegen preset <주문번호>\n");
	return 0;
}
