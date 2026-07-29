/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — Win32 글루의 공개 선언.
// 판정 로직은 전부 sherbet_update_core.hpp 에 있다(맥 clang 으로 단위테스트되는 순수 함수).
// 이 파일에는 그 함수들을 **부르는** 쪽의 진입점만 둔다.
#pragma once

#include <string>

namespace sherbet
{
	namespace update
	{
		// ── 부팅 경로(스펙 §5.2 / §5.4 R1~R11) ────────────────────────────────
		// DllMain(DLL_PROCESS_ATTACH) 에서 1회 호출한다.
		// 호출 위치: "다른 ReShade 인스턴스 이미 로드됨" 검사(`return FALSE`) **다음**,
		// hooks 설치 **앞**. 앞에 두면 로드가 취소될 프로세스에서도 마커를 세게 되고,
		// 뒤에 두면 후킹이 먼저 붙어 실패 시 디스크와 메모리가 어긋난다.
		//
		// self_path 는 반드시 `g_reshade_dll_path`(= `GetModuleFileNameW(hModule)`, dll_main.cpp:111)
		// 여야 한다. `_config_path.parent_path()` 는 `INSTALL/BasePath` 로 DLL 위치와
		// 달라질 수 있어 **절대 금지**다 — 엉뚱한 폴더의 마커를 읽고 엉뚱한 파일을 옮긴다.
		//
		// ⚠️ 이 함수는 **로더 락 안**에서 돈다. 금지(스펙 §5.2): 새 이미지의 LoadLibrary,
		//    스레드 생성·교차 스레드 대기, CNG/BCrypt, WinInet/윈속, COM, 셸 API,
		//    PathCch*/SHLWAPI 경로 정규화, UI(MessageBox 포함), 자식 프로세스.
		//    허용(이미 dll_main.cpp:117~201 의 관행): C++ 표준 라이브러리·힙 할당.
		//    그래서 판정은 코어 헤더의 순수 함수를 **그대로 호출한다** — 다시 구현하지 않는다.
		void on_process_attach(const std::wstring &self_path);

		// ── 부팅 성공 래치(스펙 §5.3) ─────────────────────────────────────────
		// 게이트 (1)(2) 를 통과하고 `state==pending` 인 마커만 지운다.
		// `rolledback` 은 건드리지 않는다 — 지우면 블랙리스트가 날아가 무한 재설치가 된다.
		//
		// ⚠️ **완료될 때까지 재시도한다.** 뮤텍스를 못 잡았거나 삭제가 실패하면 내부 래치를
		//    세우지 않으므로, 렌더 스레드가 매 프레임 불러도 되고 **불러야 한다**. 한 번
		//    실패한 것으로 끝내면 멀쩡한 설치가 다음 부팅에 tries=2 로 롤백된다.
		//    성공했거나 '지울 마커가 없다' 가 확정되면 그 뒤 호출은 원자적 플래그 확인 하나다.
		//    스왑체인이 둘이면 렌더 스레드도 둘이라 내부에서 직렬화한다.
		void mark_boot_ok();

		// ── 롤백 블랙리스트(스펙 §5.4 R4/R10) ─────────────────────────────────
		// `should_offer(cur, u, rolled_back_version(), rolled_back_sha())` 로 넘긴다.
		// 마커 상태와 **무관하게** 채워진다 — `rolledback` 일 때만 읽으면 복구가
		// `.sherbet-new` 로 self 를 되살리며 마커를 `pending` 으로 확정하는 경로에서
		// 블랙리스트가 한 세션 통째로 적용되지 않는다.
		//
		// ⚠️ 셋 다 `on_process_attach`(로더 락, 단일 스레드) 안에서만 쓰이고 그 뒤로는
		//    읽기 전용이다. DllMain 은 렌더 스레드가 생기기 전에 끝나므로 별도 동기화가 없다.
		const std::string &rolled_back_version();
		const std::string &rolled_back_sha();

		// 롤백 직후 세션인가(스펙 §5.4 R11). true 면 `update_effects` 를 조기 반환시키고
		// Sherbet UI 대신 "이전 버전으로 되돌렸습니다" 패널만 그린다.
		// **`return FALSE` 로 로딩을 중단하면 안 된다** — dxgi 프록시 export 가 사라져
		// 게임 자체가 안 켜진다.
		bool safe_mode();
	}
}
