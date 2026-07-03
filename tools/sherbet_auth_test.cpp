/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
#include "sherbet_auth_core.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::auth;

static void test_parse_start() {
	auto r = parse_start(R"({"state":"abc123","authorize_url":"https://discord.com/oauth2/authorize?x=1"})");
	assert(r.ok);
	assert(r.state == "abc123");
	assert(r.authorize_url == "https://discord.com/oauth2/authorize?x=1");
	auto bad = parse_start(R"({"detail":"nope"})");
	assert(!bad.ok);
}

static void test_parse_poll() {
	auto p = parse_poll(R"({"status":"pending"})");
	assert(p.status == poll_result::pending);
	auto r = parse_poll(R"({"status":"ready","token":"JWT.tok.en","name":"정렬"})");
	assert(r.status == poll_result::ready && r.token == "JWT.tok.en" && r.name == "정렬");
	auto d = parse_poll(R"({"status":"denied","reason":"no_buyer_role"})");
	assert(d.status == poll_result::denied && d.reason == "no_buyer_role");
}

static void test_parse_verify() {
	auto ok = parse_verify(R"({"valid":true,"sub":"123","roles":["sherbet-buyer","900"],"name":"정렬"})", 200);
	assert(ok.ok && !ok.upstream_down && ok.sub == "123");
	assert(ok.roles.size() == 2 && ok.roles[0] == "sherbet-buyer");
	assert(ok.name == "정렬");
	auto no = parse_verify(R"({"valid":false})", 200);
	assert(!no.ok && !no.upstream_down);
	auto down = parse_verify(R"({"valid":null,"error":"upstream_unavailable"})", 503);
	assert(!down.ok && down.upstream_down);
	auto transport = parse_verify("", 0); // 전송 자체 실패
	assert(!transport.ok && transport.upstream_down);
}

static void test_cache_roundtrip() {
	token_cache c; c.token = "JWT.tok.en"; c.hwid = "HWABC_123"; c.last_verified_unix = 1751560000LL;
	std::string s = serialize_cache(c);
	token_cache back;
	assert(parse_cache(s, back));
	assert(back.token == c.token && back.hwid == c.hwid && back.last_verified_unix == c.last_verified_unix);
	token_cache empty;
	assert(!parse_cache("garbage-no-fields", empty)); // 토큰 없으면 실패
}

static void test_grace() {
	// grace=86400. 마지막 검증 t=1000.
	assert(allow_offline(1000, 1000 + 86399, 86400));   // 24h 직전 → 허용
	assert(!allow_offline(1000, 1000 + 86401, 86400));  // 24h 초과 → 거부
	assert(!allow_offline(0, 99999, 86400));            // last_verified 없음(0) → 거부
}

static void test_decide() {
	token_cache c; c.token = "t"; c.last_verified_unix = 1000;
	// 온라인 검증 성공 → authed
	verify_result ok; ok.ok = true;
	assert(decide(ok, c, 5000, 86400) == gate::authed);
	// 구매자 아님(valid:false) → locked (그레이스 무관)
	verify_result no;
	assert(decide(no, c, 1000 + 10, 86400) == gate::locked);
	// 서버 다운 + 그레이스 내 → authed
	verify_result down; down.upstream_down = true;
	assert(decide(down, c, 1000 + 100, 86400) == gate::authed);
	// 서버 다운 + 그레이스 초과 → locked
	assert(decide(down, c, 1000 + 90000, 86400) == gate::locked);
	// 서버 다운 + 캐시 토큰 없음 → locked
	token_cache none;
	assert(decide(down, none, 1000, 86400) == gate::locked);
}

int main() {
	test_parse_start();
	test_parse_poll();
	test_parse_verify();
	test_cache_roundtrip();
	test_grace();
	test_decide();
	std::printf("sherbet_auth_core parsing: ALL PASS\n");
	return 0;
}
