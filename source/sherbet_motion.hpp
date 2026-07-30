/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 화면 이동 추정(실험) — 순수 로직(플랫폼 비의존).
// Windows·D3D·ImGui 는 여기 넣지 않는다. 픽셀을 떠 오는 것은 runtime.cpp,
// 그리는 것은 runtime_gui.cpp 가 맡는다.
// 판정 로직 100% 를 여기 몰아넣어 맥 clang 으로 실제 단위테스트한다(tools/sherbet_motion_test.cpp).
// 실물 프레임에서의 정확도는 맥에서 확인할 방법이 없으므로, **합성 데이터로 정답을 아는
// 상황**(정해진 시프트 · 노이즈 · 머즐 플래시 · 무늬 없는 화면 · 탐색범위 밖)을 전부 못 박는다.
//
// ── 무엇을 재는가 ────────────────────────────────────────────────────────────
//   화면 이동 = 게임 반동 + 내 마우스 이동   →   반동 = 화면 이동 − 마우스 이동
// 마우스 이동은 이미 있다(input::raw_mouse_delta_x/y). 없는 항이 **화면 이동**이고
// 이 파일이 그걸 추정한다. 2D 매칭은 프레임당 비용이 말이 안 되므로 1D 투영으로 줄인다:
//   행 합 → 세로 프로파일(길이 h) · 열 합 → 가로 프로파일(길이 w)
//   각각 이전 프레임 것과 ±max_shift 범위에서 맞춰보고 차이가 최소인 시프트를 고른다.
// 반동은 대부분 세로라 세로 프로파일이 본체다.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sherbet
{
	namespace motion
	{
		// 백버퍼 픽셀 형식. 초록 채널 하나만 쓴다 — 휘도의 대부분이 초록이고,
		// RGBA8 과 BGRA8 **둘 다 초록이 바이트 1번**이라 분기가 필요 없다.
		// R10G10B10A2 와 B10G10R10A2 도 초록이 같은 비트 자리(10..19)다.
		enum class pixel_kind
		{
			rgba8 = 0,   // r8g8b8a8 / b8g8r8a8 (srgb 포함) — 8비트 4채널
			rgb10a2 = 1, // r10g10b10a2 / b10g10r10a2
		};

		// 그래디언트 RMS 하한(채널 레벨/px). 이보다 평평하면 "특징이 없다"고 보고
		// 자신 있게 틀린 답을 내는 대신 무효를 반환한다(하늘만 보고 있을 때).
		constexpr float kFlatFloor = 0.30f;
		// 개별 원소 차이의 상한(정규화 단위의 제곱). 머즐 플래시처럼 일부 구간만
		// 확 망가지는 경우 그 구간이 비용 전체를 지배하지 못하게 자른다.
		constexpr float kOutlierCap = 4.0f;
		// 서로 무관한 두 정규화 신호의 기대 제곱차 = 2. 비용이 여기 가까우면 매칭 실패다.
		constexpr float kUncorrelated = 2.0f;
		// 최저점 주변 이 반경 안은 '같은 봉우리'로 보고 2등 후보에서 제외한다.
		constexpr int kPeakRadius = 3;

		// data 는 행 우선, 행 간격은 row_pitch 바이트. w×h 픽셀.
		// step 은 샘플 간격(1 이면 전부) — 세로 프로파일은 열을 step 으로 건너뛰고,
		// 가로 프로파일은 행을 step 으로 건너뛴다. **프로파일 자체의 길이는 줄지 않는다**
		// (세로는 행마다 한 칸, 가로는 열마다 한 칸) — 줄이면 시프트 해상도가 그만큼 나빠진다.
		// vprof[h], hprof[w] 에 평균 채널값(0..255)을 쓴다.
		inline bool build_profiles(const void *data, int w, int h, std::size_t row_pitch, pixel_kind kind, int step, float *vprof, float *hprof)
		{
			if (data == nullptr || vprof == nullptr || hprof == nullptr)
				return false;
			if (w <= 0 || h <= 0 || step <= 0)
				return false;
			if (row_pitch < static_cast<std::size_t>(w) * 4u)
				return false;

			const auto *const base = static_cast<const std::uint8_t *>(data);

			for (int x = 0; x < w; ++x)
				hprof[x] = 0.0f;

			int rows_sampled = 0;
			const int cols_sampled = (w + step - 1) / step;

			for (int y = 0; y < h; ++y)
			{
				const std::uint8_t *const row = base + static_cast<std::size_t>(y) * row_pitch;
				const bool take_row = (y % step) == 0;

				float sum = 0.0f;
				if (kind == pixel_kind::rgba8)
				{
					for (int x = 0; x < w; x += step)
						sum += static_cast<float>(row[static_cast<std::size_t>(x) * 4u + 1u]);

					if (take_row)
					{
						for (int x = 0; x < w; ++x)
							hprof[x] += static_cast<float>(row[static_cast<std::size_t>(x) * 4u + 1u]);
					}
				}
				else
				{
					// 정렬을 가정할 수 없으므로 memcpy 로 읽는다(엄격한 앨리어싱·비정렬 UB 회피).
					for (int x = 0; x < w; x += step)
					{
						std::uint32_t v = 0;
						std::memcpy(&v, row + static_cast<std::size_t>(x) * 4u, sizeof(v));
						sum += static_cast<float>((v >> 10) & 0x3FFu) * (255.0f / 1023.0f);
					}

					if (take_row)
					{
						for (int x = 0; x < w; ++x)
						{
							std::uint32_t v = 0;
							std::memcpy(&v, row + static_cast<std::size_t>(x) * 4u, sizeof(v));
							hprof[x] += static_cast<float>((v >> 10) & 0x3FFu) * (255.0f / 1023.0f);
						}
					}
				}

				vprof[y] = sum / static_cast<float>(cols_sampled);
				if (take_row)
					rows_sampled++;
			}

			if (rows_sampled == 0)
				return false;

			const float inv_rows = 1.0f / static_cast<float>(rows_sampled);
			for (int x = 0; x < w; ++x)
				hprof[x] *= inv_rows;

			return true;
		}

		struct match_result
		{
			// cur[i] ≈ prev[i - shift] 를 만족하는 시프트. **양수 = 화면 내용이 인덱스가
			// 커지는 쪽(아래/오른쪽)으로 이동**했다는 뜻이다. 부호를 뒤집는 것은 tracker 몫.
			float shift = 0.0f;
			float confidence = 0.0f; // 0..1. 0 이면 숫자를 믿지 말라는 뜻이다
			bool valid = false;      // 입력 자체가 못 쓸 것(너무 짧다/평평하다)이면 false
		};

		// match_profile 이 프레임마다 vector 를 새로 잡지 않도록 호출 측이 들고 다니는 버퍼.
		struct scratch
		{
			std::vector<float> gp, gc;
		};

		// prev/cur 두 프로파일을 ±max_shift 범위에서 맞춰본다.
		//
		// 처리 순서와 이유:
		//  1) 1차 차분(그래디언트) — 화면 전체가 같은 양만큼 밝아지는 변화(플래시·페이드)를 지운다
		//  2) RMS 로 정규화 — 밝기가 배로 곱해지는 변화를 지운다
		//  3) 절단 제곱차(kOutlierCap) 합 — 일부 구간만 망가진 경우(머즐 플래시가 화면 일부를
		//     태움) 그 구간이 비용을 지배하지 못하게 자른다. 제곱을 쓰는 이유는 최저점 부근이
		//     포물선 모양이 되어 서브픽셀 보간이 성립하기 때문이다
		//  4) 최저점 ±1 로 포물선 보간 — 반동은 한 발에 몇 px 이라 정수 해상도로는 너무 거칠다
		inline match_result match_profile(const float *prev, const float *cur, int n, int max_shift, scratch &sc, float flat_floor = kFlatFloor)
		{
			match_result r;

			if (prev == nullptr || cur == nullptr || max_shift < 1)
				return r;

			const int m = n - 1; // 그래디언트 길이
			// 모든 시프트에서 **같은 개수**의 원소를 비교해야 비용을 서로 견줄 수 있다.
			// 그래서 비교 창을 [max_shift, m - max_shift) 로 고정한다 — 그만큼은 남아야 한다.
			if (m < 2 * max_shift + 8)
				return r;

			sc.gp.resize(static_cast<std::size_t>(m));
			sc.gc.resize(static_cast<std::size_t>(m));
			float *const gp = sc.gp.data();
			float *const gc = sc.gc.data();

			double sp = 0.0, scq = 0.0;
			for (int i = 0; i < m; ++i)
			{
				gp[i] = prev[i + 1] - prev[i];
				gc[i] = cur[i + 1] - cur[i];
				sp += static_cast<double>(gp[i]) * gp[i];
				scq += static_cast<double>(gc[i]) * gc[i];
			}

			const float rms_p = static_cast<float>(std::sqrt(sp / m));
			const float rms_c = static_cast<float>(std::sqrt(scq / m));
			// 평평한 화면 — 어떤 시프트든 비용이 똑같아서 최저점이 사실상 랜덤이다.
			// 그럴듯한 숫자를 내는 것이 조용히 틀리는 것보다 나쁘다.
			if (rms_p < flat_floor || rms_c < flat_floor)
				return r;

			const float inv_p = 1.0f / rms_p, inv_c = 1.0f / rms_c;
			for (int i = 0; i < m; ++i)
			{
				gp[i] *= inv_p;
				gc[i] *= inv_c;
			}

			const int lo = max_shift, hi = m - max_shift;
			const int span = 2 * max_shift + 1;
			// max_shift 는 호출 측이 정하는 작은 상수(32 내외)라 스택 대신 vector 를 쓸 이유가 없지만,
			// 방어적으로 상한을 두고 넘치면 무효 처리한다.
			if (span > 257)
				return r;
			float cost[257];

			for (int s = -max_shift; s <= max_shift; ++s)
			{
				double acc = 0.0;
				for (int i = lo; i < hi; ++i)
				{
					const float d = gc[i] - gp[i - s];
					const float q = d * d;
					acc += (q > kOutlierCap) ? kOutlierCap : q;
				}
				cost[s + max_shift] = static_cast<float>(acc / (hi - lo));
			}

			int best = 0;
			for (int k = 1; k < span; ++k)
				if (cost[k] < cost[best])
					best = k;

			const int best_shift = best - max_shift;
			float shift = static_cast<float>(best_shift);
			// 신뢰도 계산에 쓰는 '최저 비용'. 서브픽셀 보간이 성공하면 정수 격자 위의 값이
			// 아니라 **보간된 골짜기 바닥**을 쓴다. 이걸 안 하면 진짜 시프트가 정수 중간
			// (예: 1.5px)일 때 격자 위 비용이 크게 남아, 답이 완벽히 맞는데도 신뢰도가
			// 반토막 난다 — 반동은 몇 px 단위라 딱 그 구간에서 늘 쓰레기로 표시된다.
			float c_min = cost[best];

			// 서브픽셀 — 최저점 양옆이 있을 때만. 절단 제곱차라 최저점 근처는 포물선에 가깝다.
			if (best > 0 && best < span - 1)
			{
				const float c_prev = cost[best - 1], c_mid = cost[best], c_next = cost[best + 1];
				const float denom = c_prev - 2.0f * c_mid + c_next;
				if (denom > 1e-9f)
				{
					float d = 0.5f * (c_prev - c_next) / denom;
					if (d > 0.5f)
						d = 0.5f;
					else if (d < -0.5f)
						d = -0.5f;
					shift += d;

					// 세 점을 지나는 포물선의 최솟값 = c_mid − (c_next−c_prev)²/(8·denom)
					const float drop = (c_next - c_prev) * (c_next - c_prev) / (8.0f * denom);
					c_min = c_mid - drop;
					if (c_min < 0.0f)
						c_min = 0.0f;
				}
			}

			// 2등 후보 — 최저점에서 kPeakRadius 넘게 떨어진 곳의 최소 비용.
			// 이게 최저점과 비슷하면 "어디에 맞춰도 비슷하다"는 뜻이라 답을 믿을 수 없다.
			float second = 0.0f;
			bool have_second = false;
			for (int k = 0; k < span; ++k)
			{
				const int dk = k > best ? k - best : best - k;
				if (dk <= kPeakRadius)
					continue;
				if (!have_second || cost[k] < second)
				{
					second = cost[k];
					have_second = true;
				}
			}

			const float c0 = c_min;
			float uniqueness = 0.0f;
			if (have_second && second > 1e-6f)
				uniqueness = 1.0f - c0 / second;
			else if (have_second)
				uniqueness = 0.0f; // 2등도 0 — 주기적 무늬. 못 고른다
			if (uniqueness < 0.0f)
				uniqueness = 0.0f;
			if (uniqueness > 1.0f)
				uniqueness = 1.0f;

			// 절대 품질 — 유일하게 최저여도 잔차가 무관한 신호 수준이면 매칭에 실패한 것이다.
			float quality = 1.0f - c0 / kUncorrelated;
			if (quality < 0.0f)
				quality = 0.0f;
			if (quality > 1.0f)
				quality = 1.0f;

			r.shift = shift;
			r.confidence = uniqueness * quality;
			r.valid = true;

			// 탐색 범위 끝에 붙었으면 잘렸을 수 있다 — 실제 이동이 범위 밖인지 딱 끝인지
			// 구분할 방법이 없다. 조용히 틀린 값을 내느니 "못 믿는다"고 말한다.
			if (best == 0 || best == span - 1)
				r.confidence = 0.0f;

			return r;
		}

		// 스크래치를 직접 들고 다니고 싶지 않을 때(테스트 등).
		inline match_result match_profile(const float *prev, const float *cur, int n, int max_shift, float flat_floor = kFlatFloor)
		{
			scratch sc;
			return match_profile(prev, cur, n, max_shift, sc, flat_floor);
		}

		// 한 프레임의 결과. 부호는 **전부 raw 마우스와 같은 규약**이다
		// (오른쪽 +x, 아래 +y). 화면 내용이 아니라 **카메라가 어디로 돌았나**로 환산한 값이다.
		struct sample
		{
			float screen_dx = 0.0f, screen_dy = 0.0f; // 추정 화면(카메라) 이동
			float mouse_dx = 0.0f, mouse_dy = 0.0f;   // 같은 프레임의 raw 마우스 이동
			float conf_x = 0.0f, conf_y = 0.0f;       // 0..1
			bool valid = false;

			// 반동 = 화면 − 마우스. 위로 차는 반동은 dy 가 **음수**다(마우스 위 = −y 규약).
			float recoil_dx() const { return screen_dx - mouse_dx; }
			float recoil_dy() const { return screen_dy - mouse_dy; }
		};

		constexpr int kHistory = 512;

		class tracker
		{
		public:
			// 프레임 하나를 넣는다. 이전 프레임 프로파일이 없거나 크기가 바뀌었으면
			// 저장만 하고 false — 첫 프레임에 엉뚱한 시프트를 내지 않기 위해서다.
			bool update(const float *vprof, int vn, const float *hprof, int hn, int max_shift, float mouse_dx, float mouse_dy)
			{
				if (vprof == nullptr || hprof == nullptr || vn <= 0 || hn <= 0)
					return false;

				const bool same_size =
					_prev_v.size() == static_cast<std::size_t>(vn) &&
					_prev_h.size() == static_cast<std::size_t>(hn);

				if (!_have_prev || !same_size)
				{
					store_prev(vprof, vn, hprof, hn);
					_have_prev = true;
					return false;
				}

				const match_result mv = match_profile(_prev_v.data(), vprof, vn, max_shift, _sc);
				const match_result mh = match_profile(_prev_h.data(), hprof, hn, max_shift, _sc);

				store_prev(vprof, vn, hprof, hn);

				sample s;
				// ⚠️ 부호 뒤집기. match_profile 이 주는 것은 **화면 내용**의 이동이고,
				// 마우스와 같은 축으로 놓으려면 **카메라**의 이동이어야 한다. 마우스를
				// 오른쪽으로 밀면 카메라가 오른쪽으로 돌고 화면 내용은 왼쪽으로 흐른다.
				// 이 부호 덕에 "마우스만 움직이면 화면 dx/dy 가 마우스와 같은 방향" 이 성립하고,
				// 그게 검증 2단계에서 눈으로 확인하는 바로 그 성질이다.
				s.screen_dx = -mh.shift;
				s.screen_dy = -mv.shift;
				s.mouse_dx = mouse_dx;
				s.mouse_dy = mouse_dy;
				s.conf_x = mh.valid ? mh.confidence : 0.0f;
				s.conf_y = mv.valid ? mv.confidence : 0.0f;
				s.valid = mv.valid && mh.valid;
				if (!mh.valid)
					s.screen_dx = 0.0f;
				if (!mv.valid)
					s.screen_dy = 0.0f;

				push(s);
				return true;
			}

			// 이전 프레임을 버린다(기록은 남긴다). 오버레이를 열어 몇 프레임 건너뛴 뒤
			// 이어붙이면 그 사이 이동이 통째로 한 프레임 시프트로 잡혀 쓰레기가 된다.
			void drop_prev() { _have_prev = false; }

			void reset()
			{
				drop_prev();
				_count = 0;
				_head = 0;
			}

			int count() const { return _count; }
			// 0 = 가장 오래된 것, count()-1 = 가장 최근. 범위 밖은 빈 sample 을 준다
			// (기록이 없을 때 호출 측이 매번 count() 를 검사하게 만들면 언젠가 빠뜨린다).
			const sample &at(int i) const
			{
				static const sample empty;
				if (i < 0 || i >= _count)
					return empty;
				const int start = (_head - _count + kHistory * 2) % kHistory;
				return _hist[(start + i) % kHistory];
			}
			const sample &last() const { return at(_count - 1); }

		private:
			void store_prev(const float *vprof, int vn, const float *hprof, int hn)
			{
				_prev_v.assign(vprof, vprof + vn);
				_prev_h.assign(hprof, hprof + hn);
			}

			void push(const sample &s)
			{
				_hist[_head] = s;
				_head = (_head + 1) % kHistory;
				if (_count < kHistory)
					_count++;
			}

			std::vector<float> _prev_v, _prev_h;
			scratch _sc;
			bool _have_prev = false;
			sample _hist[kHistory];
			int _head = 0;
			int _count = 0;
		};
	}
}
