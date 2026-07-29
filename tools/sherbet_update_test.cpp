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

static void test_url_allowed() {
	const std::string good =
		"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll";
	assert(url_allowed(good));

	// 다른 소유자·리포 — 호스트만 검사했다면 통과해버리는 케이스
	assert(!url_allowed("https://github.com/attacker/evil/releases/download/x/ReShade64.dll"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/other/releases/download/x/a.dll"));
	// 유사 호스트
	assert(!url_allowed("https://github.com.evil.kr/Jeong-Ryeol/reshade/releases/download/x/a.dll"));
	// 스킴
	assert(!url_allowed("http://github.com/Jeong-Ryeol/reshade/releases/download/x/a.dll"));
	// 경로 조작
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/../../../x.dll"));
	// userinfo 를 이용한 호스트 위장
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/@evil.kr/a.dll"));
	// 빈 문자열·접두사만
	assert(!url_allowed(""));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/"));
	// 퍼센트 인코딩 경로 조작 — 접두사 4조각을 되감는 실제 페이로드
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/"
		"%2e%2e/%2e%2e/%2e%2e/%2e%2e/attacker/evil-repo/releases/download/x/evil.dll"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/%2E%2E/evil.dll"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x%2fy/evil.dll"));
	// 제어문자 — CRLF 헤더 주입
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a.dll\r\nHost: evil.kr"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a\ndll"));
	// 길이는 리터럴 실제 크기(66)여야 한다. 68 을 주면 리터럴 밖 2바이트를 읽어(ASan
	// global-buffer-overflow) 세니타이저 빌드가 여기서 죽고 뒤 테스트가 아예 안 돈다.
	assert(!url_allowed(std::string("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a\0b.dll", 66)));
	// 정상 URL 은 여전히 통과해야 한다(과잉 차단 회귀 방지)
	assert(url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll"));
	assert(url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-10.20.30/ReShade32.dll"));
}

static void test_split_https_url() {
	std::string host, path;
	assert(split_https_url(
		"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
		host, path));
	assert(host == "github.com");
	assert(path == "/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll");

	assert(!split_https_url("http://github.com/a", host, path)); // https 아님
	assert(!split_https_url("https://github.com", host, path));  // 경로 없음
	assert(!split_https_url("", host, path));
}

// 최소한의 가짜 PE 헤더를 만든다. e_lfanew 는 0x80 에 둔다.
static std::string make_pe(unsigned short machine, unsigned short characteristics,
                           unsigned int e_lfanew = 0x80, bool mz = true, bool sig = true) {
	std::string b(0x200, '\0');
	if (mz) { b[0] = 'M'; b[1] = 'Z'; }
	b[0x3c] = static_cast<char>(e_lfanew & 0xff);
	b[0x3d] = static_cast<char>((e_lfanew >> 8) & 0xff);
	b[0x3e] = static_cast<char>((e_lfanew >> 16) & 0xff);
	b[0x3f] = static_cast<char>((e_lfanew >> 24) & 0xff);
	if (sig && e_lfanew + 24 <= b.size()) {
		b[e_lfanew] = 'P'; b[e_lfanew + 1] = 'E'; b[e_lfanew + 2] = '\0'; b[e_lfanew + 3] = '\0';
		b[e_lfanew + 4] = static_cast<char>(machine & 0xff);
		b[e_lfanew + 5] = static_cast<char>((machine >> 8) & 0xff);
		b[e_lfanew + 22] = static_cast<char>(characteristics & 0xff);
		b[e_lfanew + 23] = static_cast<char>((characteristics >> 8) & 0xff);
	}
	return b;
}

// make_pe 는 e_lfanew + 24 <= 버퍼크기 일 때만 PE 서명을 쓴다. 경계검사 자체를 시험하려면
// 버퍼 밖을 가리키는 e_lfanew 도 그대로 기록해야 하므로 여기서 직접 만든다.
// write_nt 를 켤 때는 호출자가 e_lfanew + 24 <= len 을 보장할 것.
static std::string raw_pe(std::size_t len, std::uint32_t e_lfanew, bool write_nt,
                          unsigned short machine = 0x8664, unsigned short characteristics = 0x2000) {
	std::string b(len, '\0');
	if (len >= 2) { b[0] = 'M'; b[1] = 'Z'; }
	if (len >= 0x40) {
		b[0x3c] = static_cast<char>(e_lfanew & 0xff);
		b[0x3d] = static_cast<char>((e_lfanew >> 8) & 0xff);
		b[0x3e] = static_cast<char>((e_lfanew >> 16) & 0xff);
		b[0x3f] = static_cast<char>((e_lfanew >> 24) & 0xff);
	}
	if (write_nt) {
		const std::size_t o = static_cast<std::size_t>(e_lfanew);
		b[o] = 'P'; b[o + 1] = 'E'; b[o + 2] = '\0'; b[o + 3] = '\0';
		b[o + 4] = static_cast<char>(machine & 0xff);
		b[o + 5] = static_cast<char>((machine >> 8) & 0xff);
		b[o + 22] = static_cast<char>(characteristics & 0xff);
		b[o + 23] = static_cast<char>((characteristics >> 8) & 0xff);
	}
	return b;
}

// ── 경계 술어 직접 테스트 ────────────────────────────────────────────────────
// 버퍼도 역참조도 없다. 아래 test_pe_check 의 랩어라운드 단언들은 "잘못된 판정이
// 크래시로 드러나기"를 기대하는데, 그건 운이다: 버퍼가 4GB 이상 예약 매핑 안에 있으면
// 폴트 없이 false 가 나오고, ReShade32(x86) 빌드에선 head + 0xFFFFFFFF 가 head - 1 로
// 감겨 항상 매핑된 주소라 100% 조용히 통과한다. 그래서 판정 자체를 여기서 직접 본다.
static void test_pe_bounds_ok() {
	using sherbet::update::detail::pe_bounds_ok;

	// 랩 구간 전체 [0xFFFFFFE8, 0xFFFFFFFF] × 대표 len 3종 — 예외 없이 전부 거부.
	// 0xFFFFFFE8 + 24 == 0 부터 0xFFFFFFFF + 24 == 23 까지가 32비트에서 감기는 구간이다.
	const std::size_t wrap_lens[] = { 64, 4096, 4194304 };
	for (std::size_t len : wrap_lens)
		for (std::uint64_t e = 0xFFFFFFE8ull; e <= 0xFFFFFFFFull; ++e)
			assert(!pe_bounds_ok(static_cast<std::uint32_t>(e), len));

	// 0x40 최소값 — DOS 헤더 안을 가리키는 e_lfanew 는 무효
	assert(!pe_bounds_ok(0, 4096));
	assert(!pe_bounds_ok(0x3f, 4096));
	assert(pe_bounds_ok(0x40, 4096));

	// 정확 경계: 허용되는 최대 e_lfanew 는 len - 24, len - 23 부터 거부
	const std::size_t exact_lens[] = { 88, 4096, 4194304 };
	for (std::size_t len : exact_lens) {
		const std::uint32_t max_ok = static_cast<std::uint32_t>(len - 24);
		assert(pe_bounds_ok(max_ok, len));
		assert(!pe_bounds_ok(max_ok + 1, len)); // len - 23
	}

	// 작은 len — e_lfanew >= 0x40 과 e_lfanew <= len - 24 를 동시에 만족할 수 없으므로
	// 전부 거부여야 한다(len < 88 이면 어떤 값도 통과 못 함). pe_check 의 len < 0x40
	// 가드와 어긋나지 않는다.
	const std::size_t small_lens[] = { 0, 23, 24, 63 };
	for (std::size_t len : small_lens) {
		assert(!pe_bounds_ok(0, len));
		assert(!pe_bounds_ok(0x40, len));
		assert(!pe_bounds_ok(static_cast<std::uint32_t>(len), len));
		assert(!pe_bounds_ok(0xFFFFFFFFu, len));
	}

	// 평범한 케이스
	assert(pe_bounds_ok(0x80, 4096));

	// ── 옛 결함 술어를 모델링해 이 테스트가 그것을 잡는다는 것을 증명한다 ──────
	// 옛 표현식은 `e_lfanew + 24 <= len` 을 32비트 폭에서 계산했다. 아래는 그 산술을
	// 그대로 재현한 것이다 — 역참조가 없으니 플랫폼·매핑 운에 기대지 않는다.
	// e_lfanew = 0xFFFFFFFF 에서 0xFFFFFFFF + 24 == 23 이라 옛 판정은 '경계 안'이라
	// 답하고 새 술어는 '밖'이라 답한다. 두 답이 다르다는 사실이 곧 위 랩 구간
	// 단언들이 옛 코드에서 반드시 실패한다는 증명이다.
	{
		const std::uint32_t e = 0xFFFFFFFFu;
		const std::size_t len = 64;
		const bool old_verdict = (e >= 0x40) && (static_cast<std::uint32_t>(e + 24u) <= len);
		const bool new_verdict = pe_bounds_ok(e, len);
		assert(old_verdict);            // 옛 코드는 이걸 '읽어도 안전' 으로 봤다
		assert(!new_verdict);           // 새 술어는 거부한다
		assert(old_verdict != new_verdict);
	}

	// ── pe_check 와 술어가 어긋나지 않는지 ────────────────────────────────────
	// 경계 안이면 유효한 x64 DLL 헤더를 실제로 채워 넣으므로 두 판정이 정확히 같아야 한다.
	// (랩 구간 값은 여기 넣지 않는다 — raw_pe 가 그 오프셋에 쓰려 들면 테스트 하네스가
	//  OOB 쓰기로 죽는다. 랩 구간의 pe_check 쪽 커버리지는 test_pe_check 에 있다.)
	{
		struct { std::uint32_t e; std::size_t len; } sample[] = {
			{ 0x40, 88 }, { 0x41, 88 }, { 0x3f, 4096 }, { 0, 4096 },
			{ 0x80, 4096 }, { 4072, 4096 }, { 4073, 4096 }, { 0x40, 63 }, { 0x40, 24 },
		};
		for (const auto &s : sample) {
			const bool ok = pe_bounds_ok(s.e, s.len);
			const std::string b = raw_pe(s.len, s.e, ok, 0x8664, 0x2000);
			assert(pe_check(reinterpret_cast<const unsigned char *>(b.data()), s.len, true) == ok);
		}
	}
}

static void test_pe_check() {
	const unsigned short DLL = 0x2000; // IMAGE_FILE_DLL
	const std::string x64 = make_pe(0x8664, DLL);
	const std::string x86 = make_pe(0x014c, DLL);

	assert(pe_check(reinterpret_cast<const unsigned char *>(x64.data()), x64.size(), true));
	assert(pe_check(reinterpret_cast<const unsigned char *>(x86.data()), x86.size(), false));

	// 아키텍처 교차 — 이 한 줄이 유일한 브릭 시나리오를 막는다
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x86.data()), x86.size(), true));
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x64.data()), x64.size(), false));

	// DLL 비트 없음(EXE)
	const std::string exe = make_pe(0x8664, 0x0002);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(exe.data()), exe.size(), true));

	// MZ 아님
	const std::string nomz = make_pe(0x8664, DLL, 0x80, false);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(nomz.data()), nomz.size(), true));

	// PE 서명 없음
	const std::string nosig = make_pe(0x8664, DLL, 0x80, true, false);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(nosig.data()), nosig.size(), true));

	// e_lfanew 가 버퍼 밖
	const std::string far = make_pe(0x8664, DLL, 0x00100000);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(far.data()), far.size(), true));

	// 버퍼가 너무 짧음
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x64.data()), 8, true));
	assert(!pe_check(nullptr, 0, true));

	// ── e_lfanew 32비트 랩어라운드 ────────────────────────────────────────
	// `e_lfanew + 24 > len` 을 uint32 로 계산하면 0xFFFFFFFF + 24 == 23 이 되어
	// 경계검사를 통과하고 head + 0xFFFFFFFF 를 역참조한다(SIGSEGV 또는 무관한 메모리 읽기).
	// 입력 4바이트는 전부 공격자가 고르는 값이다. 아래 세 개는 크래시 없이 false 여야 한다.
	{
		const std::string w32 = raw_pe(0x40, 0xFFFFFFFFu, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}
	{
		// 랩 구간의 하한 — 0xFFFFFFE8 + 24 == 0
		const std::string w32 = raw_pe(0x40, 0xFFFFFFE8u, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}
	{
		const std::string w32 = raw_pe(0x40, 0xFFFFFFF0u, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}

	// ── 경계 정확도: e_lfanew + 24 == len 은 통과, +1 이면 거부 ────────────
	// COFF 헤더에서 우리가 읽는 최대 오프셋이 +23 이므로 딱 24바이트면 충분하다.
	{
		const std::uint32_t e = 0x40;
		const std::string tight = raw_pe(e + 24, e, true, 0x8664, DLL); // len == e_lfanew + 24
		assert(pe_check(reinterpret_cast<const unsigned char *>(tight.data()), tight.size(), true));
		// 1바이트 부족(= e_lfanew + 24 == len + 1)이면 마지막 바이트를 못 읽으므로 거부
		assert(!pe_check(reinterpret_cast<const unsigned char *>(tight.data()), tight.size() - 1, true));
	}

	// len == 0 인데 포인터는 유효 — 널 검사와 별개로 길이만으로 걸러야 한다
	{
		const std::string any = raw_pe(0x40, 0x40, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(any.data()), 0, true));
	}
}

int main() {
	test_parse_version();
	test_version_cmp();
	test_sha256_nist();
	test_sha256_padding_boundaries();
	test_is_sha256_hex();
	test_url_allowed();
	test_split_https_url();
	test_pe_bounds_ok(); // pe_check 보다 먼저 — 경계 판정이 깨졌으면 역참조 전에 깨끗이 실패한다
	test_pe_check();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
