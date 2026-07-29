/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/t && /tmp/t
#include "sherbet_update_core.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

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
	// ── 비ASCII: 베스트핏 매핑으로 되살아나는 경로 조작 ──────────────────────
	// 전각 마침표·슬래시 U+FF0E U+FF0E U+FF0F(．．／). 여기서 통과시키면 글루가 URL 을
	// wchar_t 로 바꿀 때 CP_ACP 의 "best-fit" 매핑이 이 세 글자를 ASCII "../" 로 접어,
	// **우리 검사를 통과한 뒤에** 경로 조작이 되살아난다. 이 리포의 관행
	// (sherbet_content.cpp 의 std::wstring(p.begin(), p.end()))이 정확히 그 변환이라
	// "Phase 3 이 CP_UTF8 을 쓸 것" 이라는 약속에 보안을 걸 수 없다.
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/"
		"\xEF\xBC\x8E\xEF\xBC\x8E\xEF\xBC\x8F" "attacker/evil.dll"));
	// 접두사 뒤 어디에 있든 거부다(파일명 자리도 포함)
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/"
		"\xEF\xBC\x8E\xEF\xBC\x8E\xEF\xBC\x8F" "evil.dll"));
	// UTF-8 한글 파일명 — 정당해 보이지만 거부한다. 우리 릴리스 자산 이름은
	// release.yml 이 ReShade64.dll / ReShade32.dll 로만 만들고, 태그는
	// sherbet-<x.y.z> 뿐이라 **비ASCII URL 을 생성할 경로 자체가 없다.**
	// 통과시킬 이유가 없는 문자군을 통과시키면 위의 전각 우회가 같이 열린다.
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/"
		"\xED\x95\x9C\xEA\xB8\x80.dll")); // "한글.dll"
	// 0x80 이상 단일 바이트(잘린 UTF-8·Latin-1)도 마찬가지
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a\x80.dll"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a\xFF.dll"));
	// 0x7f(DEL)은 제어문자로도, 비ASCII 경계로도 거부돼야 한다
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a\x7f.dll"));

	// 정상 URL 은 여전히 통과해야 한다(과잉 차단 회귀 방지)
	assert(url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll"));
	assert(url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-10.20.30/ReShade32.dll"));
	// 상한 경계 — 0x7e(~)는 ASCII 인쇄가능 범위의 마지막 문자다. 여기까지는 통과해야
	// '0x7f 이상 거부' 가 한 칸 밀려 정상 문자를 자르는 회귀를 잡아낸다.
	assert(url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/x/a~b.dll"));
}

// 실제 파일명과 복구안내 문구는 제품 헤더에만 있다(시뮬레이터가 사본을 들고 있으면
// Phase 3 이 다른 이름을 써도 아무도 못 잡는다).
static void test_file_names_and_note() {
	assert(std::string(suffix_new_part()) == ".sherbet-new.part");
	assert(std::string(suffix_new()) == ".sherbet-new");
	assert(std::string(suffix_bak()) == ".sherbet-bak");
	assert(std::string(suffix_bak_old()) == ".sherbet-bak.old");
	assert(std::string(suffix_failed()) == ".sherbet-failed");
	assert(std::string(marker_name()) == "sherbet.update");

	// ⚠️ .sherbet-bak.old 는 .sherbet-bak 의 **연장**이어야 한다(S8 이 밀어내는 대상).
	assert(std::string(suffix_bak_old()).compare(0, std::string(suffix_bak()).size(), suffix_bak()) == 0);
	// .part 는 .sherbet-new 의 연장 — S7 의 rename 이 접미사 한 조각만 떼는 형태다.
	assert(std::string(suffix_new_part()).compare(0, std::string(suffix_new()).size(), suffix_new()) == 0);

	// 복구안내 파일명은 UTF-8 이고(비ASCII), 깨지면 고객에게 남는 유일한 단서가 무너진다.
	const std::string nm = recovery_note_name();
	assert(nm == "Sherbet-\xEB\xB3\xB5\xEA\xB5\xAC\xEC\x95\x88\xEB\x82\xB4.txt");
	assert(nm.size() > 12 && nm.compare(nm.size() - 4, 4, ".txt") == 0);

	// 안내문에는 **두 이름이 모두** 들어가야 한다. 부분문자열 검사로는 이 결함이 안 잡힌다:
	// "dxgi.dll.sherbet-bak" 안에 "dxgi.dll" 이 들어 있기 때문이다.
	const std::string self = "d3d11.dll";
	const std::string bak = self + suffix_bak();
	const std::string note = make_recovery_note(self, bak);
	assert(note.find(bak) != std::string::npos);
	// self 가 '더 긴 이름의 앞토막이 아닌' 형태로 최소 한 번은 나와야 한다
	bool standalone = false;
	for (std::size_t i = note.find(self); i != std::string::npos; i = note.find(self, i + 1)) {
		const std::size_t end = i + self.size();
		if (end >= note.size() || note[end] == ' ') { standalone = true; break; }
	}
	assert(standalone);
	// 문구가 통째로 비거나 파일명만 나열되면 고객이 무엇을 할지 알 수 없다
	assert(note.size() > self.size() + bak.size() + 10);
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

// ── 부팅 마커 ───────────────────────────────────────────────────────────────
static void test_marker_roundtrip() {
	boot_marker m;
	m.state = "pending"; m.version = "1.4.0"; m.prev = "1.3.0";
	m.bak = "dxgi.dll.sherbet-bak"; m.exe = "FiveM_b3095_GTAProcess.exe";
	m.sha = "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80";
	m.bad_ver = "1.3.9";
	m.bad_sha = "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
	m.tries = 1;

	boot_marker back;
	assert(parse_marker(serialize_marker(m), back));
	assert(back.state == m.state && back.version == m.version && back.prev == m.prev);
	assert(back.bak == m.bak && back.exe == m.exe && back.sha == m.sha && back.tries == 1);
	// bad_ver/bad_sha 는 블랙리스트의 두 반쪽이다. 한쪽만 살아 돌아오면 should_offer 가
	// 이미 브릭시킨 빌드를 다시 제안한다.
	assert(back.bad_ver == m.bad_ver && back.bad_sha == m.bad_sha);
	assert(back.unknown.empty());
	assert(serialize_marker(back) == serialize_marker(m));

	// tries=0 도 반드시 적어야 한다 — 생략하면 다음 writer 가 '필드 없음' 으로 읽는다.
	boot_marker zero; zero.state = "pending";
	assert(serialize_marker(zero).find("tries=0") != std::string::npos);
	boot_marker zb;
	assert(parse_marker(serialize_marker(zero), zb) && zb.tries == 0);
}

static void test_marker_preserves_unknown_keys() {
	// writer 가 셋(교체 워커 / 렌더 스레드 / 다른 프로세스의 attach)이라 모르는 키를
	// 지우면 서로의 필드가 조용히 날아간다. 보존은 선택이 아니라 정합성 요구다.
	const std::string text =
		"state=pending\nversion=1.4.0\nfuture_field=hello\ntries=1\nanother=42\n";
	boot_marker m;
	assert(parse_marker(text, m));
	assert(m.unknown.size() == 2);
	assert(m.unknown[0] == "future_field=hello" && m.unknown[1] == "another=42");

	const std::string out = serialize_marker(m);
	assert(out.find("future_field=hello") != std::string::npos);
	assert(out.find("another=42") != std::string::npos);
	assert(out.find("state=pending") != std::string::npos);
	assert(out.find("version=1.4.0") != std::string::npos);
	assert(out.find("tries=1") != std::string::npos);

	// 순서가 안정적이어야 한다: 아는 키 뒤에, 원래 등장 순서대로.
	// 흔들리면 세 writer 가 서로의 파일을 끝없이 다시 쓰고 diff 가 무의미해진다.
	assert(out.find("future_field=hello") > out.find("tries="));
	assert(out.find("future_field=hello") < out.find("another=42"));

	// serialize → parse → serialize 는 고정점이어야 한다(2회, 3회차 모두).
	boot_marker again;
	assert(parse_marker(out, again));
	assert(again.unknown == m.unknown);
	assert(serialize_marker(again) == out);
	boot_marker third;
	assert(parse_marker(serialize_marker(again), third));
	assert(serialize_marker(third) == out);
}

static void test_marker_tolerates_garbage() {
	boot_marker m;
	assert(parse_marker("state=pending\n\n= \nno-equals-here\ntries=notanumber\n", m));
	assert(m.state == "pending");
	assert(m.tries == 0); // 비정수는 0 으로
	// 깨진 줄도 버리지 않는다 — 우리가 모르는 writer 의 필드일 수 있다.
	assert(m.unknown.size() == 2);

	boot_marker empty;
	assert(!parse_marker("", empty));           // state 없으면 실패
	assert(!parse_marker("tries=3\n", empty));  // state 없으면 실패

	// 실패했을 때 out 을 오염시키지 않아야 한다 — 호출자는 실패 시 기존 마커를 그대로 쓴다.
	boot_marker keep; keep.state = "rolledback"; keep.tries = 9;
	assert(!parse_marker("tries=3\n", keep));
	assert(keep.state == "rolledback" && keep.tries == 9);

	// CRLF(윈도우 메모장) 와 마지막 개행 없음
	boot_marker crlf;
	assert(parse_marker("state=pending\r\nversion=1.4.0\r\ntries=1", crlf));
	assert(crlf.state == "pending" && crlf.version == "1.4.0" && crlf.tries == 1);

	// 손상된 tries — int 범위를 넘는 값이 감겨 음수가 되면 롤백이 영원히 미뤄진다.
	// 마커는 디스크에서 오고 세 프로세스가 쓴다. 위로 클램프해야 안전한 방향으로 틀린다.
	boot_marker huge;
	assert(parse_marker("state=pending\ntries=18446744073709551615\n", huge));
	assert(huge.tries > 0);
	assert(decide_boot(huge) == boot_action::rollback);
	boot_marker huge2;
	assert(parse_marker("state=pending\ntries=4294967296\n", huge2));
	assert(huge2.tries > 0);
	assert(decide_boot(huge2) == boot_action::rollback);
}

static void test_marker_value_cannot_forge_fields() {
	// exe 는 g_target_executable_path.filename() — 파일시스템에서 온 임의 문자열이다.
	// 줄 단위 포맷이라 값 안의 개행은 그대로 새 key=value 줄이 된다. 소독하지 않으면
	// 아래 마커가 라운드트립 후 state=rolledback 이 되어 rollback → none 으로 뒤집힌다.
	{
		boot_marker m;
		m.state = "pending"; m.tries = 1;
		m.exe = "evil\nstate=rolledback";
		const std::string out = serialize_marker(m);

		boot_marker back;
		assert(parse_marker(out, back));
		assert(back.state == "pending");                    // 위조 실패
		assert(back.tries == 1);
		assert(decide_boot(back) == boot_action::rollback);  // 판정이 뒤집히지 않는다
		assert(back.exe == "evil state=rolledback");         // 소독된 값이 그대로 라운드트립
		assert(serialize_marker(back) == out);               // 고정점
		assert(back.unknown.empty());                        // 위조 줄이 생기지도 않았다
	}
	// CR/CRLF 도 같다. tries 위조도 막힌다.
	{
		boot_marker m;
		m.state = "pending"; m.tries = 1; m.bak = "x\r\ntries=0";
		boot_marker back;
		assert(parse_marker(serialize_marker(m), back));
		assert(back.tries == 1 && back.bak == "x  tries=0");
		assert(decide_boot(back) == boot_action::rollback);
	}
	// 값이 소독 후 비면 그 줄은 아예 쓰지 않는다 — "prev=" 를 남기면 고정점이 깨진다.
	{
		boot_marker m;
		m.state = "pending"; m.prev = "\n\r";
		const std::string out = serialize_marker(m);
		assert(out.find("prev=") == std::string::npos);
		boot_marker back;
		assert(parse_marker(out, back) && back.prev.empty());
		assert(serialize_marker(back) == out);
	}
	// unknown 줄도 소독한다 — 손으로 채워 넣는 호출자가 생기면 위조 경로가 다시 열린다.
	{
		boot_marker m;
		m.state = "pending"; m.tries = 1;
		m.unknown.push_back("future=a\nstate=rolledback");
		boot_marker back;
		assert(parse_marker(serialize_marker(m), back));
		assert(back.state == "pending");
		assert(decide_boot(back) == boot_action::rollback);
	}
}

static void test_marker_whitespace() {
	// 뒤쪽 탭 — parse 는 true 를 내는데(호출자는 마커가 유효하다고 믿는다)
	// state 가 "pending\t" 라 decide_boot 만 조용히 none 이 되면 롤백이 사라진다.
	boot_marker tab;
	assert(parse_marker("state=pending\t\ntries=1\n", tab));
	assert(tab.state == "pending");
	assert(decide_boot(tab) == boot_action::rollback);

	// 줄 앞 공백/탭 — 아는 키가 unknown 으로 밀려 '마커 없음' 이 되면 안 된다
	boot_marker lead;
	assert(parse_marker("  state=pending\n\tversion=1.4.0\n \ttries=1\n", lead));
	assert(lead.state == "pending" && lead.version == "1.4.0" && lead.tries == 1);
	assert(lead.unknown.empty());
	assert(decide_boot(lead) == boot_action::rollback);

	// '=' 앞 공백 — 키도 다듬는다
	boot_marker k;
	assert(parse_marker("state =pending\ntries\t=1\n", k));
	assert(k.state == "pending" && k.tries == 1);
	assert(k.unknown.empty());
	assert(decide_boot(k) == boot_action::rollback);

	// 값 '안' 의 공백은 건드리지 않는다 — 파일명에 정당하게 들어간다
	boot_marker sp;
	assert(parse_marker("state=pending\nexe=Grand Theft Auto V.exe\n", sp));
	assert(sp.exe == "Grand Theft Auto V.exe");
	assert(serialize_marker(sp).find("exe=Grand Theft Auto V.exe\n") != std::string::npos);

	// 값 앞 공백은 값의 일부로 남긴다(파일명 일부일 수 있다) — 그리고 라운드트립한다
	boot_marker lv;
	assert(parse_marker("state=pending\nexe= a.exe\n", lv));
	assert(lv.exe == " a.exe");
	const std::string lv_out = serialize_marker(lv);
	assert(lv_out.find("exe= a.exe\n") != std::string::npos);
	boot_marker lv2;
	assert(parse_marker(lv_out, lv2) && lv2.exe == " a.exe");
	assert(serialize_marker(lv2) == lv_out);

	// 값 끝 공백은 잘린다 — serialize 쪽도 같이 잘라야 고정점이 유지된다
	boot_marker tv;
	tv.state = "pending"; tv.exe = "a.exe  ";
	const std::string tv_out = serialize_marker(tv);
	assert(tv_out.find("exe=a.exe\n") != std::string::npos);
	boot_marker tv2;
	assert(parse_marker(tv_out, tv2) && tv2.exe == "a.exe");
	assert(serialize_marker(tv2) == tv_out);

	// 공백뿐인 줄은 빈 줄과 같이 취급(unknown 을 쓰레기로 채우지 않는다)
	boot_marker ws;
	assert(parse_marker("state=pending\n   \n\t\n", ws));
	assert(ws.unknown.empty());
}

static void test_marker_duplicate_keys() {
	// 정책: 중복 키는 마지막 값이 이긴다(모든 키 동일). 단 tries 는 정수가 아닌 줄을
	// '무시' 한다 — 손상된 한 줄이 앞서 읽은 진짜 카운트를 0 으로 지우면 롤백이 사라진다.
	{
		boot_marker a;
		assert(parse_marker("state=pending\ntries=1\ntries=notanumber\n", a));
		assert(a.tries == 1);                                // 지워지지 않는다
		assert(decide_boot(a) == boot_action::rollback);
	}
	// 유효한 값끼리는 마지막이 이긴다(양방향으로 못 박는다)
	{
		boot_marker b;
		assert(parse_marker("state=pending\ntries=0\ntries=1\n", b));
		assert(b.tries == 1);
		boot_marker c;
		assert(parse_marker("state=pending\ntries=1\ntries=0\n", c));
		assert(c.tries == 0);
	}
	// 유효한 tries 가 하나도 없으면 0 (브리프에 적힌 동작 유지)
	{
		boot_marker d;
		assert(parse_marker("state=pending\ntries=notanumber\n", d));
		assert(d.tries == 0);
		assert(decide_boot(d) == boot_action::count);
	}
	// 문자열 키도 마지막이 이긴다 — 정책이 한 가지여야 읽는 사람이 헷갈리지 않는다
	{
		boot_marker e;
		assert(parse_marker("state=pending\nstate=rolledback\n", e));
		assert(e.state == "rolledback");
		boot_marker f;
		assert(parse_marker("state=rolledback\nstate=pending\n", f));
		assert(f.state == "pending");
		boot_marker g;
		assert(parse_marker("state=pending\nexe=a.exe\nexe=b.exe\n", g));
		assert(g.exe == "b.exe");
	}
}

static void test_decide_boot() {
	// ── 의미(읽는 사람이 오해할 수 없도록 못 박는다) ──────────────────────────
	// m.tries = 마커 파일에 적혀 있는 '지금까지 관찰된 부팅 실패 횟수'.
	// 이 함수에는 **디스크에서 읽은 그대로의 마커**를 넘긴다. 스펙 R6 는 판정 전에
	// tries+1 을 디스크에 먼저 쓰라고 하지만(쓰기 전 크래시하면 카운트가 안 늘어 미탐),
	// 그것은 파일 내용에 대한 요구일 뿐이고 인자까지 증가시키라는 뜻이 아니다.
	// 판정: 이번 부팅이 max_tries 번째 실패가 되는 순간 롤백 → tries + 1 >= max_tries.
	// 스펙 §5.4 의 '2회 연속 부팅 실패' = max_tries 2 = tries 가 1 인 마커로 부팅한 것.
	boot_marker none;
	assert(decide_boot(none) == boot_action::none); // 마커 없음(state 비어있음)

	// ── 계약 위반의 결과를 실행 가능한 형태로 박아 둔다 ────────────────────────
	// R6 대로 디스크에 tries+1 을 쓰면서 구조체의 tries 까지 증가시켜 넘기면
	// 임계값이 절반이 된다. 아래 두 줄이 그 차이다 — 경고문이 아니라 결과다.
	{
		boot_marker disk;                                  // 디스크에서 막 읽은 마커: 첫 실패
		disk.state = "pending"; disk.tries = 0;
		assert(decide_boot(disk) == boot_action::count);   // 올바른 사용 — 세기만 한다

		boot_marker misused = disk;
		++misused.tries;                                   // ← 금지된 사용법(R6 오독)
		// 첫 번째 부팅 실패가 곧바로 롤백이 된다. 알트탭·GPU 드라이버 결함 한 번에
		// 멀쩡한 설치가 되돌아간다는 뜻이다.
		assert(decide_boot(misused) == boot_action::rollback);
		assert(decide_boot(misused) != decide_boot(disk)); // 두 사용법은 다른 답을 낸다
		// 오용은 임계값을 정확히 절반으로 만든다: max_tries=3 에서도 한 부팅 빠르다.
		assert(decide_boot(disk, 3) == boot_action::count);
		assert(decide_boot(misused, 3) == boot_action::count);
		boot_marker disk2; disk2.state = "pending"; disk2.tries = 1;
		boot_marker misused2 = disk2; ++misused2.tries;
		assert(decide_boot(disk2, 3) == boot_action::count);
		assert(decide_boot(misused2, 3) == boot_action::rollback);
	}

	// 기본 인자가 2 여야 한다. 3 이면 롤백이 한 부팅 늦어진다.
	{
		boot_marker d; d.state = "pending"; d.tries = 1;
		assert(decide_boot(d) == boot_action::rollback);
		assert(decide_boot(d) == decide_boot(d, 2));
		assert(decide_boot(d, 3) == boot_action::count);
	}

	// pending 진리표 — tries × max_tries 전부.
	struct row { int tries; int max_tries; boot_action want; };
	static const row pending_table[] = {
		{ 0, 2, boot_action::count    }, // 0+1 = 1 < 2 → 첫 실패는 세기만 한다
		{ 1, 2, boot_action::rollback }, // 1+1 = 2 >= 2 → 두 번째 실패에서 되돌린다
		{ 2, 2, boot_action::rollback },
		{ 5, 2, boot_action::rollback },
		{ 0, 3, boot_action::count    },
		{ 1, 3, boot_action::count    }, // 1+1 = 2 < 3
		{ 2, 3, boot_action::rollback }, // 2+1 = 3 >= 3
		{ 5, 3, boot_action::rollback },
	};
	for (const row &r : pending_table) {
		boot_marker p; p.state = "pending"; p.tries = r.tries;
		assert(decide_boot(p, r.max_tries) == r.want);
	}

	// pending 이 아닌 상태는 tries·max_tries 와 무관하게 전부 none.
	//  - rolledback / rollback_failed: 블랙리스트 상태. 여기서 또 세면 이미 되돌린
	//    사용자를 다시 되돌려 되돌리기 루프가 된다.
	//  - swapping: 교체 중단은 startup_repair 가 다룬다. 여기서도 손대면 두 주체가
	//    같은 파일을 동시에 만진다.
	//  - 빈 문자열: 마커 없음.
	//  - 모르는 문자열(대소문자 포함): 미래의 writer 가 쓴 상태를 롤백으로 오해하면 안 된다.
	static const char *const inert[] = {
		"", "swapping", "rolledback", "rollback_failed", "Pending", "sherbet-from-the-future"
	};
	const int tries_v[] = { 0, 1, 2, 5 };
	const int max_v[] = { 2, 3 };
	for (const char *s : inert)
		for (int t : tries_v)
			for (int mx : max_v) {
				boot_marker q; q.state = s; q.tries = t;
				assert(decide_boot(q, mx) == boot_action::none);
			}

	// INT_MAX — int 로 더하면 UB(부호 오버플로). 마커는 디스크에서 온다.
	{
		boot_marker p; p.state = "pending"; p.tries = 2147483647;
		assert(decide_boot(p) == boot_action::rollback);
	}
}

// ── 제안·강제 판정 ──────────────────────────────────────────────────────────
static void test_should_offer() {
	const info u = parse_manifest(kGoodManifest, "x64"); // version 1.4.0
	assert(u.ok);

	assert(should_offer("1.3.0", u, "", ""));   // 신버전 → 제안
	assert(!should_offer("1.4.0", u, "", ""));  // 같음 → 안 함
	assert(!should_offer("1.5.0", u, "", ""));  // 내가 더 최신 → 안 함(allow_downgrade=false)

	// 문자열 비교였다면 틀리는 케이스
	assert(should_offer("1.9.0", parse_manifest(manifest_with("version", "\"1.10.0\""), "x64"), "", ""));

	// 블랙리스트: (버전, sha) 쌍이 모두 일치할 때만 차단
	assert(!should_offer("1.3.0", u, "1.4.0", u.sha256));
	// 같은 번호로 고쳐 재배포하면 sha 가 달라 다시 제안되어야 한다
	assert(should_offer("1.3.0", u, "1.4.0",
		"0000000000000000000000000000000000000000000000000000000000000000"));
	// 버전만 다르면 차단하지 않는다
	assert(should_offer("1.3.0", u, "1.2.0", u.sha256));
	// 둘 다 비어 있으면 블랙리스트 자체가 없는 것 — 아무것도 막지 않는다
	assert(should_offer("1.3.0", u, "", ""));

	// 반쪽만 기록된 블랙리스트는 와일드카드로 본다(브리프 대비 강화 — 보고서 참고).
	// 롤백 코드가 한쪽만 적고 죽었다면 '어떤 빌드인지 확실치 않으나 나빴다' 는 뜻이고,
	// 그때 다시 제안해 재브릭시키는 쪽이 업데이트 한 번 놓치는 것보다 훨씬 나쁘다.
	assert(!should_offer("1.3.0", u, "1.4.0", ""));      // 버전만 기록됨 → 차단
	assert(!should_offer("1.3.0", u, "", u.sha256));     // sha 만 기록됨 → 차단(정확한 지목)
	assert(should_offer("1.3.0", u, "1.2.0", ""));       // 다른 버전 → 통과
	assert(should_offer("1.3.0", u, "",
		"0000000000000000000000000000000000000000000000000000000000000000")); // 다른 sha → 통과

	// 현재 버전이 파싱 불가면 제안하지 않는다(안전 측)
	assert(!should_offer("garbage", u, "", ""));
	assert(!should_offer("", u, "", ""));

	// ok 가 아닌 매니페스트는 제안하지 않는다
	info bad;
	assert(!should_offer("1.0.0", bad, "", ""));

	// 심층 방어: 파서가 이미 보장하지만, ok=true 인데 version 이 깨진 info 를 손으로 만들어도
	// 제안하지 않아야 한다(다른 경로가 info 를 조립하게 되는 날을 위해).
	{
		info hand;
		hand.ok = true; hand.version = "not-a-version";
		hand.sha256 = u.sha256; hand.url = u.url; hand.size = u.size;
		assert(!should_offer("1.3.0", hand, "", ""));
	}
}

static void test_should_offer_downgrade() {
	const info d = parse_manifest(manifest_with("allow_downgrade", "true"), "x64");
	assert(d.ok && d.allow_downgrade);
	// 킬스위치: 이미 더 최신을 쓰는 사람도 권장 버전으로 되돌리도록 제안한다
	assert(should_offer("1.5.0", d, "", ""));
	assert(!should_offer("1.4.0", d, "", "")); // 같으면 여전히 안 함
	assert(should_offer("1.3.0", d, "", ""));  // 정방향도 그대로 제안
	// 킬스위치라고 블랙리스트를 무시하면 안 된다
	assert(!should_offer("1.5.0", d, "1.4.0", d.sha256));

	// allow_downgrade=false 면 강등은 절대 제안하지 않는다(위 케이스의 대조군)
	const info n = parse_manifest(kGoodManifest, "x64");
	assert(n.ok && !n.allow_downgrade);
	assert(!should_offer("1.5.0", n, "", ""));
}

static void test_is_mandatory() {
	const info u = parse_manifest(kGoodManifest, "x64"); // min_version 1.0.0
	assert(!is_mandatory("1.3.0", u));  // 1.3.0 >= 1.0.0
	assert(!is_mandatory("1.0.0", u));  // 경계: 같으면 필수 아님
	assert(is_mandatory("0.9.9", u));   // 미만이면 필수

	{
		const info hi = parse_manifest(manifest_with("min_version", "\"1.3.5\""), "x64");
		assert(hi.ok);
		assert(is_mandatory("1.3.0", hi));   // 1.3.0 < 1.3.5
		assert(!is_mandatory("1.3.5", hi));  // 경계
		assert(!is_mandatory("1.3.6", hi));
		// 문자열 비교였다면 틀리는 케이스: "1.10.0" < "1.3.5" 가 되어 전 고객이 강제 업데이트에 걸린다
		assert(!is_mandatory("1.10.0", hi));
	}

	// min_version 없음 → 절대 필수 아님. 오타 하나로 전 고객 UI 를 잠그지 않는다(스펙 §3.5).
	{
		const info nomin = parse_manifest(manifest_without("min_version"), "x64");
		assert(nomin.ok && nomin.min_version.empty());
		assert(!is_mandatory("0.1.0", nomin));
		assert(!is_mandatory("0.0.0", nomin));
	}

	// 현재 버전 파싱 불가 → false
	assert(!is_mandatory("garbage", u));
	assert(!is_mandatory("", u));
	// ok 아님 → false
	info bad;
	assert(!is_mandatory("1.0.0", bad));

	// 심층 방어: parse_manifest 가 이미 깨진 min_version 을 통째로 거부하므로 아래 info 는
	// 정상 경로로는 만들어질 수 없다. 그래도 false 여야 한다.
	{
		info hand;
		hand.ok = true; hand.version = "1.4.0"; hand.min_version = "9.9.x";
		assert(!is_mandatory("1.0.0", hand));
	}
}

// 거부된 매니페스트는 판정을 아무것도 촉발하지 않아야 한다.
// parse_manifest 는 모든 거부 경로에서 완전히 빈 info() 를 돌려주므로 필드가 비어 있지만,
// 그 '비어 있음' 에 기대는 판정은 새 필드가 생기는 순간 무너진다. ok 가드가 유일한 방어다.
static void test_rejected_manifest_decides_nothing() {
	const char *const bodies[] = { "", "not json at all", kShadowManifest, kDupSizeManifest };
	for (const char *b : bodies) {
		assert_rejected(b);
		const info r = parse_manifest(b, "x64");
		assert(!r.ok);
		assert(!should_offer("1.3.0", r, "", ""));
		assert(!should_offer("0.0.1", r, "", ""));
		assert(!is_mandatory("1.3.0", r));
		assert(!is_mandatory("0.0.1", r));
	}
	// arch 불일치도 마찬가지 — x86 빌드가 x64 매니페스트로 무언가를 하면 브릭이다
	const info wrong_arch = parse_manifest(kGoodManifest, "x86");
	assert(!wrong_arch.ok);
	assert(!should_offer("1.3.0", wrong_arch, "", ""));
	assert(!is_mandatory("1.3.0", wrong_arch));

	// ⚠️ 위 단언들만으로는 ok 가드를 지운 구현도 통과한다 — parse_manifest 가 거부 시
	// 필드를 전부 비우기 때문에 parse_version(u.version) 이 대신 실패해 주기 때문이다.
	// 그래서 'ok 는 false 인데 필드는 멀쩡한' info 를 손으로 만들어 가드 자체를 못 박는다.
	// 실제로 생길 수 있는 상태다: 지난 폴링의 성공 결과를 들고 있다가 이번 폴링 실패로
	// ok 만 내리는 캐시(Task 8·9)가 정확히 이 모양이 된다.
	{
		info stale;
		stale.ok = false;                 // ← 이것만이 유효성의 근거다
		stale.version = "1.4.0";
		stale.min_version = "9.9.9";
		stale.sha256 = "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80";
		stale.url = "https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll";
		stale.size = 4312576ULL;
		stale.allow_downgrade = true;
		assert(!should_offer("1.3.0", stale, "", ""));  // 신버전처럼 보여도 제안 금지
		assert(!should_offer("1.5.0", stale, "", ""));  // 강등 경로도 금지
		assert(!is_mandatory("1.3.0", stale));          // 1.3.0 < 9.9.9 라도 강제 금지
	}
}

int main() {
	test_parse_version();
	test_version_cmp();
	test_sha256_nist();
	test_sha256_padding_boundaries();
	test_is_sha256_hex();
	test_url_allowed();
	test_file_names_and_note();
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
	test_marker_roundtrip();
	test_marker_preserves_unknown_keys();
	test_marker_tolerates_garbage();
	test_marker_value_cannot_forge_fields();
	test_marker_whitespace();
	test_marker_duplicate_keys();
	test_decide_boot();
	test_should_offer();
	test_should_offer_downgrade();
	test_is_mandatory();
	test_rejected_manifest_decides_nothing();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
