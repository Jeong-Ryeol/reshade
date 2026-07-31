/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_alarm.hpp 호스트 테스트 — 맥/리눅스 clang 으로 그대로 돈다.
// "밤 11시 50분에 정확히 한 번 울린다" 는 CI 빌드가 통과해도 확인되지 않는다.
// 그래서 판정을 순수 로직으로 빼고 여기서 못 박는다.
#include "sherbet_alarm.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::alarm;

static const int kTarget = minute_of_day(23, 50); // 23:50

static void test_minute_of_day()
{
	assert(minute_of_day(0, 0) == 0);
	assert(minute_of_day(23, 50) == 1430);
	assert(minute_of_day(23, 59) == 1439);
}

// 목표 시각의 그 1분 동안 **딱 한 번만** 울린다.
// (60fps 면 그 1분에 3600 프레임이 지나간다 — 매 프레임 울리면 재앙이다.)
static void test_fires_exactly_once_within_the_minute()
{
	state st;
	int fired = 0;
	for (int frame = 0; frame < 3600; ++frame)
		if (should_fire(st, kTarget, kTarget, true))
			++fired;
	assert(fired == 1);
}

// 창을 벗어났다 다음 날 다시 들어오면 또 울린다(자정을 넘겨 계속 플레이하는 경우).
static void test_fires_again_next_day()
{
	state st;
	assert(should_fire(st, kTarget, kTarget, true));       // 1일차 23:50
	assert(!should_fire(st, kTarget, kTarget, true));      // 같은 분 — 안 울림
	assert(!should_fire(st, minute_of_day(23, 51), kTarget, true)); // 창 밖
	assert(!should_fire(st, 0, kTarget, true));            // 자정
	assert(!should_fire(st, minute_of_day(12, 0), kTarget, true));  // 낮
	assert(should_fire(st, kTarget, kTarget, true));       // 2일차 23:50 — 다시 울린다
}

// 지나간 시각은 울리지 않는다. 23:51 에 게임을 켰다고 알림이 뜨면,
// 켤 때마다 뜨는 소음이 된다.
static void test_does_not_fire_for_past_time()
{
	state st;
	assert(!should_fire(st, minute_of_day(23, 51), kTarget, true));
	assert(!should_fire(st, minute_of_day(23, 59), kTarget, true));
	assert(!should_fire(st, minute_of_day(0, 5), kTarget, true));
}

// 꺼져 있으면 안 울린다. 그리고 **켜는 순간 그날 알림을 삼키지 않는다** —
// 꺼진 동안 창 상태를 안 따라가면 fired_in_window 가 남아 그날을 통째로 잃는다.
static void test_disabled_then_enabled_inside_window()
{
	state st;
	assert(!should_fire(st, kTarget, kTarget, false)); // 꺼진 채로 23:50 진입
	assert(!should_fire(st, kTarget, kTarget, false));
	assert(should_fire(st, kTarget, kTarget, true));   // 그 분 안에서 켰다 → 울려야 한다
}

// 시스템 시각이 뒤로 점프해도(NTP 보정/수동 변경) 영영 안 울리는 상태에 빠지지 않는다.
static void test_clock_jump_backwards_does_not_wedge()
{
	state st;
	assert(should_fire(st, kTarget, kTarget, true));
	assert(!should_fire(st, minute_of_day(23, 55), kTarget, true));
	// 시각이 23:50 으로 되돌아감 — 한 번 더 울릴 뿐, 잠기지 않는다
	assert(should_fire(st, kTarget, kTarget, true));
	assert(!should_fire(st, kTarget, kTarget, true));
}

// 자정 목표(00:00)도 동작한다 — 경계값이라 따로 본다.
static void test_midnight_target()
{
	state st;
	const int midnight = minute_of_day(0, 0);
	assert(!should_fire(st, minute_of_day(23, 59), midnight, true));
	assert(should_fire(st, midnight, midnight, true));
	assert(!should_fire(st, midnight, midnight, true));
	assert(!should_fire(st, minute_of_day(0, 1), midnight, true));
}

static void test_visible_timer_and_fade()
{
	state st;
	st.remaining = 3.0f;
	assert(tick_visible(st, 1.0f));            // 2.0 남음
	assert(fade_alpha(st) == 1.0f);            // 아직 불투명
	assert(tick_visible(st, 1.5f));            // 0.5 남음
	assert(fade_alpha(st) > 0.4f && fade_alpha(st) < 0.6f); // 페이드 중
	assert(!tick_visible(st, 1.0f));           // 0 — 사라짐
	assert(fade_alpha(st) == 0.0f);
	assert(!tick_visible(st, 1.0f));           // 이미 0 이면 계속 false(음수로 안 내려간다)
	assert(st.remaining == 0.0f);
}

// 손으로 고친 ini 가 23:99 여도 '절대 안 울리는 상태' 가 되면 안 된다.
static void test_clamp_time()
{
	int h = 23, m = 99; clamp_time(h, m); assert(h == 23 && m == 59);
	h = -3; m = -7;     clamp_time(h, m); assert(h == 0 && m == 0);
	h = 99; m = 30;     clamp_time(h, m); assert(h == 23 && m == 30);
	h = 11; m = 50;     clamp_time(h, m); assert(h == 11 && m == 50); // 정상값은 그대로
}

// 시각 조회 스로틀 — 첫 프레임엔 바로 보고, 그 뒤로는 1초에 한 번.
static void test_check_throttle()
{
	state st;
	assert(due_for_check(st, 0.0f));       // 첫 프레임은 즉시(check_accum 이 1.0 에서 시작)
	assert(!due_for_check(st, 0.5f));
	assert(!due_for_check(st, 0.4f));
	assert(due_for_check(st, 0.2f));       // 누적 1.1 → 통과하고 리셋
	assert(!due_for_check(st, 0.9f));
}

int main()
{
	test_minute_of_day();
	test_fires_exactly_once_within_the_minute();
	test_fires_again_next_day();
	test_does_not_fire_for_past_time();
	test_disabled_then_enabled_inside_window();
	test_clock_jump_backwards_does_not_wedge();
	test_midnight_target();
	test_visible_timer_and_fade();
	test_clamp_time();
	test_check_throttle();
	std::puts("sherbet_alarm_test: ALL PASS");
	return 0;
}
