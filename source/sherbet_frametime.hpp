/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 게임 중 프레임 통계 + 프레임 상한 진단 — 순수 로직(플랫폼 비의존).
// tools/sherbet_frametime_test.cpp 가 이 헤더를 그대로 컴파일해 검증한다.
//
// ── 왜 ImGui 의 링을 안 쓰는가 ──────────────────────────────────────────────
// 「최적화」 탭이 보여주던 "최근 60프레임 최악" 은 ImGui 의 FramerateSecPerFrame 링인데,
// 그 링은 **오버레이를 그린 프레임에만** 채워진다(runtime_gui.cpp 의 early-out 이
// NewFrame() 보다 위에 있다). 즉 메뉴를 열어 놓은 60프레임의 최악값이다 —
// "메뉴 열면 멀쩡한데 게임하면 렉 걸려요" 문의에 확인도 반박도 할 수 없었다.
// 여기 링은 반대로 **메뉴가 닫힌 동안의 프레임만** 받는다(채우는 곳은 runtime.cpp).
//
// ── 상한 진단 ────────────────────────────────────────────────────────────
// 프레임이 어떤 숫자에 **평평하게** 붙어 있으면 그건 부하가 아니라 상한이다.
// 게임이 Present 에 넘긴 SyncInterval 과 모니터 주사율을 대조해 어느 상한인지 가른다.
// ⚠️ "주사율 배수에 가깝다" 만으로 판정하지 않는다 — GPU 바운드 72fps 도 144 의 절반이다.
//    프레임시간 평탄도(IQR / 중앙값)를 AND 로 건다. 판매자 본인이 하루를 날린 71fps 는
//    수직동기가 **꺼진** 채 프레임 생성 목표치 142÷2 였다 — 그래서 SyncInterval 을 본다.
// ⚠️ 판정은 해석이다. 실측(SyncInterval·주사율·fps·편차)은 항상 같이 보여주고,
//    문구는 "…로 보여요" 를 넘지 않는다.
#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace sherbet
{
	namespace frametime
	{
		// 링 용량. 240fps 에서도 34초, 60fps 면 2분 남짓. 8192 × 4B = 32 KiB 고정 배열 —
		// push 는 렌더 스레드에서 매 프레임 불리므로 힙을 만지지 않는다.
		constexpr std::size_t kCapacity = 8192;

		// 통계를 내는 최소 표본 — 프레임 수와 시간 **둘 다** 넘어야 한다.
		// 600프레임: 이보다 적으면 "가장 느린 1%" 가 대여섯 프레임으로 정해져 최악값과 구분이 안 된다.
		// 10초: 144fps 에서 600프레임은 4초라, 접속 직후 스트리밍 히치 몇 개가 1% low 를 통째로
		//       정해 버린다(실기에서 4.5초 표본에 1% low 41 이 찍혔다). 시간이 있어야 평소 프레임이 섞인다.
		constexpr std::size_t kMinFrames = 600;
		constexpr float kMinSeconds = 10.0f;

		// SyncInterval 을 아직 모른다(DXGI 가 아니거나 첫 Present 전).
		constexpr unsigned int kSyncUnknown = std::numeric_limits<unsigned int>::max();

		class ring
		{
		public:
			void push(float ms)
			{
				// 0 이하·10초 초과는 시계 점프나 첫 프레임이다. 통계에 넣으면 최악값을 오염시킨다.
				if (!(ms > 0.0f) || ms > 10000.0f)
					return;
				_buf[_head] = ms;
				_head = (_head + 1) % kCapacity;
				if (_count < kCapacity)
					++_count;
			}
			void clear() { _head = 0; _count = 0; }
			std::size_t size() const { return _count; }

			// i 번째로 **오래된** 프레임(0 = 가장 오래된 것). 파형처럼 순서가 필요한 곳용.
			float at(std::size_t i) const { return _buf[(_head + kCapacity - _count + i) % kCapacity]; }

			// 통째로 복사한다. 순서는 버린다 — 통계에 순서는 필요 없고, 꽉 찬 링은
			// [0, kCapacity) 전부가 유효하며 덜 찬 링은 [0, _count) 가 유효하므로 둘 다 이 한 줄이다.
			void copy_to(std::vector<float> &out) const { out.assign(_buf, _buf + _count); }

		private:
			float _buf[kCapacity] = {};
			std::size_t _head = 0;
			std::size_t _count = 0;
		};

		struct stats
		{
			std::size_t frames = 0;
			float seconds = 0.0f;     // 표본이 실제로 덮는 시간(프레임시간 합산)
			float avg_fps = 0.0f;     // frames / seconds
			float median_ms = 0.0f;
			float low1_fps = 0.0f;    // 가장 느린 1% 프레임의 평균 프레임시간 → fps
			float worst_ms = 0.0f;
			std::size_t stutters = 0; // 중앙값의 2배를 넘은 프레임 수
			float iqr_ratio = 0.0f;   // (p75 − p25) / 중앙값. 작을수록 평평하다
		};

		// 표본이 통계를 낼 만큼 모였는가. 못 모였으면 화면에는 "지금까지 N초 · M프레임" 만 나간다.
		inline bool enough(const stats &st)
		{
			return st.frames >= kMinFrames && st.seconds >= kMinSeconds;
		}

		// 표본이 모자라면 false 를 돌려주고 out.frames / out.seconds 만 채운다.
		// ⚠️ 정렬 때문에 힙을 쓴다. 렌더 경로에서 매 프레임 부르지 말 것 — 탭이 열려 있을 때
		//    4Hz 로만 부른다(호출부 규약).
		inline bool compute(const ring &r, stats &out)
		{
			out = stats();
			out.frames = r.size();
			if (out.frames == 0)
				return false;

			std::vector<float> v;
			r.copy_to(v);
			const std::size_t n = v.size();

			double sum = 0.0;
			for (float ms : v)
				sum += ms;
			out.seconds = static_cast<float>(sum / 1000.0);
			if (!enough(out))
				return false;

			std::sort(v.begin(), v.end());
			out.avg_fps = out.seconds > 0.0f ? static_cast<float>(n) / out.seconds : 0.0f;

			out.median_ms = v[n / 2];
			out.worst_ms = v[n - 1];
			const float p25 = v[n / 4], p75 = v[(n * 3) / 4];
			out.iqr_ratio = out.median_ms > 0.0f ? (p75 - p25) / out.median_ms : 0.0f;

			// 가장 느린 1%. n/100 은 kMinFrames 에서 6 이상이다.
			const std::size_t k = std::max<std::size_t>(1, n / 100);
			double slow = 0.0;
			for (std::size_t i = n - k; i < n; ++i)
				slow += v[i];
			const double slow_avg = slow / static_cast<double>(k);
			out.low1_fps = slow_avg > 0.0 ? static_cast<float>(1000.0 / slow_avg) : 0.0f;

			// 정렬돼 있으니 경계 위쪽 개수가 곧 끊김 수다.
			out.stutters = static_cast<std::size_t>(v.end() - std::upper_bound(v.begin(), v.end(), out.median_ms * 2.0f));
			return true;
		}

		enum class cap
		{
			insufficient,  // 표본 부족 — 아무 말도 하지 않는다
			none,          // 평평하지 않다 — 상한이 아니라 부하가 프레임을 정한다
			vsync,         // 수직동기 켜짐 + 주사율(÷SyncInterval)에 붙어 있음 — 모니터가 낼 수 있는 최대
			vsync_divided, // 수직동기 켜짐 + 그 1/n 에 붙어 있음 — 주사율을 못 지켜 나눠 떨어진 것
			vsync_other,   // 수직동기 켜짐 + 평평한데 주사율과 안 맞음 — 주사율을 모르거나 더 낮은 다른 상한
			other,         // 수직동기 꺼짐/모름 + 평평 — 드라이버 제한·프레임 생성 목표치·인게임 제한
		};

		// 평탄도 문턱. 수직동기·프레임 제한기에 묶인 프레임은 IQR 이 중앙값의 1% 안팎이다.
		// GPU 바운드는 정지 화면이어도 보통 5% 를 넘는다. 4% 는 그 사이다.
		constexpr float kFlatIqr = 0.04f;
		// "붙어 있다" 의 폭. 144Hz 수직동기가 143.9 로 찍히는 정도는 흔하다.
		constexpr float kNearRatio = 0.03f;
		// 주사율의 1/n 까지 본다. 그 이상은 수직동기 반토막이 아니라 다른 상한이다.
		constexpr int kMaxDivisor = 4;

		inline bool near(float fps, float target)
		{
			return target > 0.0f && fps >= target * (1.0f - kNearRatio) && fps <= target * (1.0f + kNearRatio);
		}

		struct diagnosis
		{
			cap verdict = cap::insufficient;
			float fps = 0.0f;        // 판정에 쓴 fps(중앙값 기준 — 끊김 몇 번에 흔들리지 않게)
			float target_fps = 0.0f; // vsync / vsync_divided 일 때 붙어 있는 목표치
			int divisor = 0;         // vsync_divided 일 때 n (주사율의 1/n)
		};

		//   refresh_hz     모니터 주사율. 0 = 못 읽음
		//   sync_interval  게임이 Present 에 넘긴 값. kSyncUnknown = 모름
		inline diagnosis diagnose(const stats &st, int refresh_hz, unsigned int sync_interval)
		{
			diagnosis d;
			if (!enough(st))
				return d;
			d.fps = st.median_ms > 0.0f ? 1000.0f / st.median_ms : 0.0f;

			if (st.iqr_ratio >= kFlatIqr)
			{
				d.verdict = cap::none;
				return d;
			}
			// 여기부터는 평평하다 = 무언가가 상한을 정하고 있다.
			if (sync_interval == kSyncUnknown || sync_interval == 0)
			{
				d.verdict = cap::other;
				return d;
			}
			// 수직동기가 켜져 있다. 주사율을 알면 어느 칸에 붙었는지까지 말한다.
			if (refresh_hz > 0)
			{
				const float full = static_cast<float>(refresh_hz) / static_cast<float>(sync_interval);
				for (int n = 1; n <= kMaxDivisor; ++n)
				{
					const float target = full / static_cast<float>(n);
					if (near(d.fps, target))
					{
						d.verdict = n == 1 ? cap::vsync : cap::vsync_divided;
						d.target_fps = target;
						d.divisor = n;
						return d;
					}
				}
			}
			d.verdict = cap::vsync_other;
			return d;
		}
	}
}
