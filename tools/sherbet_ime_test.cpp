/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// sherbet_ime.hpp 호스트 테스트 — 실제 플레이 순서(T → 한글로 채팅 → Enter → 무빙)대로 흘려 본다.
#include "sherbet_ime.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::ime;

static action down(state &st, unsigned int vk, bool korean_on_chat = true, bool blocking = false, bool requested = false, bool enabled = true)
{
	return on_key_down(st, enabled, korean_on_chat, 'T', vk, blocking, requested);
}
static action up(state &st, unsigned int vk, bool blocking = false, bool enabled = true)
{
	return on_key_up(st, enabled, vk, blocking);
}

// T 로 채팅을 열면 한글, Enter 를 떼면 영문. 무빙 키는 건드리지 않는다.
static void test_chat_cycle()
{
	state st;
	assert(down(st, 'W') == action::none && up(st, 'W') == action::none);
	assert(down(st, 'T') == action::to_korean);
	assert(up(st, 'T') == action::none);
	assert(down(st, 'A') == action::none && up(st, 'A') == action::none); // 채팅 중 ㅁ
	assert(down(st, 'T') == action::none);                                // 채팅 중 ㅅ
	assert(down(st, kVkReturn) == action::none);                          // 누를 땐 아직 안 바꾼다
	assert(up(st, kVkReturn) == action::to_english);
	assert(down(st, 'D') == action::none && up(st, 'D') == action::none);
	assert(!st.chat_open);
}

// 빠른 입력: T 를 떼기 전에 다음 글자를 눌러도 이미 한글이다(첫 글자가 영문으로 안 들어간다).
static void test_fast_typing_after_t()
{
	state st;
	assert(down(st, 'T') == action::to_korean); // 누르는 순간
	assert(down(st, 'D') == action::none);      // T 를 떼기 전 다음 글자
	assert(up(st, 'T') == action::none);
	assert(st.chat_open);
}

// Enter→T 연타: Enter 를 떼기 전에 새 채팅을 열면 영문으로 되돌리지 않는다.
static void test_enter_then_t_overlap()
{
	state st;
	assert(down(st, 'T') == action::to_korean);
	assert(down(st, kVkReturn) == action::none);
	assert(down(st, 'T') == action::to_korean); // 새 채팅
	assert(up(st, kVkReturn) == action::none);  // 영문 전환 취소됨
	assert(up(st, 'T') == action::none);
	assert(st.chat_open);
	assert(down(st, kVkReturn) == action::none);
	assert(up(st, kVkReturn) == action::to_english);
}

// Esc 로 채팅을 닫아도 영문.
static void test_escape_closes()
{
	state st;
	assert(down(st, 'T') == action::to_korean);
	assert(down(st, kVkEscape) == action::none);
	assert(up(st, kVkEscape) == action::to_english);
	assert(down(st, 'T') == action::to_korean); // 다음 채팅도 다시 한글
}

// 같은 이벤트가 두 번 와도(raw + WM_KEY*) 결과가 같다.
static void test_duplicate_events()
{
	state st;
	assert(down(st, 'T') == action::to_korean);
	assert(down(st, 'T') == action::none);
	assert(down(st, kVkReturn) == action::none);
	assert(down(st, kVkReturn) == action::none);
	assert(up(st, kVkReturn) == action::to_english);
	assert(up(st, kVkReturn) == action::none);
	assert(!st.chat_open && !st.english_on_release);
}

// 채팅 키를 꾹 누르고 있어도(키 반복) 한 번만.
static void test_key_repeat()
{
	state st;
	assert(down(st, 'T') == action::to_korean);
	for (int i = 0; i < 20; ++i)
		assert(down(st, 'T') == action::none);
}

// '채팅 열 때 한글' 을 끄면 T 는 아무것도 안 하고, Enter·Esc 영문 전환만 남는다.
static void test_korean_on_chat_off()
{
	state st;
	assert(down(st, 'T', false) == action::none);
	assert(st.chat_open);
	assert(down(st, kVkReturn, false) == action::none);
	assert(up(st, kVkReturn) == action::to_english);
}

// 꺼져 있거나, 셰르벳 메뉴가 키보드를 잡고 있으면(우리 입력칸에 한글 입력 중) 아무것도 안 한다.
static void test_disabled_and_blocking()
{
	state st;
	assert(down(st, 'T', true, false, false, false) == action::none);
	assert(down(st, kVkReturn, true, false, false, false) == action::none);
	assert(up(st, kVkReturn, false, false) == action::none);
	assert(down(st, 'T', true, true) == action::none);
	assert(down(st, kVkReturn, true, true) == action::none);
	assert(up(st, kVkReturn, true) == action::none);
	assert(!st.chat_open && !st.english_on_release);
}

// 메뉴를 닫으며 요청한 영문 전환은 다음 키 누름에서 일어난다. 그 키가 채팅 키면 채팅이 열리니 한글.
static void test_requested_english()
{
	state st;
	assert(down(st, 'T') == action::to_korean);
	assert(down(st, 'W', true, false, true) == action::to_english);
	assert(!st.chat_open);
	state st2;
	assert(down(st2, 'T', true, false, true) == action::to_korean);
	state st3;
	assert(down(st3, 'T', false, false, true) == action::to_english); // 채팅 한글 끔
	state st4;
	assert(down(st4, kVkReturn, true, false, true) == action::to_english);
}

// 채팅 키를 0(없음)으로 두면 T 를 눌러도 반응하지 않는다.
static void test_no_chat_key()
{
	state st;
	assert(on_key_down(st, true, true, 0, 'T', false, false) == action::none);
	assert(on_key_down(st, true, true, 0, kVkReturn, false, false) == action::none);
	assert(on_key_up(st, true, kVkReturn, false) == action::to_english);
}

// 무빙·자주 누르는 키에서는 입력기를 읽지 않는다(호출 자체가 빠른 입력을 붙잡지 않게).
static void test_reads_ime_only_rare_keys()
{
	const unsigned int busy[] = { 'W', 'A', 'S', 'D', ' ', 0x10 /*Shift*/, 0x11 /*Ctrl*/, 'E', 'F', 'R', 'Q', 'C', 'X', 'G' };
	for (unsigned int vk : busy)
		assert(!reads_ime(vk, 'T'));
	assert(reads_ime('T', 'T'));
	assert(reads_ime(kVkReturn, 'T') && reads_ime(kVkEscape, 'T') && reads_ime(kVkHangul, 'T'));
	assert(!reads_ime('T', 0));
}

// 채팅 키 2(기본 F7): 어느 키로 열어도 한글, Enter 면 영문. 0 이면 그 키는 아무 일도 안 한다.
static void test_second_chat_key()
{
	const unsigned int f7 = kDefaultChatKey2;
	state st;
	assert(on_key_down(st, true, true, 'T', f7, false, false, f7) == action::to_korean);
	assert(up(st, f7) == action::none);
	assert(on_key_down(st, true, true, 'T', 'T', false, false, f7) == action::none); // 이미 열림(채팅 중 ㅅ)
	assert(on_key_down(st, true, true, 'T', kVkReturn, false, false, f7) == action::none);
	assert(up(st, kVkReturn) == action::to_english);
	assert(on_key_down(st, true, true, 'T', 'T', false, false, f7) == action::to_korean); // T 로도 여전히
	state off;
	assert(on_key_down(off, true, true, 'T', f7, false, false, 0) == action::none);    // 키 2 안 씀
	assert(reads_ime(f7, 'T', f7) && !reads_ime(f7, 'T', 0) && !reads_ime('W', 'T', f7));
}

int main()
{
	test_chat_cycle();
	test_fast_typing_after_t();
	test_enter_then_t_overlap();
	test_escape_closes();
	test_duplicate_events();
	test_key_repeat();
	test_korean_on_chat_off();
	test_disabled_and_blocking();
	test_requested_english();
	test_no_chat_key();
	test_reads_ime_only_rare_keys();
	test_second_chat_key();
	std::printf("sherbet_ime_test: 12 passed\n");
	return 0;
}
