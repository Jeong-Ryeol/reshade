/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_magnifier.hpp 호스트 테스트.
// 사용자 최우선 요구가 "FHD·2K·4K 전부에서 되게" 이고, 그건 CI 빌드가 통과해도
// 확인되지 않는다. 여기서 해상도별로 못 박는다.
#include "sherbet_magnifier.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace sherbet::mag;

static const int kFHD_W = 1920, kFHD_H = 1080;
static const int k2K_W  = 2560, k2K_H  = 1440;
static const int k4K_W  = 3840, k4K_H  = 2160;

static bool near_eq(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

// 같은 정규화 사각형은 어떤 해상도에서도 **같은 상대 위치**를 가리킨다.
static void test_same_relative_position_across_resolutions()
{
	rect r; r.x = 0.80f; r.y = 0.90f; r.w = 0.15f; r.h = 0.06f;

	const box fhd = source_box(r, kFHD_W, kFHD_H);
	const box k2  = source_box(r, k2K_W,  k2K_H);
	const box k4  = source_box(r, k4K_W,  k4K_H);

	// 좌상단이 화면 폭 대비 같은 비율인가
	assert(near_eq(fhd.x0 / float(kFHD_W), 0.80f, 0.002f));
	assert(near_eq(k2.x0  / float(k2K_W),  0.80f, 0.002f));
	assert(near_eq(k4.x0  / float(k4K_W),  0.80f, 0.002f));

	// 크기도 비율이 같은가
	assert(near_eq(box_w(fhd) / float(kFHD_W), 0.15f, 0.002f));
	assert(near_eq(box_w(k2)  / float(k2K_W),  0.15f, 0.002f));
	assert(near_eq(box_w(k4)  / float(k4K_W),  0.15f, 0.002f));

	// 4K 박스는 FHD 박스의 정확히 2배 근처여야 한다(3840/1920 = 2)
	assert(std::abs(box_w(k4) - box_w(fhd) * 2) <= 2);
	assert(std::abs(box_h(k4) - box_h(fhd) * 2) <= 2);
}

// 박스는 절대 화면 밖으로 나가지 않는다. 나가면 copy_texture_region 이 실패한다.
static void test_box_never_exceeds_screen()
{
	const int W[] = { kFHD_W, k2K_W, k4K_W, 1280, 3440 /*울트라와이드*/ };
	const int H[] = { kFHD_H, k2K_H, k4K_H, 720,  1440 };
	for (int i = 0; i < 5; ++i)
	{
		// 일부러 화면 밖으로 나가는 사각형을 넣는다
		rect bad; bad.x = 0.95f; bad.y = 0.98f; bad.w = 0.50f; bad.h = 0.50f;
		const box b = source_box(bad, W[i], H[i]);
		assert(b.x0 >= 0 && b.y0 >= 0);
		assert(b.x1 <= W[i] && b.y1 <= H[i]);
		assert(box_w(b) >= 1 && box_h(b) >= 1);
	}
}

// 폭이나 높이가 0 인 박스로는 절대 복사하지 않는다(실수로 클릭만 해도 그렇게 된다).
static void test_never_empty_box()
{
	rect zero; zero.x = 0.5f; zero.y = 0.5f; zero.w = 0.0f; zero.h = 0.0f;
	for (int w = 1; w <= 4096; w *= 4)
	{
		const box b = source_box(zero, w, w);
		assert(box_w(b) >= 1 && box_h(b) >= 1);
	}
	// 아주 작은 화면에서도
	const box tiny = source_box(zero, 1, 1);
	assert(box_w(tiny) >= 1 && box_h(tiny) >= 1);
}

// 뒤집어 끌어도(오른쪽→왼쪽, 아래→위) 올바른 사각형이 나온다.
static void test_reversed_drag()
{
	const rect a = from_drag(0.9f, 0.9f, 0.7f, 0.8f); // 우하 → 좌상
	assert(near_eq(a.x, 0.7f) && near_eq(a.y, 0.8f));
	assert(near_eq(a.w, 0.2f) && near_eq(a.h, 0.1f));

	const rect b = from_drag(0.7f, 0.8f, 0.9f, 0.9f); // 정방향 — 같은 결과
	assert(near_eq(a.x, b.x) && near_eq(a.y, b.y));
	assert(near_eq(a.w, b.w) && near_eq(a.h, b.h));
}

// 화면 밖을 끌어도 0~1 안으로 들어온다.
static void test_drag_outside_screen()
{
	const rect r = from_drag(-0.5f, 1.8f, 0.5f, 0.5f);
	assert(r.x >= 0.0f && r.y >= 0.0f);
	assert(r.x + r.w <= 1.001f && r.y + r.h <= 1.001f);
}

// 확대창은 화면 밖으로 밀려나지 않는다 — 4K 에서 잡은 걸 FHD 에서 켜는 경우가 실제로 있다.
static void test_dest_stays_on_screen()
{
	float x = 0, y = 0;
	// 오른쪽 끝 앵커 + 큰 확대창
	dest_pos(1.0f, 1.0f, 800.0f, 400.0f, kFHD_W, kFHD_H, x, y);
	assert(x >= 0.0f && y >= 0.0f);
	assert(x + 800.0f <= kFHD_W + 0.01f);
	assert(y + 400.0f <= kFHD_H + 0.01f);

	// 왼쪽 위 앵커
	dest_pos(0.0f, 0.0f, 800.0f, 400.0f, kFHD_W, kFHD_H, x, y);
	assert(near_eq(x, 0.0f) && near_eq(y, 0.0f));

	// 확대창이 화면보다 크면 (0,0) 에 붙는다 — 밖으로 나가는 것보다 낫다
	dest_pos(0.5f, 0.5f, 5000.0f, 5000.0f, kFHD_W, kFHD_H, x, y);
	assert(near_eq(x, 0.0f) && near_eq(y, 0.0f));
}

// 앵커는 확대창의 **중심**이다(가운데 정렬이 직관적이다).
static void test_anchor_is_center()
{
	float x = 0, y = 0;
	dest_pos(0.5f, 0.5f, 400.0f, 200.0f, kFHD_W, kFHD_H, x, y);
	assert(near_eq(x + 200.0f, kFHD_W * 0.5f));
	assert(near_eq(y + 100.0f, kFHD_H * 0.5f));
}

static void test_clamp_zoom()
{
	assert(near_eq(clamp_zoom(0.1f), 1.5f));   // 축소는 의미 없다
	assert(near_eq(clamp_zoom(100.0f), 6.0f)); // 화면을 다 덮으면 안 된다
	assert(near_eq(clamp_zoom(3.0f), 3.0f));
}

// 해상도 0 은 초기화 전이나 최소화 상태에서 실제로 들어온다 — 크래시하면 안 된다.
static void test_zero_resolution_is_safe()
{
	rect r;
	const box b = source_box(r, 0, 0);
	assert(box_w(b) == 0 && box_h(b) == 0); // 호출부가 0 을 보고 건너뛴다
}

// 실수로 클릭만 한 경우를 확정하면 안 된다(sanitize 가 최소 크기를 만들어 주므로 그냥 통과한다).
static void test_is_usable_rejects_accidental_click()
{
	// 클릭만 한 경우는 어느 해상도에서도 거부
	assert(!is_usable(from_drag(0.5f, 0.5f, 0.5f, 0.5f), kFHD_W, kFHD_H));
	assert(!is_usable(from_drag(0.5f, 0.5f, 0.5f, 0.5f), k4K_W, k4K_H));

	// ⚠️ 이 기능의 **주 용도**: 얇은 가로 막대(체력바). 높이 10px 안팎이어도 통과해야 한다.
	//    예전 정규화 0.02 기준은 세로 FHD 22px / 4K 43px 을 요구해 이걸 전부 거부했고,
	//    그게 "드래그해도 아무것도 안 뜬다" 신고의 원인이었다.
	{
		// FHD 에서 폭 210px, 높이 10px 짜리 막대
		const float w = 210.0f / kFHD_W, h = 10.0f / kFHD_H;
		rect bar; bar.x = 0.80f; bar.y = 0.95f; bar.w = w; bar.h = h;
		assert(is_usable(bar, kFHD_W, kFHD_H));
	}
	{
		// 4K 에서 같은 물리 크기(픽셀 고정 HUD) — 20px 높이
		const float w = 420.0f / k4K_W, h = 20.0f / k4K_H;
		rect bar; bar.x = 0.80f; bar.y = 0.95f; bar.w = w; bar.h = h;
		assert(is_usable(bar, k4K_W, k4K_H));
	}
	// 5px 미만은 실수로 본다
	{
		rect tiny; tiny.x = 0.5f; tiny.y = 0.5f; tiny.w = 200.0f / kFHD_W; tiny.h = 3.0f / kFHD_H;
		assert(!is_usable(tiny, kFHD_W, kFHD_H));
	}
	rect r; r.x = 0.80f; r.y = 0.90f; r.w = 0.15f; r.h = 0.06f;
	assert(is_usable(r, kFHD_W, kFHD_H));
	assert(is_usable(r, k2K_W, k2K_H));
	assert(is_usable(r, k4K_W, k4K_H));
}

// 확대창이 소스 위에 겹치면 이중 Present 프레임에서 중첩이 쌓인다 — 겹침을 정확히 판정해야 한다.
static void test_overlap_detection()
{
	rect src; src.x = 0.80f; src.y = 0.90f; src.w = 0.15f; src.h = 0.06f;

	for (int i = 0; i < 3; ++i)
	{
		const int W = (i == 0 ? kFHD_W : i == 1 ? k2K_W : k4K_W);
		const int H = (i == 0 ? kFHD_H : i == 1 ? k2K_H : k4K_H);
		const box b = source_box(src, W, H);
		const float dw = box_w(b) * 3.0f, dh = box_h(b) * 3.0f;

		// 기본 표시 위치(0.5, 0.30) — 화면 위쪽 가운데라 우하단 소스와 안 겹친다
		float x = 0, y = 0;
		dest_pos(0.5f, 0.30f, dw, dh, W, H, x, y);
		assert(!overlaps(src, x, y, dw, dh, W, H));

		// 소스 바로 위로 옮기면 겹친다
		dest_pos(0.87f, 0.93f, dw, dh, W, H, x, y);
		assert(overlaps(src, x, y, dw, dh, W, H));
	}
}

int main()
{
	test_same_relative_position_across_resolutions();
	test_box_never_exceeds_screen();
	test_never_empty_box();
	test_reversed_drag();
	test_drag_outside_screen();
	test_dest_stays_on_screen();
	test_anchor_is_center();
	test_clamp_zoom();
	test_zero_resolution_is_safe();
	test_is_usable_rejects_accidental_click();
	test_overlap_detection();
	std::puts("sherbet_magnifier_test: ALL PASS");
	return 0;
}
