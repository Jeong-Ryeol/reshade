/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 한글 키밀림 방지 — 순수 로직(플랫폼 비의존).
// tools/sherbet_ime_test.cpp 가 이 헤더를 그대로 컴파일해 검증한다.
//
// 증상: 입력기가 한글 상태면 W/A/S/D 가 ㅈ/ㅁ/ㄴ/ㅇ 조합을 거쳐 게임에 들어가 무빙이 밀린다.
//       영문 상태면 멀쩡하다(구매자 실측). FiveM 은 채팅 한글 때문에 게임 창의 입력기를 켜 둔다.
// 대응: 평소엔 영문, 채팅 키(보통 T)를 누르면 한글, Enter·Esc 로 채팅을 끝내면 다시 영문.
//       한글을 아예 막으면 채팅에서 한글을 못 친다 — 그래서 막지 않고 **때맞춰 바꾼다.**
//
// ⚠️ 채팅 키는 **누를 때** 한글로 바꾼다. 뗄 때 바꾸면 T 를 떼기 전에 다음 글자를 누르는
//    빠른 입력(래피드 트리거)에서 첫 글자가 영문으로 들어간다.
// ⚠️ Enter·Esc 는 **누를 때 표시만 하고 뗄 때** 영문으로 바꾼다. 누르는 순간 바꾸면 Enter 로
//    확정되는 마지막 글자 조합과 겹칠 수 있다. 그 사이 채팅 키를 다시 누르면(Enter→T 연타)
//    영문 전환을 취소해 새 채팅이 한글로 남는다.
// ⚠️ 같은 키 이벤트가 두 번 올 수 있다(raw input 과 WM_KEY* 를 둘 다 받는 게임). 모든 판단은
//    **두 번 불러도 결과가 같게** 짠다.
// ⚠️ 셰르벳 메뉴가 키보드를 잡고 있을 땐(우리 입력칸에 한글을 치는 중) 아무것도 안 한다.
// ⚠️ 입력기 API 는 reads_ime() 키에서만 부른다. 무빙 키마다 부르면 그 호출이 게임 메시지 처리를
//    붙잡아 오히려 빠른 입력이 영향을 받는다.
#pragma once

namespace sherbet
{
	namespace ime
	{
		enum class action
		{
			none,
			to_english,
			to_korean,
		};

		constexpr unsigned int kVkReturn = 0x0D;
		constexpr unsigned int kVkEscape = 0x1B;
		constexpr unsigned int kVkHangul = 0x15; // 한/영
		constexpr unsigned int kVkMenu = 0x12;   // Alt — 오른쪽 Alt 를 한/영으로 쓰는 키보드
		constexpr unsigned int kVkRMenu = 0xA5;
		constexpr unsigned int kDefaultChatKey = 'T';

		struct state
		{
			bool chat_open = false;          // 채팅 키로 채팅이 열렸다고 보는 중(Enter·Esc 로 닫힌다)
			bool english_on_release = false; // Enter·Esc 를 눌렀다 — 떼면 영문으로
		};

		// 키를 눌렀을 때. english_requested = 메뉴를 닫으며 미뤄 둔 영문 전환
		// (메뉴 닫기는 렌더 스레드에서 일어나는데 입력기는 창 스레드에서만 바꿀 수 있다).
		inline action on_key_down(state &st, bool enabled, bool korean_on_chat, unsigned int chat_vk, unsigned int vk, bool overlay_blocking, bool english_requested)
		{
			if (!enabled || overlay_blocking)
				return action::none;
			if (english_requested)
			{
				st.chat_open = false;
				st.english_on_release = false;
			}
			if (vk == kVkReturn || vk == kVkEscape)
			{
				st.chat_open = false;
				st.english_on_release = true;
				return english_requested ? action::to_english : action::none;
			}
			if (chat_vk != 0 && vk == chat_vk && !st.chat_open)
			{
				st.chat_open = true;
				st.english_on_release = false; // Enter→T 연타: 새 채팅은 한글로 남긴다
				if (korean_on_chat)
					return action::to_korean;
			}
			return english_requested ? action::to_english : action::none;
		}

		// 키를 뗐을 때.
		inline action on_key_up(state &st, bool enabled, unsigned int vk, bool overlay_blocking)
		{
			if (!enabled || overlay_blocking)
				return action::none;
			if ((vk == kVkReturn || vk == kVkEscape) && st.english_on_release)
			{
				st.english_on_release = false;
				return action::to_english;
			}
			return action::none;
		}

		// 이 키를 뗀 뒤 입력기 상태를 읽어 화면 표시를 갱신할지. 무빙 키는 절대 아니다.
		inline bool reads_ime(unsigned int vk, unsigned int chat_vk)
		{
			return vk == kVkReturn || vk == kVkEscape || vk == kVkHangul || vk == kVkMenu || vk == kVkRMenu ||
				(chat_vk != 0 && vk == chat_vk);
		}
	}
}
