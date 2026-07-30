/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 스프레이 트레이너 — 순수 로직(플랫폼 비의존).
// Windows·ImGui·파일 IO 는 여기 넣지 않는다. 입력 수집은 input_windows.cpp,
// 그리기는 runtime_gui.cpp 가 맡는다.
// 판정 로직 100% 를 여기 몰아넣어 맥 clang 으로 실제 단위테스트한다(tools/sherbet_spray_test.cpp).
//
// 핵심 규칙: **사격 방식을 분류하지 않는다.** 클릭마다 점을 하나 찍고, 무발사 간격으로만
// 구간을 나눈다. 탭/버스트/연발은 같은 코드에서 저절로 다른 모양(점 1개 / 짧은 궤적 / 긴 궤적)
// 으로 나온다. 임계값으로 방식을 판별하면 실전에서 오분류(급한 탭 두 번 → 버스트, 렉 → 연발
// 쪼개짐)가 잦고, 오분류된 데이터는 엉뚱한 칸에 들어가 통계를 오염시킨다.
#pragma once

#include <cmath>
#include <vector>
#include <cstddef>
#include <utility>

namespace sherbet
{
	namespace spray
	{
		struct shot
		{
			float x = 0.0f, y = 0.0f; // 구간 시작 기준 누적 오프셋(raw 마우스 단위)
			float t = 0.0f;           // 구간 시작 이후 경과(초)
		};

		struct segment
		{
			std::vector<shot> shots;
		};

		// 보관하는 완료 구간 수. 넘치면 가장 오래된 것부터 버린다.
		// 세션 한정이고 스프레이 훈련은 최근 몇 구간이면 충분하다(설계 §4).
		constexpr std::size_t kMaxSegments = 20;

		// 구간 나누기 임계값의 허용 범위. 0 이나 음수가 들어오면 매 프레임 구간이 끊겨
		// 기록이 의미를 잃으므로 클램프한다(손으로 고친 ini · 잘못된 슬라이더 범위 방어).
		constexpr int kMinGapMs = 1;
		constexpr int kMaxGapMs = 10000;
		constexpr int kDefaultGapMs = 400;

		class recorder
		{
		public:
			// 매 프레임 정확히 한 번 호출한다.
			//   dt     프레임 간격(초)
			//   dx,dy  이번 프레임의 raw 이동 누적 — 호출 측이 input::raw_mouse_delta_x/y() 로
			//          읽고 리셋한 값이다(프레임당 한 번만 읽어야 한다)
			//   fire   좌클릭 상승 엣지 — 호출 측이 이전 프레임 상태와 비교해 계산한다
			//
			// 처리 순서: (구간이 살아 있으면) 이동·시간 누적 → 간격 초과면 구간 종료 → fire 처리.
			// 이 순서 때문에 "간격을 넘긴 프레임에 들어온 클릭"은 이전 구간의 꼬리가 아니라
			// 새 구간의 첫 발이 된다.
			void on_frame(float dt, int dx, int dy, bool fire)
			{
				if (_active)
				{
					// 누적 오프셋은 구간이 살아 있는 동안만 쌓인다. 구간이 없을 때의 이동은
					// 그냥 버린다 — 발사 사이에 조준만 하는 움직임은 다음 스프레이가 아니다.
					_off_x += static_cast<float>(dx);
					_off_y += static_cast<float>(dy);

					const float dt_ms = dt * 1000.0f;
					_elapsed_ms += dt_ms;
					_since_shot_ms += dt_ms;

					// 경계는 '>' 다. 정확히 임계값이면 아직 같은 구간으로 본다.
					if (_since_shot_ms > static_cast<float>(_gap_ms))
						close();
				}

				if (!fire)
					return;

				if (!_active)
				{
					// 구간 시작이 곧 첫 발이다 — 아래에서 찍는 점이 (0,0,0) 이 된다.
					_active = true;
					_current.shots.clear();
					_off_x = _off_y = 0.0f;
					_elapsed_ms = 0.0f;
					_since_shot_ms = 0.0f;
				}

				shot s;
				s.x = _off_x;
				s.y = _off_y;
				s.t = _elapsed_ms * 0.001f;
				_current.shots.push_back(s);
				_since_shot_ms = 0.0f;
			}

			// 진행 중인 구간(없으면 nullptr). 포인터는 다음 on_frame() 호출까지만 유효하다고 본다.
			const segment *current() const { return _active ? &_current : nullptr; }
			// 완료된 구간. 최신이 뒤, 최대 kMaxSegments 개.
			const std::vector<segment> &history() const { return _history; }

			void clear()
			{
				_history.clear();
				_current.shots.clear();
				_active = false;
				_off_x = _off_y = 0.0f;
				_elapsed_ms = 0.0f;
				_since_shot_ms = 0.0f;
				// _gap_ms 는 설정이므로 건드리지 않는다.
			}

			void set_gap_ms(int ms)
			{
				_gap_ms = ms < kMinGapMs ? kMinGapMs : (ms > kMaxGapMs ? kMaxGapMs : ms);
			}
			int gap_ms() const { return _gap_ms; }

		private:
			// 진행 중인 구간을 history 로 옮긴다. _active 일 때 _current.shots 는 항상
			// 한 발 이상이다(구간은 발사로만 시작하므로) — 빈 구간이 history 에 들어갈 일은 없다.
			void close()
			{
				_history.push_back(std::move(_current));
				_current.shots.clear(); // move 후 상태는 미지정이므로 명시적으로 비운다
				if (_history.size() > kMaxSegments)
					_history.erase(_history.begin());
				_active = false;
			}

			std::vector<segment> _history;
			segment _current;
			bool _active = false;
			float _off_x = 0.0f, _off_y = 0.0f; // 구간 시작 기준 누적 오프셋
			float _elapsed_ms = 0.0f;           // 구간 시작 이후 경과
			float _since_shot_ms = 0.0f;        // 마지막 발 이후 경과
			int _gap_ms = kDefaultGapMs;
		};

		// 최근 n 구간의 **마지막 점**들을 모아 그 중심(평균)에서 떨어진 거리의 평균을 낸다.
		// 값이 작을수록 매번 같은 자리에서 스프레이가 끝난다는 뜻이고, 그것이 일관성이다.
		// 점이 하나뿐인 구간(탭)도 그 점이 곧 마지막 점이므로 포함된다.
		// 유효 구간이 2개 미만이면 false 를 반환하고 out 을 건드리지 않는다.
		inline bool end_spread(const std::vector<segment> &hist, std::size_t n, float &out)
		{
			if (n == 0)
				return false;

			std::vector<shot> pts;
			pts.reserve(n);
			for (std::size_t i = hist.size(); i > 0 && pts.size() < n; --i)
				if (!hist[i - 1].shots.empty())
					pts.push_back(hist[i - 1].shots.back());

			if (pts.size() < 2)
				return false;

			const float count = static_cast<float>(pts.size());
			float sx = 0.0f, sy = 0.0f;
			for (const shot &p : pts)
			{
				sx += p.x;
				sy += p.y;
			}
			const float cx = sx / count, cy = sy / count;

			float sum = 0.0f;
			for (const shot &p : pts)
			{
				const float ex = p.x - cx, ey = p.y - cy;
				sum += std::sqrt(ex * ex + ey * ey);
			}

			out = sum / count;
			return true;
		}

		// 구간의 평균 연사 간격(초). 발이 2개 미만이면 false 를 반환하고 out 을 건드리지 않는다.
		// 간격이 고르지 않아도 (마지막 t - 첫 t) / (발수 - 1) 로 정의한다.
		inline bool avg_interval(const segment &s, float &out)
		{
			if (s.shots.size() < 2)
				return false;

			out = (s.shots.back().t - s.shots.front().t) / static_cast<float>(s.shots.size() - 1);
			return true;
		}
	}
}
