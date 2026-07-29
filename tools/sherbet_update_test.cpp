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

// ── 매니페스트 파싱 ─────────────────────────────────────────────────────────
static const char *kGoodManifest =
	"{\"schema\":\"1\",\"arch\":\"x64\",\"version\":\"1.4.0\",\"min_version\":\"1.0.0\","
	"\"allow_downgrade\":false,\"size\":\"4312576\","
	"\"sha256\":\"3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\","
	"\"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll\","
	"\"notice\":\"\",\"notes\":\"OSD 가로 배치 추가\\n로그인 만료 버그 수정\"}";

// F2 회귀 픽스처 ──────────────────────────────────────────────────────────────
// json_string 은 body 전체에서 `"key"` 의 첫 등장을 찾는다. notes 를 url 보다 앞에
// 직렬화하고 notes 값 안 따옴표가 이스케이프되지 않으면 가짜 필드가 진짜를 가린다.
// 가짜 값들은 전부 자체 정합적이다 — 가짜 url 은 피닝 접두사 안(같은 리포), 가짜 sha256 은
// 64hex, 가짜 size 는 1MiB~32MiB 안. 그래서 뒤따르는 검사가 전부 통과해버린다.
// 실측(수정 전 파서): ok=1, url=…sherbet-0.9.0-vuln…, size=2097152.
// 이 바디는 엄밀히는 유효한 JSON 이 아니다 — 그게 핵심이다. json.dumps 는 이런 걸 만들지
// 않지만 f-string 등으로 손수 조립한 라우트는 만든다. 매니페스트 라우트는 아직 없다.
static const char *kShadowManifest =
	"{\"schema\":\"1\",\"arch\":\"x64\","
	"\"notes\":\"changelog: \"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-0.9.0-vuln/ReShade64.dll\" "
	"\"sha256\":\"deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef\" "
	"\"size\":\"2097152\" end\","
	"\"version\":\"1.4.0\",\"min_version\":\"1.0.0\",\"allow_downgrade\":false,"
	"\"size\":\"4312576\","
	"\"sha256\":\"3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\","
	"\"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll\"}";

// 같은 노트를 json.dumps 처럼 제대로 이스케이프한 것. 닫는 따옴표가 `\"` 라 needle `"url"`
// 이 매치되지 않아 그림자 효과가 없다. 실측(수정 전 파서)으로도 진짜 url/size 가 나왔다.
// 따라서 이 바디는 수정 후에도 계속 '통과' 해야 한다 — 과잉 차단 회귀 방지용이다.
static const char *kEscapedNotesManifest =
	"{\"schema\":\"1\",\"arch\":\"x64\","
	"\"notes\":\"changelog: \\\"url\\\":\\\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-0.9.0-vuln/ReShade64.dll\\\" "
	"\\\"size\\\":\\\"2097152\\\" end\","
	"\"version\":\"1.4.0\",\"min_version\":\"1.0.0\",\"allow_downgrade\":false,"
	"\"size\":\"4312576\","
	"\"sha256\":\"3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\","
	"\"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll\"}";

// 중복 키 — 첫 값이 이긴다. 이스케이프와 무관하게 항상 재현된다.
// 실측(수정 전 파서): ok=1, size=1048576 (진짜 4312576 대신).
static const char *kDupSizeManifest =
	"{\"schema\":\"1\",\"arch\":\"x64\",\"size\":\"1048576\","
	"\"version\":\"1.4.0\",\"min_version\":\"1.0.0\",\"allow_downgrade\":false,"
	"\"size\":\"4312576\","
	"\"sha256\":\"3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\","
	"\"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll\"}";

// 픽스처를 손으로 센 오프셋 없이 변형한다. `"key":` 뒤 값의 [vs,ve) 를 스캔으로 찾으므로
// 길이를 잘못 세서 "다르게 망가진 픽스처가 우연히 거부되는" 초록 테스트가 나올 수 없다.
static bool field_span(const std::string &m, const char *key,
                       std::size_t &ks, std::size_t &vs, std::size_t &ve) {
	const std::string needle = std::string("\"") + key + "\":";
	ks = m.find(needle);
	if (ks == std::string::npos) return false;
	vs = ks + needle.size();
	std::size_t e = vs;
	if (e < m.size() && m[e] == '"') {          // 문자열 값 — 닫는 따옴표까지
		++e;
		while (e < m.size() && m[e] != '"') { if (m[e] == '\\' && e + 1 < m.size()) ++e; ++e; }
		if (e < m.size()) ++e;
	} else {                                     // 비문자열 값 — , 또는 } 까지
		while (e < m.size() && m[e] != ',' && m[e] != '}') ++e;
	}
	ve = e;
	return true;
}

// kGoodManifest 의 key 값을 raw 로 갈아끼운다(raw 는 따옴표까지 포함한 JSON 리터럴).
static std::string manifest_with(const char *key, const std::string &raw) {
	std::string m = kGoodManifest;
	std::size_t ks = 0, vs = 0, ve = 0;
	assert(field_span(m, key, ks, vs, ve));
	m.replace(vs, ve - vs, raw);
	// 의도한 문자열이 실제로 들어갔는지 그 자리에서 확인한다
	assert(m.find(std::string("\"") + key + "\":" + raw) != std::string::npos);
	return m;
}

// key 를 통째로 지운다(뒤따르는 콤마까지).
static std::string manifest_without(const char *key) {
	std::string m = kGoodManifest;
	std::size_t ks = 0, vs = 0, ve = 0;
	assert(field_span(m, key, ks, vs, ve));
	if (ve < m.size() && m[ve] == ',') ++ve;
	m.erase(ks, ve - ks);
	assert(m.find(std::string("\"") + key + "\"") == std::string::npos);
	return m;
}

// F3: 거부된 매니페스트는 절대 부분적으로 채워진 채 새어나가면 안 된다.
// ok 를 확인하지 않고 u.url / u.size 를 읽는 호출자(Task 7·8)가 검증 안 된 값을 쓰게 된다.
static void assert_rejected(const std::string &body, const char *arch = "x64") {
	const info u = parse_manifest(body, arch);
	assert(!u.ok);
	assert(u.version.empty() && u.min_version.empty());
	assert(u.url.empty() && u.sha256.empty());
	assert(u.notes.empty() && u.notice.empty());
	assert(u.size == 0 && !u.allow_downgrade);
}

static void test_parse_u64() {
	using sherbet::update::detail::parse_u64;
	unsigned long long v = 0;

	assert(parse_u64("0", v) && v == 0);
	assert(parse_u64("4312576", v) && v == 4312576ULL);
	assert(parse_u64("0004312576", v) && v == 4312576ULL);                        // 선행 0 허용, 값은 정확히
	assert(parse_u64("18446744073709551615", v) && v == 18446744073709551615ULL); // 2^64-1 은 통과

	// 부호·공백·내부 개행·다른 진법·지수표기·자릿수 초과·오버플로 — 전부 거부.
	// 실패했을 때 out 을 건드리지 않는 것도 함께 못 박는다.
	const char *bad[] = { "", "+1", "-1", " 1", "1 ", "1\n2", "0x10", "4.3e6",
	                      "123456789012345678901",   // 21자리
	                      "18446744073709551616" };  // 2^64 — 정확히 1 초과
	for (const char *b : bad) {
		unsigned long long keep = 0xA5A5A5A5A5A5A5A5ULL;
		assert(!parse_u64(b, keep));
		assert(keep == 0xA5A5A5A5A5A5A5A5ULL);
	}
}

static void test_has_unique_keys() {
	using sherbet::update::detail::has_unique_keys;

	assert(has_unique_keys(kGoodManifest));
	assert(has_unique_keys(""));

	// needle 이 앞 따옴표를 포함하므로 "version" 은 "min_version" 안에서 잡히지 않는다.
	// (잡힌다면 정상 매니페스트가 중복으로 오탐되어 전부 거부된다.) 추측하지 않고 못 박는다.
	assert(std::string(kGoodManifest).find("\"version\"") != std::string::npos);
	assert(std::string(kGoodManifest).find("\"min_version\"") != std::string::npos);
	assert(has_unique_keys("{\"version\":\"1.4.0\",\"min_version\":\"1.0.0\"}"));

	// 그림자 바디·중복 키 바디는 거부
	assert(!has_unique_keys(kShadowManifest));
	assert(!has_unique_keys(kDupSizeManifest));
	assert(!has_unique_keys("{\"url\":\"a\",\"url\":\"b\"}"));
	assert(!has_unique_keys("{\"schema\":\"1\",\"schema\":\"1\"}"));

	// 과잉 차단 방지 — notes 에 url 이라는 '단어'만 있거나 따옴표가 이스케이프된 경우는 통과
	assert(has_unique_keys("{\"schema\":\"1\",\"notes\":\"url 처리 개선\",\"url\":\"x\"}"));
	assert(has_unique_keys(kEscapedNotesManifest));
}

static void test_parse_manifest_good() {
	const info u = parse_manifest(kGoodManifest, "x64");
	assert(u.ok);
	assert(u.version == "1.4.0");
	assert(u.min_version == "1.0.0");
	assert(u.size == 4312576ULL);
	assert(u.sha256 == "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80");
	assert(!u.allow_downgrade);
	assert(u.notes.find('\n') != std::string::npos); // \n 이스케이프가 실제 개행으로 풀렸는지
	assert(u.notice.empty());
}

static void test_parse_manifest_rejects() {
	// 모든 거부는 assert_rejected 로 본다 — ok=false 뿐 아니라 객체가 완전히 비었는지까지(F3).

	// arch 불일치 — 서버가 x64 를 줬는데 우리가 x86 빌드
	assert_rejected(kGoodManifest, "x86");

	// 빈 바디 / 잡음 / 503 바디
	assert_rejected("");
	assert_rejected("not json at all");
	assert_rejected("{\"error\":\"upstream_unavailable\"}");

	// schema 불일치
	assert_rejected(manifest_with("schema", "\"2\""));
	assert_rejected(manifest_without("schema"));

	// arch 필드가 아예 없음 — 에코 검사를 통과시키면 안 된다
	assert_rejected(manifest_without("arch"));

	// sha256 이 64hex 아님
	assert_rejected(manifest_with("sha256",
		"\"ZZZZZZ4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\""));
	assert_rejected(manifest_with("sha256", "\"3f1c9a\""));                    // 짧음
	assert_rejected(manifest_without("sha256"));

	// url 이 피닝 접두사 밖
	assert_rejected(manifest_with("url",
		"\"https://github.com/attacker/evil00000/releases/download/sherbet-1.4.0/ReShade64.dll\""));
	assert_rejected(manifest_without("url"));

	// size 범위 밖 / 문자열이 아니라 숫자(json_string 이 못 읽으므로 거부)
	assert_rejected(manifest_with("size", "\"1000\""));
	assert_rejected(manifest_with("size", "4312576"));
	assert_rejected(manifest_without("size"));

	// version 파싱 불가 / 없음
	assert_rejected(manifest_with("version", "\"abc\""));
	assert_rejected(manifest_with("version", "1400"));
	assert_rejected(manifest_without("version"));
}

static void test_parse_manifest_key_confusion() {
	// "version" 이 "min_version" 에 오탐하면 안 된다.
	// json_string 은 따옴표를 포함한 needle 로 찾으므로 "version" 은 "min_version" 안의
	// version 에 걸리지 않는다(앞이 " 가 아니라 _ 이므로). 이 성질을 회귀로 못 박는다.
	const info u = parse_manifest(kGoodManifest, "x64");
	assert(u.version == "1.4.0" && u.min_version == "1.0.0");
}

static void test_parse_manifest_key_shadowing() {
	// F2: 다른 값 안에 숨은 가짜 키·중복 키가 진짜 다운로드 삼총사(url/sha256/size)를
	// 가리는 것을 막는다. 부분 채움 없이 통째로 거부되어야 한다.
	assert_rejected(kShadowManifest);
	assert_rejected(kDupSizeManifest);

	// 왜 이 방어가 필요한지 — 밑에 깔린 json_string 이 실제로 '첫 등장 우선' 임을 못 박는다.
	// (sherbet_auth_core.hpp 는 로그인 경로와 공유라 건드리지 않는다. 여기서는 그 동작을
	//  관찰만 하고, 방어는 parse_manifest 쪽 has_unique_keys 가 맡는다.)
	{
		std::string shadowed;
		assert(sherbet::auth::json_string(kShadowManifest, "url", shadowed));
		assert(shadowed.find("sherbet-0.9.0-vuln") != std::string::npos); // 가짜가 이긴다
		std::string dup;
		assert(sherbet::auth::json_string(kDupSizeManifest, "size", dup));
		assert(dup == "1048576");                                        // 앞선 값이 이긴다
	}

	// 제대로 이스케이프된 노트는 그림자가 되지 않으므로 계속 통과해야 한다(과잉 차단 회귀 방지).
	{
		const info u = parse_manifest(kEscapedNotesManifest, "x64");
		assert(u.ok);
		assert(u.url.find("sherbet-1.4.0") != std::string::npos);
		assert(u.size == 4312576ULL);
	}
}

static void test_parse_manifest_size_bounds() {
	// 양쪽 경계를 정확히 못 박는다 — 상수를 넓히는 뮤턴트(32MiB→32GiB)를 잡기 위함이다.
	assert(parse_manifest(manifest_with("size", "\"1048576\""), "x64").ok);    // 1MiB 딱
	assert_rejected(manifest_with("size", "\"1048575\""));                      // 1MiB - 1
	assert(parse_manifest(manifest_with("size", "\"33554432\""), "x64").ok);   // 32MiB 딱
	assert_rejected(manifest_with("size", "\"33554433\""));                     // 32MiB + 1

	// 경계값이 값으로도 정확히 실려야 한다
	assert(parse_manifest(manifest_with("size", "\"33554432\""), "x64").size == 33554432ULL);
	assert(parse_manifest(manifest_with("size", "\"1048576\""), "x64").size == 1048576ULL);

	assert_rejected(manifest_with("size", "\"0\""));
	assert_rejected(manifest_with("size", "\"34359738368\""));                  // 32GiB
	assert_rejected(manifest_with("size", "\"18446744073709551615\""));         // 2^64-1
}

static void test_parse_manifest_optional_fields() {
	// allow_downgrade 를 실제로 true 로 세워본다 — false 로 하드코딩한 뮤턴트를 잡는다.
	{
		const info u = parse_manifest(manifest_with("allow_downgrade", "true"), "x64");
		assert(u.ok && u.allow_downgrade);
	}
	{
		const info u = parse_manifest(manifest_with("allow_downgrade", "false"), "x64");
		assert(u.ok && !u.allow_downgrade);
	}
	{	// 키가 없으면 false (json_bool_or_null 이 -1). null 도 false.
		const info u = parse_manifest(manifest_without("allow_downgrade"), "x64");
		assert(u.ok && !u.allow_downgrade);
	}
	{
		const info u = parse_manifest(manifest_with("allow_downgrade", "null"), "x64");
		assert(u.ok && !u.allow_downgrade);
	}
	// notice 가 비어있지 않은 경우도 실제로 실려야 한다 — notice 읽기를 지운 뮤턴트를 잡는다.
	// (kGoodManifest 의 notice 는 빈 문자열이라 assert(u.notice.empty()) 만으로는 공허하다.)
	{
		const info u = parse_manifest(manifest_with("notice", "\"긴급 점검 안내\""), "x64");
		assert(u.ok && u.notice == "긴급 점검 안내");
	}
	{
		const info u = parse_manifest(manifest_with("notes", "\"첫 줄\\n둘째 줄\""), "x64");
		assert(u.ok && u.notes == "첫 줄\n둘째 줄");
	}
}

static void test_parse_manifest_min_version() {
	// 키가 없으면 통과 + 빈 문자열 (스펙 §3.5: is_mandatory 가 false 를 낸다)
	{
		const info u = parse_manifest(manifest_without("min_version"), "x64");
		assert(u.ok && u.min_version.empty());
	}
	// 키가 있는데 형식이 틀리면 version 과 동일하게 매니페스트 전체를 거부한다.
	// size 를 숫자로 내면 거부되는 것과 대칭이어야 하기 때문이다(스펙 §3.3 — 둘 다 문자열 타입).
	// 여기서 조용히 무시하면 str() 을 빠뜨린 라우트가 강제 업데이트를 소리 없이 무력화한다.
	assert_rejected(manifest_with("min_version", "\"9.9.x\""));
	assert_rejected(manifest_with("min_version", "\"\""));
	assert_rejected(manifest_with("min_version", "\"1.0\""));
	assert_rejected(manifest_with("min_version", "9990000"));   // 문자열이 아니라 숫자
	assert_rejected(manifest_with("min_version", "null"));

	// 형식이 맞으면 그대로 실린다. '형식은 맞지만 값이 과한' 9.9.9 는 계속 통과해야 한다 —
	// 그건 여기서 막을 문제가 아니고(스펙 §3.5 강제 업데이트 판단은 Task 7), 막으면 오히려
	// 정당한 강제 업데이트를 못 하게 된다.
	{
		const info u = parse_manifest(manifest_with("min_version", "\"9.9.9\""), "x64");
		assert(u.ok && u.min_version == "9.9.9");
	}
}

static void test_parse_manifest_truncated() {
	// 잘린 입력 전수. 진짜 범위 초과 검사는 이 루프를 -fsanitize=address 로 돌릴 때 나온다
	// (호스트 테스트를 ASan 으로도 빌드해 돌리는 것이 이 테스트의 실질적 검증 수단이다).
	// 그와 별개로 여기서는 "ok=true 면 내용도 반드시 일관적" 을 못 박는다 — ok 여부를
	// 통째로 무시하면 쓰레기를 ok 로 돌려주는 파서도 통과해버리기 때문이다.
	const std::string full = kGoodManifest;
	for (std::size_t n = 0; n < full.size(); ++n) {
		const info u = parse_manifest(full.substr(0, n), "x64");
		if (!u.ok) {                               // 잘린 입력이 거부되는 것은 정상
			// F3: 거부라면 부분적으로 채워진 채 새어나가면 안 된다.
			assert(u.version.empty() && u.url.empty() && u.sha256.empty());
			assert(u.min_version.empty() && u.size == 0);
			continue;
		}
		version3 tmp;
		assert(parse_version(u.version, tmp));     // ok 라면 버전은 반드시 파싱된다
		assert(is_sha256_hex(u.sha256));
		assert(url_allowed(u.url));
		assert(u.size >= (1ULL << 20) && u.size <= (32ULL << 20));
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
	test_parse_u64();
	test_has_unique_keys(); // parse_manifest 보다 먼저 — 키 유일성이 첫 관문이다
	test_parse_manifest_good();
	test_parse_manifest_rejects();
	test_parse_manifest_key_confusion();
	test_parse_manifest_key_shadowing();
	test_parse_manifest_size_bounds();
	test_parse_manifest_optional_fields();
	test_parse_manifest_min_version();
	test_parse_manifest_truncated();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
