/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/t && /tmp/t
#include "sherbet_update_core.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::update;

static void test_parse_version() {
	version3 v;
	assert(parse_version("1.4.0", v) && v.major == 1 && v.minor == 4 && v.patch == 0);
	assert(parse_version("0.0.0", v) && v.major == 0 && v.minor == 0 && v.patch == 0);
	assert(parse_version("10.20.30", v) && v.major == 10 && v.minor == 20 && v.patch == 30);

	// 거부 케이스
	assert(!parse_version("", v));
	assert(!parse_version("1.4", v));          // 필드 부족
	assert(!parse_version("1.4.0.1", v));      // 필드 초과
	assert(!parse_version("1.4.x", v));        // 숫자 아님
	assert(!parse_version("v1.4.0", v));       // 접두사
	assert(!parse_version(" 1.4.0", v));       // 앞 공백
	assert(!parse_version("1.4.0 ", v));       // 뒤 공백
	assert(!parse_version("1..0", v));         // 빈 필드
	assert(!parse_version("1.4.99999999", v)); // 거대 정수

	// 실패 시 out 이 오염되지 않아야 한다
	version3 keep{ 7, 7, 7 };
	assert(!parse_version("garbage", keep));
	assert(keep.major == 7 && keep.minor == 7 && keep.patch == 7);
}

static void test_version_cmp() {
	version3 a, b;
	assert(parse_version("1.4.0", a) && parse_version("1.4.0", b));
	assert(version_cmp(a, b) == 0);

	// 문자열 비교였다면 틀리는 케이스 — "1.10.0" < "1.9.0" 이 되어버린다
	assert(parse_version("1.10.0", a) && parse_version("1.9.0", b));
	assert(version_cmp(a, b) == 1);
	assert(version_cmp(b, a) == -1);

	assert(parse_version("2.0.0", a) && parse_version("1.99.99", b));
	assert(version_cmp(a, b) == 1);

	assert(parse_version("1.0.1", a) && parse_version("1.0.0", b));
	assert(version_cmp(a, b) == 1);
}

int main() {
	test_parse_version();
	test_version_cmp();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
