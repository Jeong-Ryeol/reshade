/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_frametime.hpp 호스트 단위테스트 (게임 중 프레임 통계 + 상한 진단).
// ⚠️ -DNDEBUG 금지 — 전부 assert 로 검증한다.
//
// 검사하는 것은 내부값이 아니라 **사용자가 보게 될 판정**이다:
//   - 수직동기 144 에 붙은 프레임은 "수직동기" 로, 그 절반은 "1/2" 로 갈린다
//   - 수직동기가 꺼진 71fps 고정(프레임 생성 142÷2)은 "다른 상한" 이다
//   - GPU 바운드 72fps 는 144 의 절반이어도 **상한이 아니다** — 이게 이 파일의 존재 이유

#include "sherbet_frametime.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace sherbet::frametime;

static bool feq(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// 상한에 묶인 프레임 — 목표치 주위로 ±0.03ms 만 흔들린다(결정론적).
static void fill_flat(ring &r, float ms, std::size_t n)
{
	for (std::size_t i = 0; i < n; ++i)
		r.push(ms + static_cast<float>(static_cast<int>(i % 7) - 3) * 0.01f);
}

// GPU 바운드 — 같은 평균이지만 부하에 따라 ±3ms 가까이 움직인다.
static void fill_noisy(ring &r, float center_ms, std::size_t n)
{
	for (std::size_t i = 0; i < n; ++i)
		r.push(center_ms + static_cast<float>(static_cast<int>(i % 20) - 10) * 0.3f);
}

static void test_ring_basic()
{
	ring r;
	assert(r.size() == 0);
	r.push(7.0f);
	r.push(7.5f);
	assert(r.size() == 2);
	std::vector<float> v;
	r.copy_to(v);
	assert(v.size() == 2 && v[0] == 7.0f && v[1] == 7.5f);

	r.clear();
	assert(r.size() == 0);

	// 용량을 넘겨도 크기는 용량에서 멈추고, 복사본도 딱 그만큼이다.
	for (std::size_t i = 0; i < kCapacity + 100; ++i)
		r.push(1.0f + static_cast<float>(i % 3));
	assert(r.size() == kCapacity);
	r.copy_to(v);
	assert(v.size() == kCapacity);
}

static void test_ring_rejects_garbage()
{
	// 0·음수·10초 초과·NaN 은 시계 점프거나 첫 프레임이다. 넣으면 최악값이 거짓말을 한다.
	ring r;
	r.push(0.0f);
	r.push(-3.0f);
	r.push(20000.0f);
	r.push(std::numeric_limits<float>::quiet_NaN());
	assert(r.size() == 0);
	r.push(0.001f);
	assert(r.size() == 1);
}

static void test_insufficient_samples()
{
	ring r;
	fill_flat(r, 6.944f, kMinFrames - 1);
	stats st;
	assert(!compute(r, st));
	assert(st.frames == kMinFrames - 1); // "지금까지 N초 · M프레임" 을 보여주려면 이 둘은 채워져야 한다
	assert(feq(st.seconds, 6.944f * (kMinFrames - 1) / 1000.0f, 0.05f));
	assert(diagnose(st, 144, 1).verdict == cap::insufficient);

	// ★ 프레임 수만 넘겨선 안 된다. 144fps 에서 600프레임은 4초 — 접속 직후 히치 몇 개가
	//   1% low 를 통째로 정한다(실기에서 4.5초 표본에 41fps 가 찍혔다). 10초가 차야 낸다.
	r.push(6.944f);
	assert(st.frames + 1 == kMinFrames);
	assert(!compute(r, st));
	assert(st.frames == kMinFrames);
	assert(st.seconds < kMinSeconds);

	while (st.seconds < kMinSeconds)
	{
		r.push(6.944f);
		compute(r, st);
	}
	assert(compute(r, st));
	assert(st.frames >= kMinFrames && st.seconds >= kMinSeconds);

	// 반대로 60fps 에서는 10초 ≈ 600프레임이라 둘이 거의 같이 찬다(지터 때문에 정확히 600 은 9.99초).
	ring slow;
	fill_flat(slow, 1000.0f / 60.0f, kMinFrames + 10);
	assert(compute(slow, st));
	assert(st.seconds >= kMinSeconds && st.seconds < kMinSeconds + 0.5f);
}

static void test_stats_known_distribution()
{
	// 10ms 1000개 + 30ms 10개. 손으로 계산되는 값들.
	ring r;
	for (int i = 0; i < 1000; ++i) r.push(10.0f);
	for (int i = 0; i < 10; ++i) r.push(30.0f);
	stats st;
	assert(compute(r, st));
	assert(st.frames == 1010);
	assert(feq(st.seconds, 10.3f, 1e-3f));           // 10000 + 300 ms
	assert(feq(st.avg_fps, 1010.0f / 10.3f, 1e-2f));  // ≈ 98.06
	assert(feq(st.median_ms, 10.0f, 1e-6f));
	assert(feq(st.worst_ms, 30.0f, 1e-6f));
	assert(st.stutters == 10);                         // 30 > 10×2
	// 가장 느린 1% = 10개 = 전부 30ms → 33.3 fps. 평균(98)보다 한참 낮다 —
	// 이게 "평균은 멀쩡한데 끊긴다" 를 숫자로 보여주는 방식이다.
	assert(feq(st.low1_fps, 1000.0f / 30.0f, 1e-2f));
	assert(feq(st.iqr_ratio, 0.0f, 1e-6f));
}

static void test_vsync_full_refresh()
{
	ring r;
	fill_flat(r, 1000.0f / 144.0f, 2000);
	stats st;
	assert(compute(r, st));
	assert(st.iqr_ratio < kFlatIqr);
	const diagnosis d = diagnose(st, 144, 1);
	assert(d.verdict == cap::vsync);
	assert(d.divisor == 1);
	assert(feq(d.target_fps, 144.0f, 1e-3f));
	assert(feq(d.fps, 144.0f, 1.0f));
}

static void test_vsync_halved()
{
	// 판매자가 처음 본 증상: 144Hz 모니터, 수직동기 켜짐, 게임이 144 를 못 지켜 72 로 반토막.
	ring r;
	fill_flat(r, 1000.0f / 72.0f, 2000);
	stats st;
	assert(compute(r, st));
	const diagnosis d = diagnose(st, 144, 1);
	assert(d.verdict == cap::vsync_divided);
	assert(d.divisor == 2);
	assert(feq(d.target_fps, 72.0f, 1e-3f));

	// SyncInterval 2 면 주사율의 절반이 '정상' 이고, 그 절반(36)이 나눠진 것이다.
	assert(diagnose(st, 144, 2).verdict == cap::vsync);
	ring r36;
	fill_flat(r36, 1000.0f / 36.0f, 2000);
	assert(compute(r36, st));
	assert(diagnose(st, 144, 2).verdict == cap::vsync_divided);
	assert(diagnose(st, 144, 2).divisor == 2);
}

static void test_other_cap_when_vsync_off()
{
	// 판매자의 진짜 원인: 수직동기 **꺼짐**, 프레임 생성 목표치 142÷2 = 71 고정.
	// 71 은 144 의 절반(72)에서 1.4% 떨어져 있어 근접 판정에는 들지만,
	// SyncInterval 이 0 이므로 수직동기라고 말하면 안 된다.
	ring r;
	fill_flat(r, 1000.0f / 71.0f, 2000);
	stats st;
	assert(compute(r, st));
	assert(diagnose(st, 144, 0).verdict == cap::other);
	// SyncInterval 을 모르는 백엔드(비-DXGI)도 "수직동기" 라고 단정하지 않는다.
	assert(diagnose(st, 144, kSyncUnknown).verdict == cap::other);
	// 주사율을 몰라도 판정은 같다 — 평평함과 SyncInterval 만으로 결정된다.
	assert(diagnose(st, 0, 0).verdict == cap::other);
}

static void test_gpu_bound_is_not_a_cap()
{
	// ★ 이 파일의 존재 이유. 평균 72fps 인 GPU 바운드 — 144 의 절반이지만 프레임이
	//   부하에 따라 흔들린다. "수직동기 반토막" 이라고 하면 오진이다.
	ring r;
	fill_noisy(r, 1000.0f / 72.0f, 2000);
	stats st;
	assert(compute(r, st));
	assert(feq(st.avg_fps, 72.0f, 1.5f)); // 평균은 정말로 72 근처다
	assert(st.iqr_ratio >= kFlatIqr);     // 그런데 평평하지 않다
	assert(diagnose(st, 144, 1).verdict == cap::none);
	assert(diagnose(st, 144, 0).verdict == cap::none);
}

static void test_vsync_on_but_not_matching_refresh()
{
	// 수직동기가 켜져 있고 평평한데 120 — 144 의 1/n 이 아니다.
	// 드라이버 프레임 제한이 더 낮게 걸린 경우. "수직동기 때문" 이라고 하면 틀린다.
	ring r;
	fill_flat(r, 1000.0f / 120.0f, 2000);
	stats st;
	assert(compute(r, st));
	assert(diagnose(st, 144, 1).verdict == cap::vsync_other);
	// 주사율을 못 읽었을 때도 같은 칸 — 확인을 못 했다는 뜻이다.
	assert(diagnose(st, 0, 1).verdict == cap::vsync_other);
}

static void test_near_tolerance()
{
	assert(near(143.0f, 144.0f));   // 0.7% 아래
	assert(near(147.0f, 144.0f));   // 2.1% 위
	assert(!near(139.0f, 144.0f));  // 3.5% 아래 — 밖
	assert(!near(150.0f, 144.0f));  // 4.2% 위 — 밖
	assert(!near(100.0f, 0.0f));    // 목표치 0 은 항상 거짓
}

static void test_divisor_limit()
{
	// 1/4 까지만 수직동기 나눔으로 본다. 그 아래(1/5=28.8)는 다른 상한이다.
	ring r;
	fill_flat(r, 1000.0f / 36.0f, 2000); // 1/4
	stats st;
	assert(compute(r, st));
	assert(diagnose(st, 144, 1).verdict == cap::vsync_divided);
	assert(diagnose(st, 144, 1).divisor == 4);

	ring r5;
	fill_flat(r5, 1000.0f / 28.8f, 2000); // 1/5
	assert(compute(r5, st));
	assert(diagnose(st, 144, 1).verdict == cap::vsync_other);
}

int main()
{
	test_ring_basic();
	test_ring_rejects_garbage();
	test_insufficient_samples();
	test_stats_known_distribution();
	test_vsync_full_refresh();
	test_vsync_halved();
	test_other_cap_when_vsync_off();
	test_gpu_bound_is_not_a_cap();
	test_vsync_on_but_not_matching_refresh();
	test_near_tolerance();
	test_divisor_limit();
	std::printf("sherbet_frametime_test: ALL PASS\n");
	return 0;
}
