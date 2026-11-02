/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 광고알리미 — 정한 간격마다 울리는 판정 순수 로직.
//
// 광고 채팅은 서버가 쿨타임(시바 2분)을 걸어 둔다. 쿨타임이 끝날 때마다 화면에 알려 준다.
// 표시(남은 시간·페이드)는 일일 알림의 alarm::state 를 그대로 쓴다 — 그리는 길이 같다.
//
// 시각은 호출자가 넣어 준다(단조 시계, 초). 그래야 테스트가 임의의 시각을 먹일 수 있다.
#pragma once

#include "sherbet_alarm.hpp"
#include <cmath>

namespace sherbet
{
	namespace adalarm
	{
		constexpr int kMinInterval = 10;      // 초. 이보다 짧으면 알림이 아니라 깜빡임이다
		constexpr int kMaxInterval = 60 * 60; // 1시간
		constexpr int kDefaultInterval = 120; // 시바 광고 쿨타임 2분

		// 손으로 고친 ini 가 0 이나 음수여도 매 프레임 울리는 상태가 되지 않게 여기서 막는다.
		inline int clamp_interval(int seconds)
		{
			return seconds < kMinInterval ? kMinInterval : (seconds > kMaxInterval ? kMaxInterval : seconds);
		}

		// 세션 상태. config 로 왕복하지 않는다 — 게임을 켤 때마다 처음부터 센다.
		struct state
		{
			// 다음에 울릴 시각(단조 시계, 초). 음수 = 아직 세기 전.
			double next_due = -1.0;
			// 화면 표시 상태(남은 시간·페이드). 일일 알림과 같은 것을 쓴다.
			alarm::state show;
		};

		// 지금부터 다시 센다. 광고를 막 올렸을 때 누르는 「다시 세기」, 그리고 켜는 순간에 쓴다.
		inline void restart(state &st, double now, int interval)
		{
			st.next_due = now + clamp_interval(interval);
		}

		// 이번 프레임에 알림을 새로 띄워야 하는가.
		//
		// 규칙:
		//  * **켜는 순간에는 울리지 않는다.** 그때부터 한 간격을 센다.
		//  * 간격은 next_due 에 더해 간다 — 프레임 시간 오차가 쌓여 점점 늦어지지 않는다.
		//  * 로딩 화면 등으로 프레임이 오래 멈췄다 와도 **밀린 만큼 몰아서 울리지 않는다.**
		//    한 번만 울리고 지금부터 다시 센다.
		//  * 꺼져 있으면 상태를 지운다 — 다시 켰을 때 옛 next_due 로 바로 울리지 않게.
		inline bool should_fire(state &st, double now, int interval, bool enabled)
		{
			if (!enabled)
			{
				st.next_due = -1.0;
				return false;
			}

			interval = clamp_interval(interval);
			if (st.next_due < 0.0)
			{
				st.next_due = now + interval;
				return false;
			}
			if (now < st.next_due)
				return false;

			st.next_due += interval;
			if (st.next_due <= now)
				st.next_due = now + interval;
			return true;
		}

		// 다음 알림까지 남은 초(올림). 세기 전이면 -1.
		inline int seconds_left(const state &st, double now)
		{
			if (st.next_due < 0.0)
				return -1;
			const double d = st.next_due - now;
			return d <= 0.0 ? 0 : static_cast<int>(std::ceil(d));
		}
	}
}
