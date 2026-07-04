/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_auth.hpp"
#include "sherbet_content.hpp"
#include "sherbet_content_json.hpp" // content_download_targets
#include "sherbet_http.hpp"
#include "sherbet_owner.h"
#include "sherbet_nodelock.hpp" // hwid()
#include <Windows.h>
#include <shellapi.h>
#include <objbase.h> // CoInitializeEx/CoUninitialize
#include <fstream>
#include <filesystem>
#include <chrono>

namespace
{
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr wchar_t kStartPath[]  = L"/sherbet-auth/auth/start";
	constexpr wchar_t kVerifyPath[] = L"/sherbet-auth/auth/verify";
	const std::string kPollBase     = "/sherbet-auth/auth/poll?state=";

	long long now_unix()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	std::string json_escape(const std::string &s)
	{
		std::string o; o.reserve(s.size() + 2);
		for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
		return o;
	}
}

bool sherbet::auth::enabled()
{
	return SHERBET_ONLINE_AUTH != 0;
}

sherbet::auth::controller::controller() {}
sherbet::auth::controller::~controller() { _stop = true; join_worker(); join_content_worker(); }

void sherbet::auth::controller::join_worker()
{
	if (_worker.joinable()) _worker.join();
}

void sherbet::auth::controller::join_content_worker()
{
	if (_content_worker.joinable()) _content_worker.join();
}

void sherbet::auth::controller::save_cache_locked()
{
	const std::filesystem::path p = std::filesystem::u8path(_config_dir) / L"sherbet.auth";
	std::ofstream out(p, std::ios::trunc);
	if (out.is_open()) out << serialize_cache(_cache);
}

void sherbet::auth::controller::init(const std::string &config_dir_utf8)
{
	if (!enabled()) { _authed = true; return; } // 개발 빌드: 항상 통과

	_config_dir = config_dir_utf8;
	_hwid = sherbet::nodelock::hwid();

	// 캐시 로드
	const std::filesystem::path p = std::filesystem::u8path(_config_dir) / L"sherbet.auth";
	std::ifstream in(p);
	if (in.is_open()) {
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		parse_cache(text, _cache);
	}

	// 테마 캐시 로드 — 첫 프레임 take_content 로 레지스트리에 복원(오프라인/재시작 지속)
	{
		std::string cached;
		if (sherbet::content::load_cached(_config_dir, cached) && !cached.empty()) {
			std::lock_guard<std::mutex> lk(_mtx);
			_content_body = std::move(cached);
			_content_ready = true;
		}
	}

	// 시작 시 비동기 verify (토큰이 있을 때만)
	if (_cache.token.empty()) { _authed = false; return; }

	join_worker();
	_worker_done = false;
	_worker = std::thread([this]() {
		const std::string body = std::string("{\"token\":\"") + json_escape(_cache.token) +
			"\",\"hwid\":\"" + json_escape(_hwid) + "\"}";
		std::string resp;
		const int status = sherbet::http::post_json(kHost, kVerifyPath, body, resp, nullptr);
		const verify_result vr = parse_verify(resp, status);
		gate g;
		{
			std::lock_guard<std::mutex> lk(_mtx);
			g = decide(vr, _cache, now_unix(), 86400);
			if (vr.ok) { _cache.last_verified_unix = now_unix(); _pending_save = true; _owner_name = vr.name; }
		}
		_authed = (g == gate::authed);
		_worker_done = true;
	});
}

bool sherbet::auth::controller::is_authed() const
{
	if (!enabled()) return true;
	return _authed.load();
}

bool sherbet::auth::controller::login_active() const
{
	return _login_active.load();
}

std::string sherbet::auth::controller::status_text() const
{
	std::lock_guard<std::mutex> lk(_mtx);
	return _status; // 락 안에서 값 복사 — 반환 후 워커가 _status를 바꿔도 안전
}

void sherbet::auth::controller::tick()
{
	if (!enabled()) return;

	// 완료된 워커 스레드 정리 + 지연된 캐시 저장 반영
	if (_worker_done.load()) {
		join_worker();
		_worker_done = false;
		std::lock_guard<std::mutex> lk(_mtx);
		if (_pending_save) { save_cache_locked(); _pending_save = false; }
	}
}

void sherbet::auth::controller::begin_login()
{
	if (!enabled()) return;
	if (_login_active.load()) return;      // 이미 진행 중
	if (_worker.joinable() && !_worker_done.load()) return; // 시작 verify 진행 중

	_login_active = true;
	{
		std::lock_guard<std::mutex> lk(_mtx);
		_status = "\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xEC\xA4\x80\xEB\xB9\x84\xEC\xA4\x91\xE2\x80\xA6"; // "로그인 준비중…"
	}

	join_worker();
	_worker_done = false;
	_worker = std::thread([this]() {
		// 1) start
		const std::string sbody = std::string("{\"hwid\":\"") + json_escape(_hwid) + "\"}";
		std::string sresp;
		const int sstatus = sherbet::http::post_json(kHost, kStartPath, sbody, sresp, nullptr);
		const start_result sr = parse_start(sresp);
		if (sstatus == 0 || !sr.ok) {
			{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEC\x97\xB0\xEA\xB2\xB0 \xEC\x8B\xA4\xED\x8C\xA8"; } // "연결 실패"
			_login_active = false; _worker_done = true; return;
		}
		// 2) 브라우저 열기
		{
			std::wstring wurl(sr.authorize_url.begin(), sr.authorize_url.end()); // URL은 ASCII
			CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			CoUninitialize();
		}
		{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEB\xA1\x9C\xEA\xB7\xB8\xEC\x9D\xB8 \xEB\x8C\x80\xEA\xB8\xB0\xEC\xA4\x91\xE2\x80\xA6"; } // "로그인 대기중…"

		// 3) 폴링(최대 5분, 2초 간격)
		const std::string poll_path = kPollBase + sr.state;
		const std::wstring wpoll(poll_path.begin(), poll_path.end());
		for (int i = 0; i < 150; ++i) {
			if (_stop.load()) { _login_active = false; _worker_done = true; return; }
			std::string presp;
			const int pstatus = sherbet::http::get(kHost, wpoll.c_str(), presp, nullptr);
			if (pstatus == 200) {
				const poll_result pr = parse_poll(presp);
				if (pr.status == poll_result::ready) {
					std::lock_guard<std::mutex> lk(_mtx);
					_cache.token = pr.token;
					_cache.hwid = _hwid;
					_cache.last_verified_unix = now_unix();
					_pending_save = true;
					_owner_name = pr.name;
					_status.clear();
					_authed = true;
					_login_active = false; _worker_done = true; return;
				}
				if (pr.status == poll_result::denied) {
					std::lock_guard<std::mutex> lk(_mtx);
					_status = "\xEA\xB5\xAC\xEB\xA7\xA4\xEC\x9E\x90 \xEC\x97\xAD\xED\x95\xA0\xEC\x9D\xB4 \xEC\x97\x86\xEC\x8A\xB5\xEB\x8B\x88\xEB\x8B\xA4"; // "구매자 역할이 없습니다"
					_authed = false;
					_login_active = false; _worker_done = true; return;
				}
			}
			// 2초 대기를 100ms 슬라이스로 나눠 취소에 빠르게 반응
			for (int s = 0; s < 20 && !_stop.load(); ++s)
				Sleep(100);
		}
		{ std::lock_guard<std::mutex> lk(_mtx); _status = "\xEC\x8B\x9C\xEA\xB0\x84 \xEC\xB4\x88\xEA\xB3\xBC"; } // "시간 초과"
		_login_active = false; _worker_done = true;
	});
}

std::string sherbet::auth::controller::token() const
{
	std::lock_guard<std::mutex> lk(_mtx);
	return _cache.token;
}

std::string sherbet::auth::controller::owner_name() const
{
	std::lock_guard<std::mutex> lk(_mtx);
	return _owner_name;
}

std::string sherbet::auth::effective_owner_name(const controller &c)
{
	if (enabled()) {
		std::string dynamic = c.owner_name();
		if (!dynamic.empty()) return dynamic; // 서버가 릴레이한 로그인 표시이름 우선
	}
	if (sherbet::has_owner()) return SHERBET_OWNER; // 오프라인/컴파일 타임 각인 폴백
	return "";
}

bool sherbet::auth::controller::content_active() const
{
	return _content_active.load();
}

bool sherbet::auth::controller::take_content(std::string &out)
{
	if (!_content_ready.load())
		return false;
	std::lock_guard<std::mutex> lk(_mtx);
	if (_content_body.empty()) { _content_ready = false; return false; }
	out = std::move(_content_body);
	_content_body.clear();
	_content_ready = false;
	return true;
}

void sherbet::auth::controller::begin_fetch_content()
{
	if (!enabled()) return;
	if (!_authed.load()) return;                 // 인증된 상태에서만
	if (_content_active.exchange(true)) return;  // 이미 페치 중

	std::string bearer;
	{ std::lock_guard<std::mutex> lk(_mtx); bearer = _cache.token; }
	if (bearer.empty()) { _content_active = false; return; }

	join_content_worker();
	_content_worker = std::thread([this, bearer]() {
		std::string body;
		if (sherbet::content::fetch(bearer, _config_dir, body) && !body.empty()) {
			// 프리셋/이펙트 파일 다운로드(순수 계산으로 대상 산출 → 각 다운로드)
			const auto targets = sherbet::content_download_targets(
				body, _config_dir + "/Sherbet-Presets", _config_dir + "/Sherbet-Fx");
			for (const auto &t : targets) {
				if (_stop.load()) break;
				// 이미 있는 파일은 덮어쓰지 않는다 — 사용자가 조정한 프리셋/설정값을 보존하고
				// 새로 권한 생긴 콘텐츠만 받는다. (기본값으로 리셋되는 문제 방지)
				std::error_code ec;
				if (std::filesystem::exists(std::filesystem::u8path(t.dest_path), ec))
					continue;
				sherbet::content::fetch_file(bearer, t.id, t.dest_path); // 실패는 조용히 스킵
			}
			{
				std::lock_guard<std::mutex> lk(_mtx);
				_content_body = std::move(body);
				_content_ready = true;
			}
			_content_files_changed = true; // 렌더 스레드가 검색경로+reload
		}
		_content_active = false;
	});
}

bool sherbet::auth::controller::take_files_changed()
{
	return _content_files_changed.exchange(false);
}
