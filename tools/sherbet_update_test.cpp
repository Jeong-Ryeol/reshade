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

static std::string sha_of(const std::string &s) {
	sha256_ctx c; sha256_init(c);
	sha256_update(c, s.data(), s.size());
	return sha256_final_hex(c);
}

static void test_sha256_nist() {
	// NIST 표준 벡터
	assert(sha_of("") ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	assert(sha_of("abc") ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	assert(sha_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	std::string million(1000000, 'a');
	assert(sha_of(million) ==
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void test_sha256_padding_boundaries() {
	// 패딩 버그가 사는 곳이 정확히 여기다. 길이별로 일괄 해시와 스트리밍 해시가 같아야 한다.
	const std::size_t lens[] = { 0, 1, 54, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 1000 };
	for (std::size_t n : lens) {
		std::string data;
		for (std::size_t i = 0; i < n; ++i) data += static_cast<char>('a' + (i % 26));
		const std::string once = sha_of(data);

		// 홀수 청크(1,7,13,…)로 나눠 넣어도 같은 결과여야 한다
		sha256_ctx c; sha256_init(c);
		std::size_t off = 0, chunk = 1;
		while (off < data.size()) {
			const std::size_t take = (data.size() - off < chunk) ? data.size() - off : chunk;
			sha256_update(c, data.data() + off, take);
			off += take;
			chunk = chunk * 2 + 5; // 1, 7, 19, 43, …
		}
		assert(sha256_final_hex(c) == once);
	}
}

static void test_is_sha256_hex() {
	assert(is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	assert(!is_sha256_hex(""));
	assert(!is_sha256_hex("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855")); // 대문자
	assert(!is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85"));  // 63자
	assert(!is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b8555")); // 65자
	assert(!is_sha256_hex("g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")); // hex 아님
}

int main() {
	test_parse_version();
	test_version_cmp();
	test_sha256_nist();
	test_sha256_padding_boundaries();
	test_is_sha256_hex();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
