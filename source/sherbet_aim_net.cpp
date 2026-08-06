/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_aim_net.hpp"
#include "sherbet_http.hpp"

#include <cstdio>

namespace
{
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr wchar_t kScorePath[] = L"/sherbet-auth/aim/score";

	// "서버가 사용 불가" 와 "거부당함" 을 구별해서 보여준다. 뭉뚱그리면 사용자가
	// 자기 인터넷을 의심해야 하는지 기록이 반려된 건지 알 수 없다.
	const char *status_message(int status)
	{
		if (status == 0)
			return "\xEC\x84\x9C\xEB\xB2\x84\xEC\x97\x90 \xEC\x97\xB0\xEA\xB2\xB0\xED\x95\x98\xEC\xA7\x80 \xEB\xAA\xBB\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94"; // "서버에 연결하지 못했어요"
		if (status == 401)
			return "\xEB\x8B\xA4\xEC\x8B\x9C \xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "다시 로그인해 주세요"
		if (status == 400)
			return "\xEA\xB8\xB0\xEB\xA1\x9D\xEC\x9D\xB4 \xEB\xB0\x98\xEB\xA0\xA4\xEB\x90\x90\xEC\x96\xB4\xEC\x9A\x94"; // "기록이 반려됐어요"
		return "\xEC\x9E\xA0\xEC\x8B\x9C \xED\x9B\x84 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "잠시 후 다시 시도해 주세요"
	}
}

const char *sherbet::aim::level_key(level lv)
{
	switch (lv)
	{
	case level::easy:   return "easy";
	case level::normal: return "normal";
	case level::hard:   return "hard";
	case level::hell:   return "hell";
	}
	return "normal";
}

const char *sherbet::aim::duration_key(duration d)
{
	switch (d)
	{
	case duration::s10: return "s10";
	case duration::s30: return "s30";
	case duration::s60: return "s60";
	}
	return "s60";
}

const char *sherbet::aim::mode_key(mode m)
{
	switch (m)
	{
	case mode::free:  return "free";
	case mode::level: return "level";
	}
	return "free";
}

void sherbet::aim::net::begin_fetch(const std::string &bearer, level lv, duration d, mode m)
{
	if (_active.load() || bearer.empty())
		return;
	join(); // 이전 워커가 끝났지만 아직 join 안 됐을 수 있다
	_active.store(true);
	_worker = std::thread(&net::run, this, bearer, lv, d, m, false, 0, 0);
}

void sherbet::aim::net::begin_submit(const std::string &bearer, level lv, duration d, mode m, int hits, int shots)
{
	if (_active.load() || bearer.empty())
		return;
	join();
	_active.store(true);
	_worker = std::thread(&net::run, this, bearer, lv, d, m, true, hits, shots);
}

void sherbet::aim::net::run(std::string bearer, level lv, duration d, mode m, bool submit, int hits, int shots)
{
	std::string resp;
	int status = 0;

	if (submit)
	{
		char body[256];
		std::snprintf(body, sizeof(body),
			"{\"level\":\"%s\",\"duration\":\"%s\",\"mode\":\"%s\",\"hits\":%d,\"shots\":%d}",
			level_key(lv), duration_key(d), mode_key(m), hits, shots);
		status = sherbet::http::post_json(kHost, kScorePath, body, resp, bearer.c_str());
	}
	else
	{
		wchar_t path[256];
		// 난이도·길이·모드 키는 우리가 만든 고정 문자열이라 이스케이프가 필요 없다.
		std::swprintf(path, 256, L"/sherbet-auth/aim/leaderboard?level=%hs&duration=%hs&mode=%hs",
			level_key(lv), duration_key(d), mode_key(m));
		status = sherbet::http::get(kHost, path, resp, bearer.c_str());
	}

	board b;
	bool updated = false;
	bool ok = false;
	if (status == 200 && !resp.empty())
		ok = submit ? parse_submit(resp, b, updated) : parse_board(resp, b);

	{
		std::lock_guard<std::mutex> lk(_mtx);
		if (ok)
		{
			_board = b;
			_has_board = true;
			_was_submit = submit;
			_updated = updated;
			_error.clear();
		}
		else
		{
			// ⚠️ 실패해도 이전 표를 지우지 않는다. 화면에 떠 있던 순위가 네트워크가
			//    한 번 흔들렸다고 사라지면, 사용자는 자기 기록이 날아간 줄 안다.
			_error = status_message(status);
		}
	}
	_active.store(false);
}

bool sherbet::aim::net::take_board(board &out, bool &was_submit, bool &updated)
{
	std::lock_guard<std::mutex> lk(_mtx);
	if (!_has_board)
		return false;
	out = _board;
	was_submit = _was_submit;
	updated = _updated;
	// ⚠️ 이름 그대로 **가져가면 비운다.** 안 비우면 호출부가 매 프레임 참이라 벡터와
	//    문자열을 통째로 다시 복사한다 — 프레임마다 힙 할당이 여러 번 일어난다.
	//    화면에 띄울 사본은 호출부가 이미 들고 있으므로 여기 남겨 둘 이유가 없다.
	_has_board = false;
	return true;
}

bool sherbet::aim::net::take_error(std::string &out)
{
	std::lock_guard<std::mutex> lk(_mtx);
	if (_error.empty())
		return false;
	out = _error;
	_error.clear();
	return true;
}
