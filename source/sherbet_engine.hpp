/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 「엔진룸」 — 매 프레임 Sherbet 이 하는 일을 3D 로 보여주는 탭의 순수 로직.
// tools/sherbet_engine_test.cpp 가 이 헤더를 그대로 컴파일해 검증한다.
//
// 화면에 보이는 것과 그 출처(전부 실측, 지어낸 값 없음):
//   고리   = 켜진 효과(실행 순서). 크기·발광 = 그 효과의 GPU ms   ← technique.average_gpu_duration
//   입자   = 프레임. 간격 = 프레임 시간, 늦게 온 입자 = 끊김        ← _last_frame_duration
//   게이트 = 프레임 상한(수직동기/제한)                              ← sherbet_frametime::diagnose
//   성운   = 지금 화면의 색을 RGB 공간에 찍은 점구름(효과 전=회색)   ← 백버퍼 리드백
//   바닥   = 최근 게임 중 프레임 시간 파형                           ← sherbet_frametime::ring
//
// ⚠️ 이 탭은 **열려 있을 때만** 돈다. 백버퍼 복사도, 입자 갱신도, 여기 함수도 탭이 그려지는
//    프레임에만 불린다. 닫으면 비용 0 — 그게 이 기능의 계약이다(구매자 요구 사항).
// ⚠️ "최적화되는 중" 이 아니다. Sherbet 이 프레임에 얹는 일과 그 비용을 보여주는 것이다.
//    문구는 그 선을 넘지 않는다.
#pragma once

#include "sherbet_frametime.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sherbet
{
	namespace engine
	{
		struct vec3
		{
			float x = 0.0f, y = 0.0f, z = 0.0f;
		};

		// ── 장면 배치(월드 단위) ────────────────────────────────────────────
		// 파이프는 X 축. 게임(-5) → 고리들 → 게이트(+4) → 모니터(+5).
		constexpr float kPipeStart = -5.0f;
		constexpr float kPipeEnd = 5.0f;
		constexpr float kRingsStart = -3.2f;
		constexpr float kRingsEnd = 3.0f;
		constexpr float kGateX = 4.0f;
		constexpr float kNebulaCenterY = 2.8f; // 파이프 위에 뜬 정육면체
		constexpr float kNebulaSize = 2.4f;
		constexpr float kFloorY = -1.7f;       // 파형 바닥
		constexpr float kFloorHeight = 1.3f;   // 파형 최대 높이

		// ── 궤도 카메라 ────────────────────────────────────────────────────
		constexpr float kDefaultYaw = 0.55f;
		constexpr float kDefaultPitch = 0.32f;
		constexpr float kDefaultDist = 13.0f;
		constexpr float kMinDist = 5.0f;
		constexpr float kMaxDist = 30.0f;
		constexpr float kMaxPitch = 1.45f;     // 거의 수직까지, 뒤집히지는 않게
		constexpr float kOrbitPerPixel = 0.008f;
		constexpr float kSpinRate = 0.12f;     // 손대기 전 자동 회전(rad/s)
		constexpr float kFocal = 1.6f;         // 화면 세로 절반 × kFocal / 깊이 = 픽셀/월드단위

		struct camera
		{
			float yaw = kDefaultYaw;
			float pitch = kDefaultPitch;
			float dist = kDefaultDist;
			bool auto_spin = true; // 손대기 전엔 천천히 돈다(스크린샷의 "3D 자동 회전")

			void reset()
			{
				yaw = kDefaultYaw;
				pitch = kDefaultPitch;
				dist = kDefaultDist;
				auto_spin = true;
			}
			// 드래그(픽셀). 손대는 순간 자동 회전은 멈춘다 — 보던 각도가 저절로 달아나면 안 된다.
			void orbit(float dx_px, float dy_px)
			{
				yaw += dx_px * kOrbitPerPixel;
				pitch += dy_px * kOrbitPerPixel;
				if (pitch > kMaxPitch) pitch = kMaxPitch;
				if (pitch < -kMaxPitch) pitch = -kMaxPitch;
				auto_spin = false;
			}
			// 휠 눈금(+ = 당기기). 1.15 배율은 스프레이 차트와 같은 손맛.
			void zoom(float wheel)
			{
				dist *= std::pow(1.15f, -wheel);
				if (dist < kMinDist) dist = kMinDist;
				if (dist > kMaxDist) dist = kMaxDist;
			}
			void spin(float dt_seconds)
			{
				if (auto_spin)
					yaw += kSpinRate * dt_seconds;
			}
		};

		struct viewport
		{
			float cx = 0.0f, cy = 0.0f; // 화면 중심(픽셀)
			float half_h = 1.0f;        // 캔버스 세로 절반(픽셀) — 크기 기준
		};

		// 월드 → 화면. depth 는 카메라로부터의 거리(멀수록 큼), scale 은 그 점에서 월드 1 = 픽셀 몇.
		// 카메라 뒤(또는 너무 가까움)면 false — 그 점은 그리지 않는다.
		inline bool project(const camera &cam, vec3 p, const viewport &vp, float &sx, float &sy, float &depth, float &scale)
		{
			// yaw: Y 축 회전
			const float cy = std::cos(cam.yaw), sy_ = std::sin(cam.yaw);
			const float x1 = p.x * cy + p.z * sy_;
			const float z1 = -p.x * sy_ + p.z * cy;
			// pitch: X 축 회전
			const float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
			const float y2 = p.y * cp - z1 * sp;
			const float z2 = p.y * sp + z1 * cp;
			// 카메라는 z = -dist 에서 +z 를 본다
			depth = z2 + cam.dist;
			if (depth < 0.2f)
				return false;
			scale = vp.half_h * kFocal / depth;
			sx = vp.cx + x1 * scale;
			sy = vp.cy - y2 * scale;
			return true;
		}

		// ── 고리(효과) 배치 ────────────────────────────────────────────────
		struct ring_slot
		{
			float x = 0.0f;
			float radius = 0.0f;
			float glow = 0.0f; // 0~1. GPU ms 에 비례(1ms 에서 최대)
		};

		constexpr float kRingBaseRadius = 0.38f;
		constexpr float kRingRadiusPerMs = 0.45f;
		constexpr float kRingMaxMs = 2.0f; // 이 위는 다 같은 크기 — 하나가 화면을 다 먹지 않게

		// gpu_ms[n] → out[n]. 실행 순서대로 왼쪽에서 오른쪽.
		inline void layout_rings(const float *gpu_ms, int n, ring_slot *out)
		{
			for (int i = 0; i < n; ++i)
			{
				const float t = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.5f;
				float ms = gpu_ms[i];
				if (ms < 0.0f) ms = 0.0f;
				if (ms > kRingMaxMs) ms = kRingMaxMs;
				out[i].x = kRingsStart + (kRingsEnd - kRingsStart) * t;
				out[i].radius = kRingBaseRadius + ms * kRingRadiusPerMs;
				out[i].glow = ms > 1.0f ? 1.0f : ms;
			}
		}

		// ── 입자(프레임) ───────────────────────────────────────────────────
		struct particle
		{
			float t0 = 0.0f;   // 생성 시각(초)
			float lane_y = 0.0f, lane_z = 0.0f; // 빔에 두께를 주는 작은 오프셋
			bool late = false; // 이 프레임이 끊김이었나(직전 평균의 2배 초과)
		};

		constexpr int kMaxParticles = 1024;   // 240fps × 2초 = 480 이면 충분. 여유.
		constexpr float kTravelSeconds = 2.0f; // 게임 → 모니터까지 가는 시간
		constexpr float kBeamRadius = 0.12f;

		class particle_stream
		{
		public:
			// 프레임마다 하나. frame_ms 는 그 프레임의 실제 시간.
			void spawn(float now, float frame_ms)
			{
				particle p;
				p.t0 = now;
				// 결정론적 의사난수 — 같은 번호는 항상 같은 자리(테스트 가능, 깜빡임 없음)
				const std::uint32_t h = static_cast<std::uint32_t>(_spawned) * 2654435761u;
				const float a = static_cast<float>(h & 0xFFFF) / 65535.0f * 6.2831853f;
				const float r = static_cast<float>((h >> 16) & 0xFFFF) / 65535.0f * kBeamRadius;
				p.lane_y = std::cos(a) * r;
				p.lane_z = std::sin(a) * r;
				// 끊김 판정은 직전까지의 이동평균 대비. 첫 프레임은 기준이 없으니 아니다.
				p.late = _avg_ms > 0.0f && frame_ms > _avg_ms * 2.0f;
				_avg_ms = _avg_ms > 0.0f ? _avg_ms * 0.9f + frame_ms * 0.1f : frame_ms;

				_buf[(_head + _count) % kMaxParticles] = p;
				if (_count < kMaxParticles)
					++_count;
				else
					_head = (_head + 1) % kMaxParticles; // 가장 오래된 것을 덮는다
				++_spawned;
			}
			// 모니터에 도착한(여행이 끝난) 입자를 앞에서부터 버린다.
			void prune(float now)
			{
				while (_count > 0 && now - _buf[_head].t0 >= kTravelSeconds)
				{
					_head = (_head + 1) % kMaxParticles;
					--_count;
				}
			}
			void clear()
			{
				_head = 0;
				_count = 0;
				_avg_ms = 0.0f;
			}
			int count() const { return _count; }
			const particle &at(int i) const { return _buf[(_head + i) % kMaxParticles]; }
			float avg_ms() const { return _avg_ms; }
			// 0(게임) ~ 1(모니터)
			static float progress(const particle &p, float now)
			{
				float t = (now - p.t0) / kTravelSeconds;
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				return t;
			}
			static float x_of(float progress) { return kPipeStart + (kPipeEnd - kPipeStart) * progress; }

		private:
			particle _buf[kMaxParticles] = {};
			int _head = 0;
			int _count = 0;
			std::uint64_t _spawned = 0;
			float _avg_ms = 0.0f;
		};

		// ── 성운(화면 색 샘플) ─────────────────────────────────────────────
		enum class pixel_kind
		{
			rgba8,   // r8g8b8a8 (srgb 포함): 바이트 0=R 1=G 2=B
			bgra8,   // b8g8r8a8 (srgb 포함): 바이트 0=B 1=G 2=R
			rgb10a2, // r10g10b10a2: 비트 0..9=R 10..19=G 20..29=B
			bgr10a2, // b10g10r10a2: 비트 0..9=B 10..19=G 20..29=R
		};

		// w×h 픽셀을 step 간격으로 훑어 r,g,b(0~1) 세 개씩 out 에 쓴다. 쓴 점 개수를 돌려준다.
		// max_points 를 넘지 않는다(그리기 비용 상한). data 는 row_pitch 바이트 간격의 행.
		inline std::size_t sample_colors(const void *data, int w, int h, std::size_t row_pitch, pixel_kind kind, int step, float *out_rgb, std::size_t max_points)
		{
			if (data == nullptr || w <= 0 || h <= 0 || step <= 0 || out_rgb == nullptr)
				return 0;
			std::size_t n = 0;
			const auto *rows = static_cast<const std::uint8_t *>(data);
			for (int y = step / 2; y < h && n < max_points; y += step)
			{
				const std::uint8_t *row = rows + static_cast<std::size_t>(y) * row_pitch;
				for (int x = step / 2; x < w && n < max_points; x += step)
				{
					const std::uint8_t *px = row + static_cast<std::size_t>(x) * 4;
					float r, g, b;
					switch (kind)
					{
					case pixel_kind::rgba8:
						r = px[0] / 255.0f; g = px[1] / 255.0f; b = px[2] / 255.0f;
						break;
					case pixel_kind::bgra8:
						b = px[0] / 255.0f; g = px[1] / 255.0f; r = px[2] / 255.0f;
						break;
					default:
					{
						const std::uint32_t v = static_cast<std::uint32_t>(px[0]) | (static_cast<std::uint32_t>(px[1]) << 8) | (static_cast<std::uint32_t>(px[2]) << 16) | (static_cast<std::uint32_t>(px[3]) << 24);
						const float c0 = static_cast<float>(v & 0x3FF) / 1023.0f;
						const float c1 = static_cast<float>((v >> 10) & 0x3FF) / 1023.0f;
						const float c2 = static_cast<float>((v >> 20) & 0x3FF) / 1023.0f;
						g = c1;
						if (kind == pixel_kind::rgb10a2) { r = c0; b = c2; }
						else { b = c0; r = c2; }
						break;
					}
					}
					out_rgb[n * 3 + 0] = r;
					out_rgb[n * 3 + 1] = g;
					out_rgb[n * 3 + 2] = b;
					++n;
				}
			}
			return n;
		}

		// RGB(0~1) → 성운 정육면체 안의 월드 좌표. R=X, G=Y, B=Z. 중심은 파이프 위.
		inline vec3 nebula_pos(float r, float g, float b)
		{
			return { (r - 0.5f) * kNebulaSize, kNebulaCenterY + (g - 0.5f) * kNebulaSize, (b - 0.5f) * kNebulaSize };
		}

		// ── 바닥 파형(게임 중 프레임 시간) ─────────────────────────────────
		// 링(오래된 것부터)을 buckets 칸으로 나눠 칸마다 **최대값** ms 를 쓴다 — 평균으로 내면
		// 끊김 스파이크가 사라진다. 링이 비어 있으면 0 을 돌려준다.
		inline int waveform(const frametime::ring &r, int buckets, float *out_max_ms)
		{
			const std::size_t n = r.size();
			if (n == 0 || buckets <= 0)
				return 0;
			for (int b = 0; b < buckets; ++b)
			{
				const std::size_t i0 = n * static_cast<std::size_t>(b) / static_cast<std::size_t>(buckets);
				std::size_t i1 = n * static_cast<std::size_t>(b + 1) / static_cast<std::size_t>(buckets);
				if (i1 <= i0) i1 = i0 + 1;
				float m = 0.0f;
				for (std::size_t i = i0; i < i1 && i < n; ++i)
					if (r.at(i) > m) m = r.at(i);
				out_max_ms[b] = m;
			}
			return buckets;
		}
	}
}
