/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 에임 트레이너 — 순수 로직(플랫폼 비의존).
// Windows·ImGui·파일 IO·네트워크는 여기 넣지 않는다. 입력 수집은 input_windows.cpp,
// 그리기는 runtime_gui.cpp, 기록 제출은 sherbet_content 계열이 맡는다.
// 판정 100% 를 여기 몰아넣어 호스트 컴파일러로 실제 단위테스트한다(tools/sherbet_aim_test.cpp).
//
// ── 이 설계의 뼈대 ───────────────────────────────────────────────────────────
// 표적은 **화면 좌표가 아니라 방향(yaw·pitch)** 으로 산다. 화면에 고정된 표적은 조준이
// 성립하지 않는다 — 조준점도 화면 중앙 고정이라 마우스를 아무리 움직여도 둘의 상대
// 위치가 안 변하기 때문이다. 방향으로 두면 카메라가 돌 때 표적이 화면을 가로질러
// 흘러가고, 그게 실제 게임에서 조준하는 것과 같은 물리다.
//
// ⚠️ **크기와 범위는 픽셀이 아니라 각도(도)다.** 픽셀로 두면 FOV 를 넓게 쓰는 사람과
//    해상도가 다른 사람의 난이도가 달라져 리더보드가 성립하지 않는다. 명중 판정도
//    각거리로 한다 — 투영을 거치지 않으므로 FOV 를 몰라도 정확하다(투영은 그리기 전용).
//
// ⚠️ **지연 0 이 이 기능의 생명이다.** 표적 위치는 이 프레임에 들어온 마우스 입력만으로
//    결정된다. 화면 픽셀 리드백(sherbet::motion)은 GPU 복사라 몇 프레임 늦으므로 위치
//    경로에 **절대** 넣지 않는다. 스무딩·보간도 넣지 않는다 — 빠르게 휘두를 때 표적이
//    끌려오면 훈련 도구로 못 쓴다. tools/sherbet_aim_test.cpp 가 이걸 단언으로 막는다.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sherbet
{
	namespace aim
	{
		// ── 난이도 ───────────────────────────────────────────────────────────
		enum class level
		{
			easy = 0,
			normal = 1,
			hard = 2,
			hell = 3,
		};
		constexpr int kLevelCount = 4;

		// ── 판 길이 ──────────────────────────────────────────────────────────
		// 10초는 몸풀기다. 짧을수록 표본이 적어 편차가 커지는데(60초 대비 약 2.5배),
		// 싸게 여러 번 돌려 제일 잘 나온 판만 남기면 그건 실력이 아니라 운이다.
		// 그래서 10초는 순위에 올리지 않는다 — ranked() 참조.
		enum class duration
		{
			s10 = 0,
			s30 = 1,
			s60 = 2,
		};
		constexpr int kDurationCount = 3;

		inline float duration_seconds(duration d)
		{
			switch (d)
			{
			case duration::s10: return 10.0f;
			case duration::s30: return 30.0f;
			case duration::s60: return 60.0f;
			}
			return 60.0f;
		}

		// 이 길이의 기록이 리더보드에 올라가는가.
		inline bool ranked(duration d)
		{
			return d != duration::s10;
		}

		// ── 난이도 설정 ──────────────────────────────────────────────────────
		// 단위는 전부 **도**다.
		struct tuning
		{
			float radius_deg;      // 표적 반지름
			float spawn_min_deg;   // 다음 표적까지 최소 각거리 — 0 이면 제자리 연타가 된다
			float spawn_max_deg;   // 최대 각거리
			float move_speed_deg;  // 이동 속도(도/초). 0 = 정지
		};

		// 이동은 **어려움부터** 들어간다. 쉬움→보통은 크기와 범위만 조이고, 보통→어려움에서
		// "움직인다" 는 축이 새로 하나 붙는다. 한 번에 하나씩 어려워지는 계단이다.
		inline tuning tuning_for(level lv)
		{
			switch (lv)
			{
			case level::easy:   return { 2.60f, 3.0f,  9.0f,  0.0f };
			case level::normal: return { 1.70f, 4.0f, 14.0f,  0.0f };
			case level::hard:   return { 1.10f, 5.0f, 20.0f,  7.0f };
			case level::hell:   return { 0.70f, 6.0f, 26.0f, 14.0f };
			}
			return { 1.70f, 4.0f, 14.0f, 0.0f };
		}

		// ── 카운트다운 ───────────────────────────────────────────────────────
		// 시작을 누른 손을 조준 자세로 옮기는 시간이다. 없으면 첫 표적을 항상 놓친다.
		constexpr float kCountdownSeconds = 5.0f;

		// 남은 시간 → 화면에 크게 띄울 숫자. 5.0~4.0 구간이 "5", 1.0~0.0 구간이 "1".
		// 0 이하면 0 을 반환한다(= 시작). 프레임 수가 아니라 **시간**으로 세야 한다 —
		// 프레임으로 세면 60fps 인 사람과 144fps 인 사람의 5초가 달라진다.
		inline int countdown_number(float remaining)
		{
			if (remaining <= 0.0f)
				return 0;
			const int n = static_cast<int>(std::ceil(remaining));
			return n > static_cast<int>(kCountdownSeconds) ? static_cast<int>(kCountdownSeconds) : n;
		}

		// ── 각도 도우미 ──────────────────────────────────────────────────────
		// yaw 는 ±180 에서 감긴다. 감기를 빠뜨리면 179° 와 -179° 의 거리가 358° 로 나와
		// 바로 옆 표적을 "화면 밖" 으로 판정한다.
		inline float wrap_deg(float a)
		{
			while (a > 180.0f) a -= 360.0f;
			while (a < -180.0f) a += 360.0f;
			return a;
		}

		// pitch 한계. 실제 게임 카메라도 수직을 넘어가지 않는다.
		constexpr float kPitchLimit = 89.0f;

		inline float clamp_pitch(float p)
		{
			if (p > kPitchLimit) return kPitchLimit;
			if (p < -kPitchLimit) return -kPitchLimit;
			return p;
		}

		// 두 방향 사이의 각거리(도). 구면에서 정확히 계산한다 —
		// sqrt(dyaw² + dpitch²) 로 근사하면 위/아래를 볼 때 크게 어긋난다.
		//
		// ⚠️ acos(내적) 을 쓰면 **작은 각에서 정밀도가 무너진다.** 두 방향이 거의 같을 때
		//    내적이 1 에 붙는데, acos 는 거기서 기울기가 무한대라 float 오차가 통째로
		//    각도로 증폭된다(같은 방향인데 0.03° 가 나온다). 헬 난이도 반지름이 0.7° 라
		//    그 정도 오차도 판정의 몇 %다. atan2(외적크기, 내적) 는 큰 각·작은 각 양쪽에서
		//    안정적이다 — 호스트 테스트가 이걸 잡아냈다.
		inline float angular_distance(float yaw_a, float pitch_a, float yaw_b, float pitch_b)
		{
			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
			const float ya = yaw_a * kDeg2Rad, pa = pitch_a * kDeg2Rad;
			const float yb = yaw_b * kDeg2Rad, pb = pitch_b * kDeg2Rad;
			const float ax = std::cos(pa) * std::cos(ya), ay = std::sin(pa), az = std::cos(pa) * std::sin(ya);
			const float bx = std::cos(pb) * std::cos(yb), by = std::sin(pb), bz = std::cos(pb) * std::sin(yb);

			const float dot = ax * bx + ay * by + az * bz;
			const float cx = ay * bz - az * by;
			const float cy = az * bx - ax * bz;
			const float cz = ax * by - ay * bx;
			const float cross = std::sqrt(cx * cx + cy * cy + cz * cz);
			return std::atan2(cross, dot) / kDeg2Rad;
		}

		// ── 난수 ─────────────────────────────────────────────────────────────
		// xorshift32. <random> 을 쓰지 않는 이유: 구현체마다 수열이 달라 같은 시드로도
		// 다른 배치가 나오고, 그러면 "최소 거리를 지키는가" 같은 단언을 재현할 수 없다.
		// 시드를 밖에서 주입할 수 있어야 테스트가 결정적으로 돈다.
		class rng
		{
		public:
			explicit rng(std::uint32_t seed) : _s(seed != 0u ? seed : 0x9E3779B9u) {}

			std::uint32_t next()
			{
				_s ^= _s << 13;
				_s ^= _s >> 17;
				_s ^= _s << 5;
				return _s;
			}

			// [0,1)
			float unit()
			{
				return static_cast<float>(next() >> 8) / 16777216.0f;
			}

			float range(float lo, float hi)
			{
				return lo + (hi - lo) * unit();
			}

		private:
			std::uint32_t _s;
		};

		// ── 구면 위 방향 만들기 ──────────────────────────────────────────────
		// 기준 방향에서 **각거리 dist_deg, 방위 azim_deg** 인 방향을 만든다.
		//
		// ⚠️ yaw 에 각도를 그냥 더하면 안 된다. 위를 보고 있을 때 yaw 1도는 각거리 1도가
		//    아니라 cos(pitch) 도다 — 고각에서 표적이 최소 거리보다 가깝게 붙어 제자리
		//    연타가 되고, 난이도의 "뜨는 범위" 가 통째로 무너진다. 호스트 테스트가 잡았다.
		//    기준 방향 주위의 접평면에서 회전시켜야 각거리가 정확히 dist_deg 가 된다.
		inline void direction_at(float base_yaw, float base_pitch, float dist_deg, float azim_deg,
			float &out_yaw, float &out_pitch)
		{
			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
			constexpr float kRad2Deg = 180.0f / 3.14159265358979323846f;

			const float by = base_yaw * kDeg2Rad, bp = clamp_pitch(base_pitch) * kDeg2Rad;
			// 정면
			const float fx = std::cos(bp) * std::cos(by);
			const float fy = std::sin(bp);
			const float fz = std::cos(bp) * std::sin(by);

			// 오른쪽 = 정규화(정면 × 월드업). pitch 를 ±89 로 묶어 두므로 길이가 0 이 되지 않는다.
			const float rlen = std::sqrt(fz * fz + fx * fx);
			const float rx = (rlen > 1e-6f) ? (-fz / rlen) : 0.0f;
			const float rz = (rlen > 1e-6f) ? (fx / rlen) : 1.0f;
			// 위쪽 = 오른쪽 × 정면 (오른쪽·정면이 단위이고 직교라 결과도 단위).
			// right = (rx, 0, rz) 이므로 y 성분이 0 인 것을 이용해 전개한다.
			const float ux = -rz * fy;
			const float uy = rz * fx - rx * fz;
			const float uz = rx * fy;

			const float sd = std::sin(dist_deg * kDeg2Rad), cd = std::cos(dist_deg * kDeg2Rad);
			const float ca = std::cos(azim_deg * kDeg2Rad), sa = std::sin(azim_deg * kDeg2Rad);

			const float tx = fx * cd + (rx * ca + ux * sa) * sd;
			const float ty = fy * cd + (0.0f * ca + uy * sa) * sd;
			const float tz = fz * cd + (rz * ca + uz * sa) * sd;

			float t = ty;
			if (t > 1.0f) t = 1.0f;
			if (t < -1.0f) t = -1.0f;
			out_pitch = clamp_pitch(std::asin(t) * kRad2Deg);
			out_yaw = wrap_deg(std::atan2(tz, tx) * kRad2Deg);
		}

		// ── 표적 ─────────────────────────────────────────────────────────────
		// 이동은 생성 중심의 접평면 위 2D 오프셋으로 관리한다. 그래야 이동 속도가
		// 어디를 보고 있든 같고(도/초), 벗어남 판정이 sqrt(u²+v²) 로 정확해진다.
		struct target
		{
			float yaw = 0.0f;      // 계산된 절대 방향(도) — 아래 값들에서 파생
			float pitch = 0.0f;
			float home_yaw = 0.0f; // 생성 중심
			float home_pitch = 0.0f;
			float off_u = 0.0f;    // 중심 기준 접평면 오프셋(도)
			float off_v = 0.0f;
			float vu = 0.0f;       // 도/초
			float vv = 0.0f;
		};

		// 표적이 생성 위치에서 벗어날 수 있는 최대 각거리. 넘으면 속도를 뒤집는다.
		// 안 묶으면 이동 난이도에서 표적이 화면 밖으로 유유히 나가버린다.
		constexpr float kWanderLimitDeg = 4.0f;

		// ── 진행 단계 ────────────────────────────────────────────────────────
		enum class phase
		{
			idle = 0,      // 시작 전
			countdown = 1, // 5→1
			running = 2,   // 판 진행 중
			finished = 3,  // 결과
		};

		// ── 기록 ─────────────────────────────────────────────────────────────
		struct stats
		{
			int hits = 0;   // 명중 수 = 점수
			int shots = 0;  // **모든 클릭**. 빗나간 것도 분모에 들어간다
		};

		// 명중률 0~1. 클릭이 없으면 0.
		// 빗나간 클릭을 분모에 넣지 않으면 마구 쏘는 게 공짜가 되어 숫자가 의미를 잃는다.
		inline float accuracy(const stats &s)
		{
			return s.shots > 0 ? static_cast<float>(s.hits) / static_cast<float>(s.shots) : 0.0f;
		}

		// 초당 명중 수. 판 길이가 달라도 비교할 수 있게 남긴다(리더보드 부가 표시).
		inline float per_second(const stats &s, duration d)
		{
			const float secs = duration_seconds(d);
			return secs > 0.0f ? static_cast<float>(s.hits) / secs : 0.0f;
		}

		// ── 투영 (그리기 전용) ───────────────────────────────────────────────
		// 카메라 기준 상대각 → 화면 픽셀. 카메라 정면이 화면 중앙이다.
		// 원근(tan)을 제대로 쓴다 — 선형 근사는 화면 가장자리에서 눈에 띄게 어긋난다.
		// 뒤쪽(시야 밖)이면 false 를 돌려주고 out 은 건드리지 않는다.
		//
		// ⚠️ 명중 판정에는 쓰지 않는다. 판정은 각거리로 한다 — 그래야 FOV 를 몰라도 정확하다.
		inline bool project(float rel_yaw_deg, float rel_pitch_deg,
			float fov_deg, float screen_w, float screen_h, float &out_x, float &out_y)
		{
			constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
			if (fov_deg <= 1.0f || fov_deg >= 179.0f || screen_w <= 0.0f || screen_h <= 0.0f)
				return false;

			const float ry = wrap_deg(rel_yaw_deg) * kDeg2Rad;
			const float rp = rel_pitch_deg * kDeg2Rad;

			// 카메라 정면 = +x. 방향 벡터를 만들고 x 로 나눠 화면 평면에 얹는다.
			const float x = std::cos(rp) * std::cos(ry);
			const float y = std::cos(rp) * std::sin(ry);
			const float z = std::sin(rp);
			if (x <= 0.0001f)
				return false; // 뒤쪽 — 나눗셈이 폭주한다

			const float half_w = screen_w * 0.5f;
			const float focal = half_w / std::tan(fov_deg * 0.5f * kDeg2Rad);

			out_x = half_w + focal * (y / x);
			out_y = screen_h * 0.5f - focal * (z / x); // 화면 y 는 아래가 +
			return true;
		}

		// ── 판 ───────────────────────────────────────────────────────────────
		// 카메라 방향은 **밖에서** 준다. 이 헤더는 마우스도 감도도 모른다 —
		// 호출부가 raw 델타에 감도를 곱해 카메라를 굴리고, 그 결과만 넘긴다.
		class session
		{
		public:
			// 시작. 카메라의 현재 방향을 기준으로 첫 표적을 놓는다.
			void start(level lv, duration d, std::uint32_t seed, float cam_yaw, float cam_pitch, float fov_deg)
			{
				_level = lv;
				_duration = d;
				_tuning = tuning_for(lv);
				_rng = rng(seed);
				_fov = fov_deg;
				_stats = stats();
				_phase = phase::countdown;
				_countdown_left = kCountdownSeconds;
				_time_left = duration_seconds(d);
				_last_hit_ok = false;
				_shot_log = 0u;
				_shot_log_n = 0;
				spawn(cam_yaw, cam_pitch);
			}

			void stop()
			{
				_phase = phase::idle;
			}

			// 매 프레임 정확히 한 번. dt 는 초.
			// ⚠️ 여기서 표적 위치를 바꾸는 것은 **시간뿐**이다. 카메라 회전은 표적을 건드리지
			//    않는다(표적은 절대 방향이므로). 그래서 마우스를 아무리 빠르게 휘둘러도
			//    표적이 밀리거나 늦게 따라오는 일이 없다.
			void tick(float dt, float cam_yaw, float cam_pitch)
			{
				if (dt < 0.0f || !(dt == dt)) // 음수·NaN 방어(알트탭·디버거 정지)
					dt = 0.0f;
				if (dt > 0.25f)
					dt = 0.25f; // 큰 프레임 튐이 표적을 순간이동시키지 않게

				if (_phase == phase::countdown)
				{
					_countdown_left -= dt;
					if (_countdown_left <= 0.0f)
					{
						_countdown_left = 0.0f;
						_phase = phase::running;
						// 시간 재기는 **첫 표적이 뜬 순간**부터다. 카운트다운은 안 센다.
						_time_left = duration_seconds(_duration);
						spawn(cam_yaw, cam_pitch); // 카운트다운 동안 돌린 시야를 기준으로 다시 놓는다
					}
					return;
				}

				if (_phase != phase::running)
					return;

				move_target(dt);

				_time_left -= dt;
				if (_time_left <= 0.0f)
				{
					_time_left = 0.0f;
					_phase = phase::finished;
				}
			}

			// 클릭. 명중이면 true 를 돌려주고 다음 표적을 놓는다.
			// ⚠️ 카운트다운 중 클릭은 세지 않는다. 게임에는 입력이 가므로 총은 실제로
			//    나가지만, 그건 아직 판이 아니다.
			bool shoot(float cam_yaw, float cam_pitch)
			{
				if (_phase != phase::running)
					return false;

				_stats.shots++;
				const float d = angular_distance(cam_yaw, cam_pitch, _target.yaw, _target.pitch);
				const bool hit = d <= _tuning.radius_deg;
				if (hit)
				{
					_stats.hits++;
					spawn(cam_yaw, cam_pitch);
				}
				_last_hit_ok = hit;
				push_shot(hit);
				return hit;
			}

			// ── 조회 ─────────────────────────────────────────────────────────
			phase current_phase() const { return _phase; }
			int countdown_display() const { return countdown_number(_countdown_left); }
			// 남은 카운트다운(초). 숫자가 커졌다 잦아드는 연출에 쓴다.
			float countdown_remaining() const { return _countdown_left; }
			float time_left() const { return _time_left; }
			float time_elapsed() const { return duration_seconds(_duration) - _time_left; }
			const target &current_target() const { return _target; }
			const stats &result() const { return _stats; }
			level current_level() const { return _level; }
			duration current_duration() const { return _duration; }
			const tuning &current_tuning() const { return _tuning; }
			bool last_shot_hit() const { return _last_hit_ok; }

			// 최근 사격 n 발의 명중 여부(0 = 가장 최근). HUD 의 점 표시용.
			// 기록이 없으면 false 를 돌려주므로 count() 로 유효 개수를 먼저 본다.
			int shot_history_count() const { return _shot_log_n; }
			bool shot_history(int i) const
			{
				if (i < 0 || i >= _shot_log_n)
					return false;
				return ((_shot_log >> i) & 1u) != 0u;
			}

		private:
			// 지금 카메라 방향을 기준으로 링 안 무작위 지점에 표적을 놓는다.
			void spawn(float cam_yaw, float cam_pitch)
			{
				// 링 반경. 최소값은 제자리 연타 방지, 최대값은 화면 밖 방지.
				float lo = _tuning.spawn_min_deg;
				float hi = _tuning.spawn_max_deg;

				// 화면 밖으로 나가면 찾느라 시간이 날아가는데 그건 조준 실력이 아니다.
				// 시야각의 절반에서 표적 반지름만큼 물러난 곳까지만 허용한다.
				const float edge = _fov * 0.5f - _tuning.radius_deg - 1.0f;
				if (hi > edge) hi = edge;
				if (lo > hi) lo = hi > 0.0f ? hi * 0.5f : 0.0f;
				if (lo < 0.0f) lo = 0.0f;
				if (hi < 0.0f) hi = 0.0f;

				const float dist = _rng.range(lo, hi);
				const float ang = _rng.range(0.0f, 360.0f);

				// 접평면에서 회전시킨다 — 이래야 각거리가 정확히 dist 다.
				direction_at(cam_yaw, cam_pitch, dist, ang, _target.home_yaw, _target.home_pitch);
				_target.off_u = 0.0f;
				_target.off_v = 0.0f;

				if (_tuning.move_speed_deg > 0.0f)
				{
					constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
					const float mv = _rng.range(0.0f, 360.0f);
					_target.vu = _tuning.move_speed_deg * std::cos(mv * kDeg2Rad);
					_target.vv = _tuning.move_speed_deg * std::sin(mv * kDeg2Rad);
				}
				else
				{
					_target.vu = 0.0f;
					_target.vv = 0.0f;
				}
				recompute_target();
			}

			void move_target(float dt)
			{
				if (_tuning.move_speed_deg <= 0.0f)
					return;

				_target.off_u += _target.vu * dt;
				_target.off_v += _target.vv * dt;

				// 생성 중심에서 너무 벗어나면 되돌린다. 안 묶으면 유유히 화면 밖으로 나간다.
				// 오프셋이 접평면 2D 라 벗어남 판정이 그냥 반지름이다.
				const float r = std::sqrt(_target.off_u * _target.off_u + _target.off_v * _target.off_v);
				if (r > kWanderLimitDeg && r > 1e-6f)
				{
					// 반경 방향으로 반사한다. 그냥 뒤집기만 하면 왔던 길을 되짚어 왕복만 한다.
					const float nu = _target.off_u / r, nv = _target.off_v / r;
					const float dotv = _target.vu * nu + _target.vv * nv;
					_target.vu -= 2.0f * dotv * nu;
					_target.vv -= 2.0f * dotv * nv;
					// 경계 안쪽으로 되돌려 놓는다(경계에 붙어 떨지 않게).
					_target.off_u = nu * kWanderLimitDeg;
					_target.off_v = nv * kWanderLimitDeg;
				}
				recompute_target();
			}

			// 접평면 오프셋 → 절대 방향.
			void recompute_target()
			{
				const float r = std::sqrt(_target.off_u * _target.off_u + _target.off_v * _target.off_v);
				if (r <= 1e-6f)
				{
					_target.yaw = _target.home_yaw;
					_target.pitch = _target.home_pitch;
					return;
				}
				constexpr float kRad2Deg = 180.0f / 3.14159265358979323846f;
				const float azim = std::atan2(_target.off_v, _target.off_u) * kRad2Deg;
				direction_at(_target.home_yaw, _target.home_pitch, r, azim, _target.yaw, _target.pitch);
			}

			void push_shot(bool hit)
			{
				_shot_log = (_shot_log << 1) | (hit ? 1u : 0u);
				if (_shot_log_n < 32)
					_shot_log_n++;
			}

			level _level = level::normal;
			duration _duration = duration::s60;
			tuning _tuning = tuning_for(level::normal);
			rng _rng { 1u };
			float _fov = 90.0f;
			phase _phase = phase::idle;
			float _countdown_left = 0.0f;
			float _time_left = 0.0f;
			target _target;
			stats _stats;
			bool _last_hit_ok = false;
			std::uint32_t _shot_log = 0u; // 비트 0 = 가장 최근
			int _shot_log_n = 0;
		};
	}
}
