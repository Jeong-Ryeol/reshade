/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
//   clang++ -std=c++17 -Wall -Isource -Ideps/imgui tools/sherbet_paid_test.cpp -o /tmp/pt && /tmp/pt
// ⚠️ -DNDEBUG 를 붙이면 아래 단언이 전부 사라져 무의미하게 통과한다. 절대 붙이지 말 것.
//
// 이 스위트가 지키는 것 세 가지:
//   (1) 잠긴 기능은 **일이 돌면 안 된다** — UI 만 감추는 것으로는 부족하다.
//   (2) 잠금 화면은 **사용자의 진짜 값을 절대 보여주지 않는다** — 예시만 보여준다.
//   (3) 권한 있는 사람에게는 잠금 화면이 **뜨지 않는다** — 산 사람에게 광고를 보이면 안 된다.
#include "sherbet_paid.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

using namespace sherbet;

// ── 1. 카탈로그 모양 ──────────────────────────────────────────────────────────
// 서버 features.json 의 id 와 글자 하나까지 같아야 한다. 오타가 나면 산 사람이
// 영영 잠긴 화면을 보게 되는데 클라도 서버도 아무 에러를 내지 않는다.
static void test_catalog_shape()
{
	const std::vector<paid::feature> &c = paid::catalog();
	assert(c.size() >= 2);

	std::set<std::string> ids;
	for (const paid::feature &f : c)
	{
		assert(f.id != nullptr && f.id[0] != '\0');
		assert(f.name != nullptr && f.name[0] != '\0');
		// 이름만으로는 안 팔린다 — 한 줄 소개는 필수다.
		assert(f.pitch != nullptr && f.pitch[0] != '\0');
		assert(std::strcmp(f.pitch, f.name) != 0);
		// 최소 한 개는 "사면 뭐가 생기는지"가 있어야 한다.
		assert(f.bullets[0] != nullptr && f.bullets[0][0] != '\0');
		// nullptr 다음 칸에 문자열이 또 오면 UI 루프가 조기 종료해 항목이 조용히 사라진다.
		bool seen_null = false;
		for (const char *b : f.bullets)
		{
			if (b == nullptr) { seen_null = true; continue; }
			assert(!seen_null);
			assert(b[0] != '\0');
		}
		assert(ids.insert(f.id).second); // id 중복 금지
	}

	assert(ids.count("spray") == 1);
	assert(ids.count("optimize") == 1);

	assert(paid::find("spray") != nullptr);
	assert(paid::find("optimize") != nullptr);
	assert(std::strcmp(paid::find("spray")->id, "spray") == 0);
	assert(paid::find(nullptr) == nullptr);
	assert(paid::find("") == nullptr);
	assert(paid::find("Spray") == nullptr);      // 대소문자는 서버 계약 그대로 — 관대하게 굴지 않는다
	assert(paid::find("custompicture") == nullptr); // 진열 문구가 없는 기존 기능은 카탈로그 밖
}

// ── 2. 잠금 판정 ──────────────────────────────────────────────────────────────
static void test_lock_decision()
{
	// 권한 없음 → 잠김. 이게 팔아야 할 상태다.
	assert(!paid::unlocked("spray", false, false));
	assert(!paid::unlocked("optimize", false, false));

	// ★ 권한 있음 → 열림. 산 사람에게 판매 카드를 보이면 안 된다.
	assert(paid::unlocked("spray", true, false));
	assert(paid::unlocked("optimize", true, false));

	// 판매 화면 확인용 강제 잠금은 권한을 이긴다(그러라고 있는 스위치다).
	assert(!paid::unlocked("spray", true, true));
	assert(!paid::unlocked("optimize", true, true));
	assert(!paid::unlocked("spray", false, true));

	// 카탈로그 밖 id = 유료가 아님 → 항상 열림. 강제 잠금도 무료 기능은 건드리지 않는다
	// (조준점·조준점 마켓이 「에임」 탭 같은 자리에 있으므로 여기가 새면 무료 기능이 잠긴다).
	assert(paid::unlocked("crosshair", false, false));
	assert(paid::unlocked("crosshair", false, true));
	assert(paid::unlocked("custompicture", false, true));
	assert(paid::unlocked(nullptr, false, true));
	assert(paid::unlocked("", false, true));
}

// ── 3. 「스프레이 트레이너가 지금 켜져 있는가」 단일 술어 ─────────────────────
// 이 프로젝트가 실제로 데인 지점: draw_gui() 의 early-out 조건과 기록기 게이트가
// 따로 적혀 어긋나면, 기록이 한 프레임도 돌지 않는데 CI 는 초록불이었다.
static void test_spray_predicate()
{
	// 잠겨 있으면 토글을 뭘 어떻게 켜도 절대 돌지 않는다.
	for (int live = 0; live < 2; ++live)
		for (int chart = 0; chart < 2; ++chart)
			for (int overlay = 0; overlay < 2; ++overlay)
			{
				assert(!paid::spray_enabled(false, live != 0, chart != 0));
				assert(!paid::spray_recording(false, live != 0, chart != 0, overlay != 0));
			}

	// 열려 있으면 예전 조건(live || chart) 그대로다 — 유료화가 기존 동작을 바꾸면 안 된다.
	assert(!paid::spray_enabled(true, false, false));
	assert(paid::spray_enabled(true, true, false));
	assert(paid::spray_enabled(true, false, true));
	assert(paid::spray_enabled(true, true, true));

	// 오버레이가 열려 있는 동안은 기록하지 않는다(UI 조작 클릭은 사격이 아니다).
	assert(paid::spray_recording(true, true, false, false));
	assert(!paid::spray_recording(true, true, false, true));

	// ★ 기록은 언제나 "켜짐"의 부분집합이다. 둘이 갈라지는 조합이 하나라도 있으면 실패.
	//   (early-out 조건은 spray_enabled 로, 기록기 게이트는 spray_recording 으로 적힌다 —
	//    이 함의가 깨지면 "그릴 게 없어 early-out 했는데 기록은 돌아야 한다"가 생긴다.)
	for (int u = 0; u < 2; ++u)
		for (int live = 0; live < 2; ++live)
			for (int chart = 0; chart < 2; ++chart)
				for (int overlay = 0; overlay < 2; ++overlay)
				{
					const bool rec = paid::spray_recording(u != 0, live != 0, chart != 0, overlay != 0);
					const bool en = paid::spray_enabled(u != 0, live != 0, chart != 0);
					if (rec)
						assert(en);
					// 반대로 오버레이가 닫혀 있으면 켜진 것은 반드시 기록해야 한다.
					if (en && overlay == 0)
						assert(rec);
				}
}

// 잠금 판정이 그대로 술어에 흘러들어가는지 — 두 함수를 이어 붙인 실사용 경로.
static void test_spray_predicate_uses_lock()
{
	const bool entitled_off = paid::unlocked("spray", false, false);
	const bool entitled_on = paid::unlocked("spray", true, false);
	const bool preview = paid::unlocked("spray", true, true);

	assert(!paid::spray_recording(entitled_off, true, true, false)); // 안 산 사람: 기록 없음
	assert(paid::spray_recording(entitled_on, true, true, false));   // 산 사람: 기록됨
	assert(!paid::spray_recording(preview, true, true, false));      // 미리보기 중: 기록 멈춤
}

// ── 4. 잠금 차트는 사용자 기록을 절대 쓰지 않는다 ─────────────────────────────
// 사용자의 진짜 기록처럼 보이게 만든 고정 데이터(예시와 확실히 다른 좌표).
static std::vector<spray::segment> fake_recorded()
{
	spray::recorder r;
	for (int s = 0; s < 3; ++s)
	{
		for (int i = 0; i < 5; ++i)
			r.on_frame(0.050f, 777, -777, true); // 예시 데이터에는 나올 수 없는 크기
		r.on_frame(1.000f, 0, 0, false);
	}
	return r.history();
}

static void test_locked_chart_never_leaks_real_data()
{
	const std::vector<spray::segment> real = fake_recorded();
	assert(!real.empty());

	// 잠김: 출처가 사용자 기록이 아니어야 하고, '예시' 라고 말해야 한다.
	const paid::chart_source locked = paid::spray_chart(false, real);
	assert(locked.segments != &real);
	assert(locked.segments == &paid::demo_spray());
	assert(locked.is_example);

	// 좌표 한 점이라도 사용자 기록에서 새어 나오면 실패(포인터만 다르고 내용이 같은 경우 방어).
	for (const spray::segment &ls : *locked.segments)
		for (const spray::shot &lp : ls.shots)
			for (const spray::segment &rs : real)
				for (const spray::shot &rp : rs.shots)
					assert(!(lp.x == rp.x && lp.y == rp.y && lp.t == rp.t && (lp.x != 0.0f || lp.y != 0.0f)));

	// ★ 열림: 진짜 기록 그대로, '예시' 배지는 없다. 산 사람이 예시를 보면 안 된다.
	const paid::chart_source open = paid::spray_chart(true, real);
	assert(open.segments == &real);
	assert(!open.is_example);

	// 빈 기록(아직 한 발도 안 쏨)에서도 규칙은 같다.
	const std::vector<spray::segment> empty;
	assert(paid::spray_chart(true, empty).segments == &empty);
	assert(!paid::spray_chart(true, empty).is_example);
	assert(paid::spray_chart(false, empty).segments != &empty);
	assert(paid::spray_chart(false, empty).is_example);
}

// ── 5. 예시 스프레이가 진짜 차트 렌더러에 먹히는 모양인가 ─────────────────────
// 잠금 카드는 "스프레이 궤적을 보여준다"는 글자가 아니라 궤적 그 자체를 보여준다.
// 그러려면 예시 데이터가 실제 렌더러/통계 함수가 요구하는 조건을 전부 만족해야 한다.
static void test_demo_spray_is_renderable()
{
	const std::vector<spray::segment> &d = paid::demo_spray();

	// 겹쳐보기와 종료 지점 편차가 의미를 가지려면 구간이 여러 개 있어야 한다.
	assert(d.size() >= 2);
	assert(d.size() <= spray::kMaxSegments);

	for (const spray::segment &s : d)
	{
		assert(s.shots.size() >= 2); // 점 하나짜리만 있으면 '궤적' 이 안 보인다
		// 구간의 첫 발은 언제나 원점 — 실제 recorder 의 불변식이다.
		assert(s.shots.front().x == 0.0f && s.shots.front().y == 0.0f && s.shots.front().t == 0.0f);
		float prev_t = -1.0f;
		for (const spray::shot &p : s.shots)
		{
			assert(p.t >= prev_t); // 시간은 뒤로 가지 않는다
			prev_t = p.t;
			// 차트 캔버스(±110px, 배율 1.0 기준)를 통째로 벗어나면 미리보기가 빈 칸이 된다.
			assert(p.x > -400.0f && p.x < 400.0f);
			assert(p.y > -400.0f && p.y < 400.0f);
		}
		// 마지막 점이 첫 발과 달라야 반동을 '잡는' 그림이 나온다.
		assert(s.shots.back().x != 0.0f || s.shots.back().y != 0.0f);

		float iv = 0.0f;
		assert(spray::avg_interval(s, iv)); // 통계줄(평균 RPM)도 예시로 채워진다
		assert(iv > 0.0f);
	}

	// 종료 지점 편차도 나와야 한다(구간이 전부 똑같으면 0 이 나와 기능 설명이 안 된다).
	float spread = 0.0f;
	assert(spray::end_spread(d, 5, spread));
	assert(spread > 0.0f);

	// 매번 같은 그림이어야 한다 — 프레임마다 흔들리면 미리보기가 아니라 노이즈다.
	const std::vector<spray::segment> again = paid::build_demo_spray();
	assert(again.size() == d.size());
	for (std::size_t i = 0; i < d.size(); ++i)
	{
		assert(again[i].shots.size() == d[i].shots.size());
		for (std::size_t j = 0; j < d[i].shots.size(); ++j)
			assert(again[i].shots[j].x == d[i].shots[j].x && again[i].shots[j].y == d[i].shots[j].y);
	}
	assert(&paid::demo_spray() == &d); // 매 프레임 다시 만들지 않는다
}

// ── 6. 잠긴 「최적화」 탭은 실측값을 한 줄도 보여주지 않는다 ──────────────────
static void test_demo_optimize_rows_are_constant()
{
	const std::vector<paid::stat_row> &rows = paid::demo_optimize_rows();
	assert(rows.size() >= 5); // 몇 줄은 있어야 "이런 화면이구나" 가 전달된다

	for (const paid::stat_row &r : rows)
	{
		assert(r.label != nullptr && r.label[0] != '\0');
		assert(r.value != nullptr && r.value[0] != '\0');
		// ★ 포맷 지정자가 없다 = 런타임 값이 끼어들 자리가 없다.
		//   누가 "%.0f fps" 로 바꿔 실측 프레임을 끼워 넣는 순간 여기서 죽는다.
		assert(std::strchr(r.label, '%') == nullptr);
		assert(std::strchr(r.value, '%') == nullptr);
	}

	// 같은 배열을 계속 돌려준다(어디선가 값을 채워 넣고 있지 않다는 뜻).
	assert(&paid::demo_optimize_rows() == &rows);

	// 잠긴 경로는 실측값을 읽지 않는다. draw_gui_optimize() 의 유일한 분기점.
	assert(!paid::show_real_stats(false));
	assert(paid::show_real_stats(true));
	assert(!paid::show_real_stats(paid::unlocked("optimize", false, false)));
	assert(paid::show_real_stats(paid::unlocked("optimize", true, false)));
	assert(!paid::show_real_stats(paid::unlocked("optimize", true, true))); // 미리보기
}

int main()
{
	test_catalog_shape();
	test_lock_decision();
	test_spray_predicate();
	test_spray_predicate_uses_lock();
	test_locked_chart_never_leaks_real_data();
	test_demo_spray_is_renderable();
	test_demo_optimize_rows_are_constant();
	std::printf("sherbet_paid: ALL PASS\n");
	return 0;
}
