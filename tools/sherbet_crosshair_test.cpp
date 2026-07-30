/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// ⚠️ -DNDEBUG 를 붙이면 아래 단언이 전부 사라져 무의미하게 통과한다. 절대 붙이지 말 것.
//
// 대상: source/sherbet_crosshair.hpp — 발로란트 공유 코드 파서/제너레이터.
// 스펙: docs/superpowers/specs/2026-07-30-sherbet-valorant-crosshair-design.md (§ 표기는 그 문서)
//
// 이 스위트가 지켜야 하는 것 세 가지:
//  1. §2.9 의 실제 유통 코드 5개가 **바이트 단위로** 다시 나온다(키 매핑 오류의 유일한 검출기).
//  2. 왕복:  parse(generate(p)) == canonical(p)  — 모든 p 에 대해.
//  3. 낯선 사람이 붙여 넣은 쓰레기 코드가 절대 반쯤 채워진 프로필을 만들지 않는다.
#include "sherbet_crosshair.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

using namespace sherbet::crosshair;

// ─────────────────────────────────────────────────────────────────
// 공용 도우미
// ─────────────────────────────────────────────────────────────────

static profile parse_ok(const std::string &code)
{
	profile p;
	const parse_report r = parse_code(code, p);
	assert(r.ok());
	return p;
}

static rgba RGBA(int r, int g, int b, int a)
{
	rgba c;
	c.r = static_cast<std::uint8_t>(r);
	c.g = static_cast<std::uint8_t>(g);
	c.b = static_cast<std::uint8_t>(b);
	c.a = static_cast<std::uint8_t>(a);
	return c;
}

static bool feq(float a, float b)
{
	return (a - b) < 1e-6f && (b - a) < 1e-6f;
}

// generate 는 언제나 자기가 다시 읽을 수 있는 문자열을 내야 한다.
static void assert_well_formed(const std::string &code)
{
	assert(!code.empty());
	assert(code[0] == '0');
	assert(code.back() != ';');
	for (std::size_t i = 0; i < code.size(); ++i)
	{
		const char c = code[i];
		const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ';' || c == '.';
		assert(ok); // 로케일 소수점(',')·음수('-')·공백이 새어 나오면 여기서 걸린다
		assert(!(c == ';' && i + 1 < code.size() && code[i + 1] == ';'));
	}
	profile back;
	assert(parse_code(code, back).ok());
}

// ─────────────────────────────────────────────────────────────────
// §1 기본값
// ─────────────────────────────────────────────────────────────────

static void test_defaults()
{
	const profile d;

	// §1.1 General
	assert(d.ads_copies_primary == true); // ⚠️ 기본 true
	assert(d.override_all_primary == false);
	assert(d.advanced_options == false);

	// §1.2 Primary / ADS
	for (const layer *L : { &d.primary, &d.ads })
	{
		assert(L->color_index == 0);
		assert(L->custom_color == RGBA(255, 255, 255, 255));
		assert(L->use_custom_color == false);
		assert(L->has_outline == true);
		assert(L->outline_thickness == 1);
		assert(feq(L->outline_opacity, 0.5f));
		assert(L->show_center_dot == false);
		assert(L->center_dot_size == 2);
		assert(feq(L->center_dot_opacity, 1.0f));
		assert(L->fade_with_firing_error == true);
		assert(L->show_spectated == true);
		assert(L->fix_min_error == false);
	}

	// §1.3 Inner
	assert(d.primary.inner.show_lines == true);
	assert(d.primary.inner.thickness == 2);
	assert(d.primary.inner.length == 6);
	assert(d.primary.inner.length_vertical == 6);
	assert(d.primary.inner.allow_vert_scaling == false);
	assert(d.primary.inner.offset == 3);
	assert(feq(d.primary.inner.opacity, 0.8f));
	assert(d.primary.inner.show_movement_error == false);
	assert(feq(d.primary.inner.movement_error_scale, 1.0f));
	assert(d.primary.inner.show_shooting_error == true);
	assert(feq(d.primary.inner.firing_error_scale, 1.0f));

	// §1.3 Outer — inner 와 다른 곳이 다섯 군데다. 여기가 틀리면 발로란트 유저가 즉시 알아챈다.
	assert(d.primary.outer.show_lines == true);
	assert(d.primary.outer.thickness == 2);
	assert(d.primary.outer.length == 2);
	assert(d.primary.outer.length_vertical == 2);
	assert(d.primary.outer.allow_vert_scaling == false);
	assert(d.primary.outer.offset == 10);
	assert(feq(d.primary.outer.opacity, 0.35f));
	assert(d.primary.outer.show_movement_error == true); // ⚠️ inner 와 반대
	assert(feq(d.primary.outer.movement_error_scale, 1.0f));
	assert(d.primary.outer.show_shooting_error == true);
	assert(feq(d.primary.outer.firing_error_scale, 1.0f));

	// §1.4 Sniper
	assert(d.snipe.show_center_dot == true);
	assert(d.snipe.color_index == 7); // ⚠️ 흰색이 아니라 빨강
	assert(d.snipe.use_custom_color == false);
	assert(d.snipe.custom_color == RGBA(255, 255, 255, 255));
	assert(feq(d.snipe.center_dot_size, 1.0f));
	assert(feq(d.snipe.center_dot_opacity, 0.75f)); // ⚠️ 0.8 이 아니다

	assert(d.unknowns.empty());

	// make_line 이 종류를 구분한다(기본 생성자는 inner 다)
	assert(make_line(line_kind::inner) == d.primary.inner);
	assert(make_line(line_kind::outer) == d.primary.outer);
	assert(!(make_line(line_kind::inner) == make_line(line_kind::outer)));
}

// §1.5 프리셋 8종. 인덱스 2·3 이 틀리는 파서가 많다(§2.10).
static void test_presets()
{
	assert(preset_color(0) == (rgb { 255, 255, 255 }));
	assert(preset_color(1) == (rgb { 0, 255, 0 }));
	assert(preset_color(2) == (rgb { 127, 255, 0 })); // #7FFF00 — #BBFF00 이 아니다
	assert(preset_color(3) == (rgb { 223, 255, 0 })); // #DFFF00 — #ADFF2F / #D6E305 가 아니다
	assert(preset_color(4) == (rgb { 255, 255, 0 }));
	assert(preset_color(5) == (rgb { 0, 255, 255 }));
	assert(preset_color(6) == (rgb { 255, 0, 255 }));
	assert(preset_color(7) == (rgb { 255, 0, 0 }));

	// 범위 밖 인덱스는 클램프하지 않고 보관하므로(§2.6) 해석 함수가 total 이어야 한다.
	assert(preset_color(8) == (rgb { 255, 255, 255 }));
	assert(preset_color(-1) == (rgb { 255, 255, 255 }));
	assert(preset_color(9999) == (rgb { 255, 255, 255 }));

	// §0.3 #9 — 공유 코드에 윤곽선 색 키가 없으므로 import 되는 코드의 윤곽선은 항상 검정.
	assert(kOutlineColor == (rgb { 0, 0, 0 }));
}

// ─────────────────────────────────────────────────────────────────
// §2.9 워크드 예제 — 파싱 결과 + 바이트 단위 재출력
// ─────────────────────────────────────────────────────────────────

// 예제 E — 전부 기본값. "완전 기본 프로필의 코드는 0 한 글자다"(§2.1).
static void test_example_e_all_defaults()
{
	const profile p = parse_ok("0");
	assert(p == profile());
	assert(generate_code(p) == "0");
	assert(generate_code(profile()) == "0");

	// §2.9 예제 E: 휴지 상태 실제 오프셋은 inner 3+4=7, outer 10+4=14 (§3.4)
	assert(resting_offset(p.primary.inner, p.primary) == 7);
	assert(resting_offset(p.primary.outer, p.primary) == 14);
}

// 예제 A — 커스텀 색 + 세로 길이 분리 + 스나이퍼 섹션
static void test_example_a()
{
	const std::string code =
		"0;s;1;P;c;8;u;000000FF;o;1;b;1;0t;3;0l;1;0v;0;0g;1;0o;0;0a;1;0f;0;"
		"1t;1;1l;4;1g;1;1o;0;1a;1;1m;0;1f;0;S;s;0.664;o;1";
	const profile p = parse_ok(code);

	// 루트
	assert(p.advanced_options == true);
	assert(p.ads_copies_primary == true);  // 미기재 → 기본 true
	assert(p.override_all_primary == false);

	// P 스칼라
	assert(p.primary.color_index == 8);
	assert(p.primary.custom_color == RGBA(0x00, 0x00, 0x00, 0xFF));
	assert(feq(p.primary.outline_opacity, 1.0f)); // o 는 P 섹션에서 **윤곽선 투명도**
	assert(p.primary.use_custom_color == true);
	assert(p.primary.has_outline == true);        // 미기재 → 기본 ON
	assert(p.primary.outline_thickness == 1);
	assert(p.primary.show_center_dot == false);
	assert(p.primary.fade_with_firing_error == true);
	assert(p.primary.show_spectated == true);
	assert(p.primary.fix_min_error == false);
	assert(resolve_color(p.primary) == (rgb { 0, 0, 0 })); // 검정 조준점

	// P inner
	assert(p.primary.inner.show_lines == true);
	assert(p.primary.inner.thickness == 3);
	assert(p.primary.inner.length == 1);
	assert(p.primary.inner.length_vertical == 0);
	assert(p.primary.inner.allow_vert_scaling == true);
	assert(p.primary.inner.offset == 0);
	assert(feq(p.primary.inner.opacity, 1.0f));
	assert(p.primary.inner.show_shooting_error == false);
	assert(p.primary.inner.show_movement_error == false); // 미기재 → inner 기본 OFF
	assert(feq(p.primary.inner.movement_error_scale, 1.0f));
	assert(feq(p.primary.inner.firing_error_scale, 1.0f));
	assert(effective_vertical_length(p.primary.inner) == 0); // g=1 → v=0 → 세로 팔 없음
	assert(resting_offset(p.primary.inner, p.primary) == 0); // 0f;0 → +4px 없음

	// P outer
	assert(p.primary.outer.thickness == 1);
	assert(p.primary.outer.length == 4);
	assert(p.primary.outer.length_vertical == 2); // 미기재 → outer 기본 2
	assert(p.primary.outer.allow_vert_scaling == true);
	assert(p.primary.outer.offset == 0);
	assert(feq(p.primary.outer.opacity, 1.0f));
	assert(p.primary.outer.show_movement_error == false); // 1m;0 (기본 ON 을 껐다)
	assert(p.primary.outer.show_shooting_error == false);
	assert(effective_vertical_length(p.primary.outer) == 2);
	assert(resting_offset(p.primary.outer, p.primary) == 0);

	// A 섹션 없음 → 전 기본값
	assert(p.ads == layer());

	// S 섹션
	assert(feq(p.snipe.center_dot_size, 0.664f)); // s 는 S 섹션에서 **크기**(float)
	assert(feq(p.snipe.center_dot_opacity, 1.0f));
	assert(p.snipe.color_index == 7); // 미기재 → 기본 빨강
	assert(p.snipe.show_center_dot == true);
	assert(p.snipe.use_custom_color == false);
	assert(p.snipe.custom_color == RGBA(255, 255, 255, 255));

	assert(p.unknowns.empty());
	assert(generate_code(p) == code); // 바이트 단위 재출력
}

// 예제 B — ADS 섹션이 따로 있는 코드
static void test_example_b()
{
	const std::string code =
		"0;p;0;s;1;P;c;5;h;0;d;1;z;1;f;0;m;1;0t;1;0l;2;0o;1;0a;1;0e;0.847;1b;0;"
		"A;o;1;d;1;z;3;f;0;s;0;0b;0;1b;0;S;c;0;s;0.7;o;0.7";
	const profile p = parse_ok(code);

	assert(p.ads_copies_primary == false); // p;0 → ADS 가 Primary 를 복사하지 않는다
	assert(p.advanced_options == true);

	assert(p.primary.color_index == 5);
	assert(resolve_color(p.primary) == (rgb { 0, 255, 255 })); // 시안
	assert(p.primary.has_outline == false);
	assert(p.primary.outline_thickness == 1);      // 붕괴로 미기재 → 기본
	assert(feq(p.primary.outline_opacity, 0.5f));
	assert(p.primary.show_center_dot == true);
	assert(p.primary.center_dot_size == 1);
	assert(feq(p.primary.center_dot_opacity, 1.0f));
	assert(p.primary.fade_with_firing_error == false);
	assert(p.primary.fix_min_error == true); // m;1 → 휴지 상태 +4px 제거

	assert(p.primary.inner.thickness == 1);
	assert(p.primary.inner.length == 2);
	assert(p.primary.inner.offset == 1);
	assert(feq(p.primary.inner.opacity, 1.0f));
	assert(feq(p.primary.inner.firing_error_scale, 0.847f));
	assert(p.primary.inner.show_shooting_error == true); // 0f 미기재 → 기본 ON
	// m;1 이므로 +4px 가 없다 — 오프셋이 설정값 그대로다(§3.4)
	assert(resting_offset(p.primary.inner, p.primary) == 1);
	assert(p.primary.outer.show_lines == false);
	assert(p.primary.outer.length == 2); // 붕괴로 미기재 → outer 기본

	assert(feq(p.ads.outline_opacity, 1.0f));
	assert(p.ads.has_outline == true); // A 섹션에 h 미기재 → 기본 ON
	assert(p.ads.outline_thickness == 1);
	assert(p.ads.show_center_dot == true);
	assert(p.ads.center_dot_size == 3);
	assert(p.ads.fade_with_firing_error == false);
	assert(p.ads.show_spectated == false);
	assert(p.ads.inner.show_lines == false);
	assert(p.ads.outer.show_lines == false);
	assert(p.ads.color_index == 0);
	assert(resolve_color(p.ads) == (rgb { 255, 255, 255 })); // 색 미기재 → 흰색

	assert(p.snipe.color_index == 0);
	assert(feq(p.snipe.center_dot_size, 0.7f));
	assert(feq(p.snipe.center_dot_opacity, 0.7f));

	assert(generate_code(p) == code);
}

// 예제 C — 오차 4종 세트(m/f/s/e)가 전부 등장. 라인 그룹의 m → f → s → e 순서 근거다.
static void test_example_c()
{
	const std::string code = "0;P;h;0;0t;1;0l;4;0o;0;0a;1;0m;1;0f;0;0s;0.02;1t;3;1l;1;1o;2;1a;1;1f;0;1s;0.02";
	const profile p = parse_ok(code);

	assert(p.primary.has_outline == false);
	assert(p.primary.inner.thickness == 1);
	assert(p.primary.inner.length == 4);
	assert(p.primary.inner.offset == 0);
	assert(feq(p.primary.inner.opacity, 1.0f));
	assert(p.primary.inner.show_movement_error == true); // 기본 OFF 를 켰다
	assert(p.primary.inner.show_shooting_error == false);
	assert(feq(p.primary.inner.movement_error_scale, 0.02f));
	assert(feq(p.primary.inner.firing_error_scale, 1.0f));
	// 오프셋 0 + 발사 오차 OFF → 안쪽 팔 4개가 중앙에서 맞닿는다
	assert(resting_offset(p.primary.inner, p.primary) == 0);

	assert(p.primary.outer.thickness == 3);
	assert(p.primary.outer.length == 1);
	assert(p.primary.outer.offset == 2);
	assert(p.primary.outer.show_shooting_error == false);
	assert(feq(p.primary.outer.movement_error_scale, 0.02f));
	assert(p.primary.outer.show_movement_error == true); // 1m 미기재 → 기본 ON 유지
	assert(resting_offset(p.primary.outer, p.primary) == 2);

	assert(generate_code(p) == code);
}

// 예제 D — 점(dot) 조준점
static void test_example_d()
{
	const std::string code = "0;P;c;1;o;1;d;1;0b;0;1b;0";
	const profile p = parse_ok(code);

	assert(p.primary.color_index == 1);
	assert(resolve_color(p.primary) == (rgb { 0, 255, 0 }));
	assert(feq(p.primary.outline_opacity, 1.0f));
	assert(p.primary.has_outline == true); // h 미기재 → 기본 ON, 두께 1
	assert(p.primary.outline_thickness == 1);
	assert(p.primary.show_center_dot == true);
	assert(p.primary.center_dot_size == 2); // 미기재 → 기본 2
	assert(feq(p.primary.center_dot_opacity, 1.0f));
	assert(p.primary.inner.show_lines == false);
	assert(p.primary.outer.show_lines == false);

	assert(generate_code(p) == code);
}

// ─────────────────────────────────────────────────────────────────
// §2.3 키 전표 — 같은 글자가 섹션마다 다른 뜻이다
// 이 테스트가 "키 의미 두 개를 맞바꾸는" 변이를 잡는 주 검출기다.
// ─────────────────────────────────────────────────────────────────

static void test_key_meaning_differs_per_section()
{
	// ── 's' : 루트=고급옵션 / P=관전자 표시 / 라인=이동오차 배율(float) / S=중앙점 크기(float)
	{
		const profile p = parse_ok("0;s;1");
		assert(p.advanced_options == true);
		assert(p.primary.show_spectated == true); // 건드리지 않았다
		assert(feq(p.primary.inner.movement_error_scale, 1.0f));
		assert(feq(p.snipe.center_dot_size, 1.0f));
	}
	{
		const profile p = parse_ok("0;P;s;0");
		assert(p.primary.show_spectated == false);
		assert(p.advanced_options == false);
	}
	{
		const profile p = parse_ok("0;P;0s;2.5;1s;0.25");
		assert(feq(p.primary.inner.movement_error_scale, 2.5f));
		assert(feq(p.primary.outer.movement_error_scale, 0.25f));
		assert(p.primary.show_spectated == true);
		assert(p.advanced_options == false);
	}
	{
		const profile p = parse_ok("0;S;s;2.204");
		assert(feq(p.snipe.center_dot_size, 2.204f)); // 정수가 아니라 float 이다
		assert(p.advanced_options == false);
		assert(p.primary.show_spectated == true);
	}

	// ── 't' : P=윤곽선 두께(int) / 라인=선 두께(int) / S=커스텀 색 HEX(⚠️ u 가 아니다)
	{
		const profile p = parse_ok("0;P;t;5");
		assert(p.primary.outline_thickness == 5);
		assert(p.primary.inner.thickness == 2);
	}
	{
		const profile p = parse_ok("0;P;0t;9;1t;7");
		assert(p.primary.inner.thickness == 9);
		assert(p.primary.outer.thickness == 7);
		assert(p.primary.outline_thickness == 1);
	}
	{
		const profile p = parse_ok("0;S;t;00FF00FF");
		assert(p.snipe.custom_color == RGBA(0x00, 0xFF, 0x00, 0xFF));
		assert(p.snipe.center_dot_size == 1.0f);
		assert(p.primary.outline_thickness == 1);
	}

	// ── 'o' : P=윤곽선 투명도(float) / 라인=오프셋(int) / S=중앙점 투명도(float)
	{
		const profile p = parse_ok("0;P;o;0.25;0o;7;1o;33;S;o;0.4");
		assert(feq(p.primary.outline_opacity, 0.25f));
		assert(p.primary.inner.offset == 7);
		assert(p.primary.outer.offset == 33);
		assert(feq(p.snipe.center_dot_opacity, 0.4f));
	}

	// ── 'c' : 루트=무기별 통일(bool) / P=색 인덱스 / S=중앙점 색 인덱스
	{
		const profile p = parse_ok("0;c;1;P;c;4;S;c;2");
		assert(p.override_all_primary == true);
		assert(p.primary.color_index == 4);
		assert(p.snipe.color_index == 2);
	}

	// ── 'b' : P=커스텀 색 사용 / 라인=선 표시 / S=커스텀 색 사용
	{
		const profile p = parse_ok("0;P;b;1;0b;0;1b;0;S;b;1");
		assert(p.primary.use_custom_color == true);
		assert(p.primary.inner.show_lines == false);
		assert(p.primary.outer.show_lines == false);
		assert(p.snipe.use_custom_color == true);
	}

	// ── 'a' : P=중앙점 투명도 / 라인=선 투명도
	{
		const profile p = parse_ok("0;P;d;1;a;0.2;0a;0.9;1a;0.1");
		assert(feq(p.primary.center_dot_opacity, 0.2f));
		assert(feq(p.primary.inner.opacity, 0.9f));
		assert(feq(p.primary.outer.opacity, 0.1f));
	}

	// ── 'm' : P=Override Firing Error Offset / 라인=이동 오차 표시
	//    (§0.3 #8 — P 의 m 은 bFixMinErrorAcrossWeapons 이지 bShowMinError 가 아니다)
	{
		const profile p = parse_ok("0;P;m;1;0m;1;1m;0");
		assert(p.primary.fix_min_error == true);
		assert(p.primary.inner.show_movement_error == true);
		assert(p.primary.outer.show_movement_error == false);
	}

	// ── 'f' : P=Fade Crosshair With Firing Error / 라인=발사 오차 표시
	{
		const profile p = parse_ok("0;P;f;0;0f;0;1f;0");
		assert(p.primary.fade_with_firing_error == false);
		assert(p.primary.inner.show_shooting_error == false);
		assert(p.primary.outer.show_shooting_error == false);
	}

	// ── 'd'/'z' : P=중앙점 on/한 변 길이 / S=중앙점 on (S 에는 z 가 없다)
	{
		const profile p = parse_ok("0;P;d;1;z;6;S;d;0");
		assert(p.primary.show_center_dot == true);
		assert(p.primary.center_dot_size == 6);
		assert(p.snipe.show_center_dot == false);
	}

	// ── 'u' : P 전용. S 에서는 미지 키다(S 의 커스텀 색은 't').
	{
		const profile p = parse_ok("0;P;u;A020F0FF");
		assert(p.primary.custom_color == RGBA(0xA0, 0x20, 0xF0, 0xFF));
	}
	{
		profile p;
		const parse_report r = parse_code("0;S;u;A020F0FF", p);
		assert(r.ok());
		assert(r.unknown_items == 1);                                  // S 에는 u 가 없다
		assert(p.snipe.custom_color == RGBA(255, 255, 255, 255));    // 건드리지 않았다
	}

	// ── 'p' : 루트 전용
	{
		const profile p = parse_ok("0;p;0");
		assert(p.ads_copies_primary == false);
	}

	// ── P 와 A 는 완전히 독립이다
	{
		const profile p = parse_ok("0;P;c;3;A;c;6");
		assert(p.primary.color_index == 3);
		assert(p.ads.color_index == 6);
	}
}

// §2.3 / §2.10 — '0l' 은 두 글자 통짜 키다. "섹션 0 의 키 l" 로 쪼개 읽으면
// 안쪽선 값이 전부 조용히 사라진다(RazorReaper 가 고친 버그).
static void test_line_prefix_is_part_of_the_key()
{
	const profile p = parse_ok("0;P;0l;7;1l;9;0v;11;1v;13;0o;1;1o;2;0g;1;1g;1;0e;0.5;1e;2.5");
	assert(p.primary.inner.length == 7);
	assert(p.primary.outer.length == 9);
	assert(p.primary.inner.length_vertical == 11);
	assert(p.primary.outer.length_vertical == 13);
	assert(p.primary.inner.offset == 1);
	assert(p.primary.outer.offset == 2);
	assert(p.primary.inner.allow_vert_scaling == true);
	assert(p.primary.outer.allow_vert_scaling == true);
	assert(feq(p.primary.inner.firing_error_scale, 0.5f));
	assert(feq(p.primary.outer.firing_error_scale, 2.5f));

	// inner 만 건드리면 outer 는 기본값 그대로여야 한다(접두사를 무시하면 여기서 깨진다)
	const profile q = parse_ok("0;P;0l;7");
	assert(q.primary.inner.length == 7);
	assert(q.primary.outer.length == 2);
	assert(q.primary.inner.thickness == 2);

	// 접두사가 2/3 이면 알려진 키가 아니다 → 미지 키로 보관
	profile r;
	const parse_report rep = parse_code("0;P;2l;7", r);
	assert(rep.ok() && rep.unknown_items == 1);
	assert(r.primary.inner.length == 6 && r.primary.outer.length == 2);
}

// ─────────────────────────────────────────────────────────────────
// §2.4 기본값 생략 규칙
// ─────────────────────────────────────────────────────────────────

// "읽기는 관대하게, 쓰기는 기본값 생략" — 기본값을 전부 명시한 코드도 받아들이고,
// 그 결과를 다시 내보내면 "0" 한 글자가 된다.
static void test_explicit_defaults_collapse_to_zero()
{
	const std::string verbose =
		"0;p;1;c;0;s;0;"
		"P;c;0;u;FFFFFFFF;b;0;h;1;t;1;o;0.5;d;0;z;2;a;1;f;1;s;1;m;0;"
		"0b;1;0t;2;0l;6;0v;6;0g;0;0o;3;0a;0.8;0m;0;0s;1;0f;1;0e;1;"
		"1b;1;1t;2;1l;2;1v;2;1g;0;1o;10;1a;0.35;1m;1;1s;1;1f;1;1e;1;"
		"S;d;1;c;7;b;0;t;FFFFFFFF;s;1;o;0.75";
	profile p;
	const parse_report r = parse_code(verbose, p);
	assert(r.ok());
	assert(r.unknown_items == 0 && r.bad_values == 0);
	assert(p == profile());
	assert(generate_code(p) == "0");
}

// 필드 하나만 비-기본값으로 바꾸면 코드에 정확히 그 키 하나만 나온다.
static void test_single_field_emits_single_key()
{
	{ profile p; p.ads_copies_primary = false;         assert(generate_code(p) == "0;p;0"); }
	{ profile p; p.override_all_primary = true;        assert(generate_code(p) == "0;c;1"); }
	{ profile p; p.advanced_options = true;            assert(generate_code(p) == "0;s;1"); }
	{ profile p; p.primary.color_index = 6;            assert(generate_code(p) == "0;P;c;6"); }
	{ profile p; p.primary.custom_color = RGBA(0xA0, 0x20, 0xF0, 0xFF);
	                                                   assert(generate_code(p) == "0;P;u;A020F0FF"); }
	{ profile p; p.primary.use_custom_color = true;    assert(generate_code(p) == "0;P;u;FFFFFFFF;b;1"); }
	{ profile p; p.primary.has_outline = false;        assert(generate_code(p) == "0;P;h;0"); }
	{ profile p; p.primary.outline_thickness = 4;      assert(generate_code(p) == "0;P;t;4"); }
	{ profile p; p.primary.outline_opacity = 0.25f;    assert(generate_code(p) == "0;P;o;0.25"); }
	{ profile p; p.primary.show_center_dot = true;     assert(generate_code(p) == "0;P;d;1"); }
	{ profile p; p.primary.fade_with_firing_error = false; assert(generate_code(p) == "0;P;f;0"); }
	{ profile p; p.primary.show_spectated = false;     assert(generate_code(p) == "0;P;s;0"); }
	{ profile p; p.primary.fix_min_error = true;       assert(generate_code(p) == "0;P;m;1"); }
	{ profile p; p.primary.inner.thickness = 5;        assert(generate_code(p) == "0;P;0t;5"); }
	{ profile p; p.primary.inner.length = 12;          assert(generate_code(p) == "0;P;0l;12"); }
	{ profile p; p.primary.inner.length_vertical = 5;  assert(generate_code(p) == "0;P;0v;5"); }
	{ profile p; p.primary.inner.allow_vert_scaling = true; assert(generate_code(p) == "0;P;0g;1"); }
	{ profile p; p.primary.inner.offset = 0;           assert(generate_code(p) == "0;P;0o;0"); }
	{ profile p; p.primary.inner.opacity = 1.0f;       assert(generate_code(p) == "0;P;0a;1"); }
	{ profile p; p.primary.inner.show_movement_error = true;  assert(generate_code(p) == "0;P;0m;1"); }
	{ profile p; p.primary.inner.show_shooting_error = false; assert(generate_code(p) == "0;P;0f;0"); }
	{ profile p; p.primary.inner.movement_error_scale = 0.02f; assert(generate_code(p) == "0;P;0s;0.02"); }
	{ profile p; p.primary.inner.firing_error_scale = 0.847f;  assert(generate_code(p) == "0;P;0e;0.847"); }
	{ profile p; p.primary.outer.show_movement_error = false;  assert(generate_code(p) == "0;P;1m;0"); }
	{ profile p; p.primary.outer.offset = 40;          assert(generate_code(p) == "0;P;1o;40"); }
	{ profile p; p.ads.color_index = 2;                assert(generate_code(p) == "0;A;c;2"); }
	{ profile p; p.snipe.show_center_dot = false;      assert(generate_code(p) == "0;S;d;0"); }
	{ profile p; p.snipe.use_custom_color = true;      assert(generate_code(p) == "0;S;b;1;t;FFFFFFFF"); }
	{ profile p; p.snipe.color_index = 0;              assert(generate_code(p) == "0;S;c;0"); }
	{ profile p; p.snipe.custom_color = RGBA(0xFF, 0x00, 0x00, 0xFF);
	                                                   assert(generate_code(p) == "0;S;t;FF0000FF"); }
	{ profile p; p.snipe.center_dot_size = 0.628f;     assert(generate_code(p) == "0;S;s;0.628"); }
	{ profile p; p.snipe.center_dot_opacity = 1.0f;    assert(generate_code(p) == "0;S;o;1"); }
}

// §2.4 붕괴 규칙
static void test_collapse_rules()
{
	// bShowLines == false → <p>b;0 하나만. 나머지 라인 키는 일절 출력하지 않는다.
	{
		profile p;
		p.primary.inner.show_lines = false;
		p.primary.inner.thickness = 9;
		p.primary.inner.length = 17;
		p.primary.inner.offset = 19;
		p.primary.inner.opacity = 0.12f;
		assert(generate_code(p) == "0;P;0b;0");
	}
	{
		profile p;
		p.primary.outer.show_lines = false;
		p.primary.outer.length = 8;
		assert(generate_code(p) == "0;P;1b;0");
	}
	// bHasOutline == false → h;0 만. t/o 는 출력하지 않는다.
	{
		profile p;
		p.primary.has_outline = false;
		p.primary.outline_thickness = 6;
		p.primary.outline_opacity = 0.1f;
		assert(generate_code(p) == "0;P;h;0");
	}
	// bDisplayCenterDot == false → d/z/a 전부 출력하지 않는다.
	{
		profile p;
		p.primary.show_center_dot = false;
		p.primary.center_dot_size = 6;
		p.primary.center_dot_opacity = 0.3f;
		assert(generate_code(p) == "0");
	}
	// 켜져 있으면 당연히 나온다.
	{
		profile p;
		p.primary.show_center_dot = true;
		p.primary.center_dot_size = 6;
		p.primary.center_dot_opacity = 0.3f;
		assert(generate_code(p) == "0;P;d;1;z;6;a;0.3");
	}
	// 붕괴는 canonical() 에 반영된다 — 그래야 왕복이 닫힌다.
	{
		profile p;
		p.primary.inner.show_lines = false;
		p.primary.inner.length = 17;
		const profile back = parse_ok(generate_code(p));
		assert(back == canonical(p));
		assert(back.primary.inner.length == 6); // 코드가 담지 못한 값은 기본값으로 돌아온다
		assert(!(back == p));
	}
	// §2.4 "0b;0 뒤에 0t/0l/0o 가 붙은 코드는 두 코퍼스 통틀어 단 하나도 없다."
	// 그래도 파서는 그런 코드를 받아들인다(읽기는 관대하게).
	{
		const profile p = parse_ok("0;P;0b;0;0t;4;0l;9");
		assert(p.primary.inner.show_lines == false);
		assert(p.primary.inner.thickness == 4);
		assert(p.primary.inner.length == 9);
		assert(generate_code(p) == "0;P;0b;0"); // 다시 내보낼 때는 붕괴한다
	}
	// S 섹션에는 붕괴를 적용하지 않는다(증거가 없다 — 값을 잃지 않는 쪽을 택했다).
	{
		profile p;
		p.snipe.show_center_dot = false;
		p.snipe.center_dot_size = 2.5f;
		assert(generate_code(p) == "0;S;d;0;s;2.5");
	}
}

// §2.4 — 여기서 대부분의 파서가 틀린다. v 는 g 와 무관하게 비-기본값이면 항상 출력된다.
// genesy·ruwiss 가 v 를 g 켜졌을 때만 내보내 값을 잃는 바로 그 자리다.
static void test_vertical_length_is_not_gated_by_g()
{
	// g 없이 v 만 있는 코드 — 코퍼스 실측 27건
	{
		const std::string code = "0;P;0v;5";
		const profile p = parse_ok(code);
		assert(p.primary.inner.length_vertical == 5);
		assert(p.primary.inner.allow_vert_scaling == false);
		assert(generate_code(p) == code); // g 가 꺼져 있어도 v 를 잃지 않는다
		// 링크 상태이므로 렌더링에는 안 쓰인다 — 저장값만 남아 있는 것이다
		assert(effective_vertical_length(p.primary.inner) == 6);
	}
	// g 가 켜지면 v 가 실제로 쓰인다
	{
		const profile p = parse_ok("0;P;0v;5;0g;1");
		assert(effective_vertical_length(p.primary.inner) == 5);
	}
	{
		profile p;
		p.primary.outer.length_vertical = 7;
		assert(generate_code(p) == "0;P;1v;7");
	}
}

// §2.8 커스텀 색 — 3개 키가 한 세트
static void test_custom_color_triple()
{
	// c;8 + u + b;1 세트
	{
		const profile p = parse_ok("0;P;c;8;u;008000FF;b;1");
		assert(uses_custom_color(p.primary));
		assert(resolve_color(p.primary) == (rgb { 0x00, 0x80, 0x00 })); // 녹색
		// §0.3 #1 — RRGGBBAA 다. AARRGGBB 였다면 알파가 0(완전투명)이 되어 모순이다.
		assert(p.primary.custom_color.a == 0xFF);
	}
	// u 만 있고 b;1 이 없는 코드 — 코퍼스 41건. "저장돼 있지만 지금은 프리셋을 쓰는 중".
	{
		const profile p = parse_ok("0;s;1;P;c;7;u;FF0000FF;h;0");
		assert(p.primary.color_index == 7);
		assert(p.primary.use_custom_color == false);
		assert(!uses_custom_color(p.primary));
		assert(resolve_color(p.primary) == (rgb { 255, 0, 0 })); // 프리셋 7(빨강)이 쓰인다
		assert(p.primary.custom_color == RGBA(0xFF, 0x00, 0x00, 0xFF)); // 값은 보관된다
		assert(generate_code(p) == "0;s;1;P;c;7;u;FF0000FF;h;0");
	}
	// c;8 만 있고 b;1 이 없는 코드 — §2.8 의 판정은 OR 이다: (colorIdx == 8) || useCustomColorFlag.
	// 한쪽만 구현하면 여기서 프리셋 0(흰색)으로 조용히 떨어진다.
	{
		const std::string code = "0;P;c;8;u;FF0000FF";
		const profile p = parse_ok(code);
		assert(p.primary.color_index == 8);
		assert(p.primary.use_custom_color == false);
		assert(uses_custom_color(p.primary));
		assert(resolve_color(p.primary) == (rgb { 0xFF, 0x00, 0x00 }));
		assert(generate_code(p) == code); // b;1 을 지어내지 않는다
	}
	// b;1 만 있고 c;8 이 없는 코드 — 게임은 안 내지만 외부 툴은 낼 수 있다(§2.8).
	{
		const profile p = parse_ok("0;P;u;A020F0FF;b;1");
		assert(p.primary.color_index == 0);
		assert(uses_custom_color(p.primary));
		assert(resolve_color(p.primary) == (rgb { 0xA0, 0x20, 0xF0 }));
		// export 가 c 를 8 로 조용히 덮어쓰면 안 된다
		assert(generate_code(p) == "0;P;u;A020F0FF;b;1");
	}
	// §2.7 규칙 3 — 커스텀 색을 쓰는 중이면 기본 HEX 여도 u 를 낸다.
	{
		profile p;
		p.primary.color_index = 8;
		p.primary.use_custom_color = true;
		assert(generate_code(p) == "0;P;c;8;u;FFFFFFFF;b;1");
	}
	// §2.6 6자리 HEX 는 뒤에 FF 를 붙인다.
	{
		const profile p = parse_ok("0;P;u;00FF00;b;1");
		assert(p.primary.custom_color == RGBA(0x00, 0xFF, 0x00, 0xFF));
		assert(generate_code(p) == "0;P;u;00FF00FF;b;1"); // 출력은 언제나 8자리
	}
	// 소문자 HEX 도 받아 대문자로 정규화한다.
	{
		const profile p = parse_ok("0;P;u;a020f0ff");
		assert(p.primary.custom_color == RGBA(0xA0, 0x20, 0xF0, 0xFF));
		assert(generate_code(p) == "0;P;u;A020F0FF");
	}
	// 알파는 읽어서 보관하고 export 때 되돌려 주되 **렌더링에는 쓰지 않는다**(§2.8).
	{
		const profile p = parse_ok("0;P;u;11223344;b;1");
		assert(p.primary.custom_color.a == 0x44);
		assert(resolve_color(p.primary) == (rgb { 0x11, 0x22, 0x33 })); // 알파가 섞이지 않는다
		assert(generate_code(p) == "0;P;u;11223344;b;1");
	}
	// 스나이퍼도 같은 판정(단 키가 't')
	{
		const profile p = parse_ok("0;S;c;8;t;123456FF;b;1");
		assert(uses_custom_color(p.snipe));
		assert(resolve_color(p.snipe) == (rgb { 0x12, 0x34, 0x56 }));
	}
}

// ─────────────────────────────────────────────────────────────────
// §2.6 거부 경로 — 낯선 사람이 붙여 넣은 코드가 들어온다
// "부분 적용은 없다": 거부 사유에 걸리면 out 을 한 글자도 건드리지 않는다.
// ─────────────────────────────────────────────────────────────────

static void assert_rejected(const std::string &code, parse_error expect)
{
	// out 을 알아볼 수 있는 값으로 채워 두고, 실패 후에도 그대로인지 본다.
	profile sentinel;
	sentinel.primary.color_index = 5;
	sentinel.primary.inner.length = 19;
	sentinel.advanced_options = true;

	profile out = sentinel;
	const parse_report r = parse_code(code, out);
	assert(!r.ok());
	assert(r.error == expect);
	assert(out == sentinel); // 반쯤 채워진 프로필이 나오면 안 된다
}

static void test_rejections()
{
	// 빈 입력
	assert_rejected("", parse_error::empty);
	assert_rejected("   ", parse_error::empty);
	assert_rejected("\n\t ", parse_error::empty);

	// 접두 토큰 (§0.3 #7 — 맨 앞 '0' 은 프로필 인덱스가 아니라 고정 토큰이다)
	assert_rejected("1;P;c;5", parse_error::bad_prefix);
	assert_rejected("00;P;c;5", parse_error::bad_prefix);
	assert_rejected("P;c;5", parse_error::bad_prefix);
	assert_rejected(";P;c;5", parse_error::bad_prefix);
	assert_rejected("0P;c;5", parse_error::bad_prefix);

	// 불법 문자 — 게임의 정규식 ^0[a-zA-Z0-9;.]*$ 와 같은 집합
	assert_rejected("0;P;c;-1", parse_error::illegal_char);   // '-' 는 코드에 존재할 수 없다
	assert_rejected("0;P;c; 5", parse_error::illegal_char);   // 내부 공백
	assert_rejected("0;P;c;5\n;h;0", parse_error::illegal_char);
	assert_rejected("0;P;c;5,h;0", parse_error::illegal_char); // 로케일 소수점
	assert_rejected("0;P;u;#FF0000", parse_error::illegal_char);
	assert_rejected("0;P:c;5", parse_error::illegal_char);
	assert_rejected("0;P;c;5;<script>", parse_error::illegal_char);

	// 빈 토큰 — 키나 값 자리가 비었다
	assert_rejected("0;", parse_error::empty_token);
	assert_rejected("0;;", parse_error::empty_token);
	assert_rejected("0;;s;1", parse_error::empty_token);
	assert_rejected("0;P;c;5;", parse_error::empty_token);
	assert_rejected("0;P;;5", parse_error::empty_token);

	// 값 없는 키로 끝나는 절단 코드 — 커뮤니티가 "숫자로 끝날 때까지 백스페이스" 하는 이유
	assert_rejected("0;P;c", parse_error::dangling_key);
	assert_rejected("0;s", parse_error::dangling_key);
	assert_rejected("0;P;c;5;h", parse_error::dangling_key);
	assert_rejected("0;P;c;5;0t;3;0l", parse_error::dangling_key);

	// 터무니없는 길이
	{
		std::string huge = "0";
		while (huge.size() <= kMaxCodeLen)
			huge += ";P;c;5";
		assert_rejected(huge, parse_error::too_long);
	}

	// §2.9 예제 A 를 앞에서부터 한 글자씩 잘라 전수로 먹인다. 어느 것도 죽으면 안 되고,
	// 통과한 것은 반드시 완결된 프로필이어야 한다.
	{
		const std::string code =
			"0;s;1;P;c;8;u;000000FF;o;1;b;1;0t;3;0l;1;0v;0;0g;1;0o;0;0a;1;0f;0;"
			"1t;1;1l;4;1g;1;1o;0;1a;1;1m;0;1f;0;S;s;0.664;o;1";
		for (std::size_t n = 0; n <= code.size(); ++n)
		{
			profile p;
			const parse_report r = parse_code(code.substr(0, n), p);
			if (r.ok())
				assert_well_formed(generate_code(p));
		}
		// 뒤에서부터 자른 것도
		for (std::size_t n = 0; n <= code.size(); ++n)
		{
			profile p;
			const parse_report r = parse_code(code.substr(n), p);
			if (r.ok())
				assert_well_formed(generate_code(p));
		}
	}
}

// ─────────────────────────────────────────────────────────────────
// §2.6 관대한 경로 — 거부하지 않고 그 키만 무시하거나 보관하는 것들
// ─────────────────────────────────────────────────────────────────

static void test_lenient_paths()
{
	// 앞뒤 공백·개행은 trim 후 진행(게임은 거부하지만 우리는 관대하게)
	{
		const profile p = parse_ok("  \n 0;P;c;5 \t\r\n ");
		assert(p.primary.color_index == 5);
	}

	// int 필드에 소수 → **그 키만 무시**. 코드 전체는 살린다.
	{
		profile p;
		const parse_report r = parse_code("0;P;0l;4.5;0t;3", p);
		assert(r.ok());
		assert(r.bad_values == 1);
		assert(p.primary.inner.length == 6); // 기본값 유지
		assert(p.primary.inner.thickness == 3); // 뒤의 키는 정상 적용
	}
	// 숫자가 아닌 값, 지수 표기도 마찬가지
	{
		profile p;
		const parse_report r = parse_code("0;P;0l;abc;0a;1e2;0o;5", p);
		assert(r.ok());
		assert(r.bad_values == 2);
		assert(p.primary.inner.length == 6);
		assert(feq(p.primary.inner.opacity, 0.8f));
		assert(p.primary.inner.offset == 5);
	}
	// 망가진 HEX
	{
		profile p;
		const parse_report r = parse_code("0;P;u;GGGGGGGG;c;3", p);
		assert(r.ok() && r.bad_values == 1);
		assert(p.primary.custom_color == RGBA(255, 255, 255, 255));
		assert(p.primary.color_index == 3);
	}
	{
		profile p;
		assert(parse_code("0;P;u;FFF", p).bad_values == 1);   // 3자리
		assert(parse_code("0;P;u;FFFFFFF", p).bad_values == 1); // 7자리
		assert(parse_code("0;P;u;FFFFFFFFFF", p).bad_values == 1);
	}

	// §2.6 범위 밖 값은 **클램프하지 않고 그대로 수용**한다. UI 슬라이더 한계는
	// §1 이지만 텍스트 입력으로 초과 가능하고, 바깥선 상한은 아직 실물 미확인이다(§6 #1).
	{
		const profile p = parse_ok("0;P;1o;60;1l;18;0l;25;0o;40;c;12;z;9;t;9");
		assert(p.primary.outer.offset == 60);  // UI 상한 40 을 넘겨도 그대로
		assert(p.primary.outer.length == 18);  // UI 상한 10 을 넘겨도 그대로
		assert(p.primary.inner.length == 25);
		assert(p.primary.inner.offset == 40);
		assert(p.primary.color_index == 12);
		assert(p.primary.center_dot_size == 9);
		assert(p.primary.outline_thickness == 9);
		// 범위 밖 값은 export 때도 그대로 나간다. z;9 는 중앙점이 꺼져 있어 붕괴로 사라진다.
		assert(generate_code(p) == "0;P;c;12;t;9;0l;25;0o;40;1l;18;1o;60");
	}
	// 배율도 0–3 을 넘겨 받는다
	{
		const profile p = parse_ok("0;P;0s;7.5;1e;9");
		assert(feq(p.primary.inner.movement_error_scale, 7.5f));
		assert(feq(p.primary.outer.firing_error_scale, 9.0f));
	}
	// 안전 상한에서만 잘라낸다 — 자릿수가 터무니없어도 UB 없이 포화한다.
	{
		const profile p = parse_ok("0;P;0l;99999999999999999999;0o;12345");
		assert(p.primary.inner.length == kValueCap);
		assert(p.primary.inner.offset == kValueCap);
	}
	{
		const profile p = parse_ok("0;P;0a;99999999999999999999.99999");
		assert(feq(p.primary.inner.opacity, kFloatCap));
	}

	// 미지 키 → 값 소비 후 계속. 경고 카운트만 올린다.
	{
		profile p;
		const parse_report r = parse_code("0;P;q;7;c;5;zz;abc", p);
		assert(r.ok());
		assert(r.unknown_items == 2);
		assert(p.primary.color_index == 5); // 미지 키가 스트림을 밀어내지 않았다
	}
	// 미지 키의 값이 1글자 대문자여도 키+값을 원자적으로 소비하므로 섹션이 안 바뀐다(§2.6)
	{
		profile p;
		const parse_report r = parse_code("0;P;q;A;c;5", p);
		assert(r.ok() && r.unknown_items == 1);
		assert(p.primary.color_index == 5);
		assert(p.ads == layer());
	}

	// 중복 키 → 나중 것이 이긴다(토큰 스트림이므로 자연스러운 결과)
	{
		const profile p = parse_ok("0;P;c;1;c;5;0l;3;0l;9;h;1;h;0");
		assert(p.primary.color_index == 5);
		assert(p.primary.inner.length == 9);
		assert(p.primary.has_outline == false);
	}
	// 섹션이 다시 나와도 된다
	{
		const profile p = parse_ok("0;P;c;5;A;c;6;P;h;0");
		assert(p.primary.color_index == 5);
		assert(p.primary.has_outline == false);
		assert(p.ads.color_index == 6);
	}

	// bool 값이 0/1 이 아니면 0 이 아닌 값을 참으로 본다(읽기는 관대하게)
	{
		const profile p = parse_ok("0;P;h;2;d;0");
		assert(p.primary.has_outline == true);
		assert(p.primary.show_center_dot == false);
	}
}

// §2.6 미지 마커 안전장치 — focusMode 용 마커나 미래 패치의 새 섹션이 나와도
// 그 뒤 전체가 밀리지 않아야 한다. 여기가 깨지면 미래 코드가 통째로 오염된다.
static void test_unknown_section_markers()
{
	// 미지 섹션의 키는 **어디에도 적용되지 않는다**
	{
		profile p;
		const parse_report r = parse_code("0;F;c;5;0l;9;d;1", p);
		assert(r.ok());
		assert(r.unknown_items == 3);
		assert(p.primary == layer());
		assert(p.ads == layer());
		assert(p.snipe == sniper());
	}
	// 미지 섹션 뒤에 알려진 섹션이 오면 정상 복귀한다
	{
		profile p;
		const parse_report r = parse_code("0;F;x;1;y;2;P;c;5;S;o;0.5", p);
		assert(r.ok() && r.unknown_items == 2);
		assert(p.primary.color_index == 5);
		assert(feq(p.snipe.center_dot_opacity, 0.5f));
	}
	// 마커만 있고 키가 없어도 안전하다
	{
		const profile p = parse_ok("0;F");
		assert(p == profile());
		assert(generate_code(p) == "0");
	}
	// 소문자 1글자는 마커가 아니라 키다(루트 's' = 고급 옵션)
	{
		const profile p = parse_ok("0;s;1");
		assert(p.advanced_options == true);
	}

	// §2.7 규칙 8 — 미지 키/미지 섹션을 원문 그대로 보관했다가 다시 내보낸다.
	{
		const profile p = parse_ok("0;zz;9;P;c;5;qq;7;F;a;1;S;o;0.5");
		assert(p.unknowns.size() == 3);
		assert(p.unknowns[0].marker == "");
		assert(p.unknowns[0].items.size() == 1 && p.unknowns[0].items[0].key == "zz" && p.unknowns[0].items[0].value == "9");
		assert(p.unknowns[1].marker == "P");
		assert(p.unknowns[1].items[0].key == "qq");
		assert(p.unknowns[2].marker == "F");
		assert(p.unknowns[2].items[0].key == "a");

		// 미지 섹션은 알려진 섹션 뒤로 밀린다 → 코드 문자열은 달라지지만 뜻은 같다
		const std::string out = generate_code(p);
		assert(out == "0;zz;9;P;c;5;qq;7;S;o;0.5;F;a;1");
		assert(parse_ok(out) == canonical(p));
		assert(generate_code(parse_ok(out)) == out); // 두 번째부터는 고정점이다
	}
}

// ─────────────────────────────────────────────────────────────────
// §2.5 값 포맷
// ─────────────────────────────────────────────────────────────────

static void test_float_format()
{
	profile p;
	p.snipe.center_dot_opacity = 1.0f;
	assert(generate_code(p) == "0;S;o;1"); // "1.000" 이 아니다
	p.snipe.center_dot_opacity = 0.5f;
	assert(generate_code(p) == "0;S;o;0.5"); // "0.500" 이 아니다
	p.snipe.center_dot_opacity = 0.802f;
	assert(generate_code(p) == "0;S;o;0.802");
	p.snipe.center_dot_opacity = 0.053f;
	assert(generate_code(p) == "0;S;o;0.053");
	p.snipe.center_dot_opacity = 0.02f;
	assert(generate_code(p) == "0;S;o;0.02");
	p.snipe.center_dot_opacity = 0.0f;
	assert(generate_code(p) == "0;S;o;0");

	// §0.3 #14 — 파싱에는 정밀도 제약이 없다. 실세이브의 float32 잔차를 그대로 받는다.
	{
		const profile q = parse_ok("0;S;o;0.30000001192092896");
		assert(feq(q.snipe.center_dot_opacity, 0.3f));
		assert(generate_code(q) == "0;S;o;0.3"); // 출력은 3자리 관례
	}
	// 소수점만 있거나 앞자리가 없는 형태도 parseFloat 처럼 받는다
	{
		const profile q = parse_ok("0;S;o;.5");
		assert(feq(q.snipe.center_dot_opacity, 0.5f));
	}
	{
		const profile q = parse_ok("0;S;s;2.");
		assert(feq(q.snipe.center_dot_size, 2.0f));
	}
	// '.' 하나만은 값이 아니다
	{
		profile q;
		assert(parse_code("0;S;o;.", q).bad_values == 1);
	}
	// 3자리 넘는 입력은 출력에서 반올림되고, canonical() 이 그 반올림을 반영한다
	{
		profile q;
		q.snipe.center_dot_opacity = 0.123456f;
		assert(generate_code(q) == "0;S;o;0.123");
		assert(parse_ok(generate_code(q)) == canonical(q));
	}
}

// ─────────────────────────────────────────────────────────────────
// 순수 헬퍼
// ─────────────────────────────────────────────────────────────────

static void test_helpers()
{
	// §3.2 세로 팔 길이
	{
		line L = make_line(line_kind::inner);
		L.length = 6;
		L.length_vertical = 2;
		L.allow_vert_scaling = false;
		assert(effective_vertical_length(L) == 6);
		L.allow_vert_scaling = true;
		assert(effective_vertical_length(L) == 2);
	}

	// §3.4 휴지 상태 +4px. inner/outer 가 각자 독립적으로 받는다.
	// ⚠️ 아래 단언들은 §6 #6 의 **채택한 읽기**를 못 박는다. 실물 확인 후 게이트가
	//    bShowMinError 로 밝혀지면 kMinErrorGate 를 바꾸고 여기도 같이 고쳐야 한다.
	assert(kMinErrorGate == min_error_gate::firing_error);
	{
		profile p;
		assert(resting_offset(p.primary.inner, p.primary) == 3 + kMinErrorPx);
		assert(resting_offset(p.primary.outer, p.primary) == 10 + kMinErrorPx);
		// m;1 이면 4px 가 사라진다
		p.primary.fix_min_error = true;
		assert(resting_offset(p.primary.inner, p.primary) == 3);
		assert(resting_offset(p.primary.outer, p.primary) == 10);
		// f;0 이면 그 라인만 4px 가 사라진다
		p.primary.fix_min_error = false;
		p.primary.inner.show_shooting_error = false;
		assert(resting_offset(p.primary.inner, p.primary) == 3);
		assert(resting_offset(p.primary.outer, p.primary) == 14);
		// +4 는 발사 오차 **배율**로 곱해지지 않는다 — 불리언만 본다
		p.primary.inner.show_shooting_error = true;
		p.primary.inner.firing_error_scale = 3.0f;
		assert(resting_offset(p.primary.inner, p.primary) == 3 + kMinErrorPx);
	}

	// §3.6 알파 — 반올림이 아니라 절삭이다
	assert(alpha_from_opacity(1.0f) == 255);
	assert(alpha_from_opacity(0.0f) == 0);
	assert(alpha_from_opacity(0.5f) == 127);
	assert(alpha_from_opacity(0.8f) == 204);
	assert(alpha_from_opacity(-1.0f) == 0);   // 범위 밖은 클램프
	assert(alpha_from_opacity(9.0f) == 255);

	assert(clamp_int(-5) == 0 && clamp_int(5) == 5 && clamp_int(9999) == kValueCap);
	assert(feq(clamp_float(-1.0), 0.0f) && feq(clamp_float(0.5), 0.5f));
	assert(feq(clamp_float(1e9), kFloatCap));

	assert(kShowMinError == true);
	assert(kScaleToResolutionDefault == false);
	assert(kMinErrorPx == 4);
}

// ─────────────────────────────────────────────────────────────────
// 왕복 — 가장 세게 못 박는 성질
// ─────────────────────────────────────────────────────────────────

static std::uint32_t g_rnd = 2463534242u;
static std::uint32_t rnd()
{
	g_rnd ^= g_rnd << 13;
	g_rnd ^= g_rnd >> 17;
	g_rnd ^= g_rnd << 5;
	return g_rnd;
}
static int rnd_int(int lo, int hi) { return lo + static_cast<int>(rnd() % static_cast<std::uint32_t>(hi - lo + 1)); }
static bool rnd_bool() { return (rnd() & 1u) != 0; }
static float rnd_float()
{
	switch (rnd() % 7)
	{
	case 0: return 0.0f;
	case 1: return 1.0f;
	case 2: return static_cast<float>(rnd() % 1001) / 1000.0f;             // 0..1, 3자리
	case 3: return static_cast<float>(rnd() % 3001) / 1000.0f;             // 0..3, 3자리
	case 4: return 0.30000001192092896f;                                   // float32 잔차
	case 5: return static_cast<float>(rnd() % 100000) / 99991.0f;          // 3자리를 넘는 값
	default: return static_cast<float>(rnd() % 20000) / 10.0f;             // 안전 상한 근처
	}
}
static rgba rnd_rgba()
{
	rgba c;
	c.r = static_cast<std::uint8_t>(rnd() & 0xFF);
	c.g = static_cast<std::uint8_t>(rnd() & 0xFF);
	c.b = static_cast<std::uint8_t>(rnd() & 0xFF);
	c.a = static_cast<std::uint8_t>(rnd() & 0xFF);
	return c;
}
static line rnd_line(line_kind k)
{
	line L = make_line(k);
	L.show_lines = (rnd() % 4) != 0; // 대부분 켜 둬야 나머지 필드가 실제로 실린다
	L.thickness = rnd_int(0, 14);
	L.length = rnd_int(0, 30);
	L.length_vertical = rnd_int(0, 30);
	L.allow_vert_scaling = rnd_bool();
	L.offset = rnd_int(0, 60);
	L.opacity = rnd_float();
	L.show_movement_error = rnd_bool();
	L.movement_error_scale = rnd_float();
	L.show_shooting_error = rnd_bool();
	L.firing_error_scale = rnd_float();
	if ((rnd() % 16) == 0) // 안전 상한을 실제로 때린다
		L.length = rnd_int(kValueCap - 2, kValueCap + 500);
	return L;
}
static layer rnd_layer()
{
	layer L;
	L.color_index = rnd_int(0, 12); // 범위 밖(9..12)도 섞는다
	L.custom_color = rnd_rgba();
	L.use_custom_color = rnd_bool();
	L.has_outline = (rnd() % 4) != 0;
	L.outline_thickness = rnd_int(0, 9);
	L.outline_opacity = rnd_float();
	L.show_center_dot = rnd_bool();
	L.center_dot_size = rnd_int(0, 9);
	L.center_dot_opacity = rnd_float();
	L.fade_with_firing_error = rnd_bool();
	L.show_spectated = rnd_bool();
	L.fix_min_error = rnd_bool();
	L.inner = rnd_line(line_kind::inner);
	L.outer = rnd_line(line_kind::outer);
	return L;
}
static sniper rnd_sniper()
{
	sniper S;
	S.show_center_dot = rnd_bool();
	S.color_index = rnd_int(0, 10);
	S.use_custom_color = rnd_bool();
	S.custom_color = rnd_rgba();
	S.center_dot_size = rnd_float();
	S.center_dot_opacity = rnd_float();
	return S;
}
static void rnd_unknowns(unknown_store &u)
{
	static const char *markers[5] = { "", "P", "A", "S", "F" };
	static const char *keys[4] = { "qq", "zz", "0q", "9" };
	static const char *vals[4] = { "7", "abc", "1.25", "FFEE00AA" };
	const int n = rnd_int(0, 3);
	for (int i = 0; i < n; ++i)
	{
		const std::string m = markers[rnd() % 5];
		std::vector<unknown_entry> &b = detail::unknown_bucket(u, m);
		b.push_back(unknown_entry { keys[rnd() % 4], vals[rnd() % 4] });
	}
}

// 모든 p 에 대해:
//   parse(generate(p)) == canonical(p)      — 코드가 담을 수 있는 것은 전부 담긴다
//   generate(parse(generate(p))) == generate(p) — 두 번째부터는 고정점이다
//   generate 의 출력은 언제나 다시 읽힌다
static void test_round_trip_sweep()
{
	for (int iter = 0; iter < 4000; ++iter)
	{
		profile p;
		p.ads_copies_primary = rnd_bool();
		p.override_all_primary = rnd_bool();
		p.advanced_options = rnd_bool();
		p.primary = rnd_layer();
		p.ads = rnd_layer();
		p.snipe = rnd_sniper();
		if ((rnd() % 3) == 0)
			rnd_unknowns(p.unknowns);

		const std::string code = generate_code(p);
		assert_well_formed(code);

		profile back;
		const parse_report r = parse_code(code, back);
		assert(r.ok());
		assert(r.bad_values == 0); // 우리가 낸 코드에 타입 오류가 있으면 안 된다

		const profile canon = canonical(p);
		assert(back == canon);
		assert(canonical(canon) == canon);     // canonical 은 멱등
		assert(generate_code(back) == code);   // 고정점
	}
}

// 실제 유통 코드 형태를 파싱 → 재생성 → 재파싱 해도 값이 흔들리지 않는다.
static void test_round_trip_from_codes()
{
	static const char *codes[] = {
		"0",
		"0;s;1;P;c;8;u;000000FF;o;1;b;1;0t;3;0l;1;0v;0;0g;1;0o;0;0a;1;0f;0;1t;1;1l;4;1g;1;1o;0;1a;1;1m;0;1f;0;S;s;0.664;o;1",
		"0;p;0;s;1;P;c;5;h;0;d;1;z;1;f;0;m;1;0t;1;0l;2;0o;1;0a;1;0e;0.847;1b;0;A;o;1;d;1;z;3;f;0;s;0;0b;0;1b;0;S;c;0;s;0.7;o;0.7",
		"0;P;h;0;0t;1;0l;4;0o;0;0a;1;0m;1;0f;0;0s;0.02;1t;3;1l;1;1o;2;1a;1;1f;0;1s;0.02",
		"0;P;c;1;o;1;d;1;0b;0;1b;0",
		"0;s;1;P;c;7;u;FF0000FF;h;0;0b;0;1b;0",
		"0;P;u;A020F0FF;o;0.298;0l;3;0o;2;1b;0",
		"0;c;1;s;1;P;0v;5;1v;9;1g;1",
		"0;P;0b;0;1t;4;1l;10;1o;40;1a;1",
		"0;S;d;0;b;1;c;8;t;00FF00FF;s;3.5;o;0.9",
	};
	for (const char *c : codes)
	{
		const std::string code(c);
		const profile p = parse_ok(code);
		// 예제들은 §2.4 생략 규칙과 §2.7 순서를 지키고 있으므로 바이트 단위로 돌아온다
		assert(generate_code(p) == code);
		assert(parse_ok(generate_code(p)) == p);
		assert(canonical(p) == p);
	}
}

// ─────────────────────────────────────────────────────────────────
// §3 기하 — "이 파라미터면 어떤 사각형이 어디에 있는가"
// ─────────────────────────────────────────────────────────────────

static bool contains(const rect &r, int x, int y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// §3.3 검산: t=3, off=0, lenH=6, C=64.
//   우 = x∈[64,70), 좌 = x∈[57,63) → 두 팔의 중점 63.5
//   상/하 팔의 x = floor(64 − 1.5) = 62, 3px → [62,65) → 중심 63.5 ✔ 일치
// 스펙이 손으로 계산해 둔 유일한 known-answer 다. 여기가 틀리면 나머지는 볼 것도 없다.
static void test_arm_geometry_worked_example()
{
	line L = make_line(line_kind::inner);
	L.thickness = 3;
	L.length = 6;
	L.allow_vert_scaling = false; // 세로 길이 = 가로 길이 = 6

	rect a[4];
	line_arms(L, 64, 64, 0, a);

	assert(a[0] == (rect { 64, 62, 6, 3 })); // 우: x∈[64,70), y∈[62,65)
	assert(a[1] == (rect { 57, 62, 6, 3 })); // 좌: x∈[57,63)
	assert(a[2] == (rect { 62, 64, 3, 6 })); // 하: x∈[62,65)
	assert(a[3] == (rect { 62, 57, 3, 6 })); // 상

	// 두 가로 팔의 중점 = (64 + 63) / 2 = 63.5
	assert((a[0].x + (a[1].x + a[1].w)) == 127); // 63.5 × 2
	// 세로 팔의 중심도 63.5
	assert((a[2].x + (a[2].x + a[2].w)) == 127);
	// 세로 방향도 같은 값이어야 대칭이다
	assert((a[2].y + (a[3].y + a[3].h)) == 127);
	assert((a[0].y + (a[0].y + a[0].h)) == 127);

	// 홀수 두께면 조준점 전체가 좌상단으로 0.5px 스냅된다 → 좌 팔이 우 팔보다 1px 더 나간다
	assert(kOddThicknessShiftsTopLeft); // 지금 채택한 읽기(§6 #3)
	assert(a[1].x == 64 - 0 - 6 - 1);
}

// 짝수 두께는 par==0 이라 정확히 (cx, cy) 가 중심이다.
static void test_arm_geometry_even_thickness()
{
	line L = make_line(line_kind::inner);
	L.thickness = 4;
	L.length = 6;

	rect a[4];
	line_arms(L, 100, 100, 0, a);
	assert(a[0] == (rect { 100, 98, 6, 4 }));  // 우
	assert(a[1] == (rect { 94, 98, 6, 4 }));   // 좌 — par 가 붙지 않는다
	assert(a[2] == (rect { 98, 100, 4, 6 }));  // 하
	assert(a[3] == (rect { 98, 94, 4, 6 }));   // 상
	// 좌우가 완전 대칭: 중점 정확히 100
	assert((a[0].x + (a[1].x + a[1].w)) == 200);
	assert((a[2].y + (a[3].y + a[3].h)) == 200);

	// 두께 2 도 마찬가지
	L.thickness = 2;
	line_arms(L, 100, 100, 0, a);
	assert(a[0] == (rect { 100, 99, 6, 2 }));
	assert(a[1] == (rect { 94, 99, 6, 2 }));

	// 두께 1(홀수)은 다시 좌상단 스냅
	L.thickness = 1;
	line_arms(L, 100, 100, 0, a);
	assert(a[0] == (rect { 100, 99, 6, 1 }));
	assert(a[1] == (rect { 93, 99, 6, 1 })); // 100-0-6-1
	assert(a[2] == (rect { 99, 100, 1, 6 }));
	assert(a[3] == (rect { 99, 93, 1, 6 }));
}

// §3.2 #1/#2 — 오프셋은 중심에서 선의 **안쪽 끝**까지의 거리이고, 길이는 바깥으로 자란다.
// 선 중앙까지로 재거나 배율을 곱하는 구현이 흔하다(ValorantCC 는 offset 을 2배로 쟀다).
static void test_offset_is_inner_edge_and_length_grows_outward()
{
	line L = make_line(line_kind::inner);
	L.thickness = 2;
	L.length = 6;

	rect a[4];
	line_arms(L, 100, 100, 3, a);
	// 우 팔은 중심에서 3px 떨어진 곳에서 **시작**해서 바깥으로 6px 자란다 → [103, 109)
	assert(a[0].x == 103 && a[0].w == 6);
	// 좌 팔은 [100-3-6, 100-3) = [91, 97)
	assert(a[1].x == 91 && a[1].x + a[1].w == 97);
	// 하 팔은 [103, 109)
	assert(a[2].y == 103 && a[2].y + a[2].h == 109);
	// 상 팔은 [91, 97)
	assert(a[3].y == 91 && a[3].y + a[3].h == 97);

	// 오프셋을 키우면 안쪽 끝만 밀린다 — 길이는 그대로다(§4.1 #2 의 전제)
	rect b[4];
	line_arms(L, 100, 100, 10, b);
	assert(b[0].x == 110 && b[0].w == a[0].w);
	assert(b[1].x + b[1].w == 90 && b[1].w == a[1].w);
	// 두께 방향 좌표는 오프셋과 무관하다
	assert(b[0].y == a[0].y && b[2].x == a[2].x);

	// §3.2 #4 — 바깥선도 안쪽선 끝 기준이 아니라 똑같이 중심에서 재는 절대 오프셋이다.
	line O = make_line(line_kind::outer);
	O.thickness = 2;
	O.length = 2;
	rect o[4];
	line_arms(O, 100, 100, 10, o);
	assert(o[0].x == 110); // inner 의 끝(109)과 무관하게 중심 + 10
}

// §3.2 #5 — w<=0 또는 h<=0 이면 그 팔을 아예 그리지 않는다(윤곽선도 없다).
// `0l;0` + `0v;5` 면 세로 팔만 있는 조준점이 된다.
static void test_zero_length_arms_are_dropped()
{
	profile p = parse_ok("0;P;h;0;0l;0;0v;5;0g;1;0o;0;1b;0");
	quad_list q;
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);

	// 가로 팔 2개는 길이 0 이라 사라지고 세로 팔 2개만 남는다
	assert(q.size() == 2);
	for (const quad &Q : q)
		assert(Q.r.w > 0 && Q.r.h > 0);
	assert(q[0].r.h == 5 && q[1].r.h == 5);

	// 두께 0 이면 팔이 전부 사라진다
	profile z = parse_ok("0;P;h;0;0t;0;1b;0");
	build_crosshair(z.primary, 100, 100, 0.0f, 0.0f, q);
	assert(q.empty());

	// 길이 0 인 팔은 윤곽선도 없다 — 윤곽선을 켜도 사각형이 늘지 않는다
	profile g = parse_ok("0;P;t;3;0l;0;0v;5;0g;1;0o;0;1b;0");
	build_crosshair(g.primary, 100, 100, 0.0f, 0.0f, q);
	assert(q.size() == 2 * (1 + 4)); // 세로 팔 2개 × (본체 + 링 4조각)
}

// §3.6 — 링은 본체를 사방 T 픽셀로 감싸되 조각끼리 겹치지 않아야 한다.
// 이 성질 하나가 "끝단을 감싸는가"(구멍 없음)와 "모서리가 진해지지 않는가"(중복 없음)를
// 동시에 못 박는다. 확장 사각형 전체를 픽셀 단위로 훑는다.
static bool ring_covers_exactly(const rect &body, int T)
{
	quad_list q;
	detail::push_ring(q, body, T, 255);

	for (int y = body.y - T - 2; y < body.y + body.h + T + 2; ++y)
		for (int x = body.x - T - 2; x < body.x + body.w + T + 2; ++x)
		{
			int n = 0;
			for (const quad &Q : q)
				if (contains(Q.r, x, y))
					++n;

			const bool in_expanded = contains(rect { body.x - T, body.y - T, body.w + 2 * T, body.h + 2 * T }, x, y);
			const bool in_body = contains(body, x, y);
			const int want = (in_expanded && !in_body) ? 1 : 0;
			if (n != want)
				return false;
		}
	return true;
}

static void test_outline_ring_geometry()
{
	// 손계산 한 건: 본체 (10,20,6,2), T=1
	{
		quad_list q;
		detail::push_ring(q, rect { 10, 20, 6, 2 }, 1, 255);
		assert(q.size() == 4);
		assert(q[0].r == (rect { 9, 19, 8, 1 }));  // 상 — 모서리까지 포함해 w+2T
		assert(q[1].r == (rect { 9, 22, 8, 1 }));  // 하
		assert(q[2].r == (rect { 9, 20, 1, 2 }));  // 좌
		assert(q[3].r == (rect { 16, 20, 1, 2 })); // 우
		for (const quad &Q : q)
		{
			assert(Q.color == kOutlineColor); // 항상 검정(§0.3 #9)
			assert(Q.alpha == 255);
		}
	}

	// 성질: 링은 (확장 사각형 − 본체) 를 정확히 한 번씩 덮는다.
	// 끝단 조각을 빼면 구멍이 생기고, 조각이 겹치면 2가 나온다. 본체를 채우면 본체에서 1이 나온다.
	for (int T = 1; T <= 6; ++T)
		for (int w = 1; w <= 7; ++w)
			for (int h = 1; h <= 7; ++h)
				assert(ring_covers_exactly(rect { 30, 40, w, h }, T));

	// 세로로 긴 본체(세로 팔)도 마찬가지
	assert(ring_covers_exactly(rect { 0, 0, 2, 20 }, 6));
	assert(ring_covers_exactly(rect { -5, -5, 20, 2 }, 3));

	// T=0 이거나 본체가 비면 링이 없다
	{
		quad_list q;
		detail::push_ring(q, rect { 10, 20, 6, 2 }, 0, 255);
		assert(q.empty());
		detail::push_ring(q, rect { 10, 20, 0, 2 }, 3, 255);
		assert(q.empty());
		detail::push_ring(q, rect { 10, 20, 6, 2 }, 3, 0); // 알파 0 → 그릴 게 없다
		assert(q.empty());
	}
}

// §3.6 #3 — 본체 알파와 링 알파는 곱해지지 않는다. 각자 독립적으로 합성된다.
static void test_body_and_ring_alpha_are_independent()
{
	// 안쪽선 불투명도 0.8(기본), 윤곽선 불투명도 0.5(기본)
	profile p;
	quad_list q;
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);

	int body = 0, ring = 0;
	for (const quad &Q : q)
	{
		if (Q.color == kOutlineColor)
		{
			assert(Q.alpha == alpha_from_opacity(0.5f)); // 127 — 0.8 × 0.5 = 0.4(102) 가 아니다
			++ring;
		}
		else
		{
			++body;
		}
	}
	assert(body > 0 && ring > 0);
	// 기본 조준점의 안쪽선은 0.8, 바깥선은 0.35 — 서로 다른 값이 그대로 살아 있어야 한다
	bool saw_inner = false, saw_outer = false;
	for (const quad &Q : q)
	{
		if (Q.color == kOutlineColor)
			continue;
		if (Q.alpha == alpha_from_opacity(0.8f))
			saw_inner = true;
		if (Q.alpha == alpha_from_opacity(0.35f))
			saw_outer = true;
	}
	assert(saw_inner && saw_outer);

	// 라인 불투명도 0 이면 본체는 사라지고 링만 남는다(꺼진 것과 다르다)
	profile t = parse_ok("0;P;0a;0;1b;0");
	build_crosshair(t.primary, 100, 100, 0.0f, 0.0f, q);
	assert(!q.empty());
	for (const quad &Q : q)
		assert(Q.color == kOutlineColor);
}

// 윤곽선을 끄면 링이 하나도 안 나온다.
static void test_outline_toggle()
{
	quad_list on, off;
	profile a = parse_ok("0;P;1b;0");        // 윤곽선 기본 ON
	profile b = parse_ok("0;P;h;0;1b;0");    // 윤곽선 OFF
	build_crosshair(a.primary, 100, 100, 0.0f, 0.0f, on);
	build_crosshair(b.primary, 100, 100, 0.0f, 0.0f, off);

	assert(off.size() == 4); // 안쪽 팔 4개 본체만
	assert(on.size() == 4 * (1 + 4));
	for (const quad &Q : off)
		assert(Q.color != kOutlineColor);

	// 윤곽선 두께 0 도 같은 결과(링 없음)
	profile c = parse_ok("0;P;t;0;1b;0");
	quad_list z;
	build_crosshair(c.primary, 100, 100, 0.0f, 0.0f, z);
	assert(z.size() == 4);
}

// §3.5 — 중앙 점은 **정사각형**이고 값은 반지름이 아니라 한 변의 길이다.
static void test_center_dot_is_a_square()
{
	layer L;
	L.show_center_dot = true;

	L.center_dot_size = 2;
	assert(center_dot_rect(L, 100, 100) == (rect { 99, 99, 2, 2 })); // floor(100-1.0)
	L.center_dot_size = 3;
	assert(center_dot_rect(L, 100, 100) == (rect { 98, 98, 3, 3 })); // floor(100-1.5)
	L.center_dot_size = 1;
	assert(center_dot_rect(L, 100, 100) == (rect { 99, 99, 1, 1 })); // floor(100-0.5)
	L.center_dot_size = 6;
	assert(center_dot_rect(L, 100, 100) == (rect { 97, 97, 6, 6 }));
	// 한 변이지 반지름이 아니다 — 6 이면 6px 짜리 정사각형이지 12px 이 아니다
	assert(center_dot_rect(L, 100, 100).w == 6);
	// 정사각형이다
	const rect d = center_dot_rect(L, 100, 100);
	assert(d.w == d.h);

	L.center_dot_size = 0;
	quad_list q;
	detail::push_dot(q, L, 100, 100, rgb { 255, 255, 255 }, 1, 255);
	assert(q.empty()); // 한 변 0 → 본체도 링도 없다

	// §2.9 예제 D 전체를 사각형 단위로 못 박는다:
	// "검은 윤곽선이 1px 둘린 초록 2×2 정사각형 하나. 선 없음."
	const profile p = parse_ok("0;P;c;1;o;1;d;1;0b;0;1b;0");
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
	assert(q.size() == 5); // 본체 1 + 링 4
	assert(q[0].r == (rect { 99, 99, 2, 2 }));
	assert(q[0].color == (rgb { 0, 255, 0 }));
	assert(q[0].alpha == 255);
	assert(q[1].r == (rect { 98, 98, 4, 1 }));  // 상
	assert(q[2].r == (rect { 98, 101, 4, 1 })); // 하
	assert(q[3].r == (rect { 98, 99, 1, 2 }));  // 좌
	assert(q[4].r == (rect { 101, 99, 1, 2 })); // 우
	for (std::size_t i = 1; i < q.size(); ++i)
	{
		assert(q[i].color == kOutlineColor);
		assert(q[i].alpha == 255); // o;1 → 윤곽선 불투명도 1
	}

	// 오차가 커져도 중앙 점은 움직이지 않는다(§3.5)
	quad_list e;
	build_crosshair(p.primary, 100, 100, 30.0f, 30.0f, e);
	assert(e[0].r == q[0].r);
}

// §3.7 그리기 순서: Inner(우→좌→하→상) → 중앙 점 → Outer(우→좌→하→상).
// 가로 먼저 세로 나중, outer 가 inner 위 — 4개 소스 전부 일치, 확정이다.
static void test_draw_order()
{
	// 윤곽선을 꺼서 본체만 남긴다 — 순서만 본다. 두께는 짝수로 둔다(홀수 시프트는
	// test_arm_geometry_worked_example 이 따로 못 박는다).
	const profile p = parse_ok("0;P;h;0;d;1;0t;2;0l;4;0o;0;1t;2;1l;3;1o;20");
	quad_list q;
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);

	assert(q.size() == 4 + 1 + 4);

	// inner 4개: 오프셋 0 + 발사 오차 기본 ON → 0+4 = 4
	assert(q[0].r.x == 104);              // 우
	assert(q[1].r.x + q[1].r.w == 96);    // 좌
	assert(q[2].r.y == 104);              // 하
	assert(q[3].r.y + q[3].r.h == 96);    // 상
	// 가로 먼저 세로 나중 — 처음 둘은 가로(w>h), 다음 둘은 세로(h>w)
	assert(q[0].r.w > q[0].r.h && q[1].r.w > q[1].r.h);
	assert(q[2].r.h > q[2].r.w && q[3].r.h > q[3].r.w);

	// 중앙 점이 inner 와 outer 사이에 온다(§6 #5 의 채택 읽기)
	assert(kDotOrder == dot_order::above_inner);
	assert(q[4].r == center_dot_rect(p.primary, 100, 100));

	// outer 4개: 오프셋 20 + 4 = 24
	assert(q[5].r.x == 124);
	assert(q[6].r.x + q[6].r.w == 76);
	assert(q[7].r.y == 124);
	assert(q[8].r.y + q[8].r.h == 76);

	// 각 팔은 [본체 → 자기 링] 1-pass 다(§3.6 #6 · §6 #4)
	assert(kOutlineOnePass);
	const profile o = parse_ok("0;P;t;1;0t;2;0l;4;0o;0;1b;0");
	build_crosshair(o.primary, 100, 100, 0.0f, 0.0f, q);
	assert(q.size() == 4 * 5);
	for (int arm = 0; arm < 4; ++arm)
	{
		assert(q[arm * 5 + 0].color != kOutlineColor); // 본체 먼저
		for (int k = 1; k < 5; ++k)
			assert(q[arm * 5 + k].color == kOutlineColor); // 그 다음 자기 링
	}
}

// §3.4 — 휴지 상태 +4px 가 실제 좌표에 나타난다. 기본 조준점은 inner 7, outer 14 다.
static void test_min_error_offset_in_geometry()
{
	{
		const profile p; // 전 기본값
		quad_list q;
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		// inner 우 팔의 안쪽 끝 = 100 + 3 + 4 = 107
		assert(q[0].r.x == 107);
		// outer 우 팔 = 100 + 10 + 4 = 114. inner 5개(본체+링4) × 4팔 = 20 뒤에 온다.
		assert(q[20].r.x == 114);
	}
	// m;1 이면 +4 가 사라진다
	{
		const profile p = parse_ok("0;P;m;1;h;0;1b;0");
		quad_list q;
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r.x == 103);
	}
	// 0f;0 이면 그 라인만 +4 가 사라진다 (§6 #6 의 채택한 읽기 — kMinErrorGate 로 뒤집는다)
	{
		assert(kMinErrorGate == min_error_gate::firing_error);
		const profile p = parse_ok("0;P;h;0;0f;0");
		quad_list q;
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r.x == 103);  // inner: 3 + 0
		assert(q[4].r.x == 114);  // outer: 10 + 4
	}
	// 동적 오차는 오프셋만 늘린다 — 길이·두께는 그대로다(§4.1 #2)
	{
		const profile p = parse_ok("0;P;h;0;1b;0");
		quad_list a, b;
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, a);
		build_crosshair(p.primary, 100, 100, 6.4f, 0.0f, b);
		assert(b[0].r.x == a[0].r.x + 6); // floor(6.4 + 0.5) = 6
		assert(b[0].r.w == a[0].r.w && b[0].r.h == a[0].r.h);
		quad_list c;
		build_crosshair(p.primary, 100, 100, 6.5f, 0.0f, c);
		assert(c[0].r.x == a[0].r.x + 7); // 1px 계단(§4.3)
	}
}

// §1 범위의 양 끝. 상한을 넘겨도 클램프하지 않으므로(§2.6) 좌표가 그대로 커져야 한다.
static void test_range_extremes()
{
	quad_list q;

	// inner 최대: 길이 20, 두께 10, 오프셋 20 / outer 최대: 길이 10, 오프셋 40
	{
		const profile p = parse_ok("0;P;h;0;0t;10;0l;20;0o;20;0f;0;1t;10;1l;10;1o;40;1f;0");
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r == (rect { 120, 95, 20, 10 }));  // inner 우
		assert(q[4].r == (rect { 140, 95, 10, 10 }));  // outer 우
	}
	// 최소: 전부 0
	{
		const profile p = parse_ok("0;P;h;0;0t;1;0l;1;0v;1;0o;0;0f;0;1b;0");
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q.size() == 4);
		assert(q[0].r == (rect { 100, 99, 1, 1 }));
		assert(q[1].r == (rect { 98, 99, 1, 1 })); // 100-0-1-1
	}
	// UI 상한을 넘는 값도 좌표에 그대로 반영된다(§6 #1 이 정해지기 전까지 클램프 금지)
	{
		const profile p = parse_ok("0;P;h;0;0b;0;1l;18;1o;60;1f;0");
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r.x == 160 && q[0].r.w == 18);
	}
	// 안전 상한(200px)까지 밀어도 정수 연산이 무너지지 않는다
	{
		const profile p = parse_ok("0;P;h;0;0t;200;0l;200;0o;200;0f;0;1b;0");
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r == (rect { 300, 0, 200, 200 }));
	}
	// 세로 길이 링크 해제가 기하에 반영된다
	{
		const profile p = parse_ok("0;P;h;0;0l;6;0v;2;0g;1;0o;0;0f;0;1b;0");
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[0].r.w == 6); // 가로 팔
		assert(q[2].r.h == 2); // 세로 팔은 v
	}
	{
		const profile p = parse_ok("0;P;h;0;0l;6;0v;2;0o;0;0f;0;1b;0"); // g 없음 → 링크 상태
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
		assert(q[2].r.h == 6); // 세로 팔도 l 을 쓴다
	}
}

// 선이 꺼져 있으면 그 그룹은 통째로 사라진다.
static void test_show_lines_gate()
{
	quad_list q;
	const profile p = parse_ok("0;P;h;0;0b;0;1b;0;d;1");
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
	assert(q.size() == 1); // 중앙 점 본체만
	assert(q[0].r == center_dot_rect(p.primary, 100, 100));
}

// 그리기 좌표는 cx/cy 의 순수 평행이동이다(음수 좌표에서도 깨지지 않는다).
static void test_translation_invariance()
{
	const profile p = parse_ok("0;P;c;3;t;2;d;1;z;5;0t;3;0l;7;0v;2;0g;1;1t;1;1l;4");
	quad_list a, b;
	build_crosshair(p.primary, 0, 0, 0.0f, 0.0f, a);
	build_crosshair(p.primary, -640, 360, 0.0f, 0.0f, b);
	assert(a.size() == b.size());
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		assert(b[i].r.x == a[i].r.x - 640);
		assert(b[i].r.y == a[i].r.y + 360);
		assert(b[i].r.w == a[i].r.w && b[i].r.h == a[i].r.h);
		assert(b[i].color == a[i].color && b[i].alpha == a[i].alpha);
	}
}

// 색 해석이 기하까지 이어진다 — 커스텀 색의 알파는 렌더링에 쓰이지 않는다(§2.8).
static void test_geometry_uses_resolved_color()
{
	const profile p = parse_ok("0;P;c;8;u;11223344;b;1;h;0;1b;0");
	quad_list q;
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, q);
	assert(!q.empty());
	for (const quad &Q : q)
	{
		assert(Q.color == (rgb { 0x11, 0x22, 0x33 }));
		assert(Q.alpha == alpha_from_opacity(0.8f)); // 라인 불투명도이지 커스텀 색의 0x44 가 아니다
	}
}

// §2.6 파싱 후처리 — 원본을 파괴하지 않고 사용 시점에 고른다.
static void test_effective_ads()
{
	{
		const profile p = parse_ok("0;p;0;s;1;P;c;5;A;c;6");
		assert(effective_ads(p).color_index == 6); // 고급 옵션 ON + 복사 안 함 → A
		assert(p.ads.color_index == 6);            // 원본은 그대로
	}
	{
		const profile p = parse_ok("0;s;1;P;c;5;A;c;6"); // p 미기재 → 기본 true(복사)
		assert(effective_ads(p).color_index == 5);
		assert(p.ads.color_index == 6); // 파싱된 원본은 파괴되지 않는다
	}
	{
		const profile p = parse_ok("0;p;0;P;c;5;A;c;6"); // 고급 옵션 OFF → Primary
		assert(effective_ads(p).color_index == 5);
		assert(p.ads.color_index == 6);
	}
}

// 어떤 프로필이 와도 기하가 폭주하지 않는다(세니타이저 패스에서 진짜 의미가 있다).
static void test_geometry_sweep()
{
	quad_list q;
	for (int iter = 0; iter < 3000; ++iter)
	{
		layer L = rnd_layer();
		const int cx = rnd_int(-2000, 4000), cy = rnd_int(-2000, 4000);
		build_crosshair(L, cx, cy, static_cast<float>(rnd() % 40), static_cast<float>(rnd() % 40), q);

		assert(q.size() <= 2 * 4 * 5 + 5); // 안쪽·바깥 팔 4개씩 × (본체+링4) + 중앙 점 5
		for (const quad &Q : q)
		{
			assert(Q.r.w > 0 && Q.r.h > 0); // 빈 사각형은 목록에 들어가지 않는다
			assert(Q.alpha > 0);            // 안 보이는 것도 마찬가지
			// 좌표가 안전 상한 안에 머문다(중심에서 최대 offset+length+outline)
			assert(Q.r.x >= cx - 900 && Q.r.x <= cx + 900);
			assert(Q.r.y >= cy - 900 && Q.r.y <= cy + 900);
		}
	}
}

// ─────────────────────────────────────────────────────────────────
// §1.6 클래식 → 발로란트 변환
// ─────────────────────────────────────────────────────────────────

// UI 슬라이더 범위를 넘는 값이 들어왔는지 — 넘었으면 UI 가 "슬라이더를 건드리면 잘린다"고
// 미리 알려야 한다(§2.6 은 import 때 클램프를 금지하므로 이 상태가 정상적으로 생긴다).
static void test_exceeds_ui_range()
{
	assert(!exceeds_ui_range(layer()));                       // 기본값은 당연히 범위 안
	assert(!exceeds_ui_range(parse_ok("0").primary));

	assert(exceeds_ui_range(parse_ok("0;P;1o;60").primary));  // 바깥선 간격 상한 40
	assert(exceeds_ui_range(parse_ok("0;P;1l;18").primary));  // 바깥선 길이 상한 10 (§6 #1)
	assert(exceeds_ui_range(parse_ok("0;P;0l;25").primary));  // 안쪽선 길이 상한 20
	assert(exceeds_ui_range(parse_ok("0;P;0t;11").primary));
	assert(exceeds_ui_range(parse_ok("0;P;c;12").primary));
	assert(exceeds_ui_range(parse_ok("0;P;t;7").primary));    // 윤곽선 두께 상한 6
	assert(exceeds_ui_range(parse_ok("0;P;z;9").primary));
	assert(exceeds_ui_range(parse_ok("0;P;0s;5").primary));   // 배율 상한 3
	assert(exceeds_ui_range(parse_ok("0;P;0a;2").primary));   // 투명도 상한 1

	// 경계값은 범위 안이다
	assert(!exceeds_ui_range(parse_ok("0;P;0l;20;1l;10;0o;20;1o;40;0t;10;t;6;z;6;c;8;0s;3;1e;3").primary));

	// ⚠️ 윤곽선 두께의 min 은 0 이 아니라 1 이다(§1.2) — 0 은 범위 밖으로 잡혀야 한다
	assert(exceeds_ui_range(parse_ok("0;P;t;0").primary));
	assert(exceeds_ui_range(parse_ok("0;P;z;0").primary));
	// 선 두께의 min 은 0 이 맞다
	assert(!exceeds_ui_range(parse_ok("0;P;0t;0").primary));
}

static void test_classic_migration()
{
	// 십자: size 24 / gap 6 / thick 2 → 팔 길이 24/2−6 = 6, 오프셋 6, 두께 2
	{
		classic_crosshair c;
		c.shape = 2;
		c.size = 24.0f;
		c.gap = 6.0f;
		c.thickness = 2.0f;
		c.opacity = 1.0f;
		c.color[0] = 1.0f; c.color[1] = 0.0f; c.color[2] = 0.5f; c.color[3] = 1.0f;

		const layer L = layer_from_classic(c);
		assert(L.inner.show_lines);
		assert(L.inner.offset == 6);
		assert(L.inner.length == 6);
		assert(L.inner.length_vertical == 6);
		assert(L.inner.thickness == 2);
		assert(!L.inner.allow_vert_scaling);
		assert(!L.outer.show_lines);   // 클래식은 1계층이다
		assert(!L.has_outline);        // 없던 검은 테두리가 생기면 안 된다
		assert(!L.show_center_dot);
		assert(uses_custom_color(L));
		assert(resolve_color(L) == (rgb { 255, 0, 128 })); // 0.5 → 128(반올림)
		assert(L.custom_color.a == 255);
		assert(feq(L.inner.opacity, 1.0f));

		// ★ 가장 중요한 단언: 휴지 오프셋이 **정확히 클래식의 간격**이어야 한다.
		//   발사 오차를 켠 채로 가져오면 §3.4 의 +4px 이 붙어 10 이 된다.
		assert(!L.inner.show_shooting_error && !L.inner.show_movement_error);
		assert(resting_offset(L.inner, L) == 6);

		// 실제 사각형도 클래식과 같은 자리에서 시작한다
		quad_list q;
		build_crosshair(L, 100, 100, 0.0f, 0.0f, q);
		assert(q.size() == 4); // 윤곽선 없음 → 본체 4개
		assert(q[0].r.x == 106 && q[0].r.w == 6);
		assert(q[1].r.x + q[1].r.w == 94);
	}

	// 점: 반지름 max(1.5, thick) 의 원 → 한 변이 그 지름인 정사각형
	{
		classic_crosshair c;
		c.shape = 1;
		c.thickness = 2.0f;
		const layer L = layer_from_classic(c);
		assert(L.show_center_dot);
		assert(L.center_dot_size == 4); // round(2 × 2.0)
		assert(!L.inner.show_lines && !L.outer.show_lines);

		c.thickness = 1.0f; // max(1.5, 1.0) = 1.5 → 3
		assert(layer_from_classic(c).center_dot_size == 3);
		c.thickness = 9.0f; // 18 → UI 상한 6 으로 자른다
		assert(layer_from_classic(c).center_dot_size == kUiCenterDotSize.hi);
	}

	// 십자+점: 둘 다
	{
		classic_crosshair c;
		c.shape = 4;
		const layer L = layer_from_classic(c);
		assert(L.inner.show_lines && L.show_center_dot);
	}

	// 원은 발로란트에 없다 → 십자로 근사한다(비어 있는 것보다 낫다)
	{
		classic_crosshair c;
		c.shape = 3;
		const layer L = layer_from_classic(c);
		assert(L.inner.show_lines);
	}

	// ⚠️ 이미지 모드는 대응이 **없다** — 변환하면 화면에 아무것도 안 남는다.
	//    함수는 정직하게 빈 결과를 주고, 그래서 UI 가 이 경우 버튼을 막아야 한다.
	{
		classic_crosshair c;
		c.shape = 0;
		const layer L = layer_from_classic(c);
		quad_list q;
		build_crosshair(L, 100, 100, 0.0f, 0.0f, q);
		assert(q.empty());
	}

	// 간격이 크면 팔 길이가 음수가 될 수 있다 → 0 으로 잘리고 그 팔은 안 그려진다
	{
		classic_crosshair c;
		c.shape = 2;
		c.size = 10.0f;
		c.gap = 40.0f;
		const layer L = layer_from_classic(c);
		assert(L.inner.length == 0);
		quad_list q;
		build_crosshair(L, 100, 100, 0.0f, 0.0f, q);
		assert(q.empty());
	}

	// 투명도는 색 알파 × 슬라이더
	{
		classic_crosshair c;
		c.shape = 2;
		c.opacity = 0.5f;
		c.color[3] = 0.5f;
		const layer L = layer_from_classic(c);
		assert(feq(L.inner.opacity, 0.25f));
	}

	// 변환 결과가 공유 코드로 왕복된다(= 그대로 export/import 가능하다)
	{
		classic_crosshair c;
		c.shape = 4;
		c.size = 30.0f;
		c.gap = 4.0f;
		c.thickness = 3.0f;
		profile p;
		p.primary = layer_from_classic(c);
		const std::string code = generate_code(p);
		assert_well_formed(code);
		assert(parse_ok(code) == canonical(p));
	}
}

// ─────────────────────────────────────────────────────────────────
// §4 오차 애니메이션 — 합성 타임라인
// ─────────────────────────────────────────────────────────────────

static const float kF60 = 1.0f / 60.0f;

// 프레임 하나. 기본은 "아무 입력 없음".
static error_input frame(float dt = kF60)
{
	error_input in;
	in.dt = dt;
	return in;
}
static void run(error_state &s, const error_tuning &t, error_input in, int frames)
{
	for (int i = 0; i < frames; ++i)
		update_error(s, t, in);
}

// 아무 입력이 없으면 값이 **정확히** 0 이어야 한다. 0.0001 이라도 남으면
// 오차를 켜 둔 유저의 조준점이 영원히 미세하게 어긋난다.
static void test_error_idle_is_exactly_zero()
{
	error_state s;
	const error_tuning t;
	run(s, t, frame(), 600); // 10초

	assert(s.vel == 0.0f);
	assert(s.fire_px == 0.0f);
	assert(movement_error_px(s, t) == 0.0f);

	const profile p; // 기본 조준점(안쪽 이동오차 OFF/발사오차 ON, 바깥 둘 다 ON)
	assert(line_error_px(p.primary.inner, s, t) == 0.0f);
	assert(line_error_px(p.primary.outer, s, t) == 0.0f);
	assert(top_arm_fade(p.primary, s, t) == 1.0f);
}

// 클릭 한 번 = 정확히 한 발. 연사 누적이 끼어들면 안 된다.
static void test_error_single_shot()
{
	error_state s;
	const error_tuning t;

	error_input down = frame();
	down.fire = true;
	update_error(s, t, down); // 상승 엣지
	assert(feq(s.fire_px, t.fire_per_shot_px)); // 2px
	assert(s.since_last_shot == 0.0f);

	// 한 발 간격(600rpm → 0.1초) 안에는 회복하지 않는다 — 연사가 이어질 수 있기 때문이다
	run(s, t, frame(), 5); // 5/60 = 0.083초 < 0.1초
	assert(feq(s.fire_px, t.fire_per_shot_px));

	// 그 간격을 넘기면 회복이 시작되고, 탭 한 번은 금방 사라진다
	run(s, t, frame(), 3);
	assert(s.fire_px < t.fire_per_shot_px);
	run(s, t, frame(), 60);
	assert(s.fire_px == 0.0f);

	// 클릭을 누르고 있지 않으면 추가 발사가 없다
	error_state s2;
	update_error(s2, t, down);
	run(s2, t, frame(), 600);
	assert(s2.fire_px == 0.0f);
}

// 누르고 있으면 fire_rate_rpm 으로 계속 쏜다고 가정한다(§4.4 #7 — 가정임을 명시).
// 600rpm = 10발/초 → 60fps 에서 6프레임마다 한 발.
static void test_error_sustained_fire_caps()
{
	error_state s;
	error_tuning t;
	error_input hold = frame();
	hold.fire = true;

	update_error(s, t, hold);           // 엣지 1발
	assert(feq(s.fire_px, 2.0f));
	run(s, t, hold, 5);                 // 5프레임 더 → auto_accum 0.1667×6 = 1.0 → 1발
	assert(feq(s.fire_px, 4.0f));

	// 계속 누르고 있으면 상한에서 멈춘다(무한 증가 금지)
	run(s, t, hold, 600);
	assert(feq(s.fire_px, t.fire_max_px));
	assert(s.since_last_shot == 0.0f);

	// 상한을 낮추면 그 값에서 멈춘다
	error_state s2;
	t.fire_max_px = 5.0f;
	run(s2, t, hold, 600);
	assert(feq(s2.fire_px, 5.0f));
}

// ★ 스프레이 중에 조준점이 **실제로 벌어져야** 한다.
// 설계 §4.3 의 의사코드를 글자 그대로 옮기면 회복이 확장을 앞질러 2px 에서 멈춘다
// (회복 37.3px/s × 발 간격 0.1초 = 3.73px 손실 vs 1발 2px 확장). 이 단언이 그 회귀를 잡는다.
static void test_error_spray_actually_blooms()
{
	error_state s;
	const error_tuning t;
	error_input hold = frame();
	hold.fire = true;

	float peak = 0.0f;
	for (int i = 0; i < 120; ++i) // 2초 연사
	{
		update_error(s, t, hold);
		if (s.fire_px > peak)
			peak = s.fire_px;
	}
	assert(feq(peak, t.fire_max_px));      // 상한까지 차오른다
	assert(peak > t.fire_per_shot_px * 3); // 한두 발 분량에서 멈추지 않는다

	// 단조 증가여야 한다 — 연사 중에 들쭉날쭉하면 조준점이 떨려 보인다
	error_state m;
	float prev = 0.0f;
	for (int i = 0; i < 120; ++i)
	{
		update_error(m, t, hold);
		assert(m.fire_px >= prev);
		prev = m.fire_px;
	}

	// 느리게 탭하면(회복 시간보다 긴 간격) 누적되지 않는다
	error_state tap;
	for (int k = 0; k < 5; ++k)
	{
		error_input down = frame();
		down.fire = true;
		update_error(tap, t, down);
		run(tap, t, frame(), 60); // 1초 쉬기
	}
	assert(tap.fire_px == 0.0f);
}

// 회복: 가득 찬 상태에서 recovery_time 만에 정확히 0 이 된다.
static void test_error_recovery_reaches_exactly_zero()
{
	error_state s;
	const error_tuning t;
	error_input hold = frame();
	hold.fire = true;
	run(s, t, hold, 300);
	assert(feq(s.fire_px, t.fire_max_px));

	// 회복은 '마지막 발 + 한 발 간격' 부터 시작해 recovery_time 에 걸쳐 진행된다.
	const float total = 60.0f / t.fire_rate_rpm + t.recovery_time; // 0.1 + 0.375
	const int need = static_cast<int>(total / kF60);               // 28프레임

	error_state a = s;
	run(a, t, frame(), need - 3);
	assert(a.fire_px > 0.0f);

	// 조금 더 지나면 정확히 0 — "거의 0" 이 아니라 0 이어야 한다
	error_state b = s;
	run(b, t, frame(), need + 3);
	assert(b.fire_px == 0.0f);

	// 그 뒤로 아무리 지나도 음수로 내려가지 않는다
	run(b, t, frame(), 600);
	assert(b.fire_px == 0.0f);
}

// 이동: 램프업 → deadzone → 램프다운. 멈추면 정확히 0.
static void test_error_movement_ramp_and_deadzone()
{
	error_state s;
	const error_tuning t;
	error_input w = frame();
	w.fwd = true;
	w.walk_key = true; // 기본 tuning 은 walk_key_means_run=true → Shift 누르면 달리기

	// deadzone(0.275) 아래에서는 오차가 0 이다 — 살짝 움직였다고 벌어지면 안 된다
	update_error(s, t, w); // vel = 12 × 1/60 = 0.2
	assert(feq(s.vel, 0.2f));
	assert(movement_error_px(s, t) == 0.0f);

	update_error(s, t, w); // vel = 0.4 → deadzone 위
	assert(feq(s.vel, 0.4f));
	assert(movement_error_px(s, t) > 0.0f);

	// 최고 속도에서는 run_err_px
	run(s, t, w, 60);
	assert(feq(s.vel, 1.0f));
	assert(feq(movement_error_px(s, t), t.run_err_px));

	// 손을 떼면 정확히 0 으로 떨어진다(잔류값 금지)
	run(s, t, frame(), 60);
	assert(s.vel == 0.0f);
	assert(movement_error_px(s, t) == 0.0f);

	// 걷기(수정키 안 누름)는 walk_speed 에서 멈춘다
	error_state slow;
	error_input jog = frame();
	jog.fwd = true;
	jog.walk_key = false;
	run(slow, t, jog, 60);
	assert(feq(slow.vel, t.walk_speed));

	// 수정키 의미를 뒤집으면 정반대가 된다(§4.4 #3 — 게임마다 반대다)
	error_tuning val = t;
	val.walk_key_means_run = false;
	error_state s3;
	run(s3, val, w, 60); // Shift 누름 = 발로란트식 걷기
	assert(feq(s3.vel, val.walk_speed));
}

// counter-strafe(A↔D): 속도가 **0 을 통과**해야 deadzoning 감각이 성립한다(§4.1 #5).
static void test_error_counter_strafe_passes_through_zero()
{
	error_state s;
	const error_tuning t;
	error_input a = frame();
	a.left = true;
	a.walk_key = true;
	run(s, t, a, 60);
	assert(feq(s.vel, 1.0f));

	// A 를 누른 채 D 를 같이 누르면 상쇄 → 목표 0 으로 감속
	error_input ad = frame();
	ad.left = true;
	ad.right = true;
	ad.walk_key = true;
	bool hit_zero = false;
	for (int i = 0; i < 60; ++i)
	{
		update_error(s, t, ad);
		if (s.vel == 0.0f)
			hit_zero = true;
	}
	assert(hit_zero);
	assert(movement_error_px(s, t) == 0.0f);

	// 전/후 상쇄도 마찬가지
	error_state s2;
	error_input wsd = frame();
	wsd.fwd = true;
	wsd.back = true;
	run(s2, t, wsd, 60);
	assert(s2.vel == 0.0f);
}

// 이동 오차와 발사 오차는 **덧셈**으로 합산된다(최댓값이 아니다, §4.1 #3).
static void test_error_is_additive()
{
	error_state s;
	const error_tuning t;
	error_input both = frame();
	both.fwd = true;
	both.walk_key = true;
	both.fire = true;
	run(s, t, both, 300);

	const float mv = movement_error_px(s, t);
	const float fr = s.fire_px;
	assert(mv > 0.0f && fr > 0.0f);

	line L = make_line(line_kind::outer); // 이동·발사 둘 다 ON 이 기본
	assert(L.show_movement_error && L.show_shooting_error);
	assert(feq(line_error_px(L, s, t), mv + fr));   // 덧셈
	assert(line_error_px(L, s, t) > (mv > fr ? mv : fr)); // 최댓값이 아니다

	// 배율은 각각에만 곱해진다(0..3)
	L.movement_error_scale = 2.0f;
	L.firing_error_scale = 0.5f;
	assert(feq(line_error_px(L, s, t), mv * 2.0f + fr * 0.5f));

	// 토글을 하나씩 끄면 그쪽 항만 사라진다
	L.movement_error_scale = 1.0f;
	L.firing_error_scale = 1.0f;
	L.show_movement_error = false;
	assert(feq(line_error_px(L, s, t), fr));
	L.show_movement_error = true;
	L.show_shooting_error = false;
	assert(feq(line_error_px(L, s, t), mv));
	L.show_movement_error = false;
	assert(line_error_px(L, s, t) == 0.0f); // 둘 다 끄면 **정확히** 0
}

// ★ 오차를 끈 조준점은 태스크 2 의 정적 결과와 바이트 단위로 같아야 한다.
static void test_error_off_is_byte_identical_to_static()
{
	// 격렬하게 움직이고 쏜 상태를 만든다
	error_state s;
	const error_tuning t;
	error_input busy = frame();
	busy.fwd = true;
	busy.walk_key = true;
	busy.fire = true;
	run(s, t, busy, 300);
	assert(s.vel > 0.0f && s.fire_px > 0.0f);

	// (a) 이동·발사 오차 + fade 를 전부 끈 레이어 → 언제나 정적 결과와 동일
	{
		profile p = parse_ok("0;P;f;0;0m;0;0f;0;1m;0;1f;0");
		quad_list anim, stat;
		build_crosshair_animated(p.primary, 100, 100, s, t, anim);
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, stat);
		assert(anim == stat);
	}
	// (b) 오차만 끄고 fade 는 켠 경우 → 좌표는 같고 위쪽 팔 알파만 다르다
	//     (fade 는 오차 토글과 **별개 항목**이다 — §4.1 #8)
	{
		profile p = parse_ok("0;P;h;0;0m;0;0f;0;1b;0"); // f 는 기본 ON
		assert(p.primary.fade_with_firing_error);
		quad_list anim, stat;
		build_crosshair_animated(p.primary, 100, 100, s, t, anim);
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, stat);
		assert(anim.size() == stat.size());
		for (std::size_t i = 0; i < anim.size(); ++i)
			assert(anim[i].r == stat[i].r); // 좌표는 한 픽셀도 안 움직인다
		assert(anim[3].alpha < stat[3].alpha); // 위쪽 팔(인덱스 3)만 흐려진다
		assert(anim[0].alpha == stat[0].alpha);
		assert(anim[1].alpha == stat[1].alpha);
		assert(anim[2].alpha == stat[2].alpha);
	}
	// (b2) 윤곽선을 켠 경우 — 위쪽 팔의 **링도 같이** 흐려져야 한다.
	//      본체만 흐려지면 검은 윤곽선만 남아 유령 같은 사각형이 떠 있게 된다.
	{
		profile p = parse_ok("0;P;t;2;0m;0;0f;0;1b;0"); // 윤곽선 ON, 안쪽선만
		quad_list anim, stat;
		build_crosshair_animated(p.primary, 100, 100, s, t, anim);
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, stat);
		assert(anim.size() == stat.size() && anim.size() == 4 * 5);
		// 팔 3(위쪽) = 인덱스 15..19 : 본체 1 + 링 4
		for (std::size_t i = 15; i < 20; ++i)
		{
			assert(anim[i].r == stat[i].r);
			assert(anim[i].alpha < stat[i].alpha);
		}
		// 나머지 팔은 손대지 않는다
		for (std::size_t i = 0; i < 15; ++i)
			assert(anim[i].alpha == stat[i].alpha);
	}
	// (c) 휴지 상태(아무 입력 없음)에서는 오차를 켜 둬도 정적 결과와 동일하다
	{
		error_state idle;
		run(idle, t, frame(), 600);
		const profile p; // 전 기본값 = 오차 토글 켜져 있음
		quad_list anim, stat;
		build_crosshair_animated(p.primary, 100, 100, idle, t, anim);
		build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, stat);
		assert(anim == stat);
	}
}

// 일시정지(오버레이 열림 · 핫키): 오차가 자라지 않고, 풀 때 없던 발이 생기지 않는다.
static void test_error_pause()
{
	error_state s;
	const error_tuning t;

	// 일시정지 중에는 WASD 도 클릭도 무시된다
	error_input busy = frame();
	busy.fwd = true;
	busy.fire = true;
	busy.paused = true;
	run(s, t, busy, 120);
	assert(s.vel == 0.0f);
	assert(s.fire_px == 0.0f);

	// ★ 클릭을 누른 채로 일시정지를 풀어도 유령 발사가 없다 —
	//   엣지는 일시정지 중에도 갱신되기 때문이다.
	error_input unpaused = busy;
	unpaused.paused = false;
	unpaused.fwd = false;
	update_error(s, t, unpaused);
	assert(s.fire_px == 0.0f);

	// 뗐다 다시 누르면 정상적으로 한 발
	error_input up = frame();
	update_error(s, t, up);
	error_input down = frame();
	down.fire = true;
	update_error(s, t, down);
	assert(feq(s.fire_px, t.fire_per_shot_px));
}

// dt 가 통째로 튀어도(알트탭·로딩·디버거) 죽거나 멈추지 않는다.
static void test_error_dt_spike_guard()
{
	error_state s;
	const error_tuning t;

	error_input spike = frame(1000.0f); // 1000초짜리 프레임
	spike.fire = true;
	update_error(s, t, spike);          // 연사 루프가 폭주하면 여기서 멈춘다
	assert(s.fire_px <= t.fire_max_px);
	assert(s.fire_px >= 0.0f);
	// ★ 알트탭 한 번에 조준점이 상한까지 튀면 안 된다 — dt 상한이 그걸 막는다.
	//   가드가 없으면 1초짜리 프레임에 10발이 한꺼번에 들어가 곧장 최대가 된다.
	assert(s.fire_px < t.fire_max_px);
	assert(feq(s.fire_px, t.fire_per_shot_px * 3.0f)); // dt 0.25초 → 엣지 1발 + 자동 2발

	// 음수·NaN dt 도 안전하다
	error_state s2;
	error_input neg = frame(-5.0f);
	neg.fwd = true;
	run(s2, t, neg, 10);
	assert(s2.vel == 0.0f);

	error_input nan_dt = frame(0.0f / 0.0f);
	nan_dt.fwd = true;
	update_error(s2, t, nan_dt);
	assert(s2.vel >= 0.0f && s2.vel <= 1.0f);

	// 말도 안 되는 연사 속도
	error_tuning crazy = t;
	crazy.fire_rate_rpm = 1.0e9f;
	error_state s3;
	error_input hold = frame();
	hold.fire = true;
	run(s3, crazy, hold, 10);
	assert(s3.fire_px <= crazy.fire_max_px);
}

// §4.1 #8 · §6 #8 — f 는 위쪽 팔의 알파다.
static void test_top_arm_fade()
{
	error_state s;
	const error_tuning t;
	profile p;
	assert(p.primary.fade_with_firing_error); // 기본 ON

	assert(top_arm_fade(p.primary, s, t) == 1.0f); // 안 쏘면 아무 일 없음

	error_input hold = frame();
	hold.fire = true;
	run(s, t, hold, 300);
	assert(feq(s.fire_px, t.fire_max_px));
	// 가득 찼을 때 = 1 − fade_depth
	assert(kFadeMode == fade_mode::alpha);
	assert(feq(top_arm_fade(p.primary, s, t), 1.0f - t.fade_depth));

	// 중간값에서는 연속적으로 변한다(이진이 아니다)
	error_state half;
	half.fire_px = t.fire_max_px * 0.5f;
	const float f = top_arm_fade(p.primary, half, t);
	assert(f > 1.0f - t.fade_depth && f < 1.0f);

	// 끄면 언제나 1
	p.primary.fade_with_firing_error = false;
	assert(top_arm_fade(p.primary, s, t) == 1.0f);

	// §6 #8 — 바깥선에도 걸리는지는 상수 하나로 뒤집는다
	assert(kFadeAppliesToOuter);
	{
		profile q = parse_ok("0;P;h;0");
		quad_list out;
		build_crosshair(q.primary, 100, 100, 0.0f, 0.0f, out, 0.5f);
		assert(out.size() == 8);           // 안쪽 4 + 바깥 4 (윤곽선 없음)
		assert(out[3].alpha < out[0].alpha); // 안쪽 위쪽 팔
		assert(out[7].alpha < out[4].alpha); // 바깥 위쪽 팔도 같이
	}
}

// 오차가 실제 좌표로 이어진다 — **오프셋만** 늘고 길이·두께는 고정이다(§4.1 #2).
static void test_animated_geometry()
{
	error_state s;
	const error_tuning t;
	error_input hold = frame();
	hold.fire = true;
	run(s, t, hold, 300); // fire_px = 14

	const profile p = parse_ok("0;P;h;0;0o;0;1b;0"); // 안쪽만, 오프셋 0, 발사오차 기본 ON
	quad_list q;
	build_crosshair_animated(p.primary, 100, 100, s, t, q);

	// 휴지 오프셋 0+4 = 4, 거기에 발사 오차 14 → 18
	assert(q[0].r.x == 100 + 4 + 14);
	// 길이·두께는 그대로다
	quad_list stat;
	build_crosshair(p.primary, 100, 100, 0.0f, 0.0f, stat);
	assert(q[0].r.w == stat[0].r.w && q[0].r.h == stat[0].r.h);

	// 배율 0 이면 확장이 없다
	profile z = parse_ok("0;P;h;0;0o;0;0e;0;1b;0");
	quad_list qz;
	build_crosshair_animated(z.primary, 100, 100, s, t, qz);
	assert(qz[0].r.x == 100 + 4); // +4(min error)는 배율과 무관하다(§3.4)
}

int main()
{
	test_defaults();
	test_presets();
	test_example_e_all_defaults();
	test_example_a();
	test_example_b();
	test_example_c();
	test_example_d();
	test_key_meaning_differs_per_section();
	test_line_prefix_is_part_of_the_key();
	test_explicit_defaults_collapse_to_zero();
	test_single_field_emits_single_key();
	test_collapse_rules();
	test_vertical_length_is_not_gated_by_g();
	test_custom_color_triple();
	test_rejections();
	test_lenient_paths();
	test_unknown_section_markers();
	test_float_format();
	test_helpers();
	test_round_trip_sweep();
	test_round_trip_from_codes();
	test_arm_geometry_worked_example();
	test_arm_geometry_even_thickness();
	test_offset_is_inner_edge_and_length_grows_outward();
	test_zero_length_arms_are_dropped();
	test_outline_ring_geometry();
	test_body_and_ring_alpha_are_independent();
	test_outline_toggle();
	test_center_dot_is_a_square();
	test_draw_order();
	test_min_error_offset_in_geometry();
	test_range_extremes();
	test_show_lines_gate();
	test_translation_invariance();
	test_geometry_uses_resolved_color();
	test_effective_ads();
	test_geometry_sweep();
	test_exceeds_ui_range();
	test_classic_migration();
	test_error_idle_is_exactly_zero();
	test_error_single_shot();
	test_error_sustained_fire_caps();
	test_error_spray_actually_blooms();
	test_error_recovery_reaches_exactly_zero();
	test_error_movement_ramp_and_deadzone();
	test_error_counter_strafe_passes_through_zero();
	test_error_is_additive();
	test_error_off_is_byte_identical_to_static();
	test_error_pause();
	test_error_dt_spike_guard();
	test_top_arm_fade();
	test_animated_geometry();
	std::printf("sherbet_crosshair: ALL PASS\n");
	return 0;
}
