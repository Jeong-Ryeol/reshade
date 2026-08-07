/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_aim.hpp 호스트 단위테스트.
//
//   clang++ -std=c++17 -Wall -Isource tools/sherbet_aim_test.cpp -o /tmp/t && /tmp/t
//   (윈도우: tools\run_host_tests.ps1 이 전부 돌린다)
//
// ⚠️ -DNDEBUG 를 붙이지 않는다. 이 파일은 전부 assert 로 검증하므로 NDEBUG 가 켜지면
//    단언이 전부 사라져 무조건 통과한다(무의미한 green).

#include "sherbet_aim.hpp"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <limits>
#include <set>

using namespace sherbet::aim;

static bool feq(float a, float b, float eps = 1e-3f)
{
	return std::fabs(a - b) <= eps;
}

// 실제 프레임 간격으로 seconds 만큼 흘린다.
// ⚠️ tick() 은 dt 를 0.25초로 자른다(알트탭에서 돌아온 거대한 프레임이 판을 한 번에
//    끝내지 않게). 그래서 tick(1.0f) 한 번은 1초가 아니라 0.25초다 — 테스트도 실제
//    프레임처럼 잘게 흘려야 한다.
static void advance(session &s, float seconds, float cam_yaw = 0.0f, float cam_pitch = 0.0f)
{
	constexpr float kStep = 1.0f / 120.0f;
	const int n = static_cast<int>(seconds / kStep + 0.5f);
	for (int i = 0; i < n; ++i)
		s.tick(kStep, cam_yaw, cam_pitch);
}

// ── 1. 판 길이와 순위 자격 ───────────────────────────────────────────────────
static void test_duration()
{
	assert(feq(duration_seconds(duration::s10), 10.0f));
	assert(feq(duration_seconds(duration::s30), 30.0f));
	assert(feq(duration_seconds(duration::s60), 60.0f));

	// ★ 10초는 몸풀기다. 표본이 적어 편차가 크고, 싸게 여러 번 돌려 제일 잘 나온 판만
	//   남기면 리더보드가 운으로 채워진다. 순위에 올리지 않는 것이 이 기능의 계약이다.
	assert(!ranked(duration::s10));
	assert(ranked(duration::s30));
	assert(ranked(duration::s60));
}

// ── 2. 난이도 계단 ───────────────────────────────────────────────────────────
static void test_tuning_ladder()
{
	const tuning e = tuning_for(level::easy);
	const tuning n = tuning_for(level::normal);
	const tuning h = tuning_for(level::hard);
	const tuning x = tuning_for(level::hell);

	// 어려워질수록 표적은 작아진다.
	assert(e.radius_deg > n.radius_deg);
	assert(n.radius_deg > h.radius_deg);
	assert(h.radius_deg > x.radius_deg);

	// 어려워질수록 원뿔이 넓어진다(큰 플릭).
	assert(e.cone_deg < n.cone_deg);
	assert(n.cone_deg < h.cone_deg);
	assert(h.cone_deg < x.cone_deg);

	// ★ 이동은 **어려움부터**. 쉬움·보통은 정지다 — 한 번에 한 축씩 어려워지는 계단.
	assert(e.move_speed_deg == 0.0f);
	assert(n.move_speed_deg == 0.0f);
	assert(h.move_speed_deg > 0.0f);
	assert(x.move_speed_deg > h.move_speed_deg);

	// 최소 간격은 원뿔 반경보다 작아야 한다. 뒤집히면 만족하는 자리가 없다.
	for (int i = 0; i < kLevelCount; ++i)
	{
		const tuning t = tuning_for(static_cast<level>(i));
		assert(t.min_gap_deg > 0.0f);          // 0 이면 제자리 연타가 된다
		assert(t.min_gap_deg < t.cone_deg);
		assert(t.radius_deg > 0.0f);
	}
}

// ── 3. 카운트다운 숫자 ───────────────────────────────────────────────────────
static void test_countdown_number()
{
	assert(countdown_number(5.0f) == 5);
	assert(countdown_number(4.9f) == 5);
	assert(countdown_number(4.0f) == 4);
	assert(countdown_number(3.01f) == 4);
	assert(countdown_number(1.0f) == 1);
	assert(countdown_number(0.01f) == 1);
	assert(countdown_number(0.0f) == 0);
	assert(countdown_number(-1.0f) == 0);
	// 남은 시간이 상한을 넘어도 5 를 넘겨 표시하지 않는다(잘못된 상태 방어).
	assert(countdown_number(99.0f) == 5);
}

// ── 4. 각도 도우미 ───────────────────────────────────────────────────────────
static void test_angles()
{
	assert(feq(wrap_deg(0.0f), 0.0f));
	assert(feq(wrap_deg(180.0f), 180.0f));
	assert(feq(wrap_deg(181.0f), -179.0f));
	assert(feq(wrap_deg(-181.0f), 179.0f));
	assert(feq(wrap_deg(720.0f + 30.0f), 30.0f));

	assert(feq(clamp_pitch(0.0f), 0.0f));
	assert(feq(clamp_pitch(95.0f), kPitchLimit));
	assert(feq(clamp_pitch(-95.0f), -kPitchLimit));

	// 같은 방향은 거리 0.
	assert(feq(angular_distance(10.0f, 20.0f, 10.0f, 20.0f), 0.0f, 1e-2f));
	// 적도에서 yaw 차이는 그대로 각거리다.
	assert(feq(angular_distance(0.0f, 0.0f, 30.0f, 0.0f), 30.0f, 1e-2f));
	// pitch 차이도 마찬가지.
	assert(feq(angular_distance(0.0f, 0.0f, 0.0f, 25.0f), 25.0f, 1e-2f));

	// ★ 감기를 빠뜨리면 여기가 358 로 나온다 — 바로 옆 표적을 화면 밖으로 판정한다.
	assert(feq(angular_distance(179.0f, 0.0f, -179.0f, 0.0f), 2.0f, 1e-2f));

	// ★ 구면 거리라 위를 볼 때 yaw 차이가 줄어든다. 평면 근사(sqrt(dy²+dp²))면
	//   여기서 30 이 나와 위쪽 표적이 부당하게 멀게 판정된다.
	const float high = angular_distance(0.0f, 80.0f, 30.0f, 80.0f);
	assert(high < 30.0f);
	assert(high > 0.0f);
}

// ── 5. 난수 ──────────────────────────────────────────────────────────────────
static void test_rng()
{
	// 같은 시드 = 같은 수열. 이게 깨지면 배치 관련 단언을 재현할 수 없다.
	rng a(12345u), b(12345u);
	for (int i = 0; i < 100; ++i)
		assert(a.next() == b.next());

	// 다른 시드는 달라야 한다.
	rng c(1u), d(2u);
	bool differ = false;
	for (int i = 0; i < 10; ++i)
		if (c.next() != d.next()) { differ = true; break; }
	assert(differ);

	// 시드 0 이 들어와도 죽지 않는다(xorshift 는 0 에서 영원히 0 이다).
	rng z(0u);
	bool nonzero = false;
	for (int i = 0; i < 10; ++i)
		if (z.next() != 0u) { nonzero = true; break; }
	assert(nonzero);

	// 범위를 벗어나지 않는다.
	rng r(777u);
	for (int i = 0; i < 5000; ++i)
	{
		const float u = r.unit();
		assert(u >= 0.0f && u < 1.0f);
		const float v = r.range(-3.0f, 7.0f);
		assert(v >= -3.0f && v <= 7.0f);
	}
}

// ── 6. 투영 ──────────────────────────────────────────────────────────────────
static void test_project()
{
	float x = 0.0f, y = 0.0f;
	const float W = 1920.0f, H = 1080.0f, FOV = 90.0f;

	// 정면은 화면 중앙.
	assert(project(0.0f, 0.0f, FOV, W, H, x, y));
	assert(feq(x, W * 0.5f, 0.5f));
	assert(feq(y, H * 0.5f, 0.5f));

	// FOV 90 이면 좌우 45도가 화면 가장자리.
	assert(project(45.0f, 0.0f, FOV, W, H, x, y));
	assert(feq(x, W, 1.0f));

	// 위를 보면 화면 y 가 작아진다(위쪽).
	assert(project(0.0f, 10.0f, FOV, W, H, x, y));
	assert(y < H * 0.5f);

	// ★ 원근이라 가장자리는 선형이 아니다. 30도는 15도의 두 배보다 **더** 멀리 간다.
	float x15 = 0.0f, x30 = 0.0f, t = 0.0f;
	assert(project(15.0f, 0.0f, FOV, W, H, x15, t));
	assert(project(30.0f, 0.0f, FOV, W, H, x30, t));
	const float d15 = x15 - W * 0.5f, d30 = x30 - W * 0.5f;
	assert(d30 > d15 * 2.0f);

	// 뒤쪽은 실패해야 한다. 안 막으면 나눗셈이 폭주해 표적이 엉뚱한 곳에 찍힌다.
	assert(!project(120.0f, 0.0f, FOV, W, H, x, y));
	assert(!project(180.0f, 0.0f, FOV, W, H, x, y));

	// ── 카메라 자세를 반영하는 투영 ──────────────────────────────────────────
	// 카메라가 수평이면 두 함수가 같은 답을 낸다.
	float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
	assert(project(20.0f, 8.0f, FOV, W, H, ax, ay));
	assert(project_from(0.0f, 0.0f, 20.0f, 8.0f, FOV, W, H, bx, by));
	assert(feq(ax, bx, 0.5f) && feq(ay, by, 0.5f));

	// ★ 카메라가 **위를 보고 있을 때** 갈린다. 위도선은 대원이 아니라 극 쪽으로 굽으므로,
	//   같은 높이의 옆 표적은 화면에서 **위로** 휘어야 한다(수평선은 아래로 떨어진다).
	//   각도를 그냥 빼면 늘 한가운데 줄에 그려져 틀린다.
	assert(project_from(0.0f, 30.0f, 25.0f, 30.0f, FOV, W, H, bx, by));
	assert(by < H * 0.5f - 1.0f); // 중앙보다 위
	// 아래를 볼 때는 반대로 아래로 휜다.
	assert(project_from(0.0f, -30.0f, 25.0f, -30.0f, FOV, W, H, bx, by));
	assert(by > H * 0.5f + 1.0f);
	// 정면은 언제나 화면 중앙이다(카메라 자세와 무관).
	assert(project_from(40.0f, -25.0f, 40.0f, -25.0f, FOV, W, H, bx, by));
	assert(feq(bx, W * 0.5f, 0.5f) && feq(by, H * 0.5f, 0.5f));
	// 뒤쪽은 거부.
	assert(!project_from(0.0f, 0.0f, 179.0f, 0.0f, FOV, W, H, bx, by));

	// 말도 안 되는 인자 방어.
	assert(!project(0.0f, 0.0f, 0.0f, W, H, x, y));
	assert(!project(0.0f, 0.0f, 180.0f, W, H, x, y));
	assert(!project(0.0f, 0.0f, FOV, 0.0f, H, x, y));
	assert(!project(0.0f, 0.0f, FOV, W, 0.0f, x, y));
}

// ── 7. 명중률 ────────────────────────────────────────────────────────────────
static void test_accuracy()
{
	stats s;
	assert(feq(accuracy(s), 0.0f)); // 클릭 0 회에 0으로 나누지 않는다

	s.hits = 5; s.shots = 10;
	assert(feq(accuracy(s), 0.5f));

	// ★ 빗나간 클릭이 분모에 들어간다. 안 그러면 마구 쏘는 게 공짜라 숫자가 의미를 잃는다.
	s.hits = 5; s.shots = 50;
	assert(feq(accuracy(s), 0.1f));

	s.hits = 7; s.shots = 7;
	assert(feq(accuracy(s), 1.0f));

	// per_second 는 duration 이 아니라 **실제 초**를 받는다 — 맛보기는 duration 선택지에
	// 없는 길이라(kTrialSeconds) enum 으로 되짚으면 틀린 값이 된다.
	stats p; p.hits = 30;
	assert(feq(per_second(p, 60.0f), 0.5f));
	assert(feq(per_second(p, 30.0f), 1.0f));
	assert(feq(per_second(p, 0.0f), 0.0f)); // 0 으로 나누지 않는다
}

// ── 맛보기 ───────────────────────────────────────────────────────────────────
static void test_trial_length()
{
	// 맛보기는 duration 선택지를 늘리지 않고 길이만 덮어쓴다.
	// enum 을 늘리면 선택 알약에 5초가 생기고 서버 계약(LEVELS·DURATIONS)까지 흔들린다.
	session s;
	s.start(level::normal, duration::s60, 3u, 0.0f, 0.0f, 90.0f, kTrialSeconds);
	advance(s, kCountdownSeconds + 0.1f);
	assert(s.current_phase() == phase::running);
	assert(feq(s.total_seconds(), kTrialSeconds, 1e-2f));
	assert(feq(s.time_left(), kTrialSeconds, 0.1f));

	// 5초면 끝난다 — 고른 60초가 아니다.
	advance(s, 4.5f);
	assert(s.current_phase() == phase::running);
	advance(s, 1.0f);
	assert(s.current_phase() == phase::finished);

	// override 를 안 주면 duration 을 따른다.
	session t;
	t.start(level::normal, duration::s30, 3u, 0.0f, 0.0f, 90.0f);
	advance(t, kCountdownSeconds + 0.1f);
	assert(feq(t.total_seconds(), 30.0f, 1e-2f));
}

// ── 진행에 따른 축소 ─────────────────────────────────────────────────────────
static void test_ramp()
{
	// 처음엔 크고, 맞출수록 작아지고, 다 줄면 더 안 줄어든다.
	assert(feq(ramp_scale(0), kRampStart));
	assert(feq(ramp_scale(-5), kRampStart));           // 음수 방어
	assert(feq(ramp_scale(kRampHits), kRampEnd));
	assert(feq(ramp_scale(kRampHits * 10), kRampEnd)); // 더 내려가지 않는다
	assert(ramp_scale(5) < ramp_scale(0));
	assert(ramp_scale(20) < ramp_scale(5));
	assert(ramp_scale(20) > ramp_scale(kRampHits));
	// 단조 감소여야 한다 — 중간에 커지면 사용자가 뭐가 뭔지 모른다.
	for (int i = 1; i <= kRampHits + 5; ++i)
		assert(ramp_scale(i) <= ramp_scale(i - 1) + 1e-6f);

	// ★ 축소 범위가 거리 변화 폭보다 확실히 넓어야 한다. 비슷하면 크기가 그냥
	//   랜덤해 보이고 "쏘다 보면 작아진다" 가 읽히지 않는다(실제로 그렇게 보였다).
	const float ramp_span = kRampStart / kRampEnd;
	const float depth_span = kDepthNear / kDepthFar;
	assert(ramp_span > depth_span * 1.5f);
}

// ── 수평 모드 ────────────────────────────────────────────────────────────────
static void test_level_mode()
{
	// ★ 실전에서는 대부분 같은 높이에 서 있다. 이 모드는 위아래를 안 쓴다.
	for (level lv : { level::easy, level::hell }) // 이동 없는 것과 있는 것 둘 다
	{
		session s;
		s.start(lv, duration::s60, 4242u, 0.0f, 12.0f, 90.0f, 0.0f, mode::level);
		advance(s, kCountdownSeconds + 0.1f, 0.0f, 12.0f);
		assert(s.current_phase() == phase::running);
		assert(s.current_mode() == mode::level);

		// ★★★ 이 모드의 계약: 표적은 **화면 세로 정중앙**에 있어야 한다.
		//     각도로 검사하면 안 잡힌다 — 표적 pitch 를 카메라 pitch 에 맞추면 각도상은
		//     '같은 높이' 라 통과하지만, 좌우로 벌어진 표적은 위도선 곡률 때문에 화면에서
		//     위아래로 휜다. 카메라가 수평선을 벗어날수록 커지고, 사용자가 그걸 쫓아
		//     올라가면 표적이 따라 올라가 끝없이 도망간다(1.8.4 까지 실제로 그랬다).
		//     그래서 **투영까지 통과시켜** 픽셀로 검사한다.
		{
			const float fov = 90.0f, sw = 1920.0f, sh = 1080.0f;
			for (float cp : { 0.0f, -8.0f, -22.0f, -45.0f, -68.0f, -85.0f, 17.0f, 52.0f, 80.0f })
			{
				for (float cy : { 0.0f, 37.0f, -120.0f, 175.0f })
				{
					session q;
					q.start(lv, duration::s60, 31337u, cy, cp, fov, 0.0f, mode::level);
					advance(q, kCountdownSeconds + 0.1f, cy, cp);
					for (int i = 0; i < 40; ++i)
					{
						q.tick(1.0f / 60.0f, cy, cp);
						if (q.current_phase() != phase::running)
							break;
						const target t = q.current_target();
						float sx = 0.0f, sy = 0.0f;
						assert(project_from(cy, cp, t.yaw, t.pitch, fov, sw, sh, sx, sy));
						assert(std::fabs(sy - sh * 0.5f) < 1.0f); // 세로 정중앙(±1px)
						// 좌우로만 맞춰도 명중해야 한다. 정렬한 뒤 한 프레임 돌린다 —
						// 그 사이 표적이 조준선 높이로 수렴한다.
						q.tick(1.0f / 60.0f, t.yaw, cp);
						assert(q.shoot(t.yaw, cp));
					}
				}
			}
		}

		// ★★ 반동 표류 방어 — 이 모드의 핵심이다.
		//    게임에서 실제로 쏘면 반동으로 화면이 위로 튀는데 우리 가상 카메라는 마우스만
		//    봐서 내려 당긴 것만 본다. 그래서 우리 카메라 pitch 가 계속 내려간다.
		//    그 와중에도 표적은 **화면 세로 정중앙**을 지켜야 한다.
		//    ⚠️ pitch 로 비교하지 마라 — 그게 1.8.4 까지 틀렸던 계약이다. 반드시 투영으로.
		const float fov2 = 90.0f, sw2 = 1920.0f, sh2 = 1080.0f;
		float cam_y = 0.0f, cam_p = 12.0f;
		for (int i = 0; i < 120; ++i)
		{
			// 반동 보정을 흉내낸다 — 매번 시야가 조금씩 아래로 밀린다.
			// 120번 돌아도 pitch 한계(±89)에 안 닿게 잡는다.
			cam_p = clamp_pitch(cam_p - 0.15f);
			s.tick(1.0f / 60.0f, cam_y, cam_p);

			const target t = s.current_target();
			float sx = 0.0f, sy = 0.0f;
			assert(project_from(cam_y, cam_p, t.yaw, t.pitch, fov2, sw2, sh2, sx, sy));
			assert(std::fabs(sy - sh2 * 0.5f) < 1.0f); // 화면에서 안 뜬다

			// 좌우로만 맞춘다. 정렬 뒤 한 프레임 돌리면 표적이 조준선으로 수렴한다.
			cam_y = t.yaw;
			s.tick(1.0f / 60.0f, cam_y, cam_p);
			assert(s.shoot(cam_y, cam_p)); // 세로를 안 맞춰도 맞아야 한다
			// 이동 난이도에서도, 이동이 없는 난이도에서도 계속 정중앙이어야 한다.
			for (int k = 0; k < 30; ++k)
			{
				cam_p = clamp_pitch(cam_p - 0.01f);
				s.tick(1.0f / 60.0f, cam_y, cam_p);
			}
			// ⚠️ 위상 검사가 단언보다 **먼저**다. 60초 판은 이 루프 도중에 끝나고,
			//    끝난 판은 표적을 갱신하지 않으므로 그 뒤 단언은 의미가 없다.
			if (s.current_phase() != phase::running)
				break;
			const target t2 = s.current_target();
			assert(project_from(cam_y, cam_p, t2.yaw, t2.pitch, fov2, sw2, sh2, sx, sy));
			assert(std::fabs(sy - sh2 * 0.5f) < 1.0f);
		}
		// 실제로 시야가 꽤 내려갔는데도 끝까지 정중앙을 지켰다는 뜻이다.
		assert(cam_p < -20.0f);
	}

	// 자유 모드는 반대로 위아래를 실제로 쓴다(안 그러면 모드를 나눈 의미가 없다).
	// 다만 **원이 아니라 타원**이다 — 사람은 대체로 같은 높이에 서 있어서 세로 조준이
	// 드물다. 가로는 그대로 두고 세로만 절반으로 눌렀다.
	session f;
	f.start(level::normal, duration::s60, 77u, 0.0f, 0.0f, 90.0f, 0.0f, mode::free);
	advance(f, kCountdownSeconds + 0.1f);
	bool saw_pitch = false;
	float max_dy = 0.0f, max_dp = 0.0f;
	// 여기서는 반동이 없다(쏜 뒤 시야가 안 움직인다). 그러면 잰 반동 보정량이 0 이라
	// 세로 중심 = 앵커다. 그래서 앵커 기준으로 재는 것이 맞다.
	for (int i = 0; i < 400; ++i)
	{
		const target t = f.current_target();
		const float dp = std::fabs(t.pitch - f.anchor_pitch());
		const float dy = std::fabs(wrap_deg(t.yaw - f.anchor_yaw()));
		if (dp > 1.0f) saw_pitch = true;
		if (dp > max_dp) max_dp = dp;
		if (dy > max_dy) max_dy = dy;
		f.shoot(t.yaw, t.pitch);
	}
	assert(saw_pitch);
	// ★ 세로 폭이 가로 폭의 kVerticalSquash 배 언저리여야 한다. 표본이 많으니 양 극단에
	//   닿는다. 여기가 1 에 가까워지면 눌림이 풀린 것이고, 0 이면 수평 모드와 같아진다.
	assert(max_dp < max_dy);
	assert(max_dp < max_dy * (kVerticalSquash * 1.6f));
	assert(max_dp > max_dy * (kVerticalSquash * 0.4f));

	// ★★★ 자유 모드: 반동 보정으로 시야가 계속 내려가는 동안에도 표적이 조준선
	//     **위아래 양쪽**에 떠야 한다. 사용자 보고 그대로다 —
	//     "자유모드는 계속 올라가기만 하고 내려오는 구는 없네?"
	//     앵커에 세로를 고정해 두면 표적 무리가 화면에서 위로만 밀린다.
	{
		const float fov3 = 90.0f, sw3 = 1920.0f, sh3 = 1080.0f;
		session r;
		r.start(level::normal, duration::s60, 4711u, 0.0f, 0.0f, fov3, 0.0f, mode::free);
		advance(r, kCountdownSeconds + 0.1f);
		// ⚠️ **발사 속도를 바꿔 가며** 돌린다. 빠른 사람은 초당 2~3발을 쏜다(사용자).
		//    시간 기반 추종으로는 그 속도에서 반동과 조준을 구분할 수 없어서, 반동을
		//    발당 직접 재는 방식으로 갔다. 세 속도 전부에서 같아야 그게 증명된다.
		for (int fps_shot = 1; fps_shot <= 3; ++fps_shot)
		{
			const int frames_per_shot = 60 / fps_shot; // 60/30/20 프레임 = 1/2/3발 per second
			const int aim_frames = frames_per_shot / 2, after_frames = frames_per_shot - aim_frames;
			session q;
			q.start(level::normal, duration::s60, 4711u, 0.0f, 0.0f, fov3, 0.0f, mode::free);
			advance(q, kCountdownSeconds + 0.1f);

			float cam_y = 0.0f, cam_p = 0.0f;
			int above = 0, below = 0, shots = 0;
			for (int i = 0; i < 60 * fps_shot; ++i)
			{
				const target t = q.current_target();
				float sx = 0.0f, sy = 0.0f;
				assert(project_from(cam_y, cam_p, t.yaw, t.pitch, fov3, sw3, sh3, sx, sy));
				// 화면 위쪽이 y 작은 쪽. 앞 15발은 반동 추정이 자리를 잡는 구간이라 뺀다.
				if (i >= 15)
				{
					if (sy < sh3 * 0.5f - 4.0f) ++above;
					else if (sy > sh3 * 0.5f + 4.0f) ++below;
				}

				cam_y = t.yaw; cam_p = t.pitch; // 조준을 맞춘다
				for (int k = 0; k < aim_frames; ++k)
					q.tick(1.0f / 60.0f, cam_y, cam_p);
				if (q.current_phase() != phase::running)
					break;
				assert(q.shoot(cam_y, cam_p));
				++shots;
				// 반동 보정 — 한 발마다 시야가 아래로 밀린다(우리는 내려 당긴 것만 본다).
				cam_p = clamp_pitch(cam_p - 1.0f);
				for (int k = 0; k < after_frames; ++k)
					q.tick(1.0f / 60.0f, cam_y, cam_p);
			}
			assert(shots > 30);
			// 양쪽에 고르게 떠야 한다. 한쪽이 0 이면 그게 바로 사용자가 본 증상이다.
			assert(above > 5);
			assert(below > 5);
		}
	}

	// ★★ 자유 모드 앵커는 판이 끝날 때까지 **절대 안 움직인다**. 시야가 아무리
	//    돌아가고 밀려도 그대로여야 한다. 여기가 새면 표적 무리가 시야를 따라다니며
	//    판이 통째로 떠내려간다(예전에 좌우에서 실제로 겪은 문제다).
	//    수평 모드처럼 시야에 묶고 싶어지는 자리라서 못박아 둔다 — 자유 모드는 표적마다
	//    세로를 다시 맞추므로 반동 오차가 매 명중마다 저절로 리셋되고, 따라서 여기서
	//    보정할 것이 없다.
	{
		session r;
		r.start(level::normal, duration::s60, 909u, 0.0f, 0.0f, 90.0f, 0.0f, mode::free);
		advance(r, kCountdownSeconds + 0.1f);
		const float ap0 = r.anchor_pitch(), ay0 = r.anchor_yaw();

		float cam_p = 0.0f;
		for (int i = 0; i < 600; ++i) // 10초 동안 시야를 크게 밀어 본다
		{
			cam_p = clamp_pitch(cam_p - 0.1f);
			r.tick(1.0f / 60.0f, 40.0f, cam_p);
			if (r.current_phase() != phase::running)
				break;
			const target t = r.current_target();
			r.shoot(t.yaw, t.pitch);
		}
		assert(cam_p < -50.0f); // 시야가 실제로 크게 밀렸는데도
		assert(feq(r.anchor_pitch(), ap0, 1e-4f));
		assert(feq(r.anchor_yaw(), ay0, 1e-4f));
	}
}

// ── 8. 판 진행: 카운트다운 → 시작 ────────────────────────────────────────────
static void test_phase_flow()
{
	session s;
	assert(s.current_phase() == phase::idle);

	s.start(level::normal, duration::s30, 42u, 0.0f, 0.0f, 90.0f);
	assert(s.current_phase() == phase::countdown);
	assert(s.countdown_display() == 5);

	// 카운트다운 중에는 시간이 안 줄고 클릭도 안 센다.
	// ⚠️ 경계(정확히 4.0초 남음)에 앉히지 않는다 — 1/120 씩 더한 부동소수는 4.0 바로
	//    위아래로 흔들려서 5 가 나오기도 4 가 나오기도 한다. 경계 자체의 동작은
	//    countdown_number() 순수 함수 테스트가 못박는다.
	advance(s, 1.05f);
	assert(s.current_phase() == phase::countdown);
	assert(s.countdown_display() == 4);
	assert(!s.shoot(0.0f, 0.0f));
	assert(s.result().shots == 0); // ★ 카운트다운 클릭은 기록에 없다

	advance(s, 2.0f);
	assert(s.current_phase() == phase::countdown);
	assert(s.countdown_display() == 2);

	// 5초가 지나면 시작. **전환되는 그 프레임**에서 확인해야 한다 —
	// 통으로 흘려보내면 이미 판이 얼마간 진행된 뒤라 시작 시각을 볼 수 없다.
	for (int i = 0; i < 2000 && s.current_phase() == phase::countdown; ++i)
		s.tick(1.0f / 120.0f, 0.0f, 0.0f);
	assert(s.current_phase() == phase::running);

	// 시간 재기는 첫 표적이 뜬 순간부터다 — 카운트다운 5초가 판 시간을 깎으면 안 된다.
	assert(feq(s.time_left(), 30.0f, 1e-2f));
	assert(feq(s.time_elapsed(), 0.0f, 1e-2f));
}

// ── 9. 프레임률과 무관한 카운트다운 ──────────────────────────────────────────
static void test_frame_rate_independence()
{
	// 60fps 와 144fps 가 같은 실제 시간에 시작해야 한다.
	// 프레임 수로 세면 여기가 깨진다.
	auto run_until_start = [](float dt) {
		session s;
		s.start(level::easy, duration::s10, 7u, 0.0f, 0.0f, 90.0f);
		float t = 0.0f;
		for (int i = 0; i < 100000 && s.current_phase() == phase::countdown; ++i)
		{
			s.tick(dt, 0.0f, 0.0f);
			t += dt;
		}
		assert(s.current_phase() == phase::running);
		return t;
	};

	const float t60 = run_until_start(1.0f / 60.0f);
	const float t144 = run_until_start(1.0f / 144.0f);
	assert(feq(t60, kCountdownSeconds, 0.05f));
	assert(feq(t144, kCountdownSeconds, 0.05f));
	assert(feq(t60, t144, 0.05f));
}

// 판을 시작 상태까지 돌리는 도우미.
static void arm(session &s, level lv, duration d, std::uint32_t seed,
	float cam_yaw = 0.0f, float cam_pitch = 0.0f, float fov = 90.0f)
{
	s.start(lv, d, seed, cam_yaw, cam_pitch, fov);
	advance(s, kCountdownSeconds + 0.1f, cam_yaw, cam_pitch);
	assert(s.current_phase() == phase::running);
}

// ── 10. ★ 지연 0 — 이 기능의 생명 ───────────────────────────────────────────
static void test_zero_latency()
{
	// 표적은 절대 방향이다. 카메라를 아무리 크게 휘둘러도 표적의 방향은 그대로여야 한다.
	// 누군가 스무딩·보간·화면 리드백을 위치 경로에 넣으면 여기서 걸린다.
	session s;
	arm(s, level::normal, duration::s60, 99u); // 보통 = 이동 없음

	const target t0 = s.current_target();

	// 한 프레임에 400도를 휘두른다(빠른 플릭보다도 크다).
	s.tick(1.0f / 144.0f, 400.0f, 30.0f);
	const target t1 = s.current_target();
	assert(feq(t0.yaw, t1.yaw, 1e-4f));
	assert(feq(t0.pitch, t1.pitch, 1e-4f));

	// 여러 프레임에 걸쳐 마구 휘둘러도 마찬가지.
	for (int i = 0; i < 200; ++i)
		s.tick(1.0f / 144.0f, static_cast<float>(i * 37 % 360), static_cast<float>((i % 60) - 30));
	const target t2 = s.current_target();
	assert(feq(t0.yaw, t2.yaw, 1e-4f));
	assert(feq(t0.pitch, t2.pitch, 1e-4f));
}

// ── 11. 배치 규칙 ────────────────────────────────────────────────────────────
static void test_spawn_rules()
{
	for (int li = 0; li < kLevelCount; ++li)
	{
		const level lv = static_cast<level>(li);
		const tuning tn = tuning_for(lv);
		const float fov = 90.0f;

		session s;
		arm(s, lv, duration::s60, 1234u + static_cast<std::uint32_t>(li), 0.0f, 0.0f, fov);
		const float ay = s.anchor_yaw(), ap = s.anchor_pitch();

		float cam_y = 0.0f, cam_p = 0.0f;
		float prev_y = 0.0f, prev_p = 0.0f;
		bool have_prev = false;
		int close_calls = 0;

		for (int i = 0; i < 300; ++i)
		{
			const target t = s.current_target();

			// ★★ 표류 금지 — 표적은 **판 시작 방향** 주위 원뿔을 절대 벗어나지 않는다.
			//     지금 조준 방향 기준으로 놓으면 맞출 때마다 기준이 옮겨가는 랜덤워크가 되어
			//     인게임에서 제자리를 뱅뱅 돌게 된다(실제로 그랬다). 이게 이 테스트의 핵심이다.
			assert(angular_distance(ay, ap, t.yaw, t.pitch) <= tn.cone_deg + 0.5f);

			// ★ 화면 안 — 조준은 늘 직전 표적 위에 있으므로 최악은 원뿔 지름이다.
			const float d = angular_distance(cam_y, cam_p, t.yaw, t.pitch);
			assert(d <= fov * 0.5f);

			// 직전 표적과 너무 붙지 않는다. 기각 표집이라 드물게 못 지킬 수 있으므로
			// 개수로 본다 — 대부분은 지켜야 훈련이 된다.
			if (have_prev && angular_distance(t.yaw, t.pitch, prev_y, prev_p) < tn.min_gap_deg - 0.5f)
				close_calls++;

			assert(t.pitch <= kPitchLimit + 1e-3f && t.pitch >= -kPitchLimit - 1e-3f);

			// ★ 거리 배율 — 표적마다 크기가 다르되 정해진 범위 안이어야 한다.
			assert(t.scale >= kDepthFar - 1e-3f && t.scale <= kDepthNear + 1e-3f);
			// ★ 보이는 크기와 맞는 크기가 같아야 한다.
			//   판정 반지름 = 기본 × 거리 배율 × 진행 축소. 셋 다 곱해져야 한다.
			assert(feq(s.target_radius_deg(),
				tn.radius_deg * t.scale * ramp_scale(s.result().hits), 1e-3f));

			prev_y = t.yaw; prev_p = t.pitch; have_prev = true;
			cam_y = t.yaw; cam_p = t.pitch;
			assert(s.shoot(cam_y, cam_p)); // 표적 위에서 쏘면 반드시 명중
		}
		assert(close_calls < 15); // 300개 중 5% 미만
		assert(s.result().hits == 300);
		assert(s.result().shots == 300);
		assert(feq(accuracy(s.result()), 1.0f));

		// ★ 카메라(=사람 시야)도 원뿔 안에 머문다. 여기가 깨지면 뱅뱅 도는 것이다.
		assert(angular_distance(ay, ap, cam_y, cam_p) <= tn.cone_deg + 0.5f);
	}
}

// 좁은 FOV 에서도 표적이 화면 안에 있어야 한다(범위가 시야를 넘으면 잘라야 한다).
static void test_spawn_narrow_fov()
{
	const float fov = 60.0f;
	session s;
	arm(s, level::hell, duration::s60, 5150u, 0.0f, 0.0f, fov);

	float cam_y = 0.0f, cam_p = 0.0f;
	for (int i = 0; i < 200; ++i)
	{
		const target t = s.current_target();
		assert(angular_distance(cam_y, cam_p, t.yaw, t.pitch) <= fov * 0.5f);
		cam_y = t.yaw; cam_p = t.pitch;
		s.shoot(cam_y, cam_p);
	}
}

// 하늘을 보고 있어도(pitch 극단) 배치가 무너지지 않는다.
static void test_spawn_near_pole()
{
	for (float start_pitch : { 85.0f, -85.0f })
	{
		session s;
		arm(s, level::hard, duration::s60, 31337u, 0.0f, start_pitch, 90.0f);
		float cam_y = 0.0f, cam_p = start_pitch;
		for (int i = 0; i < 100; ++i)
		{
			const target t = s.current_target();
			assert(t.pitch <= kPitchLimit + 1e-3f && t.pitch >= -kPitchLimit - 1e-3f);
			assert(t.yaw >= -180.0f && t.yaw <= 180.0f);
			cam_y = t.yaw; cam_p = t.pitch;
			s.shoot(cam_y, cam_p);
		}
	}
}

// ── 12. 사격 판정 ────────────────────────────────────────────────────────────
static void test_shooting()
{
	session s;
	arm(s, level::easy, duration::s60, 2024u);
	const target t = s.current_target();
	// 판정 반지름은 거리 배율이 곱해진 값이다 — tuning.radius_deg 를 직접 쓰면
	// 멀리 뜬 표적에서 어긋난다.
	const float rad = s.target_radius_deg();

	// 표적 밖을 쏘면 빗나가고, 클릭은 분모에 남고, 표적은 그대로다.
	const float far_yaw = wrap_deg(t.yaw + rad * 4.0f);
	assert(!s.shoot(far_yaw, t.pitch));
	assert(s.result().shots == 1);
	assert(s.result().hits == 0);
	assert(feq(s.current_target().yaw, t.yaw, 1e-4f)); // 빗나가면 표적이 안 바뀐다

	// 가장자리 안쪽은 명중.
	assert(s.shoot(wrap_deg(t.yaw + rad * 0.8f), t.pitch));
	assert(s.result().hits == 1);
	assert(s.result().shots == 2);
	// 명중하면 다음 표적이 뜬다.
	assert(!feq(s.current_target().yaw, t.yaw, 1e-4f) || !feq(s.current_target().pitch, t.pitch, 1e-4f));

	// 최근 사격 기록(0 = 가장 최근).
	assert(s.shot_history_count() == 2);
	assert(s.shot_history(0) == true);   // 방금 명중
	assert(s.shot_history(1) == false);  // 그 전엔 빗나감
	assert(s.shot_history(-1) == false); // 범위 밖은 조용히 false
	assert(s.shot_history(99) == false);
}

// 판이 끝나면 더 이상 세지 않는다.
static void test_finish()
{
	session s;
	arm(s, level::normal, duration::s10, 8u);

	advance(s, 9.5f);
	assert(s.current_phase() == phase::running); // 아직 안 끝났다
	advance(s, 1.0f);
	assert(s.current_phase() == phase::finished);
	assert(feq(s.time_left(), 0.0f));

	const int shots_before = s.result().shots;
	assert(!s.shoot(0.0f, 0.0f));
	assert(s.result().shots == shots_before); // ★ 끝난 뒤 클릭은 기록에 안 들어간다

	// 시간이 음수로 내려가지 않는다.
	s.tick(0.2f, 0.0f, 0.0f);
	assert(feq(s.time_left(), 0.0f));

	// ★ 끝난 뒤에도 시간을 센다 — 결과 HUD 를 언제 지울지 알아야 한다.
	//   안 세면 왼쪽 결과판이 게임 내내 화면에 눌어붙는다(실제로 그랬다).
	assert(s.since_finish() > 0.0f);
	const float before = s.since_finish();
	advance(s, 1.0f);
	assert(s.since_finish() > before);
	advance(s, kResultHudSeconds);
	assert(s.since_finish() >= kResultHudSeconds); // 이 시점엔 HUD 가 사라져 있어야 한다

	// 새 판을 시작하면 다시 0 부터.
	arm(s, level::easy, duration::s10, 9u);
	assert(feq(s.since_finish(), 0.0f));
}

// ── 13. 이동 표적 ────────────────────────────────────────────────────────────
static void test_movement()
{
	// 쉬움·보통은 정지.
	for (level lv : { level::easy, level::normal })
	{
		session s;
		arm(s, lv, duration::s60, 555u);
		const target t0 = s.current_target();
		for (int i = 0; i < 120; ++i)
			s.tick(1.0f / 60.0f, 0.0f, 0.0f);
		const target t1 = s.current_target();
		assert(feq(t0.yaw, t1.yaw, 1e-4f));
		assert(feq(t0.pitch, t1.pitch, 1e-4f));
	}

	// 어려움·헬은 움직이되, 생성 위치에서 정해진 반경을 벗어나지 않는다.
	for (level lv : { level::hard, level::hell })
	{
		session s;
		arm(s, lv, duration::s60, 909u);
		const target t0 = s.current_target();
		bool moved = false;
		for (int i = 0; i < 3600; ++i) // 60초치
		{
			s.tick(1.0f / 60.0f, 0.0f, 0.0f);
			const target t = s.current_target();
			if (!feq(t0.yaw, t.yaw, 1e-3f) || !feq(t0.pitch, t.pitch, 1e-3f))
				moved = true;
			// ★ 튕기기가 안 먹으면 표적이 유유히 화면 밖으로 나간다.
			const float wander = angular_distance(t.yaw, t.pitch, t.home_yaw, t.home_pitch);
			assert(wander <= kWanderLimitDeg + 1.0f);
			if (s.current_phase() != phase::running)
				break;
		}
		assert(moved);
	}
}

// ── 14. 이상한 dt 방어 ───────────────────────────────────────────────────────
static void test_bad_dt()
{
	session s;
	arm(s, level::hell, duration::s60, 4242u);
	const float t_before = s.time_left();

	s.tick(-5.0f, 0.0f, 0.0f);                                        // 음수
	assert(s.time_left() <= t_before);
	assert(s.time_left() >= t_before - 0.001f);                       // 시간이 되감기지 않는다

	s.tick(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);      // NaN
	assert(s.time_left() == s.time_left());                           // NaN 이 새지 않았다

	// 알트탭에서 돌아온 거대한 프레임. 한 번에 판이 끝나버리면 안 된다.
	const float t2 = s.time_left();
	s.tick(1000.0f, 0.0f, 0.0f);
	assert(s.time_left() >= t2 - 0.26f); // 상한 0.25초로 잘린다

	// 표적도 순간이동하지 않는다.
	const target t = s.current_target();
	assert(angular_distance(t.yaw, t.pitch, t.home_yaw, t.home_pitch) <= kWanderLimitDeg + 1.0f);
}

// ── 15. 같은 시드 = 같은 판 ─────────────────────────────────────────────────
static void test_deterministic()
{
	auto play = [](std::uint32_t seed) {
		session s;
		arm(s, level::hard, duration::s30, seed);
		float acc = 0.0f;
		for (int i = 0; i < 50; ++i)
		{
			const target t = s.current_target();
			acc += t.yaw * 3.0f + t.pitch;
			s.shoot(t.yaw, t.pitch);
			s.tick(1.0f / 60.0f, t.yaw, t.pitch);
		}
		return acc;
	};
	assert(feq(play(2026u), play(2026u), 1e-3f));
	assert(!feq(play(1u), play(2u), 1e-3f));
}

// 시작 전에는 아무것도 안 센다.
static void test_idle_is_inert()
{
	session s;
	assert(s.current_phase() == phase::idle);
	s.tick(1.0f, 0.0f, 0.0f);
	assert(s.current_phase() == phase::idle);
	assert(!s.shoot(0.0f, 0.0f));
	assert(s.result().shots == 0);

	// 중단하면 idle 로 돌아간다.
	arm(s, level::normal, duration::s30, 11u);
	s.stop();
	assert(s.current_phase() == phase::idle);
	const int shots = s.result().shots;
	assert(!s.shoot(0.0f, 0.0f));
	assert(s.result().shots == shots);
}

int main()
{
	test_duration();
	test_tuning_ladder();
	test_countdown_number();
	test_angles();
	test_rng();
	test_project();
	test_accuracy();
	test_trial_length();
	test_ramp();
	test_level_mode();
	test_phase_flow();
	test_frame_rate_independence();
	test_zero_latency();
	test_spawn_rules();
	test_spawn_narrow_fov();
	test_spawn_near_pole();
	test_shooting();
	test_finish();
	test_movement();
	test_bad_dt();
	test_deterministic();
	test_idle_is_inert();

	std::printf("sherbet_aim_test: ALL PASS\n");
	return 0;
}
