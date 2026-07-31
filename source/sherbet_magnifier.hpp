/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet HUD 돋보기 — 좌표 계산 순수 로직.
//
// 하는 일: 화면의 한 사각형(예: 체력·방어구 막대)을 그대로 확대해 다시 그린다.
// **게임 상태를 읽지 않는다.** 픽셀을 확대할 뿐이라 메모리 접근도 상태 판정도 없다.
//
// 왜 순수 헤더인가: 맥에서 runtime_gui.cpp 를 컴파일할 수 없어 CI 가 유일한 검증인데,
// "FHD·2K·4K 에서 같은 자리를 잡는가" 는 빌드가 통과해도 알 수 없다. 좌표 계산을
// 여기로 빼면 tools/sherbet_magnifier_test.cpp 가 해상도별로 단언한다.
//
// ⚠️ 좌표 관례: **화면 대비 정규화(0~1)**. 반반 비교 분할선(_sherbet_compare_split)과 같다.
//    OSD 의 정규화(_sherbet_osd_x)는 '화면 - 위젯크기' 대비라 관례가 **다르다** — 베끼지 말 것.
// ⚠️ 기준 해상도는 **출력 해상도**(_width/_height)다. '효과 처리 해상도'
//    (_effect_permutations[0].width)와 다를 수 있고, 돋보기가 읽는 텍스처는 출력 기준이다.
#pragma once

namespace sherbet
{
	namespace mag
	{
		// 정규화 사각형(좌상단 기준). 전부 0~1.
		struct rect { float x = 0.80f, y = 0.90f, w = 0.15f, h = 0.06f; };

		// 복사용 소스 픽셀 박스(반열림 구간 [x0,x1) ).
		struct box { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };

		inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

		// 드래그로 잡은 두 점 → 정규화 사각형. **뒤집어 끌어도**(오른쪽→왼쪽, 아래→위)
		// 올바른 사각형이 나온다. 이걸 안 하면 사용자가 반대로 끌었을 때 w/h 가 음수가 되어
		// 복사 박스가 비고 "켰는데 아무것도 안 보임" 이 된다.
		inline rect from_drag(float ax, float ay, float bx, float by)
		{
			// ⚠️ 끝점을 **먼저** 0~1 로 자른다. 폭을 나중에 자르면 x+w 가 1 을 넘을 수 있다
			//    (예: y 를 1.8 에서 0.5 로 끌면 h=1.3 → 잘라도 1.0 인데 y=0.5 라 합이 1.5).
			//    호스트 테스트 test_drag_outside_screen 이 실제로 이 버그를 잡았다.
			ax = clamp01(ax); ay = clamp01(ay);
			bx = clamp01(bx); by = clamp01(by);
			rect r;
			r.x = ax < bx ? ax : bx;
			r.y = ay < by ? ay : by;
			r.w = ax < bx ? bx - ax : ax - bx;
			r.h = ay < by ? by - ay : ay - by;
			return r;
		}

		// 화면 안으로 넣고 최소 크기를 보장한다.
		// 최소 크기가 필요한 이유: 폭이나 높이가 0 인 박스로 copy_texture_region 을 부르면
		// 아무것도 복사되지 않고, 사용자에게는 '고장' 으로 보인다. 실수로 클릭만 해도
		// 그렇게 되므로 여기서 막는다.
		inline rect sanitize(rect r, float min_size = 0.01f)
		{
			r.x = clamp01(r.x); r.y = clamp01(r.y);
			r.w = clamp01(r.w); r.h = clamp01(r.h);
			if (r.w < min_size) r.w = min_size;
			if (r.h < min_size) r.h = min_size;
			if (r.x + r.w > 1.0f) r.x = 1.0f - r.w;
			if (r.y + r.h > 1.0f) r.y = 1.0f - r.h;
			if (r.x < 0.0f) r.x = 0.0f;
			if (r.y < 0.0f) r.y = 0.0f;
			return r;
		}

		// 정규화 사각형 → 소스 픽셀 박스. 해상도가 무엇이든 같은 상대 위치를 가리킨다.
		// 반환 박스는 항상 화면 안이고 폭·높이가 최소 1 이다(빈 박스로 복사하지 않는다).
		inline box source_box(const rect &in, int screen_w, int screen_h)
		{
			const rect r = sanitize(in);
			box b;
			if (screen_w <= 0 || screen_h <= 0)
				return b; // 호출부가 0 크기를 보고 건너뛴다
			b.x0 = static_cast<int>(r.x * screen_w);
			b.y0 = static_cast<int>(r.y * screen_h);
			b.x1 = static_cast<int>((r.x + r.w) * screen_w);
			b.y1 = static_cast<int>((r.y + r.h) * screen_h);
			if (b.x1 > screen_w) b.x1 = screen_w;
			if (b.y1 > screen_h) b.y1 = screen_h;
			if (b.x0 < 0) b.x0 = 0;
			if (b.y0 < 0) b.y0 = 0;
			if (b.x1 <= b.x0) b.x0 = (b.x1 > 0 ? b.x1 - 1 : 0), b.x1 = b.x0 + 1;
			if (b.y1 <= b.y0) b.y0 = (b.y1 > 0 ? b.y1 - 1 : 0), b.y1 = b.y0 + 1;
			return b;
		}

		inline int box_w(const box &b) { return b.x1 - b.x0; }
		inline int box_h(const box &b) { return b.y1 - b.y0; }

		// 확대 결과를 그릴 좌상단 좌표. anchor 는 정규화(0~1)이고,
		// **화면 밖으로 나가지 않게 클램프**한다 — 4K 에서 잡은 걸 FHD 에서 켜면
		// 확대창이 화면 밖으로 밀려 안 보이는 사고가 난다.
		inline void dest_pos(float anchor_x, float anchor_y, float dw, float dh,
			int screen_w, int screen_h, float &out_x, float &out_y)
		{
			const float sw = static_cast<float>(screen_w);
			const float sh = static_cast<float>(screen_h);
			out_x = clamp01(anchor_x) * sw - dw * 0.5f; // 앵커는 확대창의 **중심**
			out_y = clamp01(anchor_y) * sh - dh * 0.5f;
			if (out_x + dw > sw) out_x = sw - dw;
			if (out_y + dh > sh) out_y = sh - dh;
			if (out_x < 0.0f) out_x = 0.0f;
			if (out_y < 0.0f) out_y = 0.0f;
		}

		// 확정해도 되는 크기인가. sanitize 는 최소 크기를 **만들어 주므로** 클릭 한 번(폭 0)도
		// 통과해 버린다 — 크래시는 없지만 사용자는 "엉뚱한 데가 확대됨" 을 본다.
		//
		// ⚠️ **판정은 픽셀 기준이어야 한다.** 처음엔 정규화 0.02 로 잡았는데, 그건 세로로
		//    FHD 22px / 2K 29px / 4K 43px 을 요구한다. 이 기능의 **주 용도인 체력 막대는
		//    높이가 10px 안팎의 얇은 가로 막대**라, 막대만 딱 감싸면 어느 해상도에서도
		//    거부됐다. 실제로 "드래그해도 아무것도 안 뜬다" 는 신고로 나타났다.
		//    "실수로 클릭했다"(0px)와 "얇지만 일부러 끌었다"를 가르면 충분하다.
		inline bool is_usable(const rect &r, int screen_w, int screen_h, int min_px = 6)
		{
			return r.w * screen_w >= static_cast<float>(min_px)
				&& r.h * screen_h >= static_cast<float>(min_px);
		}

		// 배율 범위. 슬라이더(runtime_gui.cpp)와 **반드시 같은 값**이어야 한다 —
		// 다르면 슬라이더를 올려도 여기서 되돌려져 "슬라이더가 안 먹는다" 로 보인다.
		inline float clamp_zoom(float z)
		{
			return z < 1.0f ? 1.0f : (z > 10.0f ? 10.0f : z);
		}
	}
}
