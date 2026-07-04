/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include "sherbet_auth_core.hpp"
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

namespace sherbet
{
	namespace auth
	{
		bool enabled(); // SHERBET_ONLINE_AUTH != 0

		class controller;

		// 화면에 각인할 소유자 이름: 온라인 인증이 켜져 있고 서버가 표시이름을 릴레이했으면 그 이름,
		// 아니면 컴파일 타임 SHERBET_OWNER(설정돼 있으면), 둘 다 없으면 빈 문자열.
		std::string effective_owner_name(const controller &c);

		class controller
		{
		public:
			controller();
			~controller();

			void init(const std::string &config_dir_utf8);
			bool is_authed() const;
			void tick();
			void begin_login();
			bool login_active() const;
			std::string status_text() const;
			std::string token() const;
			std::string owner_name() const; // 서버가 릴레이한 디스코드 표시이름(공용 DLL 각인용). 미인증/미수신이면 빈 문자열.
			void begin_fetch_content();
			bool content_active() const; // /content/me 페치가 진행 중이면 true (버튼 로딩 표시·연타 방지용)
			bool take_content(std::string &out);
			bool take_files_changed();

		private:
			void join_worker();
			void join_content_worker();
			void save_cache_locked();

			std::string _config_dir;
			std::string _hwid;
			token_cache _cache;           // _mtx 보호
			std::atomic<bool> _authed{ false };
			std::atomic<bool> _login_active{ false };
			std::atomic<bool> _worker_done{ false };
			std::atomic<bool> _stop{ false }; // 파괴 시 워커(폴링 루프) 조기 종료 신호
			std::thread _worker;
			std::thread _content_worker;
			mutable std::mutex _mtx;
			std::string _status;          // _mtx 보호
			std::string _owner_name;      // _mtx 보호: 서버 릴레이 표시이름
			bool _pending_save = false;   // _mtx 보호: tick()에서 캐시 파일 기록 트리거
			std::string _content_body;               // _mtx 보호: 페치/캐시된 /content/me 바디
			std::atomic<bool> _content_ready{ false };// 렌더 스레드가 take_content 로 인출
			std::atomic<bool> _content_active{ false };// 페치 워커 진행 중 재진입 방지
			std::atomic<bool> _content_files_changed{ false }; // 파일 새로 받음 → 렌더 스레드가 reload
		};
	}
}
