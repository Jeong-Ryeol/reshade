/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet HUD 돋보기 위치 프리셋 — 순수 로직. tools/sherbet_hotkey_test.cpp 가 함께 검증한다.
//
// 돋보기는 "어디를 잡아(rect) 어디에 몇 배로(anchor·zoom) 띄우나" 가 한 벌이다. 장면마다
// (차량 속도계·체력 막대·미니맵…) 이걸 매번 다시 잡는 게 불편해서 이름 붙여 여러 개 저장하고,
// 각자 단축키로 바로 바꾼다.
//
// 단축키를 누르면:
//   - 돋보기가 꺼져 있거나 다른 프리셋이면 → 그 프리셋으로 바꾸고 켠다
//   - 이미 그 프리셋으로 켜져 있으면     → 끈다 (같은 키로 켜고 끈다)
// 사용자가 영역·위치·배율을 손으로 바꾸면 '지금 프리셋' 표시는 풀린다(active = -1).
#pragma once

#include "sherbet_hotkey.hpp"
#include "sherbet_magnifier.hpp"

#include <string>

namespace sherbet
{
	namespace mag
	{
		// 한 사람이 쓰기에 충분한 수. ini 키가 끝없이 늘지 않게 막는 상한일 뿐이다.
		constexpr int kMaxPresets = 50;
		// 이름 최대 길이(바이트). ini 한 줄과 화면 칸에 맞춘다.
		constexpr std::size_t kMaxPresetName = 48;

		struct preset
		{
			std::string name;
			rect r;
			float anchor[2] = { 0.5f, 0.30f };
			float zoom = 3.0f;
			int cap_res[2] = { 0, 0 };
			hotkey::chord key;
		};

		enum class preset_action { apply, turn_off };

		inline preset_action on_hotkey(int idx, int active_idx, bool mag_on)
		{
			return (mag_on && active_idx == idx) ? preset_action::turn_off : preset_action::apply;
		}

		// "프리셋 3". n 은 1부터.
		inline std::string default_preset_name(int n)
		{
			return std::string("\xED\x94\x84\xEB\xA6\xAC\xEC\x85\x8B ") + std::to_string(n); // "프리셋 N"
		}

		// 이름 정리. ⚠️ 쉼표는 공백으로 바꾼다 — 리쉐이드 ini 는 쉼표를 배열 구분자로 읽어서
		// "체력, 방어구" 로 저장하면 다음 실행에 "체력" 만 남는다. 줄바꿈도 같은 이유로 뺀다.
		// 비어 있으면 fallback. 길면 UTF-8 글자 경계에서 자른다(한글이 반쪽 나지 않게).
		inline std::string clean_name(std::string s, const std::string &fallback)
		{
			for (char &ch : s)
				if (ch == ',' || ch == '\n' || ch == '\r' || ch == '=' || ch == '[' || ch == ']')
					ch = ' ';
			const std::size_t a = s.find_first_not_of(' ');
			if (a == std::string::npos)
				return fallback;
			const std::size_t b = s.find_last_not_of(' ');
			s = s.substr(a, b - a + 1);
			if (s.size() > kMaxPresetName)
			{
				std::size_t cut = kMaxPresetName;
				while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
					--cut;
				s.resize(cut);
			}
			return s;
		}

	}
}
