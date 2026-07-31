/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 일일 알림 — 시각 판정 순수 로직.
//
// 왜 순수 헤더인가: 맥에서 runtime_gui.cpp 를 컴파일할 수 없어 CI 가 유일한 검증인데,
// "밤 11시 50분에 정확히 한 번 울린다" 는 CI 로도 확인할 수 없다(빌드가 통과해도
// 로직이 틀렸는지 알 수 없다). 판정을 여기로 빼면 tools/sherbet_alarm_test.cpp 가
// 맥에서 자정 경계·시각 점프·중복 발화를 전부 단언으로 못 박는다.
//
// 시각은 호출자가 넣어 준다(여기서 localtime 을 부르지 않는다) — 그래야 테스트가
// 임의의 시각을 먹일 수 있다.
#pragma once

#include <string>

namespace sherbet
{
	namespace alarm
	{
		// 하루 중 몇 번째 분인가(0~1439). 자정=0.
		inline int minute_of_day(int hour, int minute)
		{
			return hour * 60 + minute;
		}

		// 알림 상태. config 로 왕복하지 않는다 — 세션 상태다.
		// 게임을 껐다 켜면 그날 이미 울렸는지 잊는다. 그게 낫다:
		// 기억하려면 '마지막 발화 날짜' 를 디스크에 써야 하는데, 그 대가로
		// 얻는 것(하루 두 번 볼 수도 있음)보다 잃는 것(한 번도 못 봄)이 크다.
		struct state
		{
			// 마지막으로 판정한 '하루 중 분'. -1 = 아직 한 번도 안 봄.
			int last_minute = -1;
			// 이번 '발화 창' 에서 이미 울렸는가. 창을 벗어나면 풀린다.
			bool fired_in_window = false;
			// 남은 표시 시간(초). 0 이면 안 보인다.
			float remaining = 0.0f;
			// 시각 조회 스로틀. 1.0 에서 시작해 **첫 프레임에 바로 한 번** 보게 한다.
			float check_accum = 1.0f;
		};

		// 이번 프레임에 알림을 새로 띄워야 하는가.
		//
		//   now      = 지금의 '하루 중 분'(0~1439)
		//   target   = 알림 시각의 '하루 중 분'
		//   enabled  = 옵션이 켜져 있는가
		//
		// 규칙:
		//  * **정확히 그 분에만** 울린다. 23:50 에 진입한 1분 동안 매 프레임 울리면 안 되므로
		//    fired_in_window 로 한 번만 통과시킨다.
		//  * 그 분을 벗어나면 잠금이 풀려 다음 날 다시 울린다(자정을 넘겨 계속 플레이해도 동작).
		//  * **지나간 시각은 울리지 않는다.** 23:51 에 게임을 켰다고 "11:50 이었어요" 라고
		//    띄우면 매번 켤 때마다 뜬다 — 알림이 아니라 소음이 된다.
		//  * 시스템 시각이 뒤로 점프해도(NTP 보정/수동 변경) 창 밖으로 나갔다 들어오는 것으로
		//    처리되어 최악의 경우 한 번 더 울릴 뿐, 영영 안 울리는 상태에 빠지지 않는다.
		inline bool should_fire(state &st, int now, int target, bool enabled)
		{
			if (!enabled)
			{
				// 꺼져 있는 동안에도 창 상태는 따라가야 한다 — 안 그러면 23:50 에 켜자마자
				// 옛 fired_in_window 가 남아 그날 알림을 삼킨다.
				st.last_minute = now;
				st.fired_in_window = false;
				return false;
			}

			const bool in_window = (now == target);
			if (!in_window)
			{
				st.last_minute = now;
				st.fired_in_window = false; // 창을 벗어났다 — 다음 진입 때 다시 울릴 수 있다
				return false;
			}

			st.last_minute = now;
			if (st.fired_in_window)
				return false; // 이 창에서는 이미 울렸다

			st.fired_in_window = true;
			return true;
		}

		// 시각을 다시 볼 때가 됐는가. 판정이 분 단위이므로 1초에 한 번이면 충분하다 —
		// 매 프레임 localtime 을 부르면 144fps 에서 초당 144번 타임존 변환을 한다.
		inline bool due_for_check(state &st, float delta_seconds)
		{
			st.check_accum += delta_seconds;
			if (st.check_accum < 1.0f)
				return false;
			st.check_accum = 0.0f;
			return true;
		}

		// 표시 타이머를 흘려보낸다. 남은 시간이 있으면 true(그려야 함).
		inline bool tick_visible(state &st, float delta_seconds)
		{
			if (st.remaining <= 0.0f)
				return false;
			st.remaining -= delta_seconds;
			if (st.remaining < 0.0f)
				st.remaining = 0.0f;
			return st.remaining > 0.0f;
		}

		// 0.0~1.0 알파. 마지막 1초 동안 부드럽게 사라진다.
		inline float fade_alpha(const state &st)
		{
			return st.remaining >= 1.0f ? 1.0f : (st.remaining > 0.0f ? st.remaining : 0.0f);
		}

		// 시/분을 config 에 넣기 좋은 범위로 자른다. 손으로 고친 ini 가 23:99 여도
		// 절대 울리지 않는 상태(=고장)가 되지 않도록 여기서 막는다.
		inline void clamp_time(int &hour, int &minute)
		{
			if (hour < 0) hour = 0; else if (hour > 23) hour = 23;
			if (minute < 0) minute = 0; else if (minute > 59) minute = 59;
		}
	}
}
