/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — Win32 글루의 공개 선언.
// 판정 로직은 전부 sherbet_update_core.hpp 에 있다(맥 clang 으로 단위테스트되는 순수 함수).
// 이 파일에는 그 함수들을 **부르는** 쪽의 진입점만 둔다.
#pragma once

#include "sherbet_update_core.hpp" // info — 제안된 업데이트를 그대로 들고 있는다

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

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
		// ⚠️ `on_process_attach`(로더 락, 단일 스레드)가 채우고, 그 뒤로는 렌더 스레드만
		//    만진다(`controller::clear_blacklist`). 워커는 절대 읽지 않는다 — 컨트롤러가
		//    `init()` 에서 자기 사본을 떠 간다.
		//
		// ★ **롤백 배너와 [그래도 다시 시도] 는 이 둘로 판단한다(`safe_mode()` 아님).**
		//   `safe_mode()` 는 아래처럼 게이트1 이 걸려 롤백 성공 다음 부팅부터 false 가 되는데,
		//   배너까지 같이 사라지면 고객이 블랙리스트를 풀 방법이 영영 없어진다.
		const std::string &rolled_back_version();
		const std::string &rolled_back_sha();

		// 롤백 직후 세션인가(스펙 §5.4 R11). true 면 `update_effects` 를 조기 반환시키고
		// Sherbet UI 대신 "이전 버전으로 되돌렸습니다" 패널만 그린다.
		// **`return FALSE` 로 로딩을 중단하면 안 된다** — dxgi 프록시 export 가 사라져
		// 게임 자체가 안 켜진다.
		//
		// ⚠️ `state==rolledback|rollback_failed` **그리고** `marker.version == SHERBET_VERSION`
		//    일 때만 true 다(= 지금 매핑된 이미지가 마커가 비난하는 그 바이너리일 때).
		//    버전 게이트가 없으면 롤백에 성공한 **다음 부팅부터 영구히** 켜진다 — 그 부팅의
		//    self 는 되돌려진 멀쩡한 옛 바이너리인데 마커는 계속 `rolledback` 이고
		//    §5.3 이 그 마커를 지우지 못하게 막기 때문이다(지우면 블랙리스트가 날아간다).
		bool safe_mode();

		// ── 컨트롤러(스펙 §6) ─────────────────────────────────────────────────
		// ⚠️ **프로세스 전역 싱글턴이다. `runtime` 멤버로 두지 말 것.**
		// runtime 은 스왑체인당 하나라, 멤버로 두면 스왑체인 2개일 때 워커 둘이
		// 같은 `.sherbet-new.part` 파일을 서로 자른다.
		//
		// 스레드 규약(기존 `sherbet::auth::controller` 와 동일):
		//  - 조인 가능한 워커 멤버 **1개**. `std::thread` 대입 **바로 앞에서 반드시
		//    `join_worker()`** — 끝났지만 joinable 인 스레드에 재대입하면 `std::terminate`,
		//    즉 구매자의 게임이 그 자리에서 죽는다.
		//  - 상태 문자열은 락 안에서 **값 복사**로 돌려준다(포인터·참조 금지).
		//  - `tick()` / `begin_update()` / `dismiss_session()` / `clear_blacklist()` 는
		//    렌더 스레드 전용. 나머지 게터는 아토믹이거나 락을 잡는다.
		class controller
		{
		public:
			controller();
			~controller();

			controller(const controller &) = delete;
			controller &operator=(const controller &) = delete;

			// 런타임 생성 시 1회(두 번 불러도 안전하다). self_path 는 g_reshade_dll_path.
			void init(const std::wstring &self_path);
			// 렌더 스레드에서 매 프레임. 워커 회수와 최초 매니페스트 페치를 여기서 한다.
			void tick();

			bool has_offer() const;          // 배너를 띄울까(세션 닫기가 반영된다)
			bool is_mandatory() const;       // min_version 미달 — 빨간 배너
			bool personalized() const;       // has_owner() || SHERBET_NODELOCK — 절대 제안하지 않는다
			std::string offer_version() const;
			std::string offer_notes() const;
			std::string status_text() const; // ⚠️ 값 복사
			float progress() const;          // 0.0~1.0. 다운로드 중이 아니면 0
			bool busy() const;               // 워커가 도는 중(버튼 연타 방지)
			bool need_restart() const;       // 교체 성공 — "껐다 켜면 적용돼요"

			void begin_update();             // [업데이트]
			void cancel();                   // [취소] — 다운로드 중단
			void dismiss_session();          // [나중에]
			// [그래도 다시 시도](스펙 §5.4 R13). `sherbet.update` 를 지워 블랙리스트를
			// 해제하고 다시 페치한다. **구현 시 절대 빼지 말 것** — 자동 롤백 오탐의
			// 비용을 클릭 1회로 떨어뜨리는 유일한 장치다.
			void clear_blacklist();

		private:
			void join_worker();
			void set_status(const char *s);
			bool take_mutex();               // Local\Sherbet.Update.<…> 즉시 획득 시도
			void release_mutex();
			// 워커 스레드 본체. 블랙리스트는 렌더 스레드에서 복사해 넘긴다(전역 미접근).
			void run_fetch(const std::string &bad_ver, const std::string &bad_sha);
			void run_swap(const info &u);    // 스펙 §4.4 S1 + 뮤텍스 단일 해제점 (워커 스레드)
			void do_swap(const info &u);     // 스펙 §4.4 S2~S14 (뮤텍스 보유 상태에서만)
			bool wait_ms(unsigned int ms);   // _stop/_cancel 에 반응하는 대기. false = 중단해라
			// get_to_file 의 진행률 콜백. ⚠️ 4MB 면 약 2000회 불린다 — 아토믹 저장만 한다.
			static void progress_cb(void *ctx, unsigned long long received, unsigned long long total);

			std::thread _worker;
			std::atomic<bool> _worker_done{ false };
			std::atomic<bool> _stop{ false };   // 소멸 시 워커 조기 종료
			std::atomic<bool> _cancel{ false }; // [취소] — 다운로드 청크마다 확인
			std::atomic<bool> _busy{ false };
			std::atomic<bool> _inited{ false };
			std::atomic<bool> _fetch_started{ false };
			std::atomic<bool> _has_offer{ false };
			std::atomic<bool> _mandatory{ false };
			std::atomic<bool> _dismissed{ false };
			std::atomic<bool> _need_restart{ false };
			std::atomic<unsigned long long> _recv{ 0 };
			std::atomic<unsigned long long> _total{ 0 };

			// ⚠️ 워커를 **시작**하는 경로(tick / begin_update / clear_blacklist)를 직렬화한다.
			// `tick()` 은 runtime::on_present 에서 불리고 런타임은 스왑체인당 하나라, 창이
			// 둘이면 렌더 스레드도 둘이다. 검사와 `std::thread` 대입 사이가 열려 있으면
			// 두 스레드가 같은 워커 멤버에 대입해 **std::terminate** 가 난다.
			// 락 순서는 항상 _worker_mtx → _mtx 다(워커는 _mtx 만 잡는다 — 순환이 없다).
			std::mutex _worker_mtx;

			mutable std::mutex _mtx;
			std::string _status;   // _mtx 보호
			info _offer;           // _mtx 보호
			// 블랙리스트 사본. 전역(rolled_back_*)은 on_process_attach 가 채우고
			// init() 이 한 번 복사해 온다 — 워커가 전역을 읽지 않게 해서 경합을 없앤다.
			std::string _bad_ver;  // _mtx 보호
			std::string _bad_sha;  // _mtx 보호

			void *_mutex_handle = nullptr; // HANDLE. 워커가 잡고 워커가 놓는다
		};

		// 프로세스 전역 싱글턴.
		controller &instance();
	}
}
