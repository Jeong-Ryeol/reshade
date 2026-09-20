/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_engine.hpp 호스트 단위테스트 (엔진룸 3D 순수 로직).
// ⚠️ -DNDEBUG 금지 — 전부 assert 로 검증한다.
//
// 검사하는 것은 **화면에 찍히는 것**이다: 원점은 캔버스 가운데에, 오른쪽 점은 오른쪽에,
// 먼 점은 작게, 카메라 뒤는 안 그린다. 성운은 BGRA 를 RGBA 로 잘못 읽으면 하늘이
// 주황색이 되므로 바이트 순서를 포맷별로 못 박는다.

#include "sherbet_engine.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace sherbet::engine;

static bool feq(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

static void test_projection_basics()
{
	camera cam; // 기본 각도
	viewport vp { 400.0f, 300.0f, 300.0f };
	float sx, sy, depth, scale;

	// 원점은 어느 각도에서든 캔버스 정중앙이다.
	assert(project(cam, { 0, 0, 0 }, vp, sx, sy, depth, scale));
	assert(feq(sx, 400.0f, 1e-3f) && feq(sy, 300.0f, 1e-3f));
	assert(feq(depth, kDefaultDist, 1e-4f));
	assert(feq(scale, 300.0f * kFocal / kDefaultDist, 1e-3f));

	// 정면(yaw=pitch=0)에서 +X 는 오른쪽, +Y 는 위(화면 y 는 아래로 자란다).
	cam.yaw = 0.0f; cam.pitch = 0.0f;
	assert(project(cam, { 1, 0, 0 }, vp, sx, sy, depth, scale));
	assert(sx > 400.0f && feq(sy, 300.0f, 1e-3f));
	assert(project(cam, { 0, 1, 0 }, vp, sx, sy, depth, scale));
	assert(sy < 300.0f && feq(sx, 400.0f, 1e-3f));

	// 먼 점(+Z)은 작게, 가까운 점(-Z)은 크게.
	float s_far, s_near;
	assert(project(cam, { 0, 0, 3 }, vp, sx, sy, depth, s_far));
	assert(project(cam, { 0, 0, -3 }, vp, sx, sy, depth, s_near));
	assert(s_far < s_near);

	// 카메라 뒤는 그리지 않는다.
	assert(!project(cam, { 0, 0, -kDefaultDist - 1.0f }, vp, sx, sy, depth, scale));
}

static void test_projection_yaw_moves_x()
{
	// 90도 돌리면 +X 에 있던 점이 카메라 쪽(깊이 감소)으로 오고, +Z 에 있던 점이 오른쪽으로 온다.
	camera cam; cam.pitch = 0.0f; cam.yaw = 1.5707963f;
	viewport vp { 0.0f, 0.0f, 100.0f };
	float sx, sy, depth, scale;
	assert(project(cam, { 0, 0, 1 }, vp, sx, sy, depth, scale));
	assert(sx > 0.0f);
	assert(project(cam, { 1, 0, 0 }, vp, sx, sy, depth, scale));
	assert(feq(sx, 0.0f, 1e-3f) && depth < kDefaultDist);
}

static void test_camera_controls()
{
	camera cam;
	assert(cam.auto_spin);
	cam.spin(1.0f);
	assert(feq(cam.yaw, kDefaultYaw + kSpinRate, 1e-6f));

	// 드래그하면 자동 회전이 멈추고 각도가 바뀐다.
	const float yaw_before = cam.yaw;
	cam.orbit(100.0f, 50.0f);
	assert(!cam.auto_spin);
	assert(feq(cam.yaw, yaw_before + 100.0f * kOrbitPerPixel, 1e-6f));
	assert(feq(cam.pitch, kDefaultPitch + 50.0f * kOrbitPerPixel, 1e-6f));
	cam.spin(10.0f); // 멈춘 뒤엔 안 돈다
	assert(feq(cam.yaw, yaw_before + 100.0f * kOrbitPerPixel, 1e-6f));

	// 피치는 뒤집히지 않는다.
	cam.orbit(0.0f, 100000.0f);
	assert(feq(cam.pitch, kMaxPitch, 1e-6f));
	cam.orbit(0.0f, -100000.0f);
	assert(feq(cam.pitch, -kMaxPitch, 1e-6f));

	// 휠: + 는 당기기(가까워짐). 클램프.
	cam.zoom(1.0f);
	assert(cam.dist < kDefaultDist);
	cam.zoom(-100.0f);
	assert(feq(cam.dist, kMaxDist, 1e-6f));
	cam.zoom(100.0f);
	assert(feq(cam.dist, kMinDist, 1e-6f));

	// 더블클릭 = 원위치. 자동 회전도 다시 돈다.
	cam.reset();
	assert(feq(cam.yaw, kDefaultYaw, 1e-6f) && feq(cam.pitch, kDefaultPitch, 1e-6f) && feq(cam.dist, kDefaultDist, 1e-6f));
	assert(cam.auto_spin);
}

static void test_ring_layout()
{
	const float ms[5] = { 0.0f, 0.5f, 1.0f, 2.0f, 9.0f };
	ring_slot out[5];
	layout_rings(ms, 5, out);
	// 실행 순서대로 왼쪽 → 오른쪽, 겹치지 않게 단조 증가.
	for (int i = 1; i < 5; ++i)
		assert(out[i].x > out[i - 1].x);
	assert(feq(out[0].x, kRingsStart, 1e-5f) && feq(out[4].x, kRingsEnd, 1e-5f));
	// 비쌀수록 크다. 0ms 도 최소 크기는 있다(안 보이면 "효과가 없다" 로 읽힌다).
	assert(out[0].radius > 0.0f);
	assert(out[1].radius > out[0].radius && out[2].radius > out[1].radius && out[3].radius > out[2].radius);
	// 2ms 위로는 다 같다 — 하나가 화면을 다 먹지 않게.
	assert(feq(out[4].radius, out[3].radius, 1e-6f));
	assert(feq(out[0].glow, 0.0f, 1e-6f) && feq(out[2].glow, 1.0f, 1e-6f) && feq(out[4].glow, 1.0f, 1e-6f));

	// 하나뿐이면 가운데.
	layout_rings(ms, 1, out);
	assert(feq(out[0].x, (kRingsStart + kRingsEnd) * 0.5f, 1e-5f));
}

static void test_particles_follow_frametime()
{
	particle_stream ps;
	float now = 0.0f;
	// 7ms 프레임 100개
	for (int i = 0; i < 100; ++i)
	{
		ps.spawn(now, 7.0f);
		now += 0.007f;
	}
	assert(ps.count() == 100);
	for (int i = 0; i < ps.count(); ++i)
		assert(!ps.at(i).late); // 고른 프레임엔 끊김이 없다
	assert(feq(ps.avg_ms(), 7.0f, 1e-3f));

	// 끊김 한 번(30ms) — 그 입자만 late.
	ps.spawn(now, 30.0f);
	assert(ps.at(ps.count() - 1).late);
	ps.spawn(now + 0.03f, 7.0f);
	assert(!ps.at(ps.count() - 1).late);

	// 진행도: 갓 태어난 건 0, 여행이 끝나면 1, 그 사이는 시간에 비례.
	const particle &first = ps.at(0);
	assert(feq(particle_stream::progress(first, first.t0), 0.0f, 1e-6f));
	assert(feq(particle_stream::progress(first, first.t0 + kTravelSeconds * 0.5f), 0.5f, 1e-5f));
	assert(feq(particle_stream::progress(first, first.t0 + kTravelSeconds * 3.0f), 1.0f, 1e-6f));
	assert(feq(particle_stream::x_of(0.0f), kPipeStart, 1e-6f) && feq(particle_stream::x_of(1.0f), kPipeEnd, 1e-6f));

	// 빔 두께: 오프셋은 반지름 안에 있고, 같은 번호는 항상 같은 자리(결정론).
	for (int i = 0; i < ps.count(); ++i)
	{
		const particle &p = ps.at(i);
		assert(std::sqrt(p.lane_y * p.lane_y + p.lane_z * p.lane_z) <= kBeamRadius + 1e-5f);
	}
	particle_stream ps2;
	ps2.spawn(0.0f, 7.0f);
	assert(feq(ps2.at(0).lane_y, ps.at(0).lane_y, 1e-7f) && feq(ps2.at(0).lane_z, ps.at(0).lane_z, 1e-7f));

	// 도착한 입자는 앞에서부터 버린다.
	ps.prune(first.t0 + kTravelSeconds + 0.001f);
	assert(ps.count() < 102 && ps.count() > 0);
	assert(ps.at(0).t0 > first.t0);
	ps.prune(1e9f);
	assert(ps.count() == 0);
}

static void test_particles_ring_overflow()
{
	// 용량을 넘기면 가장 오래된 것부터 덮는다 — 크래시도, 멈춤도 없다.
	particle_stream ps;
	for (int i = 0; i < kMaxParticles + 50; ++i)
		ps.spawn(static_cast<float>(i) * 0.001f, 1.0f);
	assert(ps.count() == kMaxParticles);
	assert(feq(ps.at(0).t0, 50 * 0.001f, 1e-6f)); // 앞 50개가 밀려났다
	ps.clear();
	assert(ps.count() == 0 && feq(ps.avg_ms(), 0.0f, 1e-9f));
}

static void test_sample_colors_byte_order()
{
	// 2×2. 픽셀 하나만 "순수 빨강" 으로 두고 포맷별로 제대로 빨강이 나오는지.
	float out[3 * 4];

	std::uint8_t rgba[2 * 2 * 4] = {};
	rgba[0] = 255; rgba[1] = 0; rgba[2] = 0; rgba[3] = 255; // (0,0) = 빨강
	assert(sample_colors(rgba, 2, 2, 8, pixel_kind::rgba8, 1, out, 4) == 4);
	assert(feq(out[0], 1.0f, 1e-6f) && feq(out[1], 0.0f, 1e-6f) && feq(out[2], 0.0f, 1e-6f));

	// ★ FiveM 백버퍼는 B8G8R8A8 이다. 바이트 0 이 파랑이다 — RGBA 로 읽으면 하늘이 주황이 된다.
	std::uint8_t bgra[2 * 2 * 4] = {};
	bgra[0] = 0; bgra[1] = 0; bgra[2] = 255; bgra[3] = 255; // 바이트 2 = R
	assert(sample_colors(bgra, 2, 2, 8, pixel_kind::bgra8, 1, out, 4) == 4);
	assert(feq(out[0], 1.0f, 1e-6f) && feq(out[1], 0.0f, 1e-6f) && feq(out[2], 0.0f, 1e-6f));
	// 같은 바이트를 rgba8 로 읽으면 파랑이 된다 — 구분이 실제로 의미 있다는 증거.
	assert(sample_colors(bgra, 2, 2, 8, pixel_kind::rgba8, 1, out, 4) == 4);
	assert(feq(out[0], 0.0f, 1e-6f) && feq(out[2], 1.0f, 1e-6f));

	// 10비트: R 이 하위 10비트.
	std::uint8_t r10[2 * 2 * 4] = {};
	const std::uint32_t v = 0x3FFu | (0x3FFu << 30); // R=1023, A=3
	std::memcpy(r10, &v, 4);
	assert(sample_colors(r10, 2, 2, 8, pixel_kind::rgb10a2, 1, out, 4) == 4);
	assert(feq(out[0], 1.0f, 1e-6f) && feq(out[1], 0.0f, 1e-6f) && feq(out[2], 0.0f, 1e-6f));
	assert(sample_colors(r10, 2, 2, 8, pixel_kind::bgr10a2, 1, out, 4) == 4);
	assert(feq(out[0], 0.0f, 1e-6f) && feq(out[2], 1.0f, 1e-6f));
}

static void test_sample_colors_step_and_cap()
{
	// 8×8 을 step 4 로 훑으면 4점(가운데 (2,2),(6,2),(2,6),(6,6)). row_pitch 가 폭보다 커도 된다.
	std::uint8_t buf[8 * 40] = {}; // pitch 40 (32 + 패딩 8)
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
		{
			std::uint8_t *px = buf + y * 40 + x * 4;
			px[0] = static_cast<std::uint8_t>(x * 32); // R = x
			px[1] = static_cast<std::uint8_t>(y * 32); // G = y
		}
	float out[3 * 16];
	assert(sample_colors(buf, 8, 8, 40, pixel_kind::rgba8, 4, out, 16) == 4);
	assert(feq(out[0], 2 * 32 / 255.0f, 1e-6f) && feq(out[1], 2 * 32 / 255.0f, 1e-6f)); // (2,2)
	assert(feq(out[3], 6 * 32 / 255.0f, 1e-6f) && feq(out[4], 2 * 32 / 255.0f, 1e-6f)); // (6,2)
	// 상한은 지켜진다.
	assert(sample_colors(buf, 8, 8, 40, pixel_kind::rgba8, 1, out, 5) == 5);
	// 잘못된 입력은 0.
	assert(sample_colors(nullptr, 8, 8, 40, pixel_kind::rgba8, 1, out, 5) == 0);
	assert(sample_colors(buf, 8, 8, 40, pixel_kind::rgba8, 0, out, 5) == 0);
}

static void test_nebula_pos()
{
	// 회색(0.5,0.5,0.5)은 정육면체 중심, 흰색은 (+,+,+) 꼭짓점.
	const vec3 mid = nebula_pos(0.5f, 0.5f, 0.5f);
	assert(feq(mid.x, 0.0f, 1e-6f) && feq(mid.y, kNebulaCenterY, 1e-6f) && feq(mid.z, 0.0f, 1e-6f));
	const vec3 white = nebula_pos(1.0f, 1.0f, 1.0f);
	assert(feq(white.x, kNebulaSize * 0.5f, 1e-6f) && white.y > mid.y && feq(white.z, kNebulaSize * 0.5f, 1e-6f));
}

static void test_waveform_keeps_spikes()
{
	sherbet::frametime::ring r;
	// 1..100 ms 를 순서대로. at() 은 오래된 것부터.
	for (int i = 1; i <= 100; ++i)
		r.push(static_cast<float>(i));
	assert(feq(r.at(0), 1.0f, 1e-6f) && feq(r.at(99), 100.0f, 1e-6f));

	float out[10];
	assert(waveform(r, 10, out) == 10);
	for (int b = 0; b < 10; ++b)
		assert(feq(out[b], static_cast<float>((b + 1) * 10), 1e-6f)); // 칸의 최대값

	// 스파이크 하나는 평균에 묻히지 않고 그 칸의 값이 된다.
	sherbet::frametime::ring r2;
	for (int i = 0; i < 100; ++i)
		r2.push(i == 37 ? 50.0f : 7.0f);
	assert(waveform(r2, 10, out) == 10);
	assert(feq(out[3], 50.0f, 1e-6f) && feq(out[4], 7.0f, 1e-6f));

	// 빈 링은 0.
	sherbet::frametime::ring empty;
	assert(waveform(empty, 10, out) == 0);

	// 꽉 찬 링을 넘겨 덮어써도 at() 순서가 맞다(가장 오래된 것이 0).
	sherbet::frametime::ring full;
	for (std::size_t i = 0; i < sherbet::frametime::kCapacity + 5; ++i)
		full.push(static_cast<float>(i + 1));
	assert(feq(full.at(0), 6.0f, 1e-6f));
	assert(feq(full.at(sherbet::frametime::kCapacity - 1), static_cast<float>(sherbet::frametime::kCapacity + 5), 1e-6f));
}

int main()
{
	test_projection_basics();
	test_projection_yaw_moves_x();
	test_camera_controls();
	test_ring_layout();
	test_particles_follow_frametime();
	test_particles_ring_overflow();
	test_sample_colors_byte_order();
	test_sample_colors_step_and_cap();
	test_nebula_pos();
	test_waveform_keeps_spikes();
	std::printf("sherbet_engine_test: ALL PASS\n");
	return 0;
}
