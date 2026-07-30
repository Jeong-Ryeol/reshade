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
		assert(generate_code(p) == "0;P;c;12;t;9;d;0;0l;25;0o;40;1l;18;1o;60" ||
		       generate_code(p) == "0;P;c;12;t;9;0l;25;0o;40;1l;18;1o;60");
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
	std::printf("sherbet_crosshair: ALL PASS\n");
	return 0;
}
