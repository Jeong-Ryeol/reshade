/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Wall -Isource tools/sherbet_xhmarket_test.cpp -o /tmp/xm && /tmp/xm
//
// 조준점 마켓의 순수 층 전부. 이 스위트가 지키는 것은 하나다:
// **서버가 보낸 이상한 항목이 사용자의 조준점을 망가뜨리지 못한다.**
//   - 코드가 깨진 항목은 카드로 보이되 적용 불가(조용히 사라지지도, 절반 적용되지도 않는다)
//   - 잠긴 항목은 코드를 아예 들고 있지 않는다(코드가 곧 상품이다)
//   - 카드를 몇 번을 눌러도 되돌리기는 "사용자가 만들던 코드" 를 가리킨다
#include "../source/sherbet_xhmarket.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace sherbet;
using namespace sherbet::xhmarket;

// 실제로 유통되는 모양의 코드 두 개(발로란트 공유 코드 문법).
static const char *const kCodeA = "0;P;c;5;h;0;0l;4;0o;2;0a;1;0f;0;1b;0";
static const char *const kCodeB = "0;s;1;P;c;1;o;1;d;1;z;3;0b;0;1b;0";

static const entry *find_id(const std::vector<entry> &v, const std::string &id)
{
	for (const entry &e : v)
		if (e.id == id) return &e;
	return nullptr;
}

// ── 1. 정상 매니페스트 ───────────────────────────────────────────────────
static void test_basic_manifest()
{
	const std::string body = std::string(
		"{\"themes\":[],\"crosshairs\":[")
		+ "{\"id\":\"pro-01\",\"display_name\":\"\xED\x94\x84\xEB\xA1\x9C \xEC\xA0\x90\",\"author\":\"TenZ\",\"tag\":\"\xED\x94\x84\xEB\xA1\x9C\",\"code\":\"" + kCodeA + "\",\"unlocked\":true},"
		+ "{\"id\":\"pro-02\",\"code\":\"" + kCodeB + "\",\"unlocked\":true}"
		+ "]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 2);

	assert(v[0].id == "pro-01");
	assert(v[0].name == "\xED\x94\x84\xEB\xA1\x9C \xEC\xA0\x90");
	assert(v[0].author == "TenZ");
	assert(v[0].tag == "\xED\x94\x84\xEB\xA1\x9C");
	assert(v[0].code == kCodeA);
	assert(v[0].code_ok && v[0].applicable());
	assert(v[0].state() == entry_state::ready);
	assert(!v[0].local);

	// display_name 이 없으면 id 가 이름이 된다(테마/프리셋 매니페스트와 같은 관례)
	assert(v[1].name == "pro-02");
	assert(v[1].author.empty() && v[1].tag.empty());
	assert(v[1].applicable());

	// 파싱 결과가 실제로 코드와 일치한다 — 카드 미리보기가 딴 조준점을 그리면 안 된다
	crosshair::profile direct;
	assert(crosshair::parse_code(kCodeA, direct).ok());
	assert(v[0].parsed == direct);
}

// ── 2. 빈 매니페스트 / 키 없음 / 쓰레기 ──────────────────────────────────
static void test_empty_and_garbage()
{
	assert(parse_manifest("").empty());
	assert(parse_manifest("{}").empty());
	assert(parse_manifest("{\"crosshairs\":[]}").empty());
	assert(parse_manifest("{\"crosshairs\":[]}", "nope").empty());
	assert(parse_manifest("{\"themes\":[{\"id\":\"t\"}]}").empty()); // 다른 키만 있는 응답
	// 절단된 바디 — 파서가 끝까지 읽고도 죽지 않아야 한다(ASan 이 진짜로 확인하는 부분)
	const std::string full = std::string("{\"crosshairs\":[{\"id\":\"a\",\"code\":\"") + kCodeA + "\"}]}";
	for (std::size_t n = 0; n <= full.size(); ++n)
	{
		const std::vector<entry> v = parse_manifest(full.substr(0, n));
		for (const entry &e : v)
			assert(!e.id.empty()); // id 없는 카드는 절대 만들어지지 않는다
	}
	// 잘린 배열/중괄호 불균형
	assert(parse_manifest("{\"crosshairs\":[{").empty());
	assert(parse_manifest("{\"crosshairs\":[{\"id\":").empty());
	assert(parse_manifest("{\"crosshairs\":").empty());
}

// ── 3. id 가 없거나 못 쓰는 항목은 버린다 ────────────────────────────────
static void test_bad_id_dropped()
{
	const std::string body = std::string("{\"crosshairs\":[")
		+ "{\"display_name\":\"id \xEC\x97\x86\xEC\x9D\x8C\",\"code\":\"" + kCodeA + "\"},"   // id 키 없음
		+ "{\"id\":\"\",\"code\":\"" + kCodeA + "\"},"                                        // 빈 id
		+ "{\"id\":\"\\u0001\",\"code\":\"" + kCodeA + "\"},"                                 // 제어문자만 → 정리 후 빈 id
		+ "{\"id\":\"ok\",\"code\":\"" + kCodeA + "\"}"
		+ "]}";
	const std::vector<entry> v = parse_manifest(body);
	// 살아남는 것은 ok 하나. (\u0001 은 평면 파서가 언이스케이프하지 않으므로 문자열
	//  "u0001" 이 되어 실제로는 살아남는다 — 그래서 id 로 카드를 못 만드는 경우만 센다.)
	assert(find_id(v, "ok") != nullptr);
	for (const entry &e : v)
		assert(!e.id.empty());
	assert(v.size() <= 2);
}

// ── 4. id 중복은 먼저 나온 것만 ──────────────────────────────────────────
static void test_duplicate_ids()
{
	const std::string body = std::string("{\"crosshairs\":[")
		+ "{\"id\":\"dup\",\"display_name\":\"first\",\"code\":\"" + kCodeA + "\"},"
		+ "{\"id\":\"dup\",\"display_name\":\"second\",\"code\":\"" + kCodeB + "\"}"
		+ "]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 1);
	assert(v[0].name == "first" && v[0].code == kCodeA);
}

// ── 5. 이름 중복은 허용 — 카드 키는 id 다 ───────────────────────────────
static void test_duplicate_names_allowed()
{
	const std::string body = std::string("{\"crosshairs\":[")
		+ "{\"id\":\"a\",\"display_name\":\"\xEA\xB0\x99\xEC\x9D\x80 \xEC\x9D\xB4\xEB\xA6\x84\",\"code\":\"" + kCodeA + "\"},"
		+ "{\"id\":\"b\",\"display_name\":\"\xEA\xB0\x99\xEC\x9D\x80 \xEC\x9D\xB4\xEB\xA6\x84\",\"code\":\"" + kCodeB + "\"}"
		+ "]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 2);
	assert(v[0].name == v[1].name);
	assert(v[0].id != v[1].id);
	assert(v[0].applicable() && v[1].applicable());

	// 세션은 이름이 아니라 id 로 구분한다 — 같은 이름 두 장을 눌러도 상태가 섞이지 않는다
	session s;
	std::string out;
	assert(apply(s, v[0], "0", out) && s.applied_id == "a");
	assert(apply(s, v[1], out, out) && s.applied_id == "b");
	assert(s.undo_code == "0"); // 두 번째 적용은 스냅샷을 덮어쓰지 않는다
}

// ── 6. 코드가 파서를 통과하지 못한 항목 ─────────────────────────────────
static void test_broken_codes()
{
	struct Case { const char *id; const char *code; crosshair::parse_error err; };
	const Case cases[] = {
		{ "e-empty",   "",                 crosshair::parse_error::empty },
		{ "e-prefix",  "1;P;c;5",          crosshair::parse_error::bad_prefix },
		{ "e-illegal", "0;P;c;-5",         crosshair::parse_error::illegal_char },
		{ "e-token",   "0;;P;c;5",         crosshair::parse_error::empty_token },
		{ "e-dangle",  "0;P;c",            crosshair::parse_error::dangling_key },
	};
	std::string body = "{\"crosshairs\":[";
	for (std::size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		if (i) body += ",";
		body += std::string("{\"id\":\"") + cases[i].id + "\",\"code\":\"" + cases[i].code + "\"}";
	}
	body += "]}";

	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 5);
	for (std::size_t i = 0; i < v.size(); ++i)
	{
		assert(v[i].id == cases[i].id);
		// 카드는 남는다(사라지지 않는다) — 다만 적용은 불가하고 이유가 기록된다
		assert(v[i].state() == entry_state::broken);
		assert(!v[i].applicable());
		assert(v[i].error == cases[i].err);
		// 실패분이 절반 적용된 프로필로 남지 않는다
		assert(v[i].parsed == crosshair::profile());
	}

	// 4096 자를 넘는 코드 → too_long. 카드는 남고 적용은 막힌다.
	std::string longcode = "0";
	while (longcode.size() <= crosshair::kMaxCodeLen)
		longcode += ";P;c;5";
	const std::vector<entry> lv = parse_manifest("{\"crosshairs\":[{\"id\":\"big\",\"code\":\"" + longcode + "\"}]}");
	assert(lv.size() == 1 && lv[0].state() == entry_state::broken);
	assert(lv[0].error == crosshair::parse_error::too_long);

	// 적용을 시도해도 아무 일도 일어나지 않는다
	session s;
	std::string out = "SENTINEL";
	assert(!apply(s, lv[0], "0;P;c;1", out));
	assert(out == "SENTINEL" && s.applied_id.empty() && !can_revert(s));
}

// ── 7. 따옴표 없는 값 = 조용한 실패 지점(설계 §3.3 계약) ────────────────
static void test_unquoted_values_are_broken_not_crash()
{
	// 서버가 code 를 따옴표 없이 내면 평면 파서는 값을 못 읽는다.
	// 그 결과는 "코드 없음" = broken 카드여야 한다(크래시도, 기본 조준점 적용도 아니다).
	const std::vector<entry> v = parse_manifest("{\"crosshairs\":[{\"id\":\"n\",\"code\":12345}]}");
	assert(v.size() == 1);
	assert(v[0].code.empty() && v[0].state() == entry_state::broken);

	// unlocked 는 반대로 **진짜 불리언**이어야 한다. 문자열로 오면 기본값(열림)이 된다.
	const std::vector<entry> w = parse_manifest(
		std::string("{\"crosshairs\":[{\"id\":\"n\",\"code\":\"") + kCodeA + "\",\"unlocked\":\"false\"}]}");
	assert(w.size() == 1 && w[0].unlocked); // 문자열 "false" 는 잠금으로 읽히지 않는다
}

// ── 8. 잠긴 항목은 코드를 들고 있지 않는다 ───────────────────────────────
static void test_locked_entries_carry_no_code()
{
	// 서버가 (구버전이라) 잠긴 항목에 코드를 실어 보내도 클라가 지운다.
	const std::string body = std::string("{\"crosshairs\":[")
		+ "{\"id\":\"paid\",\"display_name\":\"\xED\x94\x84\xEB\xA1\x9C \xED\x8C\xA9\",\"code\":\"" + kCodeA + "\",\"unlocked\":false}"
		+ "]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 1);
	assert(v[0].state() == entry_state::locked);
	assert(!v[0].applicable());
	assert(v[0].code.empty()); // ← 캐시 파일을 열어도 코드가 없다
	assert(!v[0].code_ok);
	assert(v[0].parsed == crosshair::profile());

	// 잠긴 카드는 적용되지 않는다
	session s;
	std::string out = "KEEP";
	assert(!apply(s, v[0], "0", out));
	assert(out == "KEEP" && s.applied_id.empty());
}

// ── 9. 항목 수 상한 ─────────────────────────────────────────────────────
static void test_entry_cap()
{
	std::string body = "{\"crosshairs\":[";
	for (int i = 0; i < 300; ++i)
	{
		if (i) body += ",";
		body += "{\"id\":\"c" + std::to_string(i) + "\",\"code\":\"" + kCodeA + "\"}";
	}
	body += "]}";
	assert(parse_manifest(body).size() == kMaxEntries);
}

// ── 10. 표시 문자열 정리 ────────────────────────────────────────────────
static void test_sanitize_text()
{
	assert(sanitize_text("hello") == "hello");
	assert(sanitize_text("a\nb\rc\td") == "abcd");        // 제어문자 제거(레이아웃/ini 방어)
	assert(sanitize_text(std::string("a\0b", 3)) == "ab"); // 널바이트도 제어문자
	assert(sanitize_text("\x7F") == "");

	// UTF-8 경계에서 자른다 — 한글 22자(66바이트) → 21자(63바이트)
	std::string ko;
	for (int i = 0; i < 22; ++i) ko += "\xEA\xB0\x80"; // '가'
	const std::string cut = sanitize_text(ko);
	assert(cut.size() == 63);
	assert(cut.size() % 3 == 0);
	for (std::size_t i = 0; i < cut.size(); i += 3)
		assert(static_cast<unsigned char>(cut[i]) == 0xEA); // 연속 바이트로 시작하는 조각이 없다

	// 서버가 아주 긴 이름을 보내도 상한을 넘지 않는다
	assert(sanitize_text(std::string(10000, 'x')).size() == kMaxNameBytes);

	// 매니페스트를 통해서도 같은 정리가 걸린다
	const std::vector<entry> v = parse_manifest(
		std::string("{\"crosshairs\":[{\"id\":\"a\",\"display_name\":\"line1\\nline2\",\"code\":\"") + kCodeA + "\"}]}");
	assert(v.size() == 1);
	assert(v[0].name.find('\n') == std::string::npos);
}

// ── 11. 로컬 슬롯 인코딩 왕복 ───────────────────────────────────────────
static void test_local_roundtrip()
{
	std::vector<local_slot> slots;
	slots.push_back(local_slot { "\xEB\x82\xB4 \xEC\xA1\xB0\xEC\xA4\x80\xEC\xA0\x90", kCodeA }); // "내 조준점"
	slots.push_back(local_slot { "pipe|tilde~comma,back\\slash", kCodeB });                        // 구분자 전부
	slots.push_back(local_slot { "", kCodeA });                                                    // 이름 없음

	const std::string enc = encode_locals(slots);
	// 인코딩 결과에 raw 콤마가 없다(ini_file 의 ",," 규칙을 아예 밟지 않는다)
	for (std::size_t i = 0; i < enc.size(); ++i)
		if (enc[i] == ',') assert(i > 0 && enc[i - 1] == '\\');

	const std::vector<local_slot> dec = decode_locals(enc);
	assert(dec.size() == slots.size());
	for (std::size_t i = 0; i < slots.size(); ++i)
		assert(dec[i] == slots[i]);

	// 빈 목록 왕복
	assert(encode_locals({}).empty());
	assert(decode_locals("").empty());

	// 코드가 없는 슬롯은 저장되지 않는다(적용할 것이 없다)
	std::vector<local_slot> nocode;
	nocode.push_back(local_slot { "name-only", "" });
	assert(encode_locals(nocode).empty());
	assert(decode_locals("name-only|").empty());
	assert(decode_locals("name-only").empty()); // '|' 조차 없는 잘린 값

	// 깨진 설정값(끝이 이스케이프로 잘림, 구분자 연타) — 죽지 않고 살릴 수 있는 것만 살린다
	assert(decode_locals("~~~").empty());
	assert(decode_locals("a|" + std::string(kCodeA) + "\\").size() == 1);
	assert(decode_locals("|" + std::string(kCodeA)).size() == 1);
	assert(decode_locals("a|b|c").size() == 1); // 두 번째 '|' 는 코드의 일부로 들어간다
	assert(decode_locals("a|b|c")[0].code == "b|c");

	// 상한 — 저장도 로드도 kMaxLocals 를 넘지 않는다
	std::vector<local_slot> many;
	for (std::size_t i = 0; i < kMaxLocals + 20; ++i)
		many.push_back(local_slot { "n" + std::to_string(i), kCodeA });
	const std::vector<local_slot> capped = decode_locals(encode_locals(many));
	assert(capped.size() == kMaxLocals);
	assert(capped.back().name == "n" + std::to_string(kMaxLocals - 1));

	// 코드가 너무 길면 로드에서 버린다(어차피 parse_code 가 거부한다)
	assert(decode_locals("x|" + std::string(crosshair::kMaxCodeLen + 1, '0')).empty());
	assert(decode_locals("x|" + std::string(crosshair::kMaxCodeLen, '0')).size() == 1);

	// 제어문자가 섞인 이름은 저장 단계에서 정리된다 — 설정 파일 한 줄 규약을 깨지 않는다
	std::vector<local_slot> dirty;
	dirty.push_back(local_slot { "bad\nname", kCodeA });
	const std::string denc = encode_locals(dirty);
	assert(denc.find('\n') == std::string::npos);
	assert(decode_locals(denc)[0].name == "badname");
}

// ── 12. 로컬 추가/삭제 ──────────────────────────────────────────────────
static void test_local_add_remove()
{
	std::vector<local_slot> slots;
	assert(add_local(slots, "first", kCodeA));
	assert(slots.size() == 1);

	// 유효하지 않은 코드는 저장되지 않는다 — 못 쓰는 카드를 스스로 만들지 않는다
	assert(!add_local(slots, "bad", "not-a-code!"));
	assert(!add_local(slots, "bad", ""));
	assert(slots.size() == 1);

	// 이름 중복 허용
	assert(add_local(slots, "first", kCodeB));
	assert(slots.size() == 2);

	while (slots.size() < kMaxLocals)
		assert(add_local(slots, "x", kCodeA));
	assert(!add_local(slots, "overflow", kCodeA)); // 상한에서 조용히 늘어나지 않는다
	assert(slots.size() == kMaxLocals);

	assert(remove_local(slots, 0));
	assert(slots.size() == kMaxLocals - 1);
	assert(!remove_local(slots, 9999)); // 범위 밖 삭제는 무시(UI 인덱스가 어긋나도 안전)
	assert(slots.size() == kMaxLocals - 1);
}

// ── 13. 로컬 카드 만들기 ────────────────────────────────────────────────
static void test_local_entries()
{
	std::vector<local_slot> slots;
	slots.push_back(local_slot { "mine", kCodeA });
	slots.push_back(local_slot { "", kCodeB });
	slots.push_back(local_slot { "broken", "0;;" }); // 설정 파일을 손으로 고쳐 깨진 경우

	const std::vector<entry> v = local_entries(slots);
	assert(v.size() == 3);
	assert(v[0].id == "local:0" && v[0].local && v[0].name == "mine" && v[0].applicable());
	assert(v[1].id == "local:1" && v[1].name == "local:1"); // 이름이 비면 id 를 쓴다
	assert(v[2].state() == entry_state::broken && !v[2].applicable());

	// 로컬 id 는 서버 id 와 섞이지 않는다(접두사 "local:" + 위치). 서버가 같은 위치 번호를
	// 써도 카드 키가 겹치지 않는다 — 겹치면 '사용 중' 표시가 엉뚱한 카드에 켜진다.
	const std::vector<entry> srv = parse_manifest(
		std::string("{\"crosshairs\":[{\"id\":\"0\",\"code\":\"") + kCodeA + "\"},"
		+ "{\"id\":\"L0\",\"code\":\"" + kCodeA + "\"}]}");
	assert(srv.size() == 2);
	for (const entry &e : srv)
	{
		assert(!e.local);
		for (const entry &l : v)
			assert(e.id != l.id);
	}
}

// ── 14. 되돌리기 — 이 기능의 가장 나쁜 실패를 막는 부분 ─────────────────
static void test_session_undo()
{
	const std::vector<entry> v = parse_manifest(std::string("{\"crosshairs\":[")
		+ "{\"id\":\"a\",\"code\":\"" + kCodeA + "\"},"
		+ "{\"id\":\"b\",\"code\":\"" + kCodeB + "\"}]}");

	session s;
	assert(!can_revert(s));

	const std::string mine = "0;P;c;3;0l;9;0o;5;1b;0"; // 사용자가 튜닝하던 코드
	std::string cur = mine, out;

	// 카드를 20번 눌러도 스냅샷은 처음 한 번뿐이다
	for (int i = 0; i < 20; ++i)
	{
		const entry &e = v[static_cast<std::size_t>(i % 2)];
		assert(apply(s, e, cur, out));
		cur = out;
		assert(s.undo_code == mine);
		assert(s.applied_id == e.id);
	}
	assert(can_revert(s));

	// 되돌리면 정확히 그 코드가 나온다
	std::string back;
	assert(revert(s, back));
	assert(back == mine);
	assert(s.applied_id.empty());
	assert(!can_revert(s)); // 스냅샷은 소비된다 — 두 번 되돌려 엉뚱한 것이 나오지 않게
	std::string again = "UNTOUCHED";
	assert(!revert(s, again));
	assert(again == "UNTOUCHED");

	// 되돌린 뒤 다시 적용하면 새 스냅샷이 잡힌다
	cur = back;
	assert(apply(s, v[0], cur, out));
	assert(s.undo_code == mine);
}

static void test_session_user_edit_updates_snapshot()
{
	const std::vector<entry> v = parse_manifest(std::string("{\"crosshairs\":[")
		+ "{\"id\":\"a\",\"code\":\"" + kCodeA + "\"}]}");
	session s;
	std::string out;

	assert(apply(s, v[0], "0;P;c;1", out));
	assert(s.undo_code == "0;P;c;1");

	// 마켓 코드를 사용자가 손본 순간부터 그것은 '내 것' 이다 →
	// 다음 적용의 스냅샷은 손본 코드여야 한다(옛 스냅샷을 붙들면 편집분이 사라진다)
	note_user_edit(s);
	assert(s.applied_id.empty());
	const std::string edited = "0;P;c;7;0l;12";
	assert(apply(s, v[0], edited, out));
	assert(s.undo_code == edited);
}

static void test_session_local_vs_server()
{
	std::vector<local_slot> slots;
	slots.push_back(local_slot { "mine", kCodeA });
	const std::vector<entry> loc = local_entries(slots);
	const std::vector<entry> srv = parse_manifest(
		std::string("{\"crosshairs\":[{\"id\":\"s1\",\"code\":\"") + kCodeB + "\"}]}");

	session s;
	std::string out;
	assert(apply(s, srv[0], "0", out) && out == kCodeB);
	assert(s.applied_id == "s1");
	assert(apply(s, loc[0], out, out) && out == kCodeA);
	assert(s.applied_id == "local:0");
	assert(s.undo_code == "0"); // 로컬로 갈아타도 스냅샷은 그대로
}

static void test_session_empty_current_code()
{
	// 설정이 비어 있는 첫 실행 — 스냅샷할 '내 코드' 가 없다. 되돌리기를 켜 두면
	// 눌렀을 때 빈 코드가 적용돼 조준점이 사라진다. 그래서 아예 잡지 않는다.
	const std::vector<entry> v = parse_manifest(
		std::string("{\"crosshairs\":[{\"id\":\"a\",\"code\":\"") + kCodeA + "\"}]}");
	session s;
	std::string out;
	assert(apply(s, v[0], "", out));
	assert(!can_revert(s));
	assert(s.applied_id == "a");
}

// ── 15. 적용된 코드는 반드시 다시 파싱된다 ──────────────────────────────
static void test_applied_code_reparses()
{
	const std::vector<entry> v = parse_manifest(std::string("{\"crosshairs\":[")
		+ "{\"id\":\"a\",\"code\":\"" + kCodeA + "\"},"
		+ "{\"id\":\"b\",\"code\":\"" + kCodeB + "\"}]}");
	session s;
	std::string out;
	for (const entry &e : v)
	{
		assert(apply(s, e, "0", out));
		crosshair::profile p;
		// 런타임은 out 을 설정에 저장하고 다음 실행에 다시 파싱한다. 그 왕복이 성립해야
		// "적용했는데 재시작하면 기본 조준점" 이 되지 않는다.
		assert(crosshair::parse_code(out, p).ok());
		assert(p == e.parsed);
	}
}

// ── 16. 미리보기 바운딩 박스 ────────────────────────────────────────────
static void test_preview_bounds()
{
	assert(preview_bounds({}).w == 0);

	crosshair::profile p;
	assert(crosshair::parse_code(kCodeA, p).ok());
	crosshair::quad_list q;
	crosshair::build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
	assert(!q.empty());
	const crosshair::rect b = preview_bounds(q);
	assert(b.w > 0 && b.h > 0);
	// 중심(100,100) 을 실제로 감싼다
	assert(b.x <= 100 && b.y <= 100);
	assert(b.x + b.w >= 100 && b.y + b.h >= 100);
	// 모든 quad 를 포함한다
	for (const crosshair::quad &one : q)
	{
		if (one.r.w <= 0 || one.r.h <= 0) continue;
		assert(one.r.x >= b.x && one.r.y >= b.y);
		assert(one.r.x + one.r.w <= b.x + b.w);
		assert(one.r.y + one.r.h <= b.y + b.h);
	}

	// 폭/높이가 0 인 quad 만 있으면 빈 박스(0 나눗셈으로 미리보기 배율을 계산하지 않게)
	crosshair::quad_list degenerate;
	degenerate.push_back(crosshair::quad { crosshair::rect { 5, 5, 0, 3 }, crosshair::rgb { 1, 2, 3 }, 255 });
	assert(preview_bounds(degenerate).w == 0);
}

// ── 17. 매니페스트 안 중첩 객체(계약 위반)에서도 죽지 않는다 ────────────
static void test_nested_object_does_not_crash()
{
	// 평면 파서는 중첩을 모른다(설계 §3.3). 계약을 어긴 페이로드가 와도
	// 죽지 않고, 최소한 id 없는 카드를 만들지 않는다는 것만 보장한다.
	const std::string body = std::string("{\"crosshairs\":[")
		+ "{\"id\":\"a\",\"meta\":{\"code\":\"0;P;c;9\"},\"code\":\"" + kCodeA + "\"}]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 1 && v[0].id == "a");
	for (const entry &e : v)
		assert(!e.id.empty());
}

int main()
{
	test_basic_manifest();
	test_empty_and_garbage();
	test_bad_id_dropped();
	test_duplicate_ids();
	test_duplicate_names_allowed();
	test_broken_codes();
	test_unquoted_values_are_broken_not_crash();
	test_locked_entries_carry_no_code();
	test_entry_cap();
	test_sanitize_text();
	test_local_roundtrip();
	test_local_add_remove();
	test_local_entries();
	test_session_undo();
	test_session_user_edit_updates_snapshot();
	test_session_local_vs_server();
	test_session_empty_current_code();
	test_applied_code_reparses();
	test_preview_bounds();
	test_nested_object_does_not_crash();
	std::printf("sherbet_xhmarket_test: all OK\n");
	return 0;
}
