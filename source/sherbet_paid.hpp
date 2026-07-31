/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 유료 기능 카탈로그와 잠금 판정 — 순수 로직(ImGui·Windows·네트워크 없음).
// tools/sherbet_paid_test.cpp 가 이 헤더를 그대로 컴파일해 맥 clang 으로 검증한다.
//
// ── 왜 카탈로그가 **클라이언트**에 있는가 ────────────────────────────────────
// 테마·프리셋·조준점은 데이터라서 서버 매니페스트가 진열을 통째로 정한다. 기능은 다르다.
// 기능의 UI 는 **코드**다 — DLL 에 없는 기능을 서버가 진열해봐야 눌러도 아무 일이 없고,
// DLL 에 있는 기능은 서버가 뭐라 하든 이미 거기 있다. 서버가 새로 정할 게 없다.
//
// 그런데 /content/me 의 features 배열은 **가진 것만** 내려온다(설계상 그래야 한다 —
// 안 산 사람에게 상품 목록을 통째로 흘리지 않기 위해서다). 그래서 클라가 카탈로그를
// 모르면 "이런 게 있는데 당신은 아직 잠겨 있다"를 영영 말할 수 없고, 지금까지의 잠금
// 기능(custompicture)이 그냥 **안 보이게** 처리된 이유가 이것이다.
//   안 보이는 기능은 한 개도 안 팔린다.
// 그래서 진열 문구(무엇인지·사면 뭐가 생기는지)는 여기 코드에 있고, 서버는 열쇠만 준다.
//
// ── 세 번째 유료 기능을 추가하려면 ──────────────────────────────────────────
// 아래 catalog() 의 초기화 목록에 feature 하나를 더한다. 그게 전부다.
// 개수를 손으로 세는 상수는 일부러 두지 않았다(레일 배열의 item_count 같은 사고 방지).
// 그 뒤 UI 쪽에서 그 기능의 그리기 지점을 `unlocked(...)` 로 감싸면 된다.
#pragma once

#include "sherbet_spray.hpp"

#include <cstring>
#include <vector>

namespace sherbet
{
	namespace paid
	{
		// 판매 중인 기능 한 개의 진열 정보.
		struct feature
		{
			// 서버 /content/me 의 features 배열에 들어오는 id. **이 문자열이 계약이다.**
			// server/content/features.json 의 id 와 글자 하나까지 같아야 한다.
			const char *id;
			const char *name;       // 진열 이름
			const char *pitch;      // 한 줄 소개 — "이게 뭐냐"에 답한다. 이름만으로는 안 팔린다.
			const char *bullets[3]; // 사면 생기는 것. 안 쓰는 칸은 nullptr.
		};

		// 이 DLL 이 파는 것 전부.
		inline const std::vector<feature> &catalog()
		{
			static const std::vector<feature> c = {
				{
					"spray",
					"\xEC\x8A\xA4\xED\x94\x84\xEB\xA0\x88\xEC\x9D\xB4 \xED\x8A\xB8\xEB\xA0\x88\xEC\x9D\xB4\xEB\x84\x88", // "스프레이 트레이너"
					// "쏘는 동안 마우스가 어떻게 움직였는지 궤적으로 남겨서, 반동을 매번 같은 손놀림으로 잡고 있는지 눈으로 보여줘요."
					"\xEC\x8F\x98\xEB\x8A\x94 \xEB\x8F\x99\xEC\x95\x88 \xEB\xA7\x88\xEC\x9A\xB0\xEC\x8A\xA4\xEA\xB0\x80 \xEC\x96\xB4\xEB\x96\xBB\xEA\xB2\x8C \xEC\x9B\x80\xEC\xA7\x81\xEC\x98\x80\xEB\x8A\x94\xEC\xA7\x80 \xEA\xB6\xA4\xEC\xA0\x81\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x82\xA8\xEA\xB2\xA8\xEC\x84\x9C, \xEB\xB0\x98\xEB\x8F\x99\xEC\x9D\x84 \xEB\xA7\xA4\xEB\xB2\x88 \xEA\xB0\x99\xEC\x9D\x80 \xEC\x86\x90\xEB\x86\x80\xEB\xA6\xBC\xEC\x9C\xBC\xEB\xA1\x9C \xEC\x9E\xA1\xEA\xB3\xA0 \xEC\x9E\x88\xEB\x8A\x94\xEC\xA7\x80 \xEB\x88\x88\xEC\x9C\xBC\xEB\xA1\x9C \xEB\xB3\xB4\xEC\x97\xAC\xEC\xA4\x98\xEC\x9A\x94.",
					{
						// "한 발 한 발이 점으로 찍히는 스프레이 궤적 — 탭·버스트·연발이 저절로 다른 모양으로 나와요"
						"\xED\x95\x9C \xEB\xB0\x9C \xED\x95\x9C \xEB\xB0\x9C\xEC\x9D\xB4 \xEC\xA0\x90\xEC\x9C\xBC\xEB\xA1\x9C \xEC\xB0\x8D\xED\x9E\x88\xEB\x8A\x94 \xEC\x8A\xA4\xED\x94\x84\xEB\xA0\x88\xEC\x9D\xB4 \xEA\xB6\xA4\xEC\xA0\x81 \xE2\x80\x94 \xED\x83\xAD\xC2\xB7\xEB\xB2\x84\xEC\x8A\xA4\xED\x8A\xB8\xC2\xB7\xEC\x97\xB0\xEB\xB0\x9C\xEC\x9D\xB4 \xEC\xA0\x80\xEC\xA0\x88\xEB\xA1\x9C \xEB\x8B\xA4\xEB\xA5\xB8 \xEB\xAA\xA8\xEC\x96\x91\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x82\x98\xEC\x99\x80\xEC\x9A\x94",
						// "종료 지점 편차와 평균 RPM — 「이번엔 잘 잡았다」를 느낌이 아니라 숫자로 확인해요"
						"\xEC\xA2\x85\xEB\xA3\x8C \xEC\xA7\x80\xEC\xA0\x90 \xED\x8E\xB8\xEC\xB0\xA8\xEC\x99\x80 \xED\x8F\x89\xEA\xB7\xA0 RPM \xE2\x80\x94 '\xEC\x9D\xB4\xEB\xB2\x88\xEC\x97\x94 \xEC\x9E\x98 \xEC\x9E\xA1\xEC\x95\x98\xEB\x8B\xA4'\xEB\xA5\xBC \xEB\x8A\x90\xEB\x82\x8C\xEC\x9D\xB4 \xEC\x95\x84\xEB\x8B\x88\xEB\x9D\xBC \xEC\x88\xAB\xEC\x9E\x90\xEB\xA1\x9C \xED\x99\x95\xEC\x9D\xB8\xED\x95\xB4\xEC\x9A\x94",
						// "최근 20구간 기록과 겹쳐보기 — 어제의 내 스프레이 위에 오늘 것을 포개 봐요"
						"\xEC\xB5\x9C\xEA\xB7\xBC 20\xEA\xB5\xAC\xEA\xB0\x84 \xEA\xB8\xB0\xEB\xA1\x9D\xEA\xB3\xBC \xEA\xB2\xB9\xEC\xB3\x90\xEB\xB3\xB4\xEA\xB8\xB0 \xE2\x80\x94 \xEC\x96\xB4\xEC\xA0\x9C\xEC\x9D\x98 \xEB\x82\xB4 \xEC\x8A\xA4\xED\x94\x84\xEB\xA0\x88\xEC\x9D\xB4 \xEC\x9C\x84\xEC\x97\x90 \xEC\x98\xA4\xEB\x8A\x98 \xEA\xB2\x83\xEC\x9D\x84 \xED\x8F\xAC\xEA\xB0\x9C \xEB\xB4\x90\xEC\x9A\x94",
					},
				},
				{
					"optimize",
					"\xEC\xB5\x9C\xEC\xA0\x81\xED\x99\x94 \xEC\x83\x81\xED\x83\x9C", // "최적화 상태"
					// "Sherbet 이 지금 이 PC 에서 실제로 무엇을 적용하고 있고 프레임을 얼마나 쓰는지, 숫자 그대로 보여줘요."
					"Sherbet \xEC\x9D\xB4 \xEC\xA7\x80\xEA\xB8\x88 \xEC\x9D\xB4 PC \xEC\x97\x90\xEC\x84\x9C \xEC\x8B\xA4\xEC\xA0\x9C\xEB\xA1\x9C \xEB\xAC\xB4\xEC\x97\x87\xEC\x9D\x84 \xEC\xA0\x81\xEC\x9A\xA9\xED\x95\x98\xEA\xB3\xA0 \xEC\x9E\x88\xEA\xB3\xA0 \xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84\xEC\x9D\x84 \xEC\x96\xBC\xEB\xA7\x88\xEB\x82\x98 \xEC\x93\xB0\xEB\x8A\x94\xEC\xA7\x80, \xEC\x88\xAB\xEC\x9E\x90 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C \xEB\xB3\xB4\xEC\x97\xAC\xEC\xA4\x98\xEC\x9A\x94.",
					{
						// "프리셋·활성 효과·컴파일 상태를 한 화면에 — 뭔가 안 켜졌을 때 어디가 문제인지 바로 보여요"
						"\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B\xC2\xB7\xED\x99\x9C\xEC\x84\xB1 \xED\x9A\xA8\xEA\xB3\xBC\xC2\xB7\xEC\xBB\xB4\xED\x8C\x8C\xEC\x9D\xBC \xEC\x83\x81\xED\x83\x9C\xEB\xA5\xBC \xED\x95\x9C \xED\x99\x94\xEB\xA9\xB4\xEC\x97\x90 \xE2\x80\x94 \xEB\xAD\x94\xEA\xB0\x80 \xEC\x95\x88 \xEC\xBC\x9C\xEC\xA1\x8C\xEC\x9D\x84 \xEB\x95\x8C \xEC\x96\xB4\xEB\x94\x94\xEA\xB0\x80 \xEB\xAC\xB8\xEC\xA0\x9C\xEC\x9D\xB8\xEC\xA7\x80 \xEB\xB0\x94\xEB\xA1\x9C \xEB\xB3\xB4\xEC\x97\xAC\xEC\x9A\x94",
						// "후처리 비용(CPU/GPU)과 최근 60프레임 최악값 — 끊김은 평균이 아니라 최악 프레임에서 보여요"
						"\xED\x9B\x84\xEC\xB2\x98\xEB\xA6\xAC \xEB\xB9\x84\xEC\x9A\xA9(CPU/GPU)\xEA\xB3\xBC \xEC\xB5\x9C\xEA\xB7\xBC 60\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84 \xEC\xB5\x9C\xEC\x95\x85\xEA\xB0\x92 \xE2\x80\x94 \xEB\x81\x8A\xEA\xB9\x80\xEC\x9D\x80 \xED\x8F\x89\xEA\xB7\xA0\xEC\x9D\xB4 \xEC\x95\x84\xEB\x8B\x88\xEB\x9D\xBC \xEC\xB5\x9C\xEC\x95\x85 \xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84\xEC\x97\x90\xEC\x84\x9C \xEB\xB3\xB4\xEC\x97\xAC\xEC\x9A\x94",
						// "출력 해상도·백버퍼 형식·그래픽 API — 내 게임이 어떤 조건으로 돌고 있는지 확인해요"
						"\xEC\xB6\x9C\xEB\xA0\xA5 \xED\x95\xB4\xEC\x83\x81\xEB\x8F\x84\xC2\xB7\xEB\xB0\xB1\xEB\xB2\x84\xED\x8D\xBC \xED\x98\x95\xEC\x8B\x9D\xC2\xB7\xEA\xB7\xB8\xEB\x9E\x98\xED\x94\xBD API \xE2\x80\x94 \xEB\x82\xB4 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\xB4 \xEC\x96\xB4\xEB\x96\xA4 \xEC\xA1\xB0\xEA\xB1\xB4\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x8F\x8C\xEA\xB3\xA0 \xEC\x9E\x88\xEB\x8A\x94\xEC\xA7\x80 \xED\x99\x95\xEC\x9D\xB8\xED\x95\xB4\xEC\x9A\x94",
					},
				},
			};
			return c;
		}

		// 카탈로그에서 id 로 찾는다. 없으면 nullptr(= 유료가 아닌 기능).
		inline const feature *find(const char *id)
		{
			if (id == nullptr || id[0] == '\0')
				return nullptr;
			for (const feature &f : catalog())
				if (std::strcmp(f.id, id) == 0)
					return &f;
			return nullptr;
		}

		// ── 잠금 판정의 단일 지점 ────────────────────────────────────────────
		//   entitled       서버 features 배열에 이 id 가 있는가 (= sherbet::has_feature(id))
		//   force_preview  판매 화면 확인용 강제 잠금 (설정 > 「잠금 화면 미리보기」)
		//
		// force_preview 가 필요한 이유: has_feature() 는 auth 가 꺼진 빌드(개발/데모)에서
		// **무조건 true** 다. 그 상태로는 잠금 화면을 한 번도 볼 수 없는데, 잠금 화면이야말로
		// 구매자가 제일 많이 보게 될 화면이다. 판매자가 눈으로 확인할 길을 남겨 둔다.
		//
		// ⚠️ 카탈로그에 없는 id 는 **열린 것**으로 본다. 아직 유료화하지 않은 기능을
		//    실수로 잠가 구매자가 쓰던 것을 잃는 쪽이, 새 유료 기능을 카탈로그에 넣는 걸
		//    잊어 공짜로 나가는 쪽보다 훨씬 나쁘다(전자는 환불, 후자는 다음 커밋).
		inline bool unlocked(const char *id, bool entitled, bool force_preview)
		{
			if (find(id) == nullptr)
				return true;
			if (force_preview)
				return false;
			return entitled;
		}

		// ── 스프레이 트레이너가 지금 살아 있는가 ─────────────────────────────
		// ⚠️ 이 술어가 하나여야 하는 이유(이 코드베이스에서 실제로 데였다):
		//    draw_gui() 는 그릴 게 없으면 early-out 하는데, 그 상태가 정확히 **총을 쏘는
		//    순간**이다(오버레이가 닫혀 있다). early-out 조건과 기록기 게이트를 각각 따로
		//    적으면 둘이 어긋나도 컴파일도 통과하고 CI 도 호스트 테스트도 전부 초록불인 채,
		//    기록만 조용히 한 프레임도 돌지 않는다. 그래서 두 곳 다 여기를 통과한다.
		//    잠금 판정도 여기서만 한다 — 잠긴 기능은 UI 뿐 아니라 **일도 돌면 안 된다**.
		inline bool spray_enabled(bool unlocked_now, bool live_on, bool chart_on)
		{
			return unlocked_now && (live_on || chart_on);
		}

		// 이번 프레임에 실제로 기록해야 하는가.
		// 오버레이가 열려 있는 동안은 기록하지 않는다 — UI 를 조작하는 클릭·이동은 사격이 아니다.
		inline bool spray_recording(bool unlocked_now, bool live_on, bool chart_on, bool overlay_open)
		{
			return spray_enabled(unlocked_now, live_on, chart_on) && !overlay_open;
		}

		// ── 잠금 카드에 그릴 예시 스프레이 ───────────────────────────────────
		// 스프레이 트레이너는 정지 화면으로 설명이 안 된다("궤적을 보여준다"는 글자는
		// 아무것도 보여주지 않는다). 그래서 **진짜 차트 렌더러**에 만들어 둔 예시 데이터를
		// 먹인다. 그 예시는 손으로 좌표를 찍는 게 아니라 **진짜 recorder** 를 통과시켜
		// 만든다 — 그래야 렌더러가 실제로 만날 수 있는 모양만 미리보기에 나온다.
		//
		// ⚠️ 이것은 예시일 뿐 사용자의 기록이 아니다. UI 는 반드시 '예시' 라고 적어야 한다
		//    (spray_chart() 가 돌려주는 is_example 플래그를 그대로 쓴다).
		inline std::vector<spray::segment> build_demo_spray()
		{
			spray::recorder r;
			r.set_gap_ms(spray::kDefaultGapMs);

			// 전형적인 소총 스프레이: 처음 몇 발이 위로 크게 튀고 그 뒤로 좌우로 흔들린다.
			// (raw 마우스 단위. 반동을 "잡는" 손은 이 반대 방향으로 움직인다)
			static const int kUp[12]   = { -15, -13, -11,  -9, -7, -5, -4, -3, -2, -2, -1, -1 };
			static const int kSide[12] = {   0,   1,   3,   6,  8,  5, -2, -8, -9, -5,  2,  6 };

			// 구간마다 조금씩 다르게 — 다 똑같으면 "겹쳐보기"가 무엇을 위한 기능인지 안 보인다.
			// 난수는 고정 시드 LCG 라 빌드마다·프레임마다 같은 그림이 나온다.
			unsigned int seed = 0x5EEDF00Du;
			auto jitter = [&seed]() -> int {
				seed = seed * 1664525u + 1013904223u;
				return static_cast<int>((seed >> 20) % 5u) - 2; // -2..+2
			};

			for (int s = 0; s < 4; ++s)
			{
				for (int i = 0; i < 12; ++i)
					r.on_frame(0.100f, kSide[i] + jitter(), kUp[i] + jitter(), true); // 100ms 간격 = 600 RPM
				r.on_frame(0.600f, 0, 0, false); // 간격(400ms) 초과 → 구간 종료
			}
			return r.history();
		}

		inline const std::vector<spray::segment> &demo_spray()
		{
			static const std::vector<spray::segment> s = build_demo_spray();
			return s;
		}

		// 차트가 그릴 데이터의 출처.
		// ⚠️ 잠긴 상태에서는 **절대** 사용자 기록을 돌려주지 않는다. 미리보기에 진짜
		//    내 숫자가 섞이면 그건 미리보기가 아니라 기능을 그냥 준 것이고, 반대로 예시를
		//    내 기록인 줄 알게 두면 거짓말이다. 둘 다 여기서 막는다.
		struct chart_source
		{
			const std::vector<spray::segment> *segments;
			bool is_example; // true 면 UI 는 반드시 '예시' 배지를 함께 그린다
		};

		inline chart_source spray_chart(bool unlocked_now, const std::vector<spray::segment> &recorded)
		{
			chart_source cs;
			if (unlocked_now)
			{
				cs.segments = &recorded;
				cs.is_example = false;
			}
			else
			{
				cs.segments = &demo_spray();
				cs.is_example = true;
			}
			return cs;
		}

		// ── 잠긴 「최적화」 탭에 그릴 예시 수치 ──────────────────────────────
		struct stat_row
		{
			const char *label;
			const char *value;
		};

		// ⚠️ 전부 **고정 문자열**이다. 실측값은 한 줄도 여기 들어오지 않는다.
		//    잠긴 사람에게 자기 PC 의 진짜 숫자를 보여주면 그건 이미 기능을 판 게 아니라 준 것이다.
		//    포맷 지정자('%')를 넣지 않는 것이 그 규칙의 기계적 표현이고,
		//    tools/sherbet_paid_test.cpp 가 그것을 단언한다 — 나중에 누가 "%.0f fps" 로
		//    바꿔 런타임 값을 끼워 넣으면 그 순간 테스트가 깨진다.
		inline const std::vector<stat_row> &demo_optimize_rows()
		{
			static const std::vector<stat_row> rows = {
				{ "\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B", "Sherbet \xEC\xA7\x80\xEC\x83\x81 \xED\x81\xB4\xEB\xA6\xB0" },                       // 프리셋 / Sherbet 지상 클린
				{ "\xED\x9A\xA8\xEA\xB3\xBC", "\xEC\xBC\x9C\xEC\xA7\x90" },                                                                    // 효과 / 켜짐
				{ "\xED\x99\x9C\xEC\x84\xB1 \xED\x9A\xA8\xEA\xB3\xBC", "12 / 18" },                                                            // 활성 효과
				{ "\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84", "237 fps  \xC2\xB7  4.22 ms" },                                                      // 프레임
				{ "\xEC\xB5\x9C\xEA\xB7\xBC 60\xED\x94\x84\xEB\xA0\x88\xEC\x9E\x84 \xEC\xB5\x9C\xEC\x95\x85", "7.90 ms" },                     // 최근 60프레임 최악
				{ "\xED\x9B\x84\xEC\xB2\x98\xEB\xA6\xAC \xEB\xB9\x84\xEC\x9A\xA9 (CPU)", "0.412 ms" },                                         // 후처리 비용 (CPU)
				{ "\xED\x9B\x84\xEC\xB2\x98\xEB\xA6\xAC \xEB\xB9\x84\xEC\x9A\xA9 (GPU)", "0.688 ms" },                                         // 후처리 비용 (GPU)
				{ "\xEC\xB6\x9C\xEB\xA0\xA5 \xED\x95\xB4\xEC\x83\x81\xEB\x8F\x84", "2560 x 1440" },                                            // 출력 해상도
				{ "\xEB\xB0\xB1\xEB\xB2\x84\xED\x8D\xBC \xED\x98\x95\xEC\x8B\x9D", "R8G8B8A8 (8 bpc)" },                                       // 백버퍼 형식
				{ "\xEA\xB7\xB8\xEB\x9E\x98\xED\x94\xBD API", "Direct3D 11" },                                                                 // 그래픽 API
			};
			return rows;
		}

		// 「최적화」 탭이 런타임 실측값을 읽어도 되는가 — draw_gui_optimize() 의 유일한 분기점.
		// 이게 false 인 경로에서는 런타임 필드에 손을 대지 않고 demo_optimize_rows() 만 그린다.
		inline bool show_real_stats(bool unlocked_now)
		{
			return unlocked_now;
		}
	}
}
