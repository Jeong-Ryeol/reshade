/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 멀티키 단축키 — 순수 로직(플랫폼 비의존). tools/sherbet_hotkey_test.cpp 가 검증한다.
//
// 리쉐이드 원본 단축키(unsigned int[4] = 키 1개 + Ctrl/Shift/Alt 여부)는 "Z+X" 나
// "마우스 옆버튼+F" 를 표현하지 못한다. 그래서 Sherbet 단축키는 **아무 키나 최대 4개**를
// 담는 조합(chord)이다. 수정키(Ctrl/Shift/Alt)도 그냥 그 4개 중 하나로 들어간다.
//
// ── 발동 규칙 ────────────────────────────────────────────────────────────
//   1. 조합의 키가 전부 눌려 있고
//   2. 그중 하나라도 **이번 프레임에 새로** 눌렸고 (꾹 누르고 있어도 한 번만 발동)
//   3. 조합에 없는 수정키(Ctrl/Shift/Alt)는 눌려 있지 않을 때
// 3번이 없으면 "F" 단축키가 Ctrl+F 를 누를 때도 같이 발동한다.
// 여러 단축키가 같은 프레임에 걸리면 **키가 가장 많은 것** 하나만 고른다(best_match) —
// "F" 와 "G+F" 를 둘 다 등록했을 때 G+F 를 누르면 G+F 만 실행돼야 한다.
//
// ── 입력 받기(capture) ───────────────────────────────────────────────────
// 칸을 누른 뒤 키를 누르고 **전부 떼는 순간** 확정한다. 동시에 눌린 키가 가장 많았던
// 순간의 조합이 남는다 — Ctrl 을 먼저 떼고 F 를 나중에 떼도 Ctrl+F 가 된다.
// Backspace = 지우기, Escape = 취소. 마우스 왼쪽·오른쪽은 받지 않는다(칸을 누르는 것
// 자체가 왼쪽 클릭이라, 받으면 모든 단축키가 "왼쪽 마우스" 로 시작해 버린다).
#pragma once

#include <string>

namespace sherbet
{
	namespace hotkey
	{
		constexpr int kMaxKeys = 4;

		constexpr unsigned int kShift = 0x10, kCtrl = 0x11, kAlt = 0x12;
		constexpr unsigned int kBackspace = 0x08, kEscape = 0x1B;
		constexpr unsigned int kMouseLeft = 0x01, kMouseRight = 0x02;

		// 0 = 빈칸. 항상 정렬돼 있다: Ctrl, Shift, Alt 가 먼저(이 순서), 나머지는 키코드 오름차순.
		struct chord
		{
			unsigned int k[kMaxKeys] = {};
		};

		inline bool is_modifier(unsigned int vk) { return vk == kShift || vk == kCtrl || vk == kAlt; }

		// 왼쪽/오른쪽 수정키를 한 가지로 — 입력 경로에 따라 둘 중 하나만 들어올 수 있어서다.
		inline unsigned int normalize(unsigned int vk)
		{
			switch (vk)
			{
			case 0xA0: case 0xA1: return kShift;
			case 0xA2: case 0xA3: return kCtrl;
			case 0xA4: case 0xA5: return kAlt;
			default: return vk;
			}
		}

		// 단축키로 받을 수 있는 키인가.
		inline bool capturable(unsigned int vk)
		{
			vk = normalize(vk);
			return vk > 0 && vk < 0xFF && vk != kMouseLeft && vk != kMouseRight && vk != kBackspace && vk != kEscape;
		}

		inline int count(const chord &c)
		{
			int n = 0;
			for (unsigned int v : c.k)
				if (v != 0)
					++n;
			return n;
		}
		inline bool empty(const chord &c) { return count(c) == 0; }
		inline bool contains(const chord &c, unsigned int vk)
		{
			vk = normalize(vk);
			for (unsigned int v : c.k)
				if (v != 0 && v == vk)
					return true;
			return false;
		}
		inline bool equal(const chord &a, const chord &b)
		{
			for (int i = 0; i < kMaxKeys; ++i)
				if (a.k[i] != b.k[i])
					return false;
			return true;
		}

		// 정렬 순위: Ctrl 0, Shift 1, Alt 2, 나머지 = 3 + 키코드.
		inline unsigned int order_of(unsigned int vk)
		{
			return vk == kCtrl ? 0u : vk == kShift ? 1u : vk == kAlt ? 2u : 3u + vk;
		}

		// 하나 넣는다(정규화·중복 제거·정렬 유지). 꽉 찼거나 못 받는 키면 false.
		inline bool add(chord &c, unsigned int vk)
		{
			vk = normalize(vk);
			if (!capturable(vk) || contains(c, vk))
				return false;
			const int n = count(c);
			if (n >= kMaxKeys)
				return false;
			int at = n;
			while (at > 0 && order_of(c.k[at - 1]) > order_of(vk))
			{
				c.k[at] = c.k[at - 1];
				--at;
			}
			c.k[at] = vk;
			return true;
		}

		// ini 에서 읽은 값처럼 믿을 수 없는 배열을 정상 조합으로 만든다.
		inline chord sanitize(const unsigned int (&raw)[kMaxKeys])
		{
			chord c;
			for (unsigned int v : raw)
				add(c, v);
			return c;
		}

		// 발동 판정. down(vk) = 지금 눌려 있나, pressed(vk) = 이번 프레임에 새로 눌렸나.
		// 수정키는 호출부가 왼쪽/오른쪽까지 포함해 판정해 넘긴다.
		template <typename Down, typename Pressed>
		inline bool triggered(const chord &c, Down down, Pressed pressed)
		{
			if (empty(c))
				return false;
			bool any_new = false;
			for (unsigned int v : c.k)
			{
				if (v == 0)
					continue;
				if (!down(v))
					return false;
				if (pressed(v))
					any_new = true;
			}
			if (!any_new)
				return false;
			for (unsigned int m : { kCtrl, kShift, kAlt })
				if (!contains(c, m) && down(m))
					return false;
			return true;
		}

		// 여러 단축키 중 이번 프레임에 발동할 하나. get(i) 는 i 번째 조합. 없으면 -1.
		template <typename Get, typename Down, typename Pressed>
		inline int best_match(int n, Get get, Down down, Pressed pressed)
		{
			int best = -1, best_keys = 0;
			for (int i = 0; i < n; ++i)
			{
				const chord &c = get(i);
				if (!triggered(c, down, pressed))
					continue;
				const int keys = count(c);
				if (keys > best_keys)
				{
					best = i;
					best_keys = keys;
				}
			}
			return best;
		}

		// ── 입력 받기 ─────────────────────────────────────────────────────
		struct capture
		{
			chord best;       // 동시에 가장 많이 눌렸던 순간
			bool any = false; // 뭔가 한 번이라도 눌렸나
		};

		enum class step_result { waiting, done, cleared, cancelled };

		// 매 프레임 부른다. down_keys = 지금 눌린 키 목록(아무 순서, 중복·못 받는 키 섞여도 됨).
		// done 이면 out 에 결과가 들어가고 st 는 초기화된다.
		inline step_result step(capture &st, const unsigned int *down_keys, int n, bool backspace_pressed, bool escape_pressed, chord &out)
		{
			if (escape_pressed)
			{
				st = capture();
				return step_result::cancelled;
			}
			if (backspace_pressed && !st.any)
			{
				out = chord();
				return step_result::cleared;
			}
			chord now;
			for (int i = 0; i < n; ++i)
				add(now, down_keys[i]);
			const int held = count(now);
			if (held > 0)
			{
				st.any = true;
				if (held >= count(st.best))
					st.best = now;
				return step_result::waiting;
			}
			if (st.any)
			{
				out = st.best;
				st = capture();
				return step_result::done;
			}
			return step_result::waiting;
		}

		// "Ctrl + Shift + F". 수정키는 짧게, 나머지는 name_of(vk) 로.
		template <typename NameOf>
		inline std::string name(const chord &c, NameOf name_of)
		{
			std::string s;
			for (unsigned int v : c.k)
			{
				if (v == 0)
					continue;
				if (!s.empty())
					s += " + ";
				if (v == kCtrl) s += "Ctrl";
				else if (v == kShift) s += "Shift";
				else if (v == kAlt) s += "Alt";
				else
				{
					const std::string n = name_of(v);
					if (!n.empty())
						s += n;
					else
					{
						char buf[16];
						static const char hex[] = "0123456789ABCDEF";
						buf[0] = 'K'; buf[1] = 'e'; buf[2] = 'y'; buf[3] = ' '; buf[4] = '0'; buf[5] = 'x';
						buf[6] = hex[(v >> 4) & 0xF]; buf[7] = hex[v & 0xF]; buf[8] = '\0';
						s += buf;
					}
				}
			}
			return s;
		}
	}
}
