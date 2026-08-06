/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 사격 훈련 리더보드 — 네트워크 글루.
//
// 파싱은 sherbet_aim_board.hpp(순수, 호스트 테스트), HTTP 는 sherbet_http, 여기는
// 그 둘을 스레드에 얹는 얇은 층이다.
//
// ⚠️ **렌더 스레드에서 HTTP 를 부르면 안 된다.** 타임아웃이 5초라 서버가 죽으면
//    게임이 5초 멈춘다. 그래서 워커 스레드로 돌린다.
// ⚠️ 워커는 **조인 가능**해야 한다. detach 하면 런타임이 파괴되는 중에 워커가
//    파괴된 뮤텍스를 만진다 — 이 프로젝트가 콘텐츠 페치에서 실제로 겪은 UAF 다.
//    소멸자가 반드시 join 한다.
#pragma once

#include "sherbet_aim.hpp"
#include "sherbet_aim_board.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace sherbet
{
	namespace aim
	{
		// 서버가 아는 이름. sherbet_aim.hpp 의 enum 과 server/app/aim.py 의 LEVELS·DURATIONS
		// **셋이 같아야 한다.** 어긋나면 기록이 엉뚱한 표에 들어가거나 통째로 거부된다.
		const char *level_key(level lv);
		const char *duration_key(duration d);

		class net
		{
		public:
			net() = default;
			~net() { join(); }
			net(const net &) = delete;
			net &operator=(const net &) = delete;

			// 리더보드 조회를 시작한다. 이미 도는 중이면 무시한다(연타 방지).
			void begin_fetch(const std::string &bearer, level lv, duration d);
			// 기록 제출을 시작한다. 응답에 갱신된 표가 같이 오므로 따로 조회하지 않는다.
			void begin_submit(const std::string &bearer, level lv, duration d, int hits, int shots);

			bool active() const { return _active.load(); }

			// 마지막으로 성공한 표. 아직 없으면 false.
			bool take_board(board &out, bool &was_submit, bool &updated);
			// 마지막 시도가 실패했는가(네트워크·인증·거부). 읽으면 지워진다.
			bool take_error(std::string &out);

			void join()
			{
				if (_worker.joinable())
					_worker.join();
			}

		private:
			void run(std::string bearer, level lv, duration d, bool submit, int hits, int shots);

			std::thread _worker;
			std::atomic<bool> _active { false };
			mutable std::mutex _mtx;
			board _board;
			bool _has_board = false;
			bool _was_submit = false;
			bool _updated = false;
			std::string _error;
		};
	}
}
