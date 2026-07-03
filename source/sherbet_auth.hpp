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
			const char *status_text() const;

		private:
			void join_worker();
			void save_cache_locked();

			std::string _config_dir;
			std::string _hwid;
			token_cache _cache;
			std::atomic<bool> _authed{ false };
			std::atomic<bool> _login_active{ false };
			std::atomic<bool> _worker_done{ false };
			std::thread _worker;
			mutable std::mutex _mtx;
			std::string _status;          // _mtx 보호
			bool _pending_save = false;   // _mtx 보호: tick()에서 캐시 파일 기록 트리거
			std::string _login_state;     // 진행 중 로그인의 state
		};
	}
}
