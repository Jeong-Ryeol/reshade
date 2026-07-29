/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 서버 응답 바디를 **진짜 클라이언트 파서**에 통과시켜 보는 최소 하네스.
//
// 존재 이유: 매니페스트 라우터의 계약은 파이썬 쪽 단언(“문자열인가”, “니들이 한 번
// 나오는가”)만으로는 지켜지지 않는다. 실제로 놓친 적이 있다 —
// `"min_version": ""` 은 파이썬 단언을 전부 통과하지만 parse_manifest 는 그 매니페스트를
// **통째로 거부**한다(키가 있는데 파싱 불가 = 전체 거부). 서버 테스트 픽스처가
// min_version 을 채워 두는 바람에 그 조합이 한 번도 실행되지 않았다.
// 그래서 서버 테스트(server/tests/test_update.py)가 라우터의 **실제 응답 바디**를 이
// 프로그램에 먹여 ok 여부를 확인한다. 수동 크로스체크를 CI 에 박아 넣은 것이다.
//
// 사용법:  sherbet_manifest_check <arch>   (바디는 stdin, 원문 그대로)
// 출력:    ok=0|1 / version=… / min_version=… / sha256=… / size=… / url=… / allow_downgrade=0|1
//          (ok=0 이면 ok= 줄만 나온다 — parse_manifest 는 거부 시 완전히 빈 info 를 준다)
// 종료코드: 0 = 판정 완료(ok 값은 stdout 으로 읽을 것) / 2 = 사용법 오류
//          거부를 종료코드로 신호하지 않는다. 거부와 '하네스가 못 돌았다' 를 구별해야
//          테스트가 조용히 통과하는 일이 없다.
//
// clang++ -std=c++17 -Wall -Isource tools/sherbet_manifest_check.cpp -o /tmp/mcheck
#include "sherbet_update_core.hpp"
#include <cstdio>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::fprintf(stderr, "usage: %s <arch>   (manifest body on stdin)\n", argv[0]);
		return 2;
	}
	const std::string arch = argv[1];

	// 바이너리 세이프하게 통째로 읽는다(널바이트가 섞인 바디도 서버는 낼 수 있다).
	std::string body;
	{
		char buf[4096];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0)
			body.append(buf, n);
	}

	const sherbet::update::info u = sherbet::update::parse_manifest(body, arch.c_str());
	std::printf("ok=%d\n", u.ok ? 1 : 0);
	if (!u.ok)
		return 0;
	std::printf("version=%s\n", u.version.c_str());
	std::printf("min_version=%s\n", u.min_version.c_str());
	std::printf("sha256=%s\n", u.sha256.c_str());
	std::printf("size=%llu\n", static_cast<unsigned long long>(u.size));
	std::printf("url=%s\n", u.url.c_str());
	std::printf("allow_downgrade=%d\n", u.allow_downgrade ? 1 : 0);
	return 0;
}
