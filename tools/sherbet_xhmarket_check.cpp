/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// /content/me 응답 바디를 **진짜 클라이언트 파서**(sherbet_xhmarket.hpp)에 통과시켜 보는 하네스.
//
// 존재 이유는 tools/sherbet_manifest_check.cpp 와 같다: 서버 계약은 파이썬 단언만으로
// 지켜지지 않는다. 조준점 마켓에서 조용히 죽는 조합은 특히 둘이다 —
//   1) 코드를 따옴표 없이 내보내면(예: "code": 0) 평면 파서가 값을 못 읽어 **전 항목이
//      '사용 불가' 카드**가 된다. 파이썬 쪽에서는 아무 오류도 안 난다.
//   2) 잠긴 항목에 code 를 실어 보내면 판매가 무의미해진다(캐시 파일에 원문이 남는다).
// 그래서 server/tests 가 라우터의 **실제 응답 바디**를 이 프로그램에 먹여 확인한다.
//
// 사용법:  sherbet_xhmarket_check      (바디는 stdin, 원문 그대로)
// 출력:    count=N
//          <i>.id= / <i>.state=ready|locked|broken / <i>.name= / <i>.author= /
//          <i>.tag= / <i>.code= / <i>.applicable=0|1
// 종료코드: 0 = 판정 완료. 거부를 종료코드로 신호하지 않는다 — '하네스가 못 돌았다' 와
//          '클라가 항목을 거부했다' 가 구별되지 않으면 테스트가 조용히 통과한다.
//
// clang++ -std=c++17 -Wall -Isource tools/sherbet_xhmarket_check.cpp -o /tmp/xmcheck
#include "sherbet_xhmarket.hpp"

#include <cstdio>
#include <string>
#include <vector>

int main()
{
	std::string body;
	{
		char buf[4096];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), stdin)) > 0)
			body.append(buf, n);
	}

	const std::vector<sherbet::xhmarket::entry> v = sherbet::xhmarket::parse_manifest(body);
	std::printf("count=%d\n", static_cast<int>(v.size()));
	for (std::size_t i = 0; i < v.size(); ++i)
	{
		const sherbet::xhmarket::entry &e = v[i];
		const char *state = "broken";
		switch (e.state())
		{
		case sherbet::xhmarket::entry_state::ready:  state = "ready"; break;
		case sherbet::xhmarket::entry_state::locked: state = "locked"; break;
		case sherbet::xhmarket::entry_state::broken: state = "broken"; break;
		}
		const int k = static_cast<int>(i);
		std::printf("%d.id=%s\n", k, e.id.c_str());
		std::printf("%d.state=%s\n", k, state);
		std::printf("%d.name=%s\n", k, e.name.c_str());
		std::printf("%d.author=%s\n", k, e.author.c_str());
		std::printf("%d.tag=%s\n", k, e.tag.c_str());
		std::printf("%d.code=%s\n", k, e.code.c_str());
		std::printf("%d.applicable=%d\n", k, e.applicable() ? 1 : 0);
	}
	return 0;
}
