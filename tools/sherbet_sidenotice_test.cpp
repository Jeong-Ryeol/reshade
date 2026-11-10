/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_sidenotice.hpp 호스트 테스트 — 단축키 창에 실제로 칠 법한 검색어(영문 상태/한글 상태/초성)로
// **첫 줄에 뜨는 장소**를 검사한다. 사용자가 보는 건 점수가 아니라 맨 위 줄이다.
#include "sherbet_sidenotice.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace sherbet::sidenotice;

static std::vector<entry> police()
{
	static const char *const zones[] = {
		"빈집1", "빈집2", "신경 위 ATM", "남부 ATM", "은행 뒤 편의점", "신경 옆 편의점", "신경 위 편의점",
		"남부 편의점", "동부 도넛가게", "서부 도넛가게", "남부 저택", "남부 은행", "시청", "보석상", "클럽",
		"구 경찰서", "서버실", "놀이공원",
	};
	std::vector<entry> v;
	int n = 0;
	for (const char *z : zones)
		v.push_back({ "p" + std::to_string(++n), "경찰청", z, std::string("/경찰청 ") + z, {} });
	return v;
}

static std::string top(const std::vector<entry> &list, const char *q)
{
	const std::vector<std::size_t> r = search(list, q);
	return r.empty() ? std::string() : list[r[0]].zone;
}
static bool has(const std::vector<entry> &list, const char *q, const char *zone)
{
	for (std::size_t i : search(list, q))
		if (list[i].zone == zone) return true;
	return false;
}

static void test_compose()
{
	assert(compose_utf8("dkssud") == "안녕");
	assert(compose_utf8("gksrmf") == "한글");
	assert(compose_utf8("qlswlq1") == "빈집1");
	assert(compose_utf8("tlsdnlvus") == "신위편");
	assert(compose_utf8("rkqt") == "값");
	assert(compose_utf8("Rk") == "까");
	assert(compose_utf8("dho") == "왜");
	assert(compose_utf8("dml") == "의");
	assert(compose_utf8("skaqn atm") == "남부 ㅁ스");          // 영문을 치면 자모가 된다(검색은 키로 따로 맞춘다)
	assert(compose_utf8("ATM") == "ㅁ쓰");                     // 대문자 T 는 Shift = ㅆ (두벌식 그대로)
	assert(compose_utf8("나a") == "남");                       // 한글 뒤에 영문 키가 이어 붙는다
	assert(compose_utf8("빈집1") == "빈집1");                   // 완성 한글은 그대로(멱등)
	assert(compose_utf8("신경 위 편의점") == "신경 위 편의점");
	assert(compose_utf8("놀이공원") == "놀이공원");
	assert(compose_utf8("") == "");
}

static void test_search_top()
{
	const std::vector<entry> l = police();
	assert(search(l, "").size() == l.size());               // 빈 검색어 = 전부, 원래 순서
	assert(l[search(l, "")[0]].zone == "빈집1");
	assert(top(l, "빈1") == "빈집1");
	assert(!has(l, "빈1", "빈집2"));
	assert(top(l, "빈2") == "빈집2");
	assert(top(l, "qls2") == "빈집2");                       // 영문 상태로 '빈2'
	assert(top(l, "신위편") == "신경 위 편의점");
	assert(top(l, "tlsdnlvus") == "신경 위 편의점");          // 영문 상태로 '신위편'
	assert(top(l, "신옆") == "신경 옆 편의점");
	assert(top(l, "신위a") == "신경 위 ATM");                 // 한글 상태에서 a → ㅁ … 도 ATM 으로
	assert(top(l, "남은") == "남부 은행");
	assert(top(l, "남저") == "남부 저택");
	assert(top(l, "동도") == "동부 도넛가게");
	assert(top(l, "서도") == "서부 도넛가게");
	assert(top(l, "시청") == "시청");
	assert(!has(l, "시청", "서버실"));
	assert(top(l, "ㅅㅊ") == "시청");                        // 초성
	assert(top(l, "서버") == "서버실");
	assert(top(l, "보석") == "보석상");
	assert(top(l, "클") == "클럽");
	assert(top(l, "구경") == "구 경찰서");
	assert(top(l, "놀") == "놀이공원");
	assert(top(l, "은뒤") == "은행 뒤 편의점");
	assert(has(l, "atm", "신경 위 ATM") && has(l, "atm", "남부 ATM"));
	assert(search(l, "xyz9").empty());
	assert(search(l, "빈3").empty());
}

static void test_mid_typing()
{
	const std::vector<entry> l = police();
	// 치는 도중: '빈집' 을 치다가 'qlsw' 에서 멈추면 조합기는 '빉' 을 만든다. 그래도 빈집이 떠야 한다.
	assert(compose_utf8("qlsw") == "빉");
	assert(top(l, "qlsw") == "빈집1");
	assert(top(l, "시") == "시청");                          // '시' 로 시작하는 '시청' 이 '신경…' 보다 위
	assert(has(l, "시", "신경 위 ATM"));                      // 그래도 '신' 도 '시' 로 찾아진다
}

// 보안국 — 화면엔 문장 속 장소명, 검색은 사람들이 부르는 줄임말(별칭)로도. 경찰청과 둘 다 가진 사람 기준.
static void test_aliases_both_groups()
{
	std::vector<entry> l = police();
	struct z { const char *zone, *alias; };
	static const z sec[] = {
		{ "북부 ATM", "ATM" }, { "북부 편의점 1", "북편1" }, { "북부 편의점 2", "북편2" }, { "북부 편의점 3", "북편3" },
		{ "북부 편의점 4", "북편4" }, { "북부 빈집 1", "북빈1" }, { "북부 빈집 2", "북빈2" }, { "북부 저택", "" },
		{ "북부 은행", "" }, { "닭공장", "" }, { "북부 밀수품 공장", "밀수품털이" }, { "보안국 습격", "보습" },
	};
	int n = 0;
	for (const z &s : sec)
	{
		entry e { "s" + std::to_string(++n), "보안국", s.zone, std::string("/보안국 ") + s.zone, {} };
		if (s.alias[0] != '\0') e.aliases.push_back(s.alias);
		l.push_back(e);
	}
	assert(top(l, "북편1") == "북부 편의점 1");
	assert(top(l, "qnrvus1") == "북부 편의점 1");         // 영문 상태로 '북편1'
	assert(top(l, "ㅂㅍ3") == "북부 편의점 3");
	assert(top(l, "북빈2") == "북부 빈집 2");
	assert(top(l, "밀수품털이") == "북부 밀수품 공장");     // 별칭 그대로
	assert(top(l, "밀수") == "북부 밀수품 공장");
	assert(top(l, "보습") == "보안국 습격");
	assert(top(l, "닭") == "닭공장");
	assert(top(l, "북은") == "북부 은행");
	assert(top(l, "빈1") == "빈집1");                      // 경찰청 빈집1 이 북부 빈집 1 보다 위(앞에서 맞음)
	assert(has(l, "빈1", "북부 빈집 1"));

	// 'ATM' 을 '템' 이라고 부른다 — 별칭 '남부 템' 등으로 '남템' 이 찾힌다(서버 매니페스트가 붙인다).
	for (entry &e : l)
	{
		const std::size_t at = e.zone.find("ATM");
		if (at != std::string::npos)
			e.aliases.push_back(e.zone.substr(0, at) + "템");
	}
	assert(top(l, "남템") == "남부 ATM");
	assert(top(l, "신위템") == "신경 위 ATM");
	assert(top(l, "북템") == "북부 ATM");
	assert(top(l, "skaxpa") == "남부 ATM");                // 영문 상태로 '남템'
}

// 영어로 친 그대로도 찾는다('atm' 이 'ㅁ스' 로만 읽히면 안 된다). 한 글자 영어가 아무 한글에나 걸리면 안 된다.
static void test_english_raw()
{
	const std::vector<entry> l = police();
	const std::string t = top(l, "atm");
	assert(t == "신경 위 ATM" || t == "남부 ATM");
	assert(has(l, "atm", "신경 위 ATM") && has(l, "atm", "남부 ATM"));
	assert(top(l, "ATM") == t);
	assert(top(l, "남atm") == "남부 ATM");                 // 한글 + 영어 섞어도
	assert(top(l, "qls1") == "빈집1");                     // 한글 자판 해석은 그대로 산다
}

static void test_parse()
{
	const std::string body =
		"{\"themes\":[],\"sidenotices\":[{\"id\":\"p1\",\"group\":\"경찰청\",\"zone\":\"빈집1\",\"text\":\"/경찰청 빈집1\\n둘째줄\"},"
		"{\"id\":\"p1\",\"group\":\"경찰청\",\"zone\":\"중복\",\"text\":\"x\"},"
		"{\"id\":\"p2\",\"group\":\"경찰청\",\"zone\":\"\",\"text\":\"x\"},"
		"{\"id\":\"p3\",\"group\":\"보안국\",\"zone\":\"시청\",\"text\":\"/보안국 시청\",\"alias\":\"시|ㅅㅊ||\"}],\"crosshairs\":[]}";
	const std::vector<entry> v = parse_manifest(body);
	assert(v.size() == 2);                     // 중복 id·빈 장소는 버린다
	assert(v[0].zone == "빈집1" && v[0].group == "경찰청");
	assert(v[0].text.find('\n') == std::string::npos); // 줄바꿈은 지운다(채팅은 한 줄)
	assert(v[0].aliases.empty());
	assert(v[1].group == "보안국" && v[1].text == "/보안국 시청");
	assert(v[1].aliases.size() == 2 && v[1].aliases[0] == "시" && v[1].aliases[1] == "ㅅㅊ"); // 빈 칸은 버린다
	assert(parse_manifest("{\"themes\":[]}").empty());
}

// 보습 '준비 시간' — 사람이 칠 법한 모양을 전부 받고, 채팅에 나갈 문장을 그대로 검사한다.
static void test_time_slot()
{
	const std::string bo = "보안국 준비 시간 [00:00]까지 입니다. 준비 완료 후 바로 안내해 드리겠습니다.";
	assert(has_time_slot(bo) && !has_time_slot("/경찰청 빈집1"));
	assert(default_time(21, 30) == "21:40");
	assert(default_time(23, 55) == "00:05");            // 자정 넘김
	assert(default_time(9, 5, 0) == "09:05");
	std::string t;
	assert(parse_time("2135", t) && t == "21:35");
	assert(parse_time("21:35", t) && t == "21:35");
	assert(parse_time("935", t) && t == "09:35");
	assert(parse_time("9:35", t) && t == "09:35");
	assert(parse_time("21.35", t) && t == "21:35");
	assert(parse_time(" 21 35 ", t) && t == "21:35");
	assert(parse_time("0005", t) && t == "00:05");
	assert(!parse_time("2460", t) && !parse_time("2400", t) && !parse_time("12345", t));
	assert(!parse_time("21:5", t) && !parse_time("ab", t) && !parse_time("", t) && !parse_time("5", t));
	assert(fill_time(bo, "21:40") == "보안국 준비 시간 [21:40]까지 입니다. 준비 완료 후 바로 안내해 드리겠습니다.");
	assert(fill_time("/경찰청 빈집1", "21:40") == "/경찰청 빈집1");
}

// 채울 칸 — 수배·알디컨·영장. 사람이 칠 법한 고번 닉네임 모양을 전부 같은 결과로, 나가는 문장을 그대로 검사.
static void test_slots()
{
	std::string t;
	assert(normalize_target("516우공비", t) && t == "516/우공비");
	assert(normalize_target("516 우공비", t) && t == "516/우공비");
	assert(normalize_target("516/우공비", t) && t == "516/우공비");
	assert(normalize_target(" 516 / 우공비 ", t) && t == "516/우공비");
	assert(normalize_target("[516/우공비]", t) && t == "516/우공비");
	assert(normalize_target("12 John Smith", t) && t == "12/John Smith");
	assert(!normalize_target("우공비", t) && !normalize_target("516", t) && !normalize_target("", t));
	// 여러 가지 닉네임 — 긴 이름·띄어쓰기·숫자·영어·괄호·기호가 섞여도 고번만 앞에서 떼고 나머지는 그대로
	assert(normalize_target("141 정렬", t) && t == "141/정렬");
	assert(normalize_target("7 가나다라마바사아자차카타파하가나다라", t) && t == "7/가나다라마바사아자차카타파하가나다라");
	assert(normalize_target("1415 도경 2호", t) && t == "1415/도경 2호");            // 닉네임 안 띄어쓰기·숫자
	assert(normalize_target("1415/도경2", t) && t == "1415/도경2");
	assert(normalize_target("99999 Kim_Jung-Ryeol", t) && t == "99999/Kim_Jung-Ryeol"); // 영어·밑줄·하이픈
	assert(normalize_target("12 [팀장] 홍길동", t) && t == "12/[팀장] 홍길동");        // 닉네임 안 괄호는 살린다
	assert(normalize_target("12 a/b", t) && t == "12/a/b");                           // 닉네임 안 '/' 도 그대로
	assert(normalize_target("  141   정렬  ", t) && t == "141/정렬");                // 앞뒤 공백
	assert(normalize_target("141 ♥정렬♥", t) && t == "141/♥정렬♥");

	const std::string su = "/보안국 보안국에서 안내드립니다. 현 시간부로 [{대상}]/[{시간+30}]까지 수배RP를 진행합니다.";
	const std::vector<slot> s = find_slots(su);
	assert(s.size() == 2 && s[0].kind == slot_kind::target && s[1].kind == slot_kind::time && s[1].ahead_min == 30);
	assert(slot_default(s[1], 20, 13) == "20:43");                      // 예시 문장 그대로(20:13 에 친 수배 → 20:43)
	std::vector<std::string> v(2);
	assert(slot_value(s[0], "516 우공비", v[0]) && slot_value(s[1], "2043", v[1]));
	assert(fill_slots(su, s, v) == "/보안국 보안국에서 안내드립니다. 현 시간부로 [516/우공비]/[20:43]까지 수배RP를 진행합니다.");

	const std::string rd = "[{대상}] 알피중 디컨으로 {시간} ~ {시간+10} 까지 10분 카운트 하겠습니다.";
	const std::vector<slot> r = find_slots(rd);
	assert(r.size() == 3 && r[1].ahead_min == 0 && r[2].ahead_min == 10);
	assert(slot_default(r[1], 23, 55) == "23:55" && slot_default(r[2], 23, 55) == "00:05");
	std::vector<std::string> rv = { "516/우공비", "23:55", "00:05" };
	assert(fill_slots(rd, r, rv) == "[516/우공비] 알피중 디컨으로 23:55 ~ 00:05 까지 10분 카운트 하겠습니다.");

	const std::string wr = "/보안국 현 시간부로 {팩션} 팩션에 영장이 발부 되었습니다.";
	const std::vector<slot> w = find_slots(wr);
	assert(w.size() == 1 && w[0].kind == slot_kind::text && w[0].label == "팩션");
	std::string f, blank;
	assert(slot_value(w[0], " 팩토리 ", f) && f == "팩토리" && !slot_value(w[0], "  ", blank));
	assert(fill_slots(wr, w, { f }) == "/보안국 현 시간부로 팩토리 팩션에 영장이 발부 되었습니다.");

	// 긴 닉네임 — 문장은 그대로 만들어지고, 채팅 상한(166자)을 넘는지 글자 수(바이트 아님)로 잰다
	{
		std::vector<std::string> lv(2);
		assert(slot_value(s[0], "141 가나다라마바사아자차카타파하가나다라마바사아자차카타파하", lv[0]) && slot_value(s[1], "2043", lv[1]));
		const std::string longer = fill_slots(su, s, lv);
		assert(longer.find("[141/가나다라마바사아자차카타파하가나다라마바사아자차카타파하]") != std::string::npos);
		assert(char_count("가나다") == 3 && char_count("abc") == 3 && char_count("") == 0);
		assert(char_count(fill_slots(su, s, v)) < kChatMaxChars);                       // 보통 닉네임은 여유
	}

	// 예전 보습 문장([00:00])도 같은 칸으로 — 대괄호는 그대로 남는다
	const std::string bo = "보안국 준비 시간 [00:00]까지 입니다.";
	const std::vector<slot> b = find_slots(bo);
	assert(b.size() == 1 && b[0].kind == slot_kind::time && b[0].ahead_min == 10 && b[0].bracketed);
	assert(fill_slots(bo, b, { "21:40" }) == "보안국 준비 시간 [21:40]까지 입니다.");
	assert(fill_slots(bo, b, { "" }, "--:--") == "보안국 준비 시간 [--:--]까지 입니다.");
	assert(!has_slots("/경찰청 경찰청에서 안내 드립니다. 현 시간부로 빈집1에 강도가 침입했습니다."));
}

static void test_poster()
{
	poster p;
	assert(!p.active() && p.tick(0) == post_action::none);
	p.start(1000, 300);
	assert(p.active());
	assert(p.tick(1000) == post_action::none);                          // 메뉴가 닫히는 중
	assert(p.tick(1000 + kCloseWaitMs) == post_action::open_chat);
	// 채팅 키는 누른 채 잠깐 둔다 — 누르자마자 떼면 게임이 못 본다(창은 닫히는데 T 가 안 열림, 구매자 실측)
	assert(kKeyHoldMs >= 50);
	assert(p.tick(1000 + kCloseWaitMs + kKeyHoldMs - 1) == post_action::none);
	assert(p.tick(1000 + kCloseWaitMs + kKeyHoldMs) == post_action::release_chat);
	const long long t_rel = 1000 + kCloseWaitMs + kKeyHoldMs;
	assert(p.tick(t_rel + 10) == post_action::none);                    // 채팅창 열리길 기다린다
	assert(p.tick(t_rel + 300) == post_action::paste);
	assert(p.tick(t_rel + 300) == post_action::none);                   // 한 프레임에 하나씩
	assert(p.tick(t_rel + 300 + kPasteToSendMs) == post_action::send);
	assert(!p.active() && p.tick(99999) == post_action::none);

	// 프레임이 크게 끊겨도 단계를 건너뛰지 않는다(채팅 열기 없이 붙여넣기 금지, 누른 키는 반드시 뗀다).
	p.start(0, 300);
	assert(p.tick(100000) == post_action::open_chat);
	assert(p.tick(100000) == post_action::release_chat);
	assert(p.tick(100000) == post_action::paste);
	assert(p.tick(100000) == post_action::send);

	p.start(0, 5); // 상한·하한
	assert(p.tick(kCloseWaitMs) == post_action::open_chat);
	assert(p.tick(kCloseWaitMs + kKeyHoldMs) == post_action::release_chat);
	assert(p.tick(kCloseWaitMs + kKeyHoldMs + kMinChatDelayMs - 1) == post_action::none);
	assert(p.tick(kCloseWaitMs + kKeyHoldMs + kMinChatDelayMs) == post_action::paste);
	p.cancel();
	assert(!p.active());
}

// 신세계렉카 — 문장의 {클립보드} 에 복사해 둔 이름('🚧하은(210)')이 그대로 들어가 정렬이 붙여 준 문장과
// 글자 하나까지 같아야 한다. 클립보드에 이름이 아닌 것(빈칸·문장·여러 줄)이 있으면 안 올린다.
static void test_tow_clipboard()
{
	const std::string tmpl = "/렉카 {클립보드} 님 2분 후 차량 압류 진행 하겠습니다.";
	assert(has_clip_slot(tmpl));
	assert(!has_clip_slot("/경찰청 [{대상}]님"));

	std::string name;
	assert(clip_name("🚧하은(210)", name) && name == "🚧하은(210)");
	assert(clip_name("  🚧하은(210)\r\n", name) && name == "🚧하은(210)"); // 복사하며 묻은 줄바꿈·공백
	assert(!clip_name("", name));
	assert(!clip_name(" \r\n", name));
	assert(!clip_name("🚧하은(210)\n🚧민수(11)", name));                  // 여러 줄
	assert(!clip_name("/렉카 🚧하은(210) 님 차량 압류 진행합니다.", name)); // 문장이 남아 있다
	assert(!clip_name("가나다라마바사아자차카타파하가나다라마바사아자차카타파하가나다라마", name)); // 너무 김
	assert(!clip_name("{대상}", name));

	const std::string filled = fill_clip(tmpl, "🚧하은(210)");
	assert(filled == "/렉카 🚧하은(210) 님 2분 후 차량 압류 진행 하겠습니다.");
	assert(find_slots(filled).empty()); // 칸 채우기 창을 거치지 않고 바로 올라간다
	assert(fill_clip("{클립보드}/{클립보드}", "a") == "a/a");

	// 우리 창(폰트에 그림 문자가 없다)에는 🚧 를 빼고 보여 준다. 채팅 문장은 그대로.
	assert(display_text("🚧하은(210)") == "하은(210)");
	assert(display_text("하은(210)") == "하은(210)");

	std::string main_text, note;
	split_note("압류 2분전 (2분)", main_text, note);
	assert(main_text == "압류 2분전" && note == "(2분)");
	split_note("보호/준보호", main_text, note);
	assert(main_text == "보호/준보호" && note.empty());

	// 목록은 빈 검색어면 서버 순서 그대로(사진 순서), 몇 글자로도 찾는다.
	static const char *const zones[] = { "압류 2분전 (2분)", "압류 2분후", "보호/준보호", "수리불가", "범죄차량", "오프라인",
		"밀배달", "팩션사유지", "버려진 배", "노래방 인근", "이벤트", "트럭비콘" };
	std::vector<entry> tow;
	int n = 0;
	for (const char *z : zones)
		tow.push_back({ "tow-" + std::to_string(++n), "신세계렉카", z, "/렉카 {클립보드} 님", {} });
	const std::vector<std::size_t> all = search(tow, "");
	assert(all.size() == 12);
	for (std::size_t i = 0; i < all.size(); ++i)
		assert(all[i] == i);
	assert(top(tow, "노래") == "노래방 인근");
	assert(top(tow, "비콘") == "트럭비콘");
	assert(top(tow, "ㅂㅋ") == "트럭비콘");
	assert(top(tow, "shfo") == "노래방 인근"); // 영문 상태로 '노래'
	assert(has(tow, "압류", "압류 2분후"));
}

// 압류 2분전 → 그 이름에 2분 쿨타임. 화면에 보이는 '2:00 … 0:01', 같은 이름에 다른 공지를 올리면 끝,
// 다 되고 3초 뒤 사라짐, ✕ 로 지우기, 여러 대 동시(최대 4).
static void test_tow_cooldown()
{
	std::string main_text, note;
	split_note("압류 2분전 (2분)", main_text, note);
	assert(note_minutes(note) == 2);
	assert(note_minutes("(10분)") == 10);
	assert(note_minutes("") == 0);
	assert(note_minutes("(분)") == 0);
	assert(note_minutes("(2초)") == 0);
	assert(note_minutes("(a분)") == 0);

	assert(format_remaining(120000) == "2:00");
	assert(format_remaining(119001) == "2:00");
	assert(format_remaining(119000) == "1:59");
	assert(format_remaining(61000) == "1:01");
	assert(format_remaining(1) == "0:01");
	assert(format_remaining(0).empty());
	assert(format_remaining(-5).empty());

	cooldowns c;
	c.posted("🚧하은(210)", 2, 1000);
	assert(c.items().size() == 1 && c.items()[0].end_ms == 121000);
	c.posted("🚧하은(210)", 2, 5000); // 다시 올리면 다시 2분(줄이 둘이 되지 않는다)
	assert(c.items().size() == 1 && c.items()[0].end_ms == 125000);
	c.posted("🚧민수(11)", 2, 6000);
	assert(c.items().size() == 2);
	c.posted("🚧하은(210)", 0, 7000); // 하은 압류 2분후 → 하은 타이머만 끝
	assert(c.items().size() == 1 && c.items()[0].target == "🚧민수(11)");

	assert(kCooldownLingerMs == 3000);
	c.prune(126000 + kCooldownLingerMs - 1); // 민수: 다 됐지만 3초는 더 보인다
	assert(c.items().size() == 1);
	c.prune(126000 + kCooldownLingerMs);
	assert(c.empty());

	for (int i = 0; i < 5; ++i)
		c.posted("t" + std::to_string(i), 2, i);
	assert(c.items().size() == kMaxCooldowns && c.items()[0].target == "t1"); // 가장 오래된 t0 이 빠졌다

	c.dismiss("t2"); // ✕ — 그 줄만
	assert(c.items().size() == 3 && c.items()[0].target == "t1" && c.items()[1].target == "t3");
	c.dismiss("nobody");
	assert(c.items().size() == 3);
}

int main()
{
	test_compose();
	test_search_top();
	test_mid_typing();
	test_aliases_both_groups();
	test_english_raw();
	test_parse();
	test_time_slot();
	test_slots();
	test_poster();
	test_tow_clipboard();
	test_tow_cooldown();
	std::printf("sherbet_sidenotice_test: 11 passed\n");
	return 0;
}
