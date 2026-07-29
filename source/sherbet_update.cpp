/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — Win32 글루.
//
// ⚠️ **이 파일의 부팅 경로는 DllMain(DLL_PROCESS_ATTACH) 안에서 돈다.**
// 고객의 게임 폴더에서 DLL 이름을 바꿔 다는 코드라, 틀리면 고객의 게임이 안 켜지고
// 그걸 고칠 코드는 방금 실패한 그 DLL 안에 있다. 그래서 규칙이 둘이다:
//
//  1. **판정을 다시 구현하지 않는다.** parse_marker / serialize_marker / classify /
//     decide_repair / decide_boot / marker_tries_cap / suffix_*() / marker_name() /
//     recovery_note_name() / make_recovery_note() / pe_check 는 전부
//     sherbet_update_core.hpp 에 있고 tools/sherbet_swap_sim.cpp 의 23만 9천 단언이
//     그 함수들을 소비한다. 여기서 손으로 다시 짜면 그 단언이 하나도 안 붙은 두 번째
//     구현이 구매자 DLL 을 바꾸게 된다(스펙 §5.2 '요구' 항목).
//  2. **로더 재진입을 부르지 않는다.** 금지(스펙 §5.2): 새 이미지 LoadLibrary /
//     스레드 생성·교차 스레드 대기 / CNG(BCrypt) / WinInet·윈속 / COM / 셸 API /
//     PathCch*·SHLWAPI 경로 정규화 / MessageBox / 자식 프로세스 / 지연로드.
//     C++ 표준 라이브러리와 힙은 금지가 아니다 — dll_main.cpp:117~201 이 같은 콜백에서
//     이미 std::filesystem·ini_file·로그파일 열기를 한다.
//  3. AV·안티치트 표면 최소화(스펙 §4.5): MOVEFILE_DELAY_UNTIL_REBOOT 금지,
//     UAC 승격 금지, 자기 이미지 VirtualProtect/WriteProcessMemory 금지.
//
// 파일 IO 는 원시 Win32 만 쓴다(CreateFileW/ReadFile/WriteFile/MoveFileExW/DeleteFileW).
// 재진입 문제라기보다 "필요 없는 의존을 늘리지 않는다" 는 규칙이다 — 이 경로는
// g_reshade_dll_path 문자열 하나면 충분하다.

#include "sherbet_update.hpp"
#include "sherbet_update_core.hpp" // ★ 판정 전부. 여기 있는 것을 다시 구현하지 않는다.
#include "sherbet_license.hpp"     // fnv1a — 뮤텍스 이름
#include "sherbet_owner.h"         // SHERBET_VERSION — 게이트1
#include "dll_log.hpp"

#include <Windows.h>
#include <atomic>
#include <mutex>

namespace
{
	using namespace sherbet::update;

#ifdef _WIN64
	constexpr bool kWantX64 = true;
#else
	constexpr bool kWantX64 = false;
#endif

	// 손상된/거대한 마커로 DllMain 에서 무한정 할당하지 않는다. 정상 마커는 200바이트 안쪽이다.
	constexpr DWORD kMaxMarkerBytes = 64 * 1024;
	// pe_check 가 보는 앞부분(스펙 §4.4 S6 과 같은 크기).
	constexpr std::size_t kPeHeadBytes = 4096;

	// ── 문자열 변환 ────────────────────────────────────────────────────────
	// ⚠️ narrow→wide 는 **반드시 CP_UTF8** 이다.
	//  - std::wstring(s.begin(), s.end())(sherbet_content.cpp 의 관례)는 UTF-8 바이트를
	//    그대로 wchar 로 늘려 "Sherbet-복구안내.txt" 를 읽을 수 없는 이름으로 만든다.
	//    self 가 사라진 고객에게 남는 유일한 단서가 그 파일이다.
	//  - CP_ACP 는 더 나쁘다: best-fit 매핑이 전각 문자(．．／)를 "../" 로 접어
	//    코어 헤더 url_allowed 가 막아 둔 경로 조작을 파일 경로 쪽에서 되살린다.
	std::wstring utf8_to_wide(const std::string &s)
	{
		if (s.empty())
			return std::wstring();
		const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
		if (n <= 0)
			return std::wstring();
		std::wstring w(static_cast<std::size_t>(n), L'\0');
		if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), &w[0], n) != n)
			return std::wstring();
		return w;
	}

	// ⚠️ 플래그는 0 이다(WC_ERR_INVALID_CHARS 아님). 마커의 exe 를 **쓰는 쪽**(Task 4)과
	// **읽고 비교하는 쪽**(게이트2)이 같은 변환을 써야 하므로, 이상한 파일명에서
	// 한쪽만 실패해 게이트2 가 영구히 어긋나는 일을 만들지 않는다.
	std::string wide_to_utf8(const std::wstring &w)
	{
		if (w.empty())
			return std::string();
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		if (n <= 0)
			return std::string();
		std::string s(static_cast<std::size_t>(n), '\0');
		if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr) != n)
			return std::string();
		return s;
	}

	std::size_t last_separator(const std::wstring &p)
	{
		for (std::size_t i = p.size(); i > 0; --i)
			if (p[i - 1] == L'\\' || p[i - 1] == L'/')
				return i - 1;
		return std::wstring::npos;
	}

	// std::filesystem::path::filename() 과 같은 결과(윈도우 경로 기준). <filesystem> 을
	// 이 TU 에 새로 들이지 않기 위해 직접 자른다.
	std::wstring base_of(const std::wstring &p)
	{
		const std::size_t at = last_separator(p);
		return (at == std::wstring::npos) ? p : p.substr(at + 1);
	}

	// ── 경로 묶음 ──────────────────────────────────────────────────────────
	// 접미사·파일명은 **전부 코어 헤더에서 온다.** 여기 문자열로 다시 타이핑하면
	// 시뮬레이터가 자기 사본을 검증하는 꼴이 되어 제품에 대해 아무것도 못 박지 못한다
	// (제품이 ".sherbet-backup" 을 써도 초록불이다).
	struct paths
	{
		std::wstring self;        // DLL 전체 경로 (= g_reshade_dll_path)
		std::wstring newf;        // <self>.sherbet-new
		std::wstring bak;         // <self>.sherbet-bak
		std::wstring bakold;      // <self>.sherbet-bak.old
		std::wstring failed;      // <self>.sherbet-failed
		std::wstring marker;      // <DLL 폴더>/sherbet.update
		std::wstring marker_part; //   그 원자 교체용 임시 파일
		std::wstring note;        // <DLL 폴더>/Sherbet-복구안내.txt
		std::wstring note_part;
		std::wstring mutex_name;  // Local\Sherbet.Update.<fnv1a(소문자 DIR)>
		std::string  self_name;   // UTF-8. make_recovery_note 의 첫 인자
		std::string  bak_name;    // UTF-8. make_recovery_note 의 둘째 인자
		std::string  exe_name;    // UTF-8. 게이트2
		bool ok = false;
	};

	// 뮤텍스 이름 = Local\Sherbet.Update.<fnv1a(소문자 DIR)>.
	// `.lock` 파일을 쓰지 않는 이유: 프로세스가 죽으면 OS 가 커널 오브젝트를 반드시
	// 해제하지만 파일은 stale 로 남아 업데이트가 영영 안 된다.
	// 소문자화는 ASCII 만 한다 — CharLowerW 는 user32.dll 이라 로더 콜백에서 부를 수 없고,
	// 이 값은 '같은 폴더를 같은 이름으로 해싱한다' 는 캐시 키일 뿐이다(대소문자만 다른
	// 경로로 로드된 두 프로세스를 같은 뮤텍스로 묶는 것이 목적).
	std::wstring make_mutex_name(const std::wstring &dir)
	{
		std::string key = wide_to_utf8(dir);
		for (char &c : key)
		{
			if (c == '/')
				c = '\\';
			else if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
		}
		const unsigned int h = sherbet::license::fnv1a(key.c_str());
		wchar_t hex[9];
		for (int i = 0; i < 8; ++i)
		{
			const unsigned int v = (h >> ((7 - i) * 4)) & 0xfu;
			hex[i] = static_cast<wchar_t>(v < 10 ? (L'0' + v) : (L'a' + (v - 10)));
		}
		hex[8] = L'\0';
		return std::wstring(L"Local\\Sherbet.Update.") + hex;
	}

	// dll_main.cpp:121 의 g_target_executable_path 와 **같은 출처**다
	// (g_target_executable_path = get_module_path(nullptr) = GetModuleFileNameW(nullptr)).
	// ini_file.hpp 의 전역을 끌어오지 않는 것은 이 TU 에 <filesystem> 을 새로 들이지
	// 않기 위해서이고, 결과 문자열은 filename().u8string() 과 바이트 단위로 같다.
	// ⚠️ 마커에 exe 를 **쓰는 쪽**(Task 4 S9)도 반드시 이 함수를 쓴다 — 쓰기와 비교가
	// 다른 변환을 타면 게이트2 가 조용히 영원히 어긋난다.
	std::string current_exe_name_utf8()
	{
		wchar_t buf[4096];
		const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
		if (n == 0 || n >= ARRAYSIZE(buf)) // 잘렸으면 이름이 틀린 것이므로 게이트2 를 통과시키지 않는다
			return std::string();
		return wide_to_utf8(base_of(std::wstring(buf, n)));
	}

	bool make_paths(const std::wstring &self_path, paths &p)
	{
		p = paths();
		if (self_path.empty())
			return false;
		const std::size_t at = last_separator(self_path);
		if (at == std::wstring::npos) // 절대 경로가 아니다 — 상대 경로로 파일을 옮기지 않는다
			return false;

		const std::wstring dir = self_path.substr(0, at + 1); // 구분자 포함
		const std::wstring w_new    = utf8_to_wide(suffix_new());
		const std::wstring w_bak    = utf8_to_wide(suffix_bak());
		const std::wstring w_bakold = utf8_to_wide(suffix_bak_old());
		const std::wstring w_failed = utf8_to_wide(suffix_failed());
		const std::wstring w_marker = utf8_to_wide(marker_name());
		const std::wstring w_note   = utf8_to_wide(recovery_note_name());
		// 코어 헤더의 리터럴이 안 바뀌면 여기서 실패할 수 없다. 그래도 확인하는 이유:
		// 빈 문자열이 섞이면 self 나 폴더 자체를 가리키는 경로가 만들어져 엉뚱한 것을 옮긴다.
		if (w_new.empty() || w_bak.empty() || w_bakold.empty() || w_failed.empty() || w_marker.empty() || w_note.empty())
			return false;

		p.self        = self_path;
		p.newf        = self_path + w_new;
		p.bak         = self_path + w_bak;
		p.bakold      = self_path + w_bakold;
		p.failed      = self_path + w_failed;
		p.marker      = dir + w_marker;
		p.marker_part = p.marker + L".part";
		p.note        = dir + w_note;
		p.note_part   = p.note + L".part";
		p.mutex_name  = make_mutex_name(dir);
		p.self_name   = wide_to_utf8(base_of(self_path));
		p.bak_name    = wide_to_utf8(base_of(p.bak));
		p.exe_name    = current_exe_name_utf8();
		if (p.self_name.empty() || p.bak_name.empty())
			return false; // 복구안내에 넣을 이름을 모르면 self 를 없앨 수 있는 절차를 시작하지 않는다
		p.ok = true;
		return true;
	}

	// ── 원시 파일 IO ───────────────────────────────────────────────────────

	// ⚠️ '모르겠다' 는 **있다** 로 본다.
	// self 를 '없다' 고 잘못 판정하면 classify 가 swapping_lost_self 로 보내 마커에
	// rolledback 을 적고 .bak 을 self 자리에 앉힌다 — 일시적인 접근 거부 한 번이
	// 멀쩡한 설치를 되돌리게 할 수는 없다. 반대 방향(있는데 없다고 못 보는 것)의 최악은
	// '이번 부팅엔 아무것도 안 한다' 이다.
	bool file_exists(const std::wstring &p)
	{
		if (p.empty())
			return false;
		const DWORD a = GetFileAttributesW(p.c_str());
		if (a != INVALID_FILE_ATTRIBUTES)
			return (a & FILE_ATTRIBUTE_DIRECTORY) == 0; // 같은 이름의 디렉터리는 DLL 소스가 아니다
		const DWORD e = GetLastError();
		return !(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND || e == ERROR_INVALID_NAME);
	}

	bool read_file(const std::wstring &path, std::string &out, DWORD max_bytes)
	{
		out.clear();
		const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			return false;

		bool ok = false;
		LARGE_INTEGER size = {};
		if (GetFileSizeEx(f, &size) && size.QuadPart >= 0 && size.QuadPart <= static_cast<LONGLONG>(max_bytes))
		{
			out.resize(static_cast<std::size_t>(size.QuadPart));
			std::size_t total = 0;
			ok = true;
			while (total < out.size())
			{
				DWORD read = 0;
				if (!ReadFile(f, &out[total], static_cast<DWORD>(out.size() - total), &read, nullptr))
				{
					ok = false;
					break;
				}
				if (read == 0) // 예상보다 짧다(다른 writer 가 줄였다) — 읽은 만큼만 쓴다
					break;
				total += read;
			}
			if (ok)
				out.resize(total);
		}
		CloseHandle(f); // ★ 어떤 경로로든 반드시 닫는다
		if (!ok)
			out.clear();
		return ok;
	}

	bool read_head(const std::wstring &path, unsigned char *buf, std::size_t cap, std::size_t &out_len)
	{
		out_len = 0;
		const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			return false;

		bool ok = true;
		std::size_t total = 0;
		while (total < cap)
		{
			DWORD read = 0;
			if (!ReadFile(f, buf + total, static_cast<DWORD>(cap - total), &read, nullptr))
			{
				ok = false;
				break;
			}
			if (read == 0)
				break; // EOF
			total += read;
		}
		CloseHandle(f);
		out_len = ok ? total : 0;
		return ok;
	}

	// `.part` 에 쓰고 MoveFileExW(REPLACE_EXISTING) 로 원자 교체(스펙 §5.1).
	// 전원이 나가도 최종 파일은 '옛 내용' 이거나 '새 내용' 이지 절대 반쯤 쓰인 내용이 아니다.
	// (= 미탐은 날 수 있어도 오탐은 안 난다. 잘린 마커가 파싱돼 엉뚱한 판정을 내는 일이 없다.)
	bool write_file_atomic(const std::wstring &final_path, const std::wstring &part_path, const std::string &data)
	{
		const HANDLE f = CreateFileW(part_path.c_str(), GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			return false;

		bool ok = true;
		std::size_t off = 0;
		while (off < data.size())
		{
			const std::size_t left = data.size() - off;
			DWORD written = 0;
			if (!WriteFile(f, data.data() + off, static_cast<DWORD>(left), &written, nullptr) || written == 0)
			{
				ok = false;
				break;
			}
			off += written;
		}
		CloseHandle(f); // ★ 열린 핸들이 있으면 MoveFileExW 도 DeleteFileW 도 실패한다

		if (ok && MoveFileExW(part_path.c_str(), final_path.c_str(), MOVEFILE_REPLACE_EXISTING))
			return true;
		DeleteFileW(part_path.c_str()); // 반쯤 쓰인 .part 를 남기지 않는다
		return false;
	}

	bool write_marker(const paths &p, const boot_marker &m)
	{
		// 코어 헤더 serialize_marker 의 전제조건: state 가 비면 안 된다.
		// 빈 state 로 쓰면 parse_marker 가 그 파일을 영구히 거부해 다른 writer 의
		// unknown 키가 통째로 사라진다. 마커를 없애려면 파일을 지운다.
		if (m.state.empty())
			return false;
		return write_file_atomic(p.marker, p.marker_part, serialize_marker(m));
	}

	// 스펙 §4.4 S9 / §5.4 R9-pre. self 를 없앨 수 있는 절차 **직전**에 쓴다.
	// 문구는 make_recovery_note 하나에서만 나온다(기록 지점이 둘이라 갈라지면 안 된다).
	void write_recovery_note(const paths &p)
	{
		// UTF-8 BOM 을 붙인다. 이 파일의 유일한 목적이 '고객이 읽는 것' 인데, BOM 없는
		// 한글 UTF-8 은 옛 메모장에서 깨져 단서가 통째로 무의미해진다.
		std::string text = "\xEF\xBB\xBF";
		text += make_recovery_note(p.self_name, p.bak_name);
		text += "\r\n";
		write_file_atomic(p.note, p.note_part, text);
	}

	// ── 프로세스 전역 상태(스펙 §5.4 R4/R10/R11) ───────────────────────────
	// 셋 다 on_process_attach(로더 락, 단일 스레드) 안에서만 쓰고, 렌더 스레드는
	// DllMain 이 끝난 뒤에야 생기므로 읽기는 자연히 그 뒤다.
	paths g_paths;
	std::string g_bad_ver;
	std::string g_bad_sha;
	bool g_safe_mode = false;

	void publish(const boot_marker &m)
	{
		// ⚠️ state 와 **무관하게** 읽는다. rolledback 일 때만 읽으면, 복구가 .sherbet-new 로
		// self 를 되살리며 마커를 pending 으로 확정하는 경로에서 블랙리스트가 한 세션
		// 통째로 적용되지 않아 방금 문제를 일으킨 빌드를 그대로 다시 제안하게 된다.
		g_bad_ver = m.bad_ver;
		g_bad_sha = m.bad_sha;
		g_safe_mode = (m.state == "rolledback" || m.state == "rollback_failed");
	}

	// ── 로그(고객 문의 때 유일한 단서다) ──────────────────────────────────
	const char *name_of(disk_state s)
	{
		switch (s)
		{
		case disk_state::normal:             return "normal";
		case disk_state::staged:             return "staged";
		case disk_state::swapping_lost_self: return "swapping_lost_self";
		case disk_state::resume_from_new:    return "resume_from_new";
		case disk_state::rollback_ready:     return "rollback_ready";
		case disk_state::broken_no_dll:      return "broken_no_dll";
		}
		return "?";
	}

	const char *name_of(repair_action a)
	{
		switch (a)
		{
		case repair_action::none:             return "none";
		case repair_action::promote_pending:  return "promote_pending";
		case repair_action::restore_from_bak: return "restore_from_bak";
		case repair_action::restore_from_new: return "restore_from_new";
		case repair_action::give_up:          return "give_up";
		}
		return "?";
	}

	// ── R3: 교체 중단 복구 ─────────────────────────────────────────────────
	// tools/sherbet_swap_sim.cpp 의 startup_repair 와 **같은 순서**다.
	// 판정은 classify → decide_repair 그대로 부르고, 여기서는 그 결과를 파일로 옮기기만 한다.
	void repair_phase(const paths &p, boot_marker &m, bool can_write)
	{
		const bool has_self = file_exists(p.self);
		const bool has_new  = file_exists(p.newf);
		const bool has_bak  = file_exists(p.bak);

		const disk_state s = classify(has_self, has_new, has_bak, m);
		const repair_action a = decide_repair(s);
		reshade::log::message(reshade::log::level::info,
			"[sherbet-update] attach: self=%d new=%d bak=%d marker=%s state=%s repair=%s",
			has_self ? 1 : 0, has_new ? 1 : 0, has_bak ? 1 : 0,
			can_write ? "ok" : "none/unparseable", name_of(s), name_of(a));

		switch (a)
		{
		case repair_action::promote_pending:
			// self 는 유효한 DLL 이다(교체 경로가 self 자리에 놓는 것은 S6 의 sha256+pe_check 를
			// 통과한 파일뿐이고 rename 은 원자적이다). 어느 버전인지는 모르지만, 틀렸을 때
			// 게이트1 이 잡아 주는 쪽이 거짓 안전모드보다 훨씬 덜 해롭다(코어 헤더 참고).
			// ⚠️ tries=0 은 장식이 아니다 — 직전 롤백 마커의 tries=2 를 물려받은 채 pending 을
			// 쓰면 새로 깐 멀쩡한 빌드가 **첫 부팅에** 롤백된다.
			if (can_write)
			{
				m.state = "pending";
				m.tries = 0;
				write_marker(p, m);
			}
			break;

		case repair_action::restore_from_bak:
			// ★ 마커 먼저, 파일 나중(self 가 없는 동안의 규칙, §5.4 R3/R10).
			// 순서를 뒤집으면 이동 직후 끊겼을 때 (self 있음 + .bak 없음 + pending) 이 되는데
			// 이건 '새 버전이 정상적으로 깔려 확정 대기 중' 과 디스크상 구별이 불가능해서
			// bad_ver/bad_sha 가 통째로 증발한다 — 방금 되돌린 불량 빌드를 다시 제안하게 된다.
			if (can_write)
			{
				m.state = "rolledback";
				m.bad_ver = m.version; // §5.4 R10 — 시뮬레이터 P8 이 이 두 줄에 의존한다
				m.bad_sha = m.sha;
				write_marker(p, m);
			}
			// 거부돼도 아무것도 되돌리지 않는다. 마커는 이미 rolledback 이고 .bak 은 그대로라
			// 다음 실행이 같은 판정을 내려 다시 시도한다. 그동안 self 가 없으므로
			// 복구안내가 유일한 단서로 남아 있어야 한다(아래 잔재 정리가 self 없을 땐 안 지운다).
			if (!MoveFileExW(p.bak.c_str(), p.self.c_str(), 0))
				reshade::log::message(reshade::log::level::error,
					"[sherbet-update] restore_from_bak: .sherbet-bak -> self 실패 (err=%lu)", GetLastError());
			break;

		case repair_action::restore_from_new:
			// ★ 같은 규칙. 마커는 pending/tries=0 으로 확정하고 bad_ver/bad_sha 는 그대로 둔다.
			// rolledback 을 남겨 두면 방금 되살린 바이너리로 안전모드가 켜져 효과가 통째로
			// 꺼지고 '되돌렸습니다' 라는 거짓 배너가 뜬다(낡은 pending 은 게이트1 이 걸러 준다).
			if (can_write)
			{
				m.state = "pending";
				m.tries = 0;
				write_marker(p, m);
			}
			if (!MoveFileExW(p.newf.c_str(), p.self.c_str(), 0))
				reshade::log::message(reshade::log::level::error,
					"[sherbet-update] restore_from_new: .sherbet-new -> self 실패 (err=%lu)", GetLastError());
			break;

		case repair_action::give_up:
			// 재료가 하나도 없다. **아무 파일도 건드리지 않고** 기록만 남긴다(§5.4 R8).
			if (can_write)
			{
				m.state = "rollback_failed";
				write_marker(p, m);
			}
			break;

		case repair_action::none:
			break;
		}

		// 잔재 정리 — **self 가 있을 때만.** self 가 없으면 남은 파일이 유일한 DLL 사본일 수
		// 있고(.sherbet-bak.old 도 한때 동작하던 진짜 바이너리다), 복구안내는 고객에게 남는
		// 유일한 단서다. '마지막 사본을 지우지 않는다' 가 이 청소의 유일한 규칙이다.
		// (.sherbet-bak 은 롤백 재료라 남긴다 — S8 이 다음 교체 때 정리한다.
		//  .sherbet-failed 도 증거 1개 보관이라 남긴다, §5.4 R9.)
		if (file_exists(p.self))
		{
			if (file_exists(p.newf))   DeleteFileW(p.newf.c_str());
			if (file_exists(p.bakold)) DeleteFileW(p.bakold.c_str());
			if (file_exists(p.note))   DeleteFileW(p.note.c_str());
		}
	}

	// ── R8~R10: 롤백 ───────────────────────────────────────────────────────
	// tools/sherbet_swap_sim.cpp 의 run_rollback 과 **같은 순서**다.
	void rollback_phase(const paths &p, boot_marker &m)
	{
		// 블랙리스트는 '되돌리기에 성공했는가' 와 무관한 사실이다. decide_boot 이 rollback 을
		// 낸 시점에 '이 버전은 2회 연속 부팅에 실패했다' 가 이미 확정됐으므로, 아래 어느
		// 실패 경로로 빠지든 bad_ver/bad_sha 는 남긴다.
		const std::string blamed_ver = m.version;
		const std::string blamed_sha = m.sha;

		// R8: **먼저 재료 검증.** self 를 밀어낸 뒤에 .bak 이 없는 걸 알면 디렉터리에
		// DLL 이 아예 없게 된다. 해시는 계산하지 않는다(§5.2) — 앞 4KB pe_check 만.
		bool material_ok = false;
		if (file_exists(p.bak))
		{
			unsigned char head[kPeHeadBytes];
			std::size_t head_len = 0;
			material_ok = read_head(p.bak, head, sizeof(head), head_len) && pe_check(head, head_len, kWantX64);
		}
		if (!material_ok)
		{
			m.state = "rollback_failed";
			m.bad_ver = blamed_ver;
			m.bad_sha = blamed_sha;
			write_marker(p, m);
			reshade::log::message(reshade::log::level::error,
				"[sherbet-update] rollback: 재료 없음/손상 (.sherbet-bak) — 파일 무변화, rollback_failed 기록");
			return;
		}

		// R9-pre: 복구안내를 (다시) 쓴다. S13 이 교체 성공 시 지웠으므로 지금은 없고,
		// 바로 아래 R9a 부터 self 가 사라진다. 그 구간에서 전원이 나가거나 되돌리기까지
		// 거부되면 자동 복구가 영영 안 도는 상태로 끝날 수 있다 — 그때 고객에게 남는 단서는
		// 이 파일 하나다(§4.4 S9 가 교체 전에 쓰는 것과 같은 이유).
		write_recovery_note(p);

		// R9a: self → .sherbet-failed. 증거는 1개만 보관한다.
		DeleteFileW(p.failed.c_str());
		if (!MoveFileExW(p.self.c_str(), p.failed.c_str(), 0))
		{
			// 아무것도 안 옮겼다 = R8 과 같은 원칙으로 파일은 그대로 두고 기록만 남긴다.
			// self 는 여전히 불량 바이너리이므로 rollback_failed 가 진실이다.
			m.state = "rollback_failed";
			m.bad_ver = blamed_ver;
			m.bad_sha = blamed_sha;
			write_marker(p, m);
			reshade::log::message(reshade::log::level::error,
				"[sherbet-update] rollback: self -> .sherbet-failed 거부 (err=%lu) — 파일 무변화", GetLastError());
			return;
		}

		// R10: 여기서 마커를 쓴다(스펙 표의 배치와 다르다). self 가 이미 없으므로 마커를
		// 먼저 써도 잃을 게 없고, 뒤로 미루면 아래 rename 직후 끊겼을 때 '되돌렸는데
		// 안 되돌렸다고 적힌' 마커(= 블랙리스트 소실)가 남는다.
		m.state = "rolledback";
		m.bad_ver = blamed_ver;
		m.bad_sha = blamed_sha;
		write_marker(p, m);

		// R9b: .bak → self
		if (MoveFileExW(p.bak.c_str(), p.self.c_str(), 0))
		{
			reshade::log::message(reshade::log::level::warning,
				"[sherbet-update] rollback 완료: '%s' 를 되돌렸습니다 (안전모드)", blamed_ver.c_str());
			return;
		}

		// R9b 거부 → 첫 번째 이동을 즉시 되돌린다.
		// ⚠️ 되돌리기 **전에** rollback_failed 를 기록한다. self 가 없는 지금 쓰는 것이 규칙이고,
		// 되돌린 뒤에 쓰면 그 사이에 끊겼을 때 (self=불량 바이너리 + 마커 rolledback) 이 되어
		// 거짓 안전모드가 켜진 채 **고정점**이 된다(복구가 손댈 이유를 못 찾는다).
		m.state = "rollback_failed";
		m.bad_ver = blamed_ver;
		m.bad_sha = blamed_sha;
		write_marker(p, m);

		// 되돌리기는 **방금 밀어낸 바로 그 파일**(.sherbet-failed)을 도로 앉히는 것이다.
		// 여기서 .bak 을 앉히면 롤백이 사실상 성공인데 마커는 rollback_failed 라 디스크와
		// 마커가 어긋난다.
		if (!MoveFileExW(p.failed.c_str(), p.self.c_str(), 0))
			reshade::log::message(reshade::log::level::error,
				"[sherbet-update] rollback: .bak->self 와 되돌리기가 모두 거부됨 (err=%lu) — 복구안내를 남긴다", GetLastError());
		else
			reshade::log::message(reshade::log::level::error,
				"[sherbet-update] rollback: .bak -> self 거부 — 원래 바이너리를 되돌려 놓았습니다");
	}

	// ── R5~R6 + R8: 3중 게이트와 부팅 카운트 ───────────────────────────────
	// tools/sherbet_swap_sim.cpp 의 simulate_boot 과 **같은 순서**다.
	void boot_phase(const paths &p, boot_marker &m, bool can_write)
	{
		if (!can_write)
			return; // 마커가 없거나 파싱 불가 — 셀 것도, 쓸 수 있는 것도 없다
		if (m.state != "pending")
			return; // rolledback/rollback_failed/swapping 을 세면 이미 되돌린 사용자를 또 되돌린다

		// 게이트1 — 지금 매핑된 바이너리를 서술하는 마커인가.
		// 크래시 정합성의 마지막 방어선이다: 이게 없으면 복구가 남길 수 있는 낡은 마커가
		// 곧바로 해롭게 변한다.
		if (m.version != SHERBET_VERSION)
		{
			reshade::log::message(reshade::log::level::info,
				"[sherbet-update] 게이트1 불일치(marker=%s, running=%s) — 세지 않는다", m.version.c_str(), SHERBET_VERSION);
			return;
		}
		// ★ 게이트2 — 교체 당시 그리던 실행파일인가.
		// 구매자는 DLL 을 dxgi.dll 로 넣으므로 is_dxgi==true → dll_main.cpp:127 의
		// "설정파일 없으면 return FALSE" 블록이 통째로 스킵되고, 그 폴더에서 dxgi 를
		// 임포트하는 FiveM 런처·게임·NUI 서브프로세스가 **전부 완전히 로드된다.**
		// 이 게이트가 없으면 정상 버전이 첫 실행에 tries 3 을 찍고 무조건 롤백된다.
		if (p.exe_name.empty() || m.exe != p.exe_name)
		{
			reshade::log::message(reshade::log::level::info,
				"[sherbet-update] 게이트2 불일치(marker=%s, running=%s) — 세지 않는다", m.exe.c_str(), p.exe_name.c_str());
			return;
		}
		// 게이트3(네임드 뮤텍스)은 이 함수에 들어오기 전에 이미 보유 중이다.

		// R6: tries+1 을 **먼저 디스크에 쓴다**(쓰기 전에 죽으면 카운트가 안 늘어 미탐).
		boot_marker persisted = m;
		persisted.tries = (m.tries >= marker_tries_cap()) ? marker_tries_cap() : m.tries + 1;
		if (!write_marker(p, persisted))
		{
			// 마커를 못 쓰는 디렉터리(읽기전용/디스크 가득)에서 파일 이동만 반쯤 성공시키지
			// 않는다. 세지도, 되돌리지도 않고 이번 부팅은 넘어간다.
			reshade::log::message(reshade::log::level::error,
				"[sherbet-update] R6 tries+1 기록 실패 — 이번 부팅은 세지 않는다");
			return;
		}

		// ⚠️ decide_boot 에는 **디스크에서 읽은 그대로**(증가 전)를 넘긴다.
		// 증가시켜 넘기면 임계값이 2→1 로 반토막 나 알트탭·드라이버 결함 한 번에 멀쩡한
		// 설치가 되돌아간다(tools/sherbet_update_test.cpp 의 `misused` 블록이 그 차이를
		// 실행 가능한 형태로 박아 두었다).
		const boot_action act = decide_boot(m);
		m = persisted; // 이후 쓰기의 기반은 디스크와 같아야 한다

		reshade::log::message(reshade::log::level::info,
			"[sherbet-update] pending '%s' 부팅 카운트 %d -> %d, 판정=%s",
			m.version.c_str(), persisted.tries - 1, persisted.tries,
			act == boot_action::rollback ? "rollback" : (act == boot_action::count ? "count" : "none"));

		if (act == boot_action::rollback)
			rollback_phase(p, m);
	}

	// 뮤텍스를 보유한 상태에서만 불린다. 여기서 어떻게 빠져나가도 호출자가 반드시 해제한다.
	void run_boot_path(const paths &p)
	{
		// R3/R4: 마커를 읽는다.
		boot_marker m;
		std::string text;
		// ⚠️ 파싱에 실패한 마커는 **절대 덮어쓰지 않는다** — 다른 writer 의 unknown 키가
		// 통째로 날아간다. 그래도 '파일 복구' 는 한다: 마커가 낡은 것보다 DLL 이 없는 쪽이
		// 훨씬 나쁘다. 시뮬레이터 startup_repair 가 정확히 이 동작을 전수 검증한다.
		const bool can_write = read_file(p.marker, text, kMaxMarkerBytes) && parse_marker(text, m);

		repair_phase(p, m, can_write); // R3
		boot_phase(p, m, can_write);   // R5~R6 → (필요하면) R8~R10

		// R4/R10/R11: 최종 마커를 프로세스 전역에 반영한다. 한 곳에서만 하므로 위의 어느
		// 갈래로 빠져나가도 블랙리스트와 안전모드가 실제 마커와 어긋날 수 없다.
		publish(m);
	}
}

void sherbet::update::on_process_attach(const std::wstring &self_path)
{
	if (!make_paths(self_path, g_paths))
	{
		reshade::log::message(reshade::log::level::warning, "[sherbet-update] 경로를 만들 수 없어 부팅 경로를 건너뜁니다");
		return;
	}
	const paths &p = g_paths;

	// 게이트3 — 네임드 뮤텍스. 다른 게임 창이 교체 중(S1~S13)이면 여기서 막힌다.
	// 그 사이에는 self 가 잠깐 없을 수 있는데(S10~S11), 뮤텍스가 없으면 이 프로세스가
	// 그걸 '교체 중단' 으로 오해해 마커에 rolledback 을 적고 되돌려 버린다.
	const HANDLE mutex = CreateMutexW(nullptr, FALSE, p.mutex_name.c_str());
	if (mutex == nullptr)
	{
		// 뮤텍스를 못 만들면 파일은 하나도 건드리지 않는다. 다만 마커 '읽기' 는 원자 교체라
		// 항상 온전한 내용이므로, 블랙리스트와 안전모드만 채우고 나간다.
		boot_marker m;
		std::string text;
		if (read_file(p.marker, text, kMaxMarkerBytes) && parse_marker(text, m))
			publish(m);
		return;
	}

	const DWORD wait = WaitForSingleObject(mutex, 0); // 즉시 시도. 대기하지 않는다(논블로킹 경로).
	// WAIT_ABANDONED = 이전 보유자가 죽었다. 그래도 **우리가 소유권을 얻은 것**이므로
	// 반드시 해제해야 한다. 잊으면 그 폴더의 업데이트가 프로세스 수명 동안 잠긴다.
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
	{
		CloseHandle(mutex);
		boot_marker m;
		std::string text;
		if (read_file(p.marker, text, kMaxMarkerBytes) && parse_marker(text, m))
			publish(m);
		return;
	}

	run_boot_path(p); // ★ 여기서 어떻게 빠져나가도 아래 두 줄이 반드시 돈다

	ReleaseMutex(mutex);
	CloseHandle(mutex);
}

void sherbet::update::mark_boot_ok()
{
	// ⚠️ '한 번 불렀으니 끝' 이 아니다. 뮤텍스를 못 잡았거나 삭제가 실패하면 래치를 세우지
	// 않아 다음 프레임에 다시 시도한다 — 여기서 포기하면 멀쩡한 설치가 다음 부팅에
	// tries=2 로 롤백된다. 성공/무관 확정 뒤에는 아토믹 확인 하나로 끝난다.
	static std::atomic<bool> done{ false };
	static std::mutex once;

	if (done.load(std::memory_order_acquire))
		return;
	if (!g_paths.ok)
		return;

	const std::lock_guard<std::mutex> lock(once); // 스왑체인이 둘이면 렌더 스레드도 둘이다
	if (done.load(std::memory_order_relaxed))
		return;

	const paths &p = g_paths;
	const HANDLE mutex = CreateMutexW(nullptr, FALSE, p.mutex_name.c_str());
	if (mutex == nullptr)
		return; // 다음 프레임에 다시
	const DWORD wait = WaitForSingleObject(mutex, 0);
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
	{
		CloseHandle(mutex); // 다른 창이 교체 중 — 끝나면 다시 시도한다
		return;
	}

	bool settled = false;
	{
		boot_marker m;
		std::string text;
		if (!read_file(p.marker, text, kMaxMarkerBytes) || !parse_marker(text, m))
		{
			settled = true; // 마커가 없거나 파싱 불가 — 지울 것이 없다(그리고 덮어쓰지 않는다)
		}
		// 스펙 §5.3: 게이트 (1)(2) 를 통과하고 state==pending 인 마커만 지운다.
		// rolledback 은 절대 건드리지 않는다 — 지우면 블랙리스트가 날아가 방금 되돌린
		// 불량 빌드를 다시 설치하는 무한 루프가 된다.
		else if (m.state != "pending" || m.version != SHERBET_VERSION || p.exe_name.empty() || m.exe != p.exe_name)
		{
			settled = true; // 우리 마커가 아니다 — 손대지 않는 것이 정답이고, 다시 볼 이유도 없다
		}
		else if (DeleteFileW(p.marker.c_str()) || !file_exists(p.marker))
		{
			settled = true;
			reshade::log::message(reshade::log::level::info,
				"[sherbet-update] 부팅 성공 — '%s' 확정, 마커를 지웠습니다", m.version.c_str());
		}
		else
		{
			reshade::log::message(reshade::log::level::warning,
				"[sherbet-update] 부팅 성공 마커 삭제 실패 (err=%lu) — 다시 시도합니다", GetLastError());
		}
	}

	ReleaseMutex(mutex);
	CloseHandle(mutex);

	if (settled)
		done.store(true, std::memory_order_release);
}

const std::string &sherbet::update::rolled_back_version()
{
	return g_bad_ver;
}

const std::string &sherbet::update::rolled_back_sha()
{
	return g_bad_sha;
}

bool sherbet::update::safe_mode()
{
	return g_safe_mode;
}
