/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_adalarm.hpp 호스트 테스트.
// "2분마다 정확히 한 번 울린다" 는 빌드가 통과해도 확인되지 않는다. 여기서 못 박는다.
#include "sherbet_adalarm.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::adalarm;

// 60fps 로 흘려 보내며 울린 횟수를 센다.
static int run(state &st, double from, double to, int interval, bool enabled = true)
{
	int fired = 0;
	for (double t = from; t < to; t += 1.0 / 60.0)
		if (should_fire(st, t, interval, enabled))
			++fired;
	return fired;
}

// 켜는 순간에는 안 울리고, 한 간격이 지나서야 울린다.
static void test_does_not_fire_on_enable()
{
	state st;
	assert(!should_fire(st, 1000.0, 120, true));
	assert(seconds_left(st, 1000.0) == 120);
	assert(run(st, 1000.0, 1119.9, 120) == 0);
	assert(should_fire(st, 1120.0, 120, true));
}

// 10분이면 2분 간격으로 정확히 5번. 매 프레임 울리지 않는다.
static void test_fires_once_per_interval()
{
	state st;
	assert(run(st, 0.0, 600.0 + 0.5, 120) == 5);
}

// 간격을 next_due 에 더해 가므로 프레임 시간 오차가 쌓이지 않는다 —
// 프레임이 들쭉날쭉해도 1시간에 30번 그대로다.
static void test_no_drift_with_uneven_frames()
{
	state st;
	int fired = 0;
	double t = 0.0;
	for (int i = 0; t < 3600.0 + 0.5; ++i)
	{
		if (should_fire(st, t, 120, true))
			++fired;
		t += (i % 3 == 0) ? 0.05 : 0.007; // 20fps 와 144fps 가 섞인다
	}
	assert(fired == 30);
}

// 로딩 화면으로 10분 멈췄다 와도 한 번만 울리고, 거기서부터 다시 센다.
static void test_long_stall_fires_once()
{
	state st;
	assert(!should_fire(st, 0.0, 120, true));
	assert(should_fire(st, 600.0, 120, true));   // 밀린 5번을 몰아서 울리지 않는다
	assert(!should_fire(st, 600.1, 120, true));
	assert(seconds_left(st, 600.0) == 120);
	assert(should_fire(st, 720.0, 120, true));
}

// 껐다 켜면 옛 next_due 로 바로 울리지 않고 처음부터 센다.
static void test_disable_resets()
{
	state st;
	assert(!should_fire(st, 0.0, 120, true));
	assert(!should_fire(st, 50.0, 120, false));
	assert(seconds_left(st, 50.0) == -1);
	assert(!should_fire(st, 500.0, 120, true));   // 다시 켠 순간 — 안 울린다
	assert(seconds_left(st, 500.0) == 120);
}

// 「다시 세기」 — 광고를 막 올렸을 때 누르면 지금부터 한 간격.
static void test_restart()
{
	state st;
	assert(!should_fire(st, 0.0, 120, true));
	restart(st, 100.0, 120);
	assert(!should_fire(st, 119.0, 120, true));   // 원래라면 120 에 울렸다
	assert(should_fire(st, 220.0, 120, true));
}

// ini 를 손으로 0·음수·엄청 큰 값으로 고쳐도 매 프레임 울리지 않는다.
static void test_clamp_interval()
{
	assert(clamp_interval(0) == kMinInterval);
	assert(clamp_interval(-5) == kMinInterval);
	assert(clamp_interval(999999) == kMaxInterval);
	assert(clamp_interval(120) == 120);
	state st;
	assert(run(st, 0.0, 60.0, 0) == 5);           // 0 → 10초 간격, 1분에 5번(60.0 직전까지)
}

int main()
{
	test_does_not_fire_on_enable();
	test_fires_once_per_interval();
	test_no_drift_with_uneven_frames();
	test_long_stall_fires_once();
	test_disable_resets();
	test_restart();
	test_clamp_interval();
	std::puts("sherbet_adalarm_test: all passed");
	return 0;
}
