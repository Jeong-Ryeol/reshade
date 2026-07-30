/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// ⚠️ -DNDEBUG 를 붙이면 아래 단언이 전부 사라져 무의미하게 통과한다. 절대 붙이지 말 것.
//
// 이 스위트가 존재하는 이유: 화면 이동 추정의 정확도는 **실물 프레임에서만** 최종
// 확인되는데 맥에는 실물이 없다. 대신 정답을 아는 합성 데이터로 추정기를 조인다.
//   · 정확한 시프트          → 그 값이 그대로 나와야 한다(서브픽셀 포함)
//   · 시프트 + 노이즈        → 여전히 맞고 신뢰도는 남아야 한다
//   · 시프트 + 머즐 플래시   → 밝기가 확 변해도 맞아야 한다
//   · 무늬 없는 화면         → **자신 있게 틀리는 대신** 무효/저신뢰여야 한다
//   · 탐색범위 끝/밖         → 신뢰도 0 으로 "못 믿는다"고 말해야 한다
#include "sherbet_motion.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

using namespace sherbet::motion;

static bool near_eq(float a, float b, float eps)
{
	return (a - b) < eps && (b - a) < eps;
}

// 재현 가능한 잡음원(rand() 는 플랫폼마다 달라 쓰지 않는다).
struct rng
{
	std::uint32_t s;
	explicit rng(std::uint32_t seed) : s(seed) {}
	std::uint32_t next()
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}
	// -1..1
	float uniform() { return static_cast<float>(next() % 20001) / 10000.0f - 1.0f; }
};

// 실제 화면 프로파일을 흉내낸다: 낮은 주파수 + 중간 주파수가 섞인 부드러운 신호.
// 부드럽게 만드는 이유 — 서브픽셀 시프트를 선형보간으로 만들 수 있어야 하고,
// 실물 프로파일(수백 픽셀 합)도 이렇게 부드럽다.
static float scene(float t)
{
	return 128.0f
		+ 30.0f * std::sin(t * 0.11f)
		+ 18.0f * std::sin(t * 0.37f + 1.1f)
		+ 9.0f * std::sin(t * 0.93f + 2.3f)
		+ 4.0f * std::sin(t * 1.7f + 0.4f);
}

static std::vector<float> make_profile(int n, float shift)
{
	std::vector<float> p(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i)
		p[static_cast<std::size_t>(i)] = scene(static_cast<float>(i) - shift);
	return p;
}

// 위 scene() 은 사인 합이라 **준주기적**이다 — 탐색 범위 밖 시프트가 범위 안의 다른
// 시프트로 그럴듯하게 접힌다(에일리어싱). 범위 밖을 다루는 테스트에는 주기가 없는
// 장면이 필요하므로, 평활화한 난수를 길게 만들어 창을 옮겨 잘라 쓴다(정수 시프트만).
static const int kPad = 192;
static std::vector<float> aperiodic_base(int len)
{
	rng g(2024);
	std::vector<float> raw(static_cast<std::size_t>(len)), out(static_cast<std::size_t>(len), 0.0f);
	for (int i = 0; i < len; ++i)
		raw[static_cast<std::size_t>(i)] = 128.0f + g.uniform() * 60.0f;
	// 반경 3 박스 평활 — 그래디언트가 순수 백색잡음이면 실제 화면과 성질이 너무 다르다
	for (int i = 0; i < len; ++i)
	{
		float s = 0.0f;
		int c = 0;
		for (int d = -3; d <= 3; ++d)
		{
			const int j = i + d;
			if (j < 0 || j >= len)
				continue;
			s += raw[static_cast<std::size_t>(j)];
			++c;
		}
		out[static_cast<std::size_t>(i)] = s / static_cast<float>(c);
	}
	return out;
}
static std::vector<float> window_of(const std::vector<float> &base, int n, int shift)
{
	std::vector<float> p(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i)
		p[static_cast<std::size_t>(i)] = base[static_cast<std::size_t>(i + kPad - shift)];
	return p;
}

// ── 1. 정확한 정수 시프트 ────────────────────────────────────────────────────
// cur[i] = prev[i - k] 이면 shift 는 +k 다(내용이 인덱스가 커지는 쪽으로 이동).
static void test_exact_integer_shift()
{
	const int n = 512;
	const std::vector<float> prev = make_profile(n, 0.0f);

	for (int k = -20; k <= 20; k += 4)
	{
		const std::vector<float> cur = make_profile(n, static_cast<float>(k));
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.valid);
		assert(near_eq(r.shift, static_cast<float>(k), 0.05f));
		assert(r.confidence > 0.8f);
	}
}

// ── 2. 서브픽셀 ─────────────────────────────────────────────────────────────
// 반동은 한 발에 몇 px 이라 정수 해상도로는 거칠다. 포물선 보간이 실제로 듣는지 본다.
// (정수만 반환한다면 0.5 짜리 시프트에서 오차가 0.5 가 되어 이 단언이 깨진다)
static void test_subpixel()
{
	const int n = 512;
	const std::vector<float> prev = make_profile(n, 0.0f);

	const float truth[] = { 0.25f, 0.5f, 1.5f, -0.75f, 3.4f, -7.6f };
	for (float k : truth)
	{
		const std::vector<float> cur = make_profile(n, k);
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.valid);
		// 포물선 보간의 편향을 감안해 0.2px. 정수 반올림(오차 0.5)과는 확실히 구분된다.
		assert(near_eq(r.shift, k, 0.2f));
		// 진짜 시프트가 정수 격자 사이일수록 격자 위 잔차가 남아 신뢰도 상한이 낮아진다
		// (0.5px 근처가 최악). 정확도는 그대로인데 신뢰도만 떨어지는 구간이라 기준을 나눈다.
		assert(r.confidence > 0.5f);
	}
}

// ── 3. 시프트 + 노이즈 ──────────────────────────────────────────────────────
static void test_shift_with_noise()
{
	const int n = 512;
	rng g(12345);
	std::vector<float> prev = make_profile(n, 0.0f);
	std::vector<float> cur = make_profile(n, 5.0f);
	for (int i = 0; i < n; ++i)
	{
		prev[static_cast<std::size_t>(i)] += g.uniform() * 3.0f;
		cur[static_cast<std::size_t>(i)] += g.uniform() * 3.0f;
	}

	const match_result r = match_profile(prev.data(), cur.data(), n, 32);
	assert(r.valid);
	assert(near_eq(r.shift, 5.0f, 0.5f));
	// 노이즈가 있으면 신뢰도는 떨어지지만 0 은 아니어야 한다 — 0 이면 실물에서
	// 모든 프레임이 쓰레기로 표시되어 기능이 성립하지 않는다.
	assert(r.confidence > 0.2f);
}

// ── 4. 시프트 + 머즐 플래시 ─────────────────────────────────────────────────
// 발사 순간 화면 일부가 확 밝아진다. 그 구간이 비용을 지배하면 답이 엉뚱해진다.
static void test_shift_with_muzzle_flash()
{
	const int n = 512;
	const std::vector<float> prev = make_profile(n, 0.0f);
	std::vector<float> cur = make_profile(n, 4.0f);

	// 가운데 40px 이 포화(255)되고 주변이 밝아진다 — 총구 화염.
	for (int i = 236; i < 276; ++i)
		cur[static_cast<std::size_t>(i)] = 255.0f;
	for (int i = 216; i < 236; ++i)
		cur[static_cast<std::size_t>(i)] += 60.0f;
	for (int i = 276; i < 296; ++i)
		cur[static_cast<std::size_t>(i)] += 60.0f;

	const match_result r = match_profile(prev.data(), cur.data(), n, 32);
	assert(r.valid);
	assert(near_eq(r.shift, 4.0f, 0.6f));
	assert(r.confidence > 0.15f);
}

// 화면 전체가 균일하게 밝아지거나(페이드) 배로 곱해져도 시프트는 그대로여야 한다.
// 1차 차분이 덧셈 변화를, RMS 정규화가 곱셈 변화를 지운다.
static void test_global_brightness_change()
{
	const int n = 512;
	const std::vector<float> prev = make_profile(n, 0.0f);

	std::vector<float> add = make_profile(n, 6.0f);
	for (float &v : add)
		v += 40.0f;
	const match_result ra = match_profile(prev.data(), add.data(), n, 32);
	assert(ra.valid && near_eq(ra.shift, 6.0f, 0.2f) && ra.confidence > 0.7f);

	std::vector<float> mul = make_profile(n, 6.0f);
	for (float &v : mul)
		v *= 1.6f;
	const match_result rm = match_profile(prev.data(), mul.data(), n, 32);
	assert(rm.valid && near_eq(rm.shift, 6.0f, 0.2f) && rm.confidence > 0.7f);
}

// ── 5. 무늬 없는 화면 ───────────────────────────────────────────────────────
// 하늘·벽만 보고 있으면 어떤 시프트든 비용이 같다. **자신 있게 틀린 답을 내면 안 된다.**
static void test_flat_profile_is_invalid()
{
	const int n = 512;
	std::vector<float> flat(static_cast<std::size_t>(n), 100.0f);
	const match_result r = match_profile(flat.data(), flat.data(), n, 32);
	assert(!r.valid);
	assert(r.confidence == 0.0f);

	// 완전 평평은 아니고 아주 미세한 잡음만 있는 경우도 무효여야 한다(하한 아래).
	rng g(777);
	std::vector<float> a(static_cast<std::size_t>(n)), b(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i)
	{
		a[static_cast<std::size_t>(i)] = 100.0f + g.uniform() * 0.05f;
		b[static_cast<std::size_t>(i)] = 100.0f + g.uniform() * 0.05f;
	}
	const match_result r2 = match_profile(a.data(), b.data(), n, 32);
	assert(!r2.valid);
}

// 서로 무관한 두 화면(장면 전환·순간이동)은 valid 이더라도 신뢰도가 낮아야 한다.
static void test_uncorrelated_low_confidence()
{
	const int n = 512;
	rng g(4242);
	std::vector<float> a(static_cast<std::size_t>(n)), b(static_cast<std::size_t>(n));
	for (int i = 0; i < n; ++i)
	{
		a[static_cast<std::size_t>(i)] = 128.0f + g.uniform() * 40.0f;
		b[static_cast<std::size_t>(i)] = 128.0f + g.uniform() * 40.0f;
	}
	const match_result r = match_profile(a.data(), b.data(), n, 32);
	assert(r.confidence < 0.25f);
}

// ── 6. 탐색 범위 끝 / 밖 ────────────────────────────────────────────────────
static void test_range_edge_and_beyond()
{
	const int n = 512;
	const std::vector<float> base = aperiodic_base(n + 2 * kPad);
	const std::vector<float> prev = window_of(base, n, 0);

	// 범위 안쪽(30, 31 < 32) — 맞고 신뢰도도 높다
	for (int k : { 30, 31 })
	{
		const std::vector<float> cur = window_of(base, n, k);
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.valid && near_eq(r.shift, static_cast<float>(k), 0.2f) && r.confidence > 0.6f);
	}
	// 딱 끝(±32) — 값은 맞지만 '잘린 것'과 구분할 방법이 없으므로 신뢰도 0 이다
	for (int k : { 32, -32 })
	{
		const std::vector<float> cur = window_of(base, n, k);
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.valid);
		assert(near_eq(r.shift, static_cast<float>(k), 0.5f));
		assert(r.confidence == 0.0f);
	}
	// 범위 밖 — 답은 아무 데나 붙지만 **신뢰도가 바닥**이라 화면에서 쓰레기로 보인다.
	// (실물에서 이 상황은 순간이동·컷신 전환·초고속 플릭이다)
	for (int k : { 33, 40, 60, -45 })
	{
		const std::vector<float> cur = window_of(base, n, k);
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.confidence < 0.05f);
	}
}

// 주기적인 화면(격자 무늬 벽·반복 텍스처)에서는 범위 밖 시프트가 범위 안으로 접힌다.
// 원리상 못 푸는 문제다 — 요구할 수 있는 것은 "**자신 있게** 틀리지는 말 것" 뿐이고,
// 그 선을 여기서 못 박는다. 실물에서 이 값은 신뢰도 필터에 걸려 회색으로 보인다.
static void test_periodic_alias_stays_unconfident()
{
	const int n = 512;
	const std::vector<float> prev = make_profile(n, 0.0f);
	for (float k : { 40.0f, 45.0f, 60.0f, 100.0f })
	{
		const std::vector<float> cur = make_profile(n, k);
		const match_result r = match_profile(prev.data(), cur.data(), n, 32);
		assert(r.confidence < 0.35f);
	}
}

// 프로파일이 너무 짧으면(비교 창이 안 나온다) 무효
static void test_too_short()
{
	const std::vector<float> p = make_profile(60, 0.0f);
	const match_result r = match_profile(p.data(), p.data(), 60, 32);
	assert(!r.valid);
	// max_shift 가 말이 안 되는 값이어도 죽지 않는다
	assert(!match_profile(p.data(), p.data(), 60, 0).valid);
	assert(!match_profile(nullptr, p.data(), 60, 4).valid);
}

// ── 7. 프로파일 만들기 (픽셀 → 1D) ──────────────────────────────────────────
// 합성 이미지를 몇 픽셀 밀어놓고, 프로파일 → 매칭이 그 시프트를 되찾는지 본다.
// build_profiles 와 match_profile 을 실제 순서대로 엮는 유일한 테스트다.
static void fill_image(std::vector<std::uint8_t> &img, int w, int h, std::size_t pitch, int sx, int sy)
{
	img.assign(pitch * static_cast<std::size_t>(h), 0);
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const float fx = static_cast<float>(x - sx);
			const float fy = static_cast<float>(y - sy);
			// ⚠️ x 에만·y 에만 의존하는 항이 반드시 있어야 한다. 두 축이 곱해진 무늬만
			// 있으면 투영(행 합/열 합)에서 서로 상쇄돼 프로파일이 평평해지고, 추정기는
			// (옳게) 무효를 반환한다 — 실물에서도 "무늬가 있는데 프로파일은 평평한" 화면이
			// 존재한다는 뜻이라 그 자체가 이 접근의 한계다. 여기서는 정상 케이스를 만든다.
			// 진폭 합이 115 라 0..255 를 넘지 않는다(클램프가 시프트 등가성을 깨지 않게).
			const float v = 128.0f
				+ 35.0f * std::sin(fx * 0.29f)
				+ 25.0f * std::sin(fy * 0.31f)
				+ 40.0f * std::sin(fx * 0.13f) * std::cos(fy * 0.09f)
				+ 15.0f * std::sin(fx * 0.47f + fy * 0.05f);
			const int c = static_cast<int>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
			std::uint8_t *const px = img.data() + static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 4u;
			px[0] = static_cast<std::uint8_t>(c / 2);
			px[1] = static_cast<std::uint8_t>(c); // 초록 — 실제로 읽는 채널
			px[2] = static_cast<std::uint8_t>(255 - c);
			px[3] = 0xFF;
		}
	}
}

static void test_build_profiles_roundtrip()
{
	const int w = 256, h = 256;
	const std::size_t pitch = static_cast<std::size_t>(w) * 4u + 64u; // 정렬 패딩이 있는 실제 리드백 흉내
	std::vector<std::uint8_t> a, b;
	std::vector<float> av(static_cast<std::size_t>(h)), ah(static_cast<std::size_t>(w));
	std::vector<float> bv(static_cast<std::size_t>(h)), bh(static_cast<std::size_t>(w));

	fill_image(a, w, h, pitch, 0, 0);
	fill_image(b, w, h, pitch, 3, 7); // 오른쪽 3, 아래 7

	assert(build_profiles(a.data(), w, h, pitch, pixel_kind::rgba8, 4, av.data(), ah.data()));
	assert(build_profiles(b.data(), w, h, pitch, pixel_kind::rgba8, 4, bv.data(), bh.data()));

	const match_result rv = match_profile(av.data(), bv.data(), h, 32);
	const match_result rh = match_profile(ah.data(), bh.data(), w, 32);
	assert(rv.valid && rh.valid);
	assert(near_eq(rv.shift, 7.0f, 0.4f));
	assert(near_eq(rh.shift, 3.0f, 0.4f));
	assert(rv.confidence > 0.5f && rh.confidence > 0.5f);

	// 값 범위 검사 — 평균 채널값이므로 0..255 안에 있어야 한다
	for (int i = 0; i < h; ++i)
		assert(av[static_cast<std::size_t>(i)] >= 0.0f && av[static_cast<std::size_t>(i)] <= 255.0f);

	// 잘못된 입력 방어
	assert(!build_profiles(nullptr, w, h, pitch, pixel_kind::rgba8, 4, av.data(), ah.data()));
	assert(!build_profiles(a.data(), w, h, 4u, pixel_kind::rgba8, 4, av.data(), ah.data())); // pitch 가 폭보다 작다
	assert(!build_profiles(a.data(), w, h, pitch, pixel_kind::rgba8, 0, av.data(), ah.data()));
	assert(!build_profiles(a.data(), 0, h, pitch, pixel_kind::rgba8, 4, av.data(), ah.data()));
}

// 10비트 포맷도 같은 자리(비트 10..19)에서 초록을 읽는다.
static void test_build_profiles_rgb10a2()
{
	const int w = 128, h = 128;
	const std::size_t pitch = static_cast<std::size_t>(w) * 4u;
	std::vector<std::uint8_t> img(pitch * static_cast<std::size_t>(h));
	std::vector<float> v(static_cast<std::size_t>(h)), hh(static_cast<std::size_t>(w));

	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
		{
			const std::uint32_t g = static_cast<std::uint32_t>((x * 8) % 1024);
			const std::uint32_t px = (g << 10) | 0x3FFu | (3u << 30);
			std::memcpy(img.data() + static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x) * 4u, &px, sizeof(px));
		}

	assert(build_profiles(img.data(), w, h, pitch, pixel_kind::rgb10a2, 1, v.data(), hh.data()));
	// x=0 열은 초록 0, x=127 열은 (127*8)=1016 → 1016/1023*255 ≈ 253.3
	assert(near_eq(hh[0], 0.0f, 0.01f));
	assert(near_eq(hh[127], 1016.0f * 255.0f / 1023.0f, 0.05f));
	// 모든 행이 같은 무늬라 세로 프로파일은 상수여야 한다
	for (int y = 1; y < h; ++y)
		assert(near_eq(v[static_cast<std::size_t>(y)], v[0], 0.01f));
}

// ── 8. tracker ──────────────────────────────────────────────────────────────
// 부호 규약을 못 박는다: **마우스를 오른쪽으로 밀면 화면 dx 도 양수**여야 한다.
// (마우스 오른쪽 → 카메라 오른쪽 → 화면 내용은 왼쪽으로 흐름 → 내용 시프트는 음수 →
//  카메라 환산은 그 부호를 뒤집은 양수) 검증 2단계에서 눈으로 보는 성질이 이것이다.
static void test_tracker_sign_and_recoil()
{
	const int n = 512;
	tracker t;

	const std::vector<float> p0 = make_profile(n, 0.0f);
	// 첫 프레임은 비교 대상이 없으므로 sample 을 만들지 않는다
	assert(!t.update(p0.data(), n, p0.data(), n, 32, 0.0f, 0.0f));
	assert(t.count() == 0);

	// 내용이 왼쪽·위로 −6 만큼 흘렀다 = 카메라가 오른쪽·아래로 +6 만큼 돌았다.
	// 마우스도 같은 프레임에 (+6, +6) 이었다면 반동은 0 이다.
	const std::vector<float> p1 = make_profile(n, -6.0f);
	assert(t.update(p1.data(), n, p1.data(), n, 32, 6.0f, 6.0f));
	assert(t.count() == 1);

	const sample &s = t.last();
	assert(s.valid);
	assert(near_eq(s.screen_dx, 6.0f, 0.3f));
	assert(near_eq(s.screen_dy, 6.0f, 0.3f));
	assert(near_eq(s.recoil_dx(), 0.0f, 0.3f));
	assert(near_eq(s.recoil_dy(), 0.0f, 0.3f));

	// 마우스는 가만히 있는데 화면이 위로 튀었다 = 반동. dy 는 음수(위 = −y).
	const std::vector<float> p2 = make_profile(n, -6.0f + 9.0f); // 내용이 아래로 +9 → 카메라 위로 −9
	assert(t.update(p2.data(), n, p2.data(), n, 32, 0.0f, 0.0f));
	const sample &s2 = t.last();
	assert(near_eq(s2.screen_dy, -9.0f, 0.4f));
	assert(near_eq(s2.recoil_dy(), -9.0f, 0.4f));
	assert(s2.recoil_dy() < -1.0f); // 위로 튄 것이 확실히 보인다
}

// 크기가 바뀌면(해상도 변경) 이전 프레임을 버리고 다시 시작한다 — 안 그러면
// 길이가 다른 두 프로파일을 비교해 쓰레기가 나오거나 범위를 넘어 읽는다.
static void test_tracker_size_change_and_drop()
{
	tracker t;
	const std::vector<float> a = make_profile(512, 0.0f);
	const std::vector<float> b = make_profile(256, 0.0f);

	assert(!t.update(a.data(), 512, a.data(), 512, 32, 0, 0));
	assert(t.update(a.data(), 512, a.data(), 512, 32, 0, 0));
	assert(!t.update(b.data(), 256, b.data(), 256, 32, 0, 0)); // 크기 변경 → 재장전
	assert(t.update(b.data(), 256, b.data(), 256, 32, 0, 0));

	t.drop_prev();
	const int before = t.count();
	assert(!t.update(b.data(), 256, b.data(), 256, 32, 0, 0)); // 버렸으니 다시 첫 프레임
	assert(t.count() == before);                               // 기록은 남는다

	t.reset();
	assert(t.count() == 0);
	// 기록이 없을 때 at()/last() 를 불러도 죽지 않는다
	assert(!t.last().valid);
	assert(!t.at(-1).valid);
	assert(!t.at(9999).valid);
}

// 링버퍼가 한 바퀴 돌아도 순서(0 = 가장 오래된 것)가 유지된다
static void test_tracker_ring()
{
	tracker t;
	const int n = 512;
	std::vector<std::vector<float>> profs;
	profs.reserve(static_cast<std::size_t>(kHistory) + 40);

	// 매 프레임 1px 씩 아래로 흐르게 해서 sample 을 kHistory + 30 개 만든다
	for (int i = 0; i < kHistory + 31; ++i)
		profs.push_back(make_profile(n, static_cast<float>(i % 17)));

	for (int i = 0; i < kHistory + 31; ++i)
		t.update(profs[static_cast<std::size_t>(i)].data(), n, profs[static_cast<std::size_t>(i)].data(), n, 32,
			static_cast<float>(i), 0.0f);

	assert(t.count() == kHistory);
	// 마지막 sample 의 마우스 값은 마지막 프레임 것이어야 한다
	assert(near_eq(t.last().mouse_dx, static_cast<float>(kHistory + 30), 0.01f));
	// 0 번은 그보다 kHistory-1 프레임 전 것
	assert(near_eq(t.at(0).mouse_dx, static_cast<float>(kHistory + 30 - (kHistory - 1)), 0.01f));
	// 순서가 단조 증가여야 한다(링이 꼬이면 여기서 깨진다)
	for (int i = 1; i < t.count(); ++i)
		assert(t.at(i).mouse_dx > t.at(i - 1).mouse_dx);
}

int main()
{
	test_exact_integer_shift();
	test_subpixel();
	test_shift_with_noise();
	test_shift_with_muzzle_flash();
	test_global_brightness_change();
	test_flat_profile_is_invalid();
	test_uncorrelated_low_confidence();
	test_range_edge_and_beyond();
	test_periodic_alias_stays_unconfident();
	test_too_short();
	test_build_profiles_roundtrip();
	test_build_profiles_rgb10a2();
	test_tracker_sign_and_recoil();
	test_tracker_size_change_and_drop();
	test_tracker_ring();

	std::printf("sherbet_motion_test: all passed\n");
	return 0;
}
