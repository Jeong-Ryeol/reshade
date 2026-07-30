/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// ⚠️ -DNDEBUG 를 붙이면 아래 단언이 전부 사라져 무의미하게 통과한다. 절대 붙이지 말 것.
#include "sherbet_spray.hpp"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace sherbet::spray;

// 오프셋은 정수를 float 에 누적한 값이라 사실 정확히 일치하지만,
// 시간은 dt 를 float 로 더한 값이라 오차가 있으므로 전부 근사 비교로 통일한다.
static bool near_eq(float a, float b, float eps = 1e-4f)
{
	return (a - b) < eps && (b - a) < eps;
}

// 60fps 한 프레임
static const float kFrame = 0.016f;

// 이동만 하고 한 발도 쏘지 않으면 구간이 아예 생기지 않는다.
static void test_movement_without_fire()
{
	recorder r;
	for (int i = 0; i < 60; ++i)
		r.on_frame(kFrame, 13, -7, false);

	assert(r.current() == nullptr);
	assert(r.history().empty());
}

// 첫 발은 언제나 (0,0,0) — 구간 시작이 곧 첫 발이다.
// 첫 발이 들어온 프레임의 이동은 구간이 시작되기 전의 이동이므로 버린다.
static void test_single_shot()
{
	recorder r;
	r.on_frame(kFrame, 100, 100, true);

	const segment *cur = r.current();
	assert(cur != nullptr);
	assert(cur->shots.size() == 1);
	assert(near_eq(cur->shots[0].x, 0.0f) && near_eq(cur->shots[0].y, 0.0f));
	assert(near_eq(cur->shots[0].t, 0.0f));
	// 진행 중인 구간은 history 에 들어가지 않는다
	assert(r.history().empty());
}

// 연발: 점들이 구간 시작 기준 누적 오프셋을 그대로 반영한다(손계산 대조).
static void test_burst_accumulates_offsets()
{
	recorder r;
	r.on_frame(kFrame, 5, 5, true);    // 구간 시작. 이 프레임 이동(5,5)은 버려짐 → (0,0,t=0)
	r.on_frame(0.100f, 3, -7, true);   // 누적 (3,-7),  t=0.1
	r.on_frame(0.100f, 3, -7, false);  // 누적 (6,-14), 발사 없음
	r.on_frame(0.100f, 4, -6, true);   // 누적 (10,-20), t=0.3

	const segment *cur = r.current();
	assert(cur != nullptr);
	assert(cur->shots.size() == 3);
	assert(near_eq(cur->shots[0].x, 0.0f)  && near_eq(cur->shots[0].y, 0.0f));
	assert(near_eq(cur->shots[1].x, 3.0f)  && near_eq(cur->shots[1].y, -7.0f));
	assert(near_eq(cur->shots[2].x, 10.0f) && near_eq(cur->shots[2].y, -20.0f));
	assert(near_eq(cur->shots[0].t, 0.0f));
	assert(near_eq(cur->shots[1].t, 0.1f, 1e-3f));
	assert(near_eq(cur->shots[2].t, 0.3f, 1e-3f));

	// 발 간격이 고르지 않아도 평균은 (마지막 t - 첫 t) / (발수-1)
	float iv = -1.0f;
	assert(avg_interval(*cur, iv));
	assert(near_eq(iv, 0.15f, 1e-3f));
}

// 구간 경계는 '>' 다. gap 400ms 에서 399 유지 / 400 유지 / 401 종료.
static void test_gap_boundary()
{
	{
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		r.on_frame(0.399f, 0, 0, false);
		assert(r.current() != nullptr);
		assert(r.history().empty());
	}
	{
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		r.on_frame(0.400f, 0, 0, false);
		// 정확히 임계값이면 아직 같은 구간이다 ('>=' 였다면 여기서 깨진다)
		assert(r.current() != nullptr);
		assert(r.history().empty());
	}
	{
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		r.on_frame(0.401f, 0, 0, false);
		assert(r.current() == nullptr);
		assert(r.history().size() == 1);
		assert(r.history()[0].shots.size() == 1);
	}
}

// 임계값 판정은 한 프레임이 아니라 마지막 발 이후 누적 시간으로 한다.
static void test_gap_accumulates_across_frames()
{
	{
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		for (int i = 0; i < 24; ++i) // 384ms
			r.on_frame(kFrame, 0, 0, false);
		assert(r.current() != nullptr);
	}
	{
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		for (int i = 0; i < 26; ++i) // 416ms
			r.on_frame(kFrame, 0, 0, false);
		assert(r.current() == nullptr);
		assert(r.history().size() == 1);
	}
	{
		// 중간에 발이 들어오면 카운터가 리셋된다 — 300ms 마다 쏘면 영원히 한 구간
		recorder r;
		r.on_frame(kFrame, 0, 0, true);
		for (int i = 0; i < 10; ++i)
			r.on_frame(0.300f, 0, 0, true);
		assert(r.current() != nullptr);
		assert(r.current()->shots.size() == 11);
		assert(r.history().empty());
	}
}

// 종료된 뒤의 첫 발은 새 구간이고 오프셋이 (0,0) 으로 리셋된다.
static void test_next_segment_resets_offset()
{
	recorder r;
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(0.100f, 40, 50, true);  // (40,50)
	r.on_frame(1.000f, 0, 0, false);   // 종료
	assert(r.history().size() == 1);

	r.on_frame(kFrame, 0, 0, true);    // 새 구간
	const segment *cur = r.current();
	assert(cur != nullptr);
	assert(cur->shots.size() == 1);
	assert(near_eq(cur->shots[0].x, 0.0f) && near_eq(cur->shots[0].y, 0.0f));

	r.on_frame(0.100f, 7, 8, true);
	assert(near_eq(cur->shots[1].x, 7.0f) && near_eq(cur->shots[1].y, 8.0f));

	// 이전 구간은 history 에 그대로 남아 있다
	assert(r.history()[0].shots.size() == 2);
	assert(near_eq(r.history()[0].shots[1].x, 40.0f));
	assert(near_eq(r.history()[0].shots[1].y, 50.0f));
}

// ⚠️ 구간이 없을 때의 이동은 다음 구간으로 새지 않는다.
// 발사 사이에 조준만 하는 움직임은 다음 스프레이의 일부가 아니다.
static void test_movement_between_segments_is_discarded()
{
	recorder r;
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(1.000f, 999, 999, false); // 구간 종료(이 이동은 어차피 기록되지 않음)
	assert(r.current() == nullptr);

	for (int i = 0; i < 30; ++i)         // 구간 없이 여기저기 조준
		r.on_frame(kFrame, 50, -50, false);

	r.on_frame(kFrame, 11, 22, true);    // 새 구간 첫 발
	const segment *cur = r.current();
	assert(cur != nullptr);
	assert(near_eq(cur->shots[0].x, 0.0f) && near_eq(cur->shots[0].y, 0.0f));

	r.on_frame(0.100f, 10, 20, true);    // 두 번째 발은 딱 이번 프레임 이동만
	assert(near_eq(cur->shots[1].x, 10.0f) && near_eq(cur->shots[1].y, 20.0f));
}

// 구간이 없을 때 흐른 시간도 다음 구간으로 새지 않는다.
static void test_time_between_segments_is_discarded()
{
	recorder r;
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(1.000f, 0, 0, false);     // 종료
	for (int i = 0; i < 100; ++i)        // 1.6초 대기
		r.on_frame(kFrame, 0, 0, false);

	r.on_frame(kFrame, 0, 0, true);      // 새 구간
	r.on_frame(0.100f, 0, 0, true);

	const segment *cur = r.current();
	assert(cur != nullptr && cur->shots.size() == 2);
	assert(near_eq(cur->shots[0].t, 0.0f));
	assert(near_eq(cur->shots[1].t, 0.1f, 1e-3f));
}

// 간격을 넘긴 바로 그 프레임에 들어온 클릭은 이전 구간의 꼬리가 아니라 새 구간의 첫 발이다.
static void test_fire_on_expiring_frame_starts_new_segment()
{
	recorder r;
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(0.401f, 50, 60, true);

	assert(r.history().size() == 1);
	assert(r.history()[0].shots.size() == 1);

	const segment *cur = r.current();
	assert(cur != nullptr);
	assert(cur->shots.size() == 1);
	assert(near_eq(cur->shots[0].x, 0.0f) && near_eq(cur->shots[0].y, 0.0f));
	assert(near_eq(cur->shots[0].t, 0.0f));
}

// 링버퍼: 21개를 만들면 20개만 남고 가장 오래된 것이 빠진다.
static void test_ring_buffer_drops_oldest()
{
	recorder r;
	for (int k = 0; k < 21; ++k)
	{
		r.on_frame(kFrame, 0, 0, true);                 // 첫 발 (0,0)
		r.on_frame(0.100f, 10 * (k + 1), 0, true);      // 두 번째 발 x = 10*(k+1)
		r.on_frame(1.000f, 0, 0, false);                // 종료
	}

	assert(r.history().size() == kMaxSegments);
	// k=0 (x=10) 이 빠지고 k=1 (x=20) 이 가장 오래된 것이 된다
	assert(near_eq(r.history().front().shots[1].x, 20.0f));
	assert(near_eq(r.history().back().shots[1].x, 210.0f));
	for (const segment &s : r.history())
		assert(!near_eq(s.shots[1].x, 10.0f));
}

static void test_clear()
{
	recorder r;
	for (int k = 0; k < 3; ++k)
	{
		r.on_frame(kFrame, 0, 0, true);
		r.on_frame(0.100f, 5, 5, true);
		r.on_frame(1.000f, 0, 0, false);
	}
	r.on_frame(kFrame, 0, 0, true); // 진행 중인 구간도 하나 남겨 둔다
	assert(r.history().size() == 3 && r.current() != nullptr);

	r.clear();
	assert(r.history().empty());
	assert(r.current() == nullptr);

	// 지운 뒤에도 정상 동작하고, 남은 오프셋이 새 구간에 새지 않는다
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(0.100f, 9, 9, true);
	assert(r.current()->shots.size() == 2);
	assert(near_eq(r.current()->shots[0].x, 0.0f));
	assert(near_eq(r.current()->shots[1].x, 9.0f));
}

static void test_set_gap_ms()
{
	recorder r;
	assert(r.gap_ms() == 400); // 기본값

	r.set_gap_ms(100);
	assert(r.gap_ms() == 100);
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(0.150f, 0, 0, false); // 기본 400 이었다면 유지됐을 시간
	assert(r.current() == nullptr);
	assert(r.history().size() == 1);

	// 임계값은 설정이므로 clear() 로 초기화되지 않는다
	r.clear();
	assert(r.gap_ms() == 100);

	// 임계값을 키우면 경계도 따라 커진다
	r.set_gap_ms(2000);
	r.on_frame(kFrame, 0, 0, true);
	r.on_frame(1.500f, 0, 0, false);
	assert(r.current() != nullptr);
	r.on_frame(0.600f, 0, 0, false); // 누적 2100ms
	assert(r.current() == nullptr);

	// 말도 안 되는 값은 클램프한다(손으로 고친 ini 나 잘못된 슬라이더 범위 방어)
	r.set_gap_ms(0);
	assert(r.gap_ms() == kMinGapMs);
	r.set_gap_ms(-5);
	assert(r.gap_ms() == kMinGapMs);
	r.set_gap_ms(999999);
	assert(r.gap_ms() == kMaxGapMs);
}

static void test_avg_interval()
{
	segment one;
	one.shots.push_back(shot { 0.0f, 0.0f, 0.0f });
	float out = -1.0f;
	assert(!avg_interval(one, out));
	assert(near_eq(out, -1.0f)); // 실패 시 out 미변경

	segment empty;
	assert(!avg_interval(empty, out));

	segment even; // 등간격 3발: 0, 0.1, 0.2
	even.shots.push_back(shot { 0.0f, 0.0f, 0.0f });
	even.shots.push_back(shot { 0.0f, 0.0f, 0.1f });
	even.shots.push_back(shot { 0.0f, 0.0f, 0.2f });
	assert(avg_interval(even, out));
	assert(near_eq(out, 0.1f));
}

static void test_end_spread()
{
	float out = -1.0f;

	std::vector<segment> none;
	assert(!end_spread(none, 5, out));
	assert(near_eq(out, -1.0f)); // 실패 시 out 미변경

	std::vector<segment> one(1);
	one[0].shots.push_back(shot { 1.0f, 2.0f, 0.0f });
	assert(!end_spread(one, 5, out)); // 구간 1개 → false

	// 마지막 점이 전부 같으면 편차 0
	std::vector<segment> same(3);
	for (segment &s : same)
	{
		s.shots.push_back(shot { 0.0f, 0.0f, 0.0f });
		s.shots.push_back(shot { 12.0f, -34.0f, 0.1f });
	}
	assert(end_spread(same, 5, out));
	assert(near_eq(out, 0.0f));

	// 손계산: 마지막 점 (0,0) (3,4) (-3,-4) → 중심 (0,0), 거리 0·5·5 → 평균 10/3
	std::vector<segment> three(3);
	three[0].shots.push_back(shot { 0.0f, 0.0f, 0.0f });
	three[1].shots.push_back(shot { 0.0f, 0.0f, 0.0f });
	three[1].shots.push_back(shot { 3.0f, 4.0f, 0.1f });
	three[2].shots.push_back(shot { 0.0f, 0.0f, 0.0f });
	three[2].shots.push_back(shot { -3.0f, -4.0f, 0.1f });
	assert(end_spread(three, 5, out));
	assert(near_eq(out, 10.0f / 3.0f));

	// n 을 줄이면 최근 것만 본다: (3,4) 와 (-3,-4) → 중심 (0,0), 거리 5·5 → 5
	assert(end_spread(three, 2, out));
	assert(near_eq(out, 5.0f));

	// 유효 구간이 2개 미만이면 false
	assert(!end_spread(three, 1, out));
	assert(!end_spread(three, 0, out));
	assert(near_eq(out, 5.0f)); // 실패해도 직전 값이 그대로

	// 점이 하나뿐인 구간(탭)도 그 점이 곧 마지막 점이므로 포함된다 — 위 three[0] 이 그 경우다.
	// 빈 구간은 건너뛴다(손으로 만든 입력 방어)
	std::vector<segment> with_empty(3);
	with_empty[0].shots.push_back(shot { 6.0f, 0.0f, 0.0f });
	// with_empty[1] 은 비어 있음
	with_empty[2].shots.push_back(shot { -6.0f, 0.0f, 0.0f });
	assert(end_spread(with_empty, 3, out));
	assert(near_eq(out, 6.0f));
}

// recorder 가 만든 history 를 그대로 통계 함수에 먹여 본다(배선 확인).
static void test_recorder_feeds_stats()
{
	recorder r;
	for (int k = 0; k < 4; ++k)
	{
		r.on_frame(kFrame, 0, 0, true);
		r.on_frame(0.100f, 0, -10, true);
		r.on_frame(0.100f, 0, -10, true); // 마지막 점 (0,-20) — 매번 같음
		r.on_frame(1.000f, 0, 0, false);
	}
	assert(r.history().size() == 4);

	float spread = -1.0f;
	assert(end_spread(r.history(), 5, spread));
	assert(near_eq(spread, 0.0f)); // 매번 같은 자리에서 끝났으므로 편차 0

	float iv = -1.0f;
	assert(avg_interval(r.history().back(), iv));
	assert(near_eq(iv, 0.1f, 1e-3f));
}

int main()
{
	test_movement_without_fire();
	test_single_shot();
	test_burst_accumulates_offsets();
	test_gap_boundary();
	test_gap_accumulates_across_frames();
	test_next_segment_resets_offset();
	test_movement_between_segments_is_discarded();
	test_time_between_segments_is_discarded();
	test_fire_on_expiring_frame_starts_new_segment();
	test_ring_buffer_drops_oldest();
	test_clear();
	test_set_gap_ms();
	test_avg_interval();
	test_end_spread();
	test_recorder_feeds_stats();
	std::printf("sherbet_spray: ALL PASS\n");
	return 0;
}
