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
	auto r = parse_poll(R"({"status":"ready","token":"JWT.tok.en"})");
	assert(r.status == poll_result::ready && r.token == "JWT.tok.en");
	auto d = parse_poll(R"({"status":"denied","reason":"no_buyer_role"})");
	assert(d.status == poll_result::denied && d.reason == "no_buyer_role");
}

static void test_parse_verify() {
	auto ok = parse_verify(R"({"valid":true,"sub":"123","roles":["sherbet-buyer","900"]})", 200);
	assert(ok.ok && !ok.upstream_down && ok.sub == "123");
	assert(ok.roles.size() == 2 && ok.roles[0] == "sherbet-buyer");
	auto no = parse_verify(R"({"valid":false})", 200);
	assert(!no.ok && !no.upstream_down);
	auto down = parse_verify(R"({"valid":null,"error":"upstream_unavailable"})", 503);
	assert(!down.ok && down.upstream_down);
	auto transport = parse_verify("", 0); // 전송 자체 실패
	assert(!transport.ok && transport.upstream_down);
}

int main() {
	test_parse_start();
	test_parse_poll();
	test_parse_verify();
	std::printf("sherbet_auth_core parsing: ALL PASS\n");
	return 0;
}
