/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */

// SHERBET 노드락 — "처음 딱 1번 켜면 그 PC로 고정".
//
// 동작(SHERBET_NODELOCK == 1 일 때만):
//   1. 이 PC의 하드웨어 지문(HWID)을 계산한다. (CPU + C: 볼륨 시리얼)
//   2. DLL 옆 sherbet.lic 이 없으면 → 첫 실행으로 보고 현재 HWID 서명을 기록(등록)한다.
//   3. sherbet.lic 이 있으면 → 저장된 서명과 현재 HWID 서명을 비교한다.
//        일치      → 인증 통과(오버레이 정상)
//        불일치    → 인증 실패(오버레이 대신 안내문만)
//
// 실패해도 DLL/게임 자체는 계속 동작한다(오버레이만 막힘). 파일 쓰기 실패 등
// 예외 상황에서는 "잠그지 않음"을 기본값으로 두어 정품 구매자가 브릭되는 일을 막는다.
//
// SHERBET_NODELOCK == 0(기본) 이면 이 모듈 전체가 no-op 이며, is_authorized() 는 항상 true.

#pragma once

#include "sherbet_owner.h"

namespace sherbet
{
	namespace nodelock
	{
		// 컴파일 시점 상수: 노드락이 이 빌드에서 켜져 있는가.
		inline bool enabled() { return SHERBET_NODELOCK != 0; }
	}
}

#if SHERBET_NODELOCK && defined(_WIN32)

#include "sherbet_license.hpp"

#include <Windows.h>
#include <intrin.h>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace sherbet
{
	namespace nodelock
	{
		// 하드웨어 지문: CPUID(feature) + C: 볼륨 시리얼 조합.
		inline std::string hwid()
		{
			int cpu[4] = { 0, 0, 0, 0 };
			__cpuid(cpu, 1);

			DWORD volume_serial = 0;
			GetVolumeInformationA("C:\\", nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0);

			std::stringstream ss;
			ss << std::hex << cpu[3] << cpu[0] << "_" << volume_serial;
			return ss.str();
		}

		// sherbet.lic 을 놓을 경로(호출자가 넘긴 디렉터리 아래). 보통 설정 파일과 같은 폴더.
		// dir 는 UTF-8 경로. 반환도 UTF-8.
		inline std::string lic_path(const std::string &dir_utf8)
		{
			std::string p = dir_utf8;
			if (!p.empty())
			{
				const char back = p.back();
				if (back != '\\' && back != '/')
					p += '\\';
			}
			p += "sherbet.lic";
			return p;
		}

		// 첫 실행이면 등록하고 true, 이후 실행이면 서명 일치 여부를 반환.
		// 파일을 열 수 없는(권한 등) 예외 상황에서는 브릭 방지를 위해 true(허용).
		inline bool check_and_register(const std::string &dir_utf8)
		{
			const std::string id = hwid();
			char sig[24];
			sherbet::license::sign_hwid(id.c_str(), sig, sizeof(sig));

			const std::string path = lic_path(dir_utf8);

			// 이미 등록돼 있으면 비교.
			{
				std::ifstream in(path.c_str());
				if (in.is_open())
				{
					std::string stored;
					std::getline(in, stored);
					in.close();
					// 개행/공백 정리
					while (!stored.empty() && (stored.back() == '\r' || stored.back() == '\n' || stored.back() == ' ' || stored.back() == '\t'))
						stored.pop_back();
					return sherbet::license::iequals(stored.c_str(), sig);
				}
			}

			// 첫 실행: 현재 PC로 고정(등록).
			std::ofstream out(path.c_str(), std::ios::trunc);
			if (out.is_open())
			{
				out << sig << "\n";
				out.close();
				return true;
			}

			// 기록 실패 → 잠그지 않음(정품 브릭 방지).
			return true;
		}

		// 세션 1회 캐시. dir 은 최초 호출값을 사용한다.
		inline bool is_authorized(const std::string &dir_utf8)
		{
			static int cached = -1; // -1=미확인, 0=거부, 1=허용
			if (cached < 0)
				cached = check_and_register(dir_utf8) ? 1 : 0;
			return cached != 0;
		}
	}
}

#else // 노드락 꺼짐: 항상 허용

#include <string>

namespace sherbet
{
	namespace nodelock
	{
		inline bool is_authorized(const std::string & /*dir_utf8*/) { return true; }
	}
}

#endif
