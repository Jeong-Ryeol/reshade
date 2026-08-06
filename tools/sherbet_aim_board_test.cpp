/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_aim_board.hpp 호스트 단위테스트 (리더보드 응답 파서).
// ⚠️ -DNDEBUG 금지 — 전부 assert 로 검증한다.

#include "sherbet_aim_board.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace sherbet::aim;

static bool feq(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// 서버가 실제로 내보내는 모양(server/app/aim.py). 숫자는 전부 따옴표다.
static const char *const kReal =
	"{\"level\":\"hard\",\"duration\":\"s60\","
	"\"top\":["
	"{\"rank\":\"1\",\"name\":\"\xEC\x84\x9C\xEC\x95\x84\xEC\x97\xB0\",\"hits\":\"55\",\"accuracy\":\"0.9167\"},"
	"{\"rank\":\"2\",\"name\":\"\xEC\xA0\x95\xEB\xA0\xAC\",\"hits\":\"40\",\"accuracy\":\"0.8000\"}"
	"],\"my_rank\":\"2\",\"my_hits\":\"40\",\"total\":\"2\"}";

static void test_real_response()
{
	board b;
	assert(parse_board(kReal, b));
	assert(b.total == 2);
	assert(b.my_rank == 2);
	assert(b.my_hits == 40);
	assert(b.top.size() == 2);
	assert(b.top[0].rank == 1);
	assert(b.top[0].hits == 55);
	assert(feq(b.top[0].accuracy, 0.9167f));
	assert(b.top[0].name == "\xEC\x84\x9C\xEC\x95\x84\xEC\x97\xB0"); // 서아연 — 한글이 온전히 온다
	assert(b.top[1].rank == 2);
	assert(b.top[1].hits == 40);
	assert(feq(b.top[1].accuracy, 0.8f));
}

// ★ 숫자를 따옴표 없이 보내면 조용히 0 이 된다. 그게 바로 이 규약이 존재하는 이유다 —
//   서버가 실수로 숫자를 그대로 내면 리더보드가 전부 '0개' 로 보이고 오류는 안 난다.
//   여기서 그 동작을 명시적으로 못박아 두면, 서버 쪽 규약이 깨졌을 때 왜 그런지 안다.
static void test_unquoted_numbers_read_as_zero()
{
	const char *raw = "{\"top\":[{\"rank\":1,\"name\":\"A\",\"hits\":30,\"accuracy\":1.0}],"
		"\"my_rank\":1,\"my_hits\":30,\"total\":\"1\"}";
	board b;
	assert(parse_board(raw, b));
	assert(b.total == 1);   // 이건 문자열이라 읽힌다
	assert(b.my_rank == 0); // 숫자로 와서 안 읽혔다
	assert(b.my_hits == 0);
	assert(b.top.empty());  // rank 가 0 이라 줄 자체가 버려진다
}

static void test_empty_board_is_success()
{
	// 새 표는 원래 비어 있다. 실패로 처리하면 "아직 아무도 없음" 을 못 보여준다.
	board b;
	assert(parse_board("{\"level\":\"easy\",\"duration\":\"s30\",\"top\":[],\"my_rank\":\"0\",\"my_hits\":\"0\",\"total\":\"0\"}", b));
	assert(b.top.empty());
	assert(b.total == 0 && b.my_rank == 0 && b.my_hits == 0);
}

static void test_garbage_rejected()
{
	board b;
	assert(!parse_board("", b));
	assert(!parse_board("not json at all", b));
	// 오류 페이지·프록시 안내문 — total 이 없으면 우리 응답이 아니다.
	assert(!parse_board("<html><body>502 Bad Gateway</body></html>", b));
	assert(!parse_board("{\"detail\":\"unauthorized\"}", b));

	// 파싱 실패 시 out 을 건드리지 않는다 — 화면에 남아 있던 표가 지워지면 안 된다.
	board keep;
	keep.total = 7;
	keep.top.push_back(board_row{ 1, "A", 10, 0.5f });
	assert(!parse_board("{\"detail\":\"x\"}", keep));
	assert(keep.total == 7 && keep.top.size() == 1);
}

static void test_bad_rows_dropped()
{
	// 이름 없는 줄, 순위 0 인 줄은 버린다. 빈 칸이 화면에 뜨느니 없는 게 낫다.
	const char *raw = "{\"top\":["
		"{\"rank\":\"1\",\"name\":\"\",\"hits\":\"9\",\"accuracy\":\"0.5\"},"
		"{\"rank\":\"0\",\"name\":\"B\",\"hits\":\"8\",\"accuracy\":\"0.5\"},"
		"{\"rank\":\"3\",\"name\":\"C\",\"hits\":\"7\",\"accuracy\":\"0.5\"}"
		"],\"my_rank\":\"3\",\"my_hits\":\"7\",\"total\":\"3\"}";
	board b;
	assert(parse_board(raw, b));
	assert(b.top.size() == 1);
	assert(b.top[0].name == "C");
}

static void test_number_parsing()
{
	const char *obj = "{\"a\":\"42\",\"b\":\"-7\",\"c\":\"\",\"d\":\"12x\",\"e\":\"999999999999\",\"f\":\"-\"}";
	using namespace sherbet::aim::detail;
	assert(json_int(obj, "a") == 42);
	assert(json_int(obj, "b") == -7);
	assert(json_int(obj, "c", -1) == -1);   // 빈 값
	assert(json_int(obj, "d", -1) == -1);   // 잔여물 — 12 로 읽고 넘어가지 않는다
	assert(json_int(obj, "e", -1) == -1);   // 상한 초과
	assert(json_int(obj, "f", -1) == -1);   // 부호만
	assert(json_int(obj, "nope", 5) == 5);  // 없는 키

	const char *fo = "{\"p\":\"0.9167\",\"q\":\"1.0000\",\"r\":\"0\",\"s\":\"1.5\",\"t\":\"abc\",\"u\":\"0.5x\",\"v\":\".5\"}";
	assert(feq(json_unit(fo, "p"), 0.9167f));
	assert(feq(json_unit(fo, "q"), 1.0f));
	assert(feq(json_unit(fo, "r"), 0.0f));
	assert(feq(json_unit(fo, "s"), 1.0f));       // 1 초과는 잘린다
	assert(feq(json_unit(fo, "t", -1.0f), -1.0f));
	assert(feq(json_unit(fo, "u", -1.0f), -1.0f)); // 잔여물 거부
	assert(feq(json_unit(fo, "v", -1.0f), -1.0f)); // 정수부 없음
}

static void test_submit_response()
{
	// updated 는 **진짜 불리언**이다(따옴표가 없다). json_bool 로 읽는다.
	const char *ok = "{\"updated\":true,\"my_rank\":\"1\",\"total\":\"3\",\"my_hits\":\"50\","
		"\"top\":[{\"rank\":\"1\",\"name\":\"A\",\"hits\":\"50\",\"accuracy\":\"1.0\"}]}";
	board b;
	bool up = false;
	assert(parse_submit(ok, b, up));
	assert(up);
	assert(b.my_rank == 1 && b.total == 3);

	const char *no = "{\"updated\":false,\"my_rank\":\"4\",\"total\":\"9\",\"my_hits\":\"12\",\"top\":[]}";
	assert(parse_submit(no, b, up));
	assert(!up);
	assert(b.my_rank == 4);

	// 응답이 아니면 updated 를 건드리지 않는다.
	bool sentinel = true;
	assert(!parse_submit("{\"detail\":\"implausible\"}", b, sentinel));
	assert(sentinel);
}

static void test_name_escapes()
{
	// 이름에 따옴표·역슬래시가 들어와도 파서가 무너지지 않아야 한다.
	const char *raw = "{\"top\":[{\"rank\":\"1\",\"name\":\"a\\\"b\",\"hits\":\"5\",\"accuracy\":\"1.0\"}],"
		"\"my_rank\":\"1\",\"my_hits\":\"5\",\"total\":\"1\"}";
	board b;
	assert(parse_board(raw, b));
	assert(b.top.size() == 1);
	assert(b.top[0].name == "a\"b");
}

int main()
{
	test_real_response();
	test_unquoted_numbers_read_as_zero();
	test_empty_board_is_success();
	test_garbage_rejected();
	test_bad_rows_dropped();
	test_number_parsing();
	test_submit_response();
	test_name_escapes();
	std::printf("sherbet_aim_board_test: ALL PASS\n");
	return 0;
}
