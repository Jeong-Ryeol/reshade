/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_hotkey.hpp + sherbet_magpreset.hpp 호스트 단위테스트.
// ⚠️ -DNDEBUG 금지 — 전부 assert 로 검증한다.
//
// 검사하는 것은 **사용자가 겪는 일**이다: Ctrl+F 를 누르면 "F" 단축키가 같이 터지지 않는다,
// 꾹 누르고 있어도 한 번만 켜진다, Ctrl 을 먼저 떼도 Ctrl+F 로 저장된다, "G+F" 와 "F" 가
// 둘 다 있으면 G+F 만 실행된다, 같은 프리셋 키를 두 번 누르면 꺼진다.

#include "sherbet_magpreset.hpp"
#include "sherbet_crosshair.hpp"

#include <cassert>
#include <cstdio>
#include <set>
#include <string>

using namespace sherbet;
using namespace sherbet::hotkey;

// 가짜 키보드: 지금 눌린 키 + 이번 프레임에 새로 눌린 키.
struct kb
{
	std::set<unsigned int> down, fresh;
	void press(unsigned int vk) { down.insert(vk); fresh.insert(vk); }
	void next_frame() { fresh.clear(); }
	void release(unsigned int vk) { down.erase(vk); fresh.erase(vk); }
	bool is_down(unsigned int vk) const { return down.count(vk) != 0; }
	bool is_pressed(unsigned int vk) const { return fresh.count(vk) != 0; }
};

static bool fire(const chord &c, const kb &k)
{
	return triggered(c, [&](unsigned int v) { return k.is_down(v); }, [&](unsigned int v) { return k.is_pressed(v); });
}

static chord make(std::initializer_list<unsigned int> keys)
{
	chord c;
	for (unsigned int v : keys)
		add(c, v);
	return c;
}

static const unsigned int F = 'F', G = 'G', Z = 'Z', X = 'X', ONE = '1';
static const unsigned int MOUSE_MID = 0x04, MOUSE_X1 = 0x05;

static void test_add_sorts_and_dedupes()
{
	chord c;
	assert(add(c, F));
	assert(add(c, kShift));
	assert(add(c, kCtrl));
	assert(!add(c, F));             // 중복
	assert(!add(c, 0xA2));          // 왼쪽 Ctrl = Ctrl, 이미 있음
	assert(c.k[0] == kCtrl && c.k[1] == kShift && c.k[2] == F && c.k[3] == 0);
	assert(add(c, kAlt));
	assert(c.k[0] == kCtrl && c.k[1] == kShift && c.k[2] == kAlt && c.k[3] == F);
	assert(!add(c, G));             // 4개 꽉 참
	assert(count(c) == 4);

	// 마우스 왼쪽·오른쪽, Backspace, Escape 는 못 받는다. 휠클릭·옆버튼은 받는다.
	chord m;
	assert(!add(m, kMouseLeft) && !add(m, kMouseRight) && !add(m, kBackspace) && !add(m, kEscape) && !add(m, 0));
	assert(add(m, MOUSE_X1) && add(m, MOUSE_MID));
	assert(m.k[0] == MOUSE_MID && m.k[1] == MOUSE_X1);
}

static void test_sanitize_from_ini()
{
	// 손으로 고친 ini: 순서 뒤죽박죽, 왼쪽/오른쪽 수정키, 못 받는 키, 중복.
	const unsigned int raw[kMaxKeys] = { F, 0xA3 /* 오른쪽 Ctrl */, kMouseLeft, F };
	const chord c = sanitize(raw);
	assert(count(c) == 2 && c.k[0] == kCtrl && c.k[1] == F);
	const unsigned int zero[kMaxKeys] = {};
	assert(empty(sanitize(zero)));
}

static void test_single_key_fires_once()
{
	const chord c = make({ F });
	kb k;
	assert(!fire(c, k));
	k.press(F);
	assert(fire(c, k));             // 누른 프레임
	k.next_frame();
	assert(!fire(c, k));            // 꾹 누르고 있어도 다시 안 터진다
	k.release(F); k.next_frame();
	k.press(F);
	assert(fire(c, k));             // 다시 누르면 또 터진다
}

static void test_extra_modifier_blocks()
{
	// ★ "F" 단축키가 Ctrl+F 에 같이 터지면 안 된다(다른 프로그램 단축키·게임 키와 겹친다).
	const chord f = make({ F });
	kb k;
	k.press(kCtrl); k.next_frame();
	k.press(F);
	assert(!fire(f, k));
	// 반대로 Ctrl+F 단축키는 이때 터진다.
	assert(fire(make({ kCtrl, F }), k));
	// Ctrl+Shift+F 를 눌렀는데 Ctrl+F 단축키가 터지면 안 된다.
	kb k2;
	k2.press(kCtrl); k2.press(kShift); k2.next_frame();
	k2.press(F);
	assert(!fire(make({ kCtrl, F }), k2));
	assert(fire(make({ kCtrl, kShift, F }), k2));
}

static void test_modifier_order_does_not_matter()
{
	// F 를 먼저 누르고 Ctrl 을 나중에 눌러도 Ctrl+F 는 터진다(새로 눌린 게 Ctrl).
	const chord c = make({ kCtrl, F });
	kb k;
	k.press(F); k.next_frame();
	assert(!fire(c, k));
	k.press(kCtrl);
	assert(fire(c, k));
}

static void test_non_modifier_combo()
{
	// 수정키 없는 조합도 된다: Z+X, 마우스 옆버튼+1.
	const chord zx = make({ Z, X });
	kb k;
	k.press(Z); k.next_frame();
	assert(!fire(zx, k));           // Z 만으로는 안 된다
	k.press(X);
	assert(fire(zx, k));
	const chord mx = make({ MOUSE_X1, ONE });
	kb m;
	m.press(MOUSE_X1); m.next_frame(); m.press(ONE);
	assert(fire(mx, m));
}

static void test_best_match_prefers_longer()
{
	// ★ "F" 와 "G+F" 가 둘 다 있을 때 G 를 잡고 F 를 누르면 G+F 만 실행돼야 한다.
	const chord list[] = { make({ F }), make({ G, F }), chord() };
	auto get = [&](int i) -> const chord & { return list[i]; };
	kb k;
	k.press(G); k.next_frame(); k.press(F);
	auto down = [&](unsigned int v) { return k.is_down(v); };
	auto pressed = [&](unsigned int v) { return k.is_pressed(v); };
	assert(best_match(3, get, down, pressed) == 1);
	// F 만 누르면 F.
	kb k2; k2.press(F);
	assert(best_match(3, get, [&](unsigned int v) { return k2.is_down(v); }, [&](unsigned int v) { return k2.is_pressed(v); }) == 0);
	// 아무것도 아니면 -1. 빈 조합은 절대 안 터진다.
	kb k3;
	assert(best_match(3, get, [&](unsigned int v) { return k3.is_down(v); }, [&](unsigned int v) { return k3.is_pressed(v); }) == -1);
}

static void test_capture_records_max_simultaneous()
{
	// Ctrl 누름 → F 누름 → Ctrl 먼저 뗌 → F 뗌 ⇒ Ctrl+F (떼는 순서와 무관).
	capture st;
	chord out;
	unsigned int d1[] = { kCtrl };
	assert(step(st, d1, 1, false, false, out) == step_result::waiting);
	unsigned int d2[] = { F, kCtrl };
	assert(step(st, d2, 2, false, false, out) == step_result::waiting);
	unsigned int d3[] = { F };
	assert(step(st, d3, 1, false, false, out) == step_result::waiting);
	assert(step(st, nullptr, 0, false, false, out) == step_result::done);
	assert(equal(out, make({ kCtrl, F })));
	assert(!st.any); // 다음 입력을 위해 초기화됐다

	// 왼쪽 마우스(칸을 누른 클릭)가 섞여도 무시된다.
	capture st2; chord out2;
	unsigned int d4[] = { kMouseLeft, Z, X };
	step(st2, d4, 3, false, false, out2);
	assert(step(st2, nullptr, 0, false, false, out2) == step_result::done);
	assert(equal(out2, make({ Z, X })));
}

static void test_capture_clear_and_cancel()
{
	capture st;
	chord out = make({ F });
	// 아무것도 안 누른 채 Backspace → 지우기.
	assert(step(st, nullptr, 0, true, false, out) == step_result::cleared);
	assert(empty(out));
	// 누르던 중 Escape → 취소, out 은 건드리지 않는다.
	chord keep = make({ G });
	unsigned int d[] = { F };
	step(st, d, 1, false, false, keep);
	assert(step(st, d, 1, false, true, keep) == step_result::cancelled);
	assert(equal(keep, make({ G })));
	// 아무것도 안 누르면 계속 기다린다(칸을 누르자마자 빈 값으로 확정되면 안 된다).
	capture st3; chord o3 = make({ F });
	assert(step(st3, nullptr, 0, false, false, o3) == step_result::waiting);
	assert(equal(o3, make({ F })));
}

static void test_name()
{
	auto nm = [](unsigned int v) -> std::string { return v == F ? "F" : v == MOUSE_X1 ? "X1 Mouse" : ""; };
	assert(name(make({ F, kShift, kCtrl }), nm) == "Ctrl + Shift + F");
	assert(name(make({ MOUSE_X1, F }), nm) == "X1 Mouse + F");
	assert(name(chord(), nm).empty());
	assert(name(make({ 0x97 }), nm) == "Key 0x97"); // 이름 없는 키도 빈칸으로 안 나온다
}

static void test_preset_hotkey_toggles()
{
	using mag::on_hotkey;
	using mag::preset_action;
	// 꺼져 있으면 켠다, 다른 프리셋이 켜져 있으면 바꾼다, 같은 프리셋이 켜져 있으면 끈다.
	assert(on_hotkey(2, -1, false) == preset_action::apply);
	assert(on_hotkey(2, 1, true) == preset_action::apply);
	assert(on_hotkey(2, 2, true) == preset_action::turn_off);
	assert(on_hotkey(2, 2, false) == preset_action::apply); // 그 프리셋이었지만 꺼져 있으면 다시 켠다
}

static void test_preset_name_clean()
{
	const std::string fb = mag::default_preset_name(3);
	assert(fb == "\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B 3");
	// ★ 쉼표는 ini 배열 구분자라 그대로 두면 다음 실행에 뒤가 잘린다.
	assert(mag::clean_name("\xEC\xB2\xB4\xEB\xA0\xA5, \xEB\xB0\xA9\xEC\x96\xB4\xEA\xB5\xAC", fb) == "\xEC\xB2\xB4\xEB\xA0\xA5  \xEB\xB0\xA9\xEC\x96\xB4\xEA\xB5\xAC");
	assert(mag::clean_name("   ", fb) == fb);
	assert(mag::clean_name("", fb) == fb);
	assert(mag::clean_name("  a\nb  ", fb) == "a b");
	// 길면 UTF-8 경계에서 자른다 — 한글(3바이트)이 반쪽 나면 화면에 깨진 글자가 찍힌다.
	std::string longname;
	for (int i = 0; i < 30; ++i)
		longname += "\xEA\xB0\x80"; // "가" × 30 = 90 바이트
	const std::string cut = mag::clean_name(longname, fb);
	assert(cut.size() <= mag::kMaxPresetName && cut.size() % 3 == 0);
}

static void test_crosshair_toggle_remembers_mode()
{
	using crosshair::toggle_mode;
	// 발로란트를 쓰다 끄고 다시 켜면 발로란트. 클래식도 마찬가지.
	auto t = toggle_mode(2, 1);
	assert(t.mode == 0 && t.last == 2);
	t = toggle_mode(t.mode, t.last);
	assert(t.mode == 2 && t.last == 2);
	t = toggle_mode(1, 2);
	assert(t.mode == 0 && t.last == 1);
	t = toggle_mode(t.mode, t.last);
	assert(t.mode == 1);
	// 처음부터 꺼져 있고 기억도 이상하면(첫 실행·손으로 고친 ini) 발로란트로 켠다.
	assert(toggle_mode(0, 0).mode == 2);
	assert(toggle_mode(0, 7).mode == 2);
	assert(toggle_mode(0, -1).last == 2);
}

int main()
{
	test_crosshair_toggle_remembers_mode();
	test_add_sorts_and_dedupes();
	test_sanitize_from_ini();
	test_single_key_fires_once();
	test_extra_modifier_blocks();
	test_modifier_order_does_not_matter();
	test_non_modifier_combo();
	test_best_match_prefers_longer();
	test_capture_records_max_simultaneous();
	test_capture_clear_and_cancel();
	test_name();
	test_preset_hotkey_toggles();
	test_preset_name_clean();
	std::printf("sherbet_hotkey_test: ALL PASS\n");
	return 0;
}
