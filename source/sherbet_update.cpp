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
#include "sherbet_owner.h"         // SHERBET_VERSION — 게이트1 / has_owner
#include "sherbet_http.hpp"        // 매니페스트 페치 · 스트리밍 다운로드(컨트롤러 전용)
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

	// ── 매니페스트 엔드포인트(스펙 §3.1) ───────────────────────────────────
	// ⚠️ **미인증이다(bearer = nullptr).** 인증을 요구하면 "로그인을 망가뜨린 빌드" 를
	// 영원히 고칠 수 없는 데드락이 생긴다 — 업데이트가 가장 필요한 순간에 못 받는다.
	// `/auth/verify` 는 한 줄도 건드리지 않는다: 업데이트 실패는 회복 가능하지만
	// 로그인 실패는 제품이 죽는다.
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr const char *kManifestPath = "/sherbet-auth/update/manifest";
#ifdef _WIN64
	constexpr const char *kArch = "x64";
#else
	constexpr const char *kArch = "x86";
#endif

	// ── 상태 문구(UTF-8) ───────────────────────────────────────────────────
	// ⚠️ `\xNN` 뒤에 16진수 ASCII 가 바로 오면 MSVC C7744 다(커밋 c966d30e 에서 겪음).
	// 아래 문자열은 전부 이스케이프 뒤에 공백이거나 또 다른 이스케이프라 안전하다.
	constexpr const char *kStatusChecking = "\xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xED\x99\x95\xEC\x9D\xB8 \xEC\xA4\x91\xE2\x80\xA6"; // "업데이트 확인 중…"
	constexpr const char *kStatusPersonal = "\xEA\xB0\x9C\xEC\x9D\xB8 \xEB\xB9\x8C\xEB\x93\x9C\xEB\x8A\x94 \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xEB\xAC\xB8\xEC\x9D\x98\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "개인 빌드는 디스코드로 문의해 주세요"
	constexpr const char *kStatusBusyOther = "\xEB\x8B\xA4\xEB\xA5\xB8 \xEA\xB2\x8C\xEC\x9E\x84 \xEC\xB0\xBD\xEC\x97\x90\xEC\x84\x9C \xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\xA4\x91\xEC\x9D\xB4\xEC\x97\x90\xEC\x9A\x94"; // "다른 게임 창에서 업데이트 중이에요"
	constexpr const char *kStatusPrepare = "\xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\xA4\x80\xEB\xB9\x84 \xEC\xA4\x91\xE2\x80\xA6"; // "업데이트 준비 중…"
	constexpr const char *kStatusRemote = "\xEB\x84\xA4\xED\x8A\xB8\xEC\x9B\x8C\xED\x81\xAC \xEB\x93\x9C\xEB\x9D\xBC\xEC\x9D\xB4\xEB\xB8\x8C\xEC\x97\x90\xEB\x8A\x94 \xEC\x84\xA4\xEC\xB9\x98\xED\x95\xA0 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94"; // "네트워크 드라이브에는 설치할 수 없어요"
	constexpr const char *kStatusNoWrite = "\xED\x8F\xB4\xEB\x8D\x94\xEC\x97\x90 \xEC\x93\xB8 \xEC\x88\x98 \xEC\x97\x86\xEC\x96\xB4\xEC\x9A\x94"; // "폴더에 쓸 수 없어요"
	constexpr const char *kStatusNoSpace = "\xEB\x94\x94\xEC\x8A\xA4\xED\x81\xAC \xEA\xB3\xB5\xEA\xB0\x84\xEC\x9D\xB4 \xEB\xB6\x80\xEC\xA1\xB1\xED\x95\xB4\xEC\x9A\x94"; // "디스크 공간이 부족해요"
	constexpr const char *kStatusDownloading = "\xEB\x8B\xA4\xEC\x9A\xB4\xEB\xA1\x9C\xEB\x93\x9C \xEC\xA4\x91\xE2\x80\xA6"; // "다운로드 중…"
	constexpr const char *kStatusVerifying = "\xED\x99\x95\xEC\x9D\xB8 \xEC\xA4\x91\xE2\x80\xA6"; // "확인 중…"
	constexpr const char *kStatusBadFile = "\xED\x8C\x8C\xEC\x9D\xBC\xEC\x9D\xB4 \xEC\x86\x90\xEC\x83\x81\xEB\x90\x90\xEC\x96\xB4\xEC\x9A\x94 \xE2\x80\x94 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84\xED\x95\xB4 \xEC\xA3\xBC\xEC\x84\xB8\xEC\x9A\x94"; // "파일이 손상됐어요 — 다시 시도해 주세요"
	constexpr const char *kStatusFailed = "\xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\x8B\xA4\xED\x8C\xA8 \xE2\x80\x94 \xEC\x9D\xB4\xEC\xA0\x84 \xEB\xB2\x84\xEC\xA0\x84 \xEA\xB7\xB8\xEB\x8C\x80\xEB\xA1\x9C\xEC\x98\x88\xEC\x9A\x94"; // "업데이트 실패 — 이전 버전 그대로예요"
	constexpr const char *kStatusCancelled = "\xEC\xB7\xA8\xEC\x86\x8C\xED\x96\x88\xEC\x96\xB4\xEC\x9A\x94"; // "취소했어요"
	constexpr const char *kStatusDone = "\xEC\x97\x85\xEB\x8D\xB0\xEC\x9D\xB4\xED\x8A\xB8 \xEC\x99\x84\xEB\xA3\x8C \xE2\x80\x94 \xEA\xB2\x8C\xEC\x9E\x84\xEC\x9D\x84 \xEA\xBB\x90\xEB\x8B\xA4 \xEC\xBC\x9C\xEB\xA9\xB4 \xEC\xA0\x81\xEC\x9A\xA9\xEB\x8F\xBC\xEC\x9A\x94"; // "업데이트 완료 — 게임을 껐다 켜면 적용돼요"
	constexpr const char *kStatusBadUrl = "\xEC\xA3\xBC\xEC\x86\x8C\xEA\xB0\x80 \xEC\x98\xAC\xEB\xB0\x94\xEB\xA5\xB4\xEC\xA7\x80 \xEC\x95\x8A\xEC\x95\x84\xEC\x9A\x94"; // "주소가 올바르지 않아요"
	constexpr const char *kStatusNetwork = "\xEC\x97\xB0\xEA\xB2\xB0 \xEC\x8B\xA4\xED\x8C\xA8 \xE2\x80\x94 \xEC\x9E\xA0\xEC\x8B\x9C \xEB\x92\xA4 \xEB\x8B\xA4\xEC\x8B\x9C \xEC\x8B\x9C\xEB\x8F\x84\xED\x95\xB4\xEC\x9A\x94"; // "연결 실패 — 잠시 뒤 다시 시도해요"

	// 매니페스트 size 상한과 같다(스펙 §3.4). 다운로드·해시 양쪽에서 같은 값을 쓴다.
	constexpr unsigned long long kMaxDownloadBytes = 32ull * 1024 * 1024;

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
		std::wstring dir;         // 마지막 구분자 포함(프리플라이트용)
		std::wstring newpart;     // <self>.sherbet-new.part — 검증 전 스트리밍 대상
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
		const std::wstring w_part   = utf8_to_wide(suffix_new_part());
		const std::wstring w_new    = utf8_to_wide(suffix_new());
		const std::wstring w_bak    = utf8_to_wide(suffix_bak());
		const std::wstring w_bakold = utf8_to_wide(suffix_bak_old());
		const std::wstring w_failed = utf8_to_wide(suffix_failed());
		const std::wstring w_marker = utf8_to_wide(marker_name());
		const std::wstring w_note   = utf8_to_wide(recovery_note_name());
		// 코어 헤더의 리터럴이 안 바뀌면 여기서 실패할 수 없다. 그래도 확인하는 이유:
		// 빈 문자열이 섞이면 self 나 폴더 자체를 가리키는 경로가 만들어져 엉뚱한 것을 옮긴다.
		if (w_part.empty() || w_new.empty() || w_bak.empty() || w_bakold.empty() || w_failed.empty() || w_marker.empty() || w_note.empty())
			return false;

		p.self        = self_path;
		p.dir         = dir;
		p.newpart     = self_path + w_part;
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

	bool file_size(const std::wstring &path, unsigned long long &out)
	{
		out = 0;
		const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			return false;
		LARGE_INTEGER size = {};
		const bool ok = GetFileSizeEx(f, &size) != 0 && size.QuadPart >= 0;
		CloseHandle(f);
		if (ok)
			out = static_cast<unsigned long long>(size.QuadPart);
		return ok;
	}

	// ⚠️ **매 호출이 새 sha256_init 으로 시작한다.**
	// `sha256_final_hex` 는 멱등하지 않다 — 패딩과 길이를 컨텍스트에 밀어 넣으므로
	// 같은 컨텍스트에 두 번째로 부르면 결과가 쓰레기다. S6(다운로드 검증)과
	// S12(교체 직후 재검증)가 컨텍스트를 공유하면 S12 가 **항상** 불일치를 내
	// 멀쩡한 업데이트를 매번 롤백한다. 그래서 컨텍스트를 밖으로 내보내지 않는다.
	bool hash_file(const std::wstring &path, std::string &out_hex)
	{
		out_hex.clear();
		const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (f == INVALID_HANDLE_VALUE)
			return false;

		sha256_ctx ctx;
		sha256_init(ctx); // ★ 항상 여기서 시작한다
		bool ok = true;
		unsigned long long total = 0;
		std::vector<unsigned char> buf(64 * 1024);
		for (;;)
		{
			DWORD read = 0;
			if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr))
			{
				ok = false;
				break;
			}
			if (read == 0)
				break; // EOF
			total += read;
			if (total > kMaxDownloadBytes) // 32MiB 상한 — 매니페스트 size 상한과 같다
			{
				ok = false;
				break;
			}
			sha256_update(ctx, buf.data(), read);
		}
		CloseHandle(f);
		if (ok)
			out_hex = sha256_final_hex(ctx);
		return ok;
	}

	// 스펙 §4.4 S6 — 검증 순서: 수신 크기 == size → sha256 == sha256 → pe_check(앞 4KB).
	// ⚠️ 이 셋은 **무조건** 돈다. WinInet 이 GitHub 리다이렉트를 자동 추종하므로
	//    url_allowed 가 피닝하는 것은 최초 URL 뿐이고, 실제로 도착한 바이트를 검증하는
	//    것은 sha256 과 pe_check 뿐이다.
	bool verify_staged(const std::wstring &path, const info &u)
	{
		unsigned long long size = 0;
		if (!file_size(path, size) || size != u.size)
			return false;
		std::string hex;
		if (!hash_file(path, hex) || hex != u.sha256)
			return false;
		// sha256 은 '바이트가 온전한가' 만 보고 '무엇인가' 는 못 본다. x64 슬롯에 32비트
		// DLL 을 넣는 운영 실수 한 번이면 다음 실행에 ERROR_BAD_EXE_FORMAT 으로
		// 롤백 코드조차 안 돈다.
		unsigned char head[kPeHeadBytes];
		std::size_t len = 0;
		if (!read_head(path, head, sizeof(head), len))
			return false;
		return pe_check(head, len, kWantX64);
	}

	// 스펙 §4.4 S3 프리플라이트. 실패 사유를 고객이 읽을 문구로 돌려준다(nullptr = 통과).
	// ⚠️ **UAC 승격은 절대 하지 않는다** — 게임 오버레이발 UAC 프롬프트는 그 자체가 악성 신호다.
	const char *preflight(const paths &p, unsigned long long size)
	{
		// 네트워크 경로면 중단. 크로스 볼륨 MoveFile 은 copy+delete 로 강등되어
		// 사용 중인 파일에서 실패한다.
		if (p.dir.size() >= 2 && p.dir[0] == L'\\' && p.dir[1] == L'\\')
			return kStatusRemote; // UNC
		std::wstring root = p.dir;
		if (p.dir.size() >= 3 && p.dir[1] == L':')
			root = p.dir.substr(0, 3); // "C:\" — GetDriveTypeW 는 루트를 원한다
		if (GetDriveTypeW(root.c_str()) == DRIVE_REMOTE)
			return kStatusRemote;

		// 쓰기 실측. 속성만 보고 판단하지 않는다 — 실제로 만들어 봐야 안다.
		// FILE_FLAG_DELETE_ON_CLOSE 라 핸들이 닫히는 순간(크래시 포함) 사라진다.
		const std::wstring probe = p.self + L".sherbet-probe";
		DeleteFileW(probe.c_str()); // 전원차단으로 남았을 수 있는 묵은 프로브를 치운다
		const HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return kStatusNoWrite;
		CloseHandle(h);

		// size*3 + 32MiB (.part + .sherbet-new + .sherbet-bak 이 동시에 존재할 수 있다).
		// 조회 실패는 중단 사유가 아니다 — 가상 볼륨에서 실패할 수 있고, 공간이 정말
		// 없으면 아래 다운로드가 어차피 실패한다.
		ULARGE_INTEGER avail = {};
		if (GetDiskFreeSpaceExW(p.dir.c_str(), &avail, nullptr, nullptr))
		{
			const unsigned long long need = size * 3 + kMaxDownloadBytes;
			if (avail.QuadPart < need)
				return kStatusNoSpace;
		}
		return nullptr;
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

		// ⚠️ **안전모드는 게이트1 로 한 번 더 거른다.** R11 의 안전모드는 "지금 매핑된
		// 이미지가 마커가 비난하는 그 바이너리다" 일 때만 옳다.
		// 게이트1 없이 state 만 보면 롤백에 **성공한** 다음 부팅부터 영구히 켜진다:
		// 그 부팅의 self 는 되돌려진 멀쩡한 옛 바이너리인데 마커는 계속 rolledback 이고
		// (§5.3 이 rolledback 마커를 지우지 못하게 막는다 — 지우면 블랙리스트가 날아간다),
		// 그러면 고객이 돈 주고 산 효과가 통째로, 영원히 꺼진다.
		// 코어 헤더의 restore_from_new 주석이 같은 위험을 "방금 되살린 바이너리로 효과가
		// 통째로 꺼지고 '되돌렸습니다' 라는 거짓 배너가 뜬다" 로 못 박아 두었다.
		// ⚠️ 블랙리스트(위 두 줄)는 **게이트를 걸지 않는다** — 그건 상태가 아니라 사실이고,
		//    롤백 배너와 [그래도 다시 시도] 는 rolled_back_version() 이 비어 있지 않은지로
		//    판단해야 한다(안전모드로 판단하면 다음 부팅부터 배너가 사라져 블랙리스트를
		//    풀 방법이 없어진다).
		g_safe_mode = (m.state == "rolledback" || m.state == "rollback_failed") &&
			m.version == SHERBET_VERSION;
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

// ══════════════════════════════════════════════════════════════════════════
//  컨트롤러 — 매니페스트 페치 + 교체 오케스트레이션 (스펙 §3 / §6)
// ══════════════════════════════════════════════════════════════════════════

sherbet::update::controller &sherbet::update::instance()
{
	// 함수 지역 static — C++11 이 스레드 안전 초기화를 보장한다.
	// ⚠️ 소멸은 CRT 종료(사실상 DLL_PROCESS_DETACH) 시점이다. 두 경로 모두 안전하다:
	//  - 프로세스 종료: OS 가 다른 스레드를 **먼저** 끝내므로 join 이 즉시 돌아온다.
	//  - FreeLibrary: 소멸자가 _stop 과 _cancel 을 세우고, get_to_file 이 청크마다
	//    cancel 을 보므로 대기가 청크 하나(수 ms)로 줄어든다.
	static controller c;
	return c;
}

sherbet::update::controller::controller()
{
}

sherbet::update::controller::~controller()
{
	_stop = true;
	_cancel = true; // 다운로드 중이면 청크 단위로 즉시 빠져나온다
	join_worker();
	release_mutex();
}

void sherbet::update::controller::join_worker()
{
	// ⚠️ 이 함수는 std::thread 대입 **바로 앞**에서 반드시 불려야 한다.
	// 끝났지만 joinable 인 스레드에 재대입하면 std::terminate — 게임이 그 자리에서 죽는다.
	if (_worker.joinable())
		_worker.join();
}

void sherbet::update::controller::set_status(const char *s)
{
	const std::lock_guard<std::mutex> lk(_mtx);
	_status = (s != nullptr) ? s : "";
}

bool sherbet::update::controller::take_mutex()
{
	if (_mutex_handle != nullptr)
		return true; // 이미 보유 중
	if (!g_paths.ok)
		return false;
	const HANDLE h = CreateMutexW(nullptr, FALSE, g_paths.mutex_name.c_str());
	if (h == nullptr)
		return false;
	const DWORD wait = WaitForSingleObject(h, 0); // 즉시 시도 — 절대 대기하지 않는다
	// WAIT_ABANDONED 도 소유권 획득이다(이전 보유자가 죽었다). 반드시 해제해야 한다.
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
	{
		CloseHandle(h);
		return false;
	}
	_mutex_handle = h;
	return true;
}

void sherbet::update::controller::release_mutex()
{
	if (_mutex_handle == nullptr)
		return;
	const HANDLE h = static_cast<HANDLE>(_mutex_handle);
	_mutex_handle = nullptr;
	ReleaseMutex(h);
	CloseHandle(h);
}

void sherbet::update::controller::init(const std::wstring &self_path)
{
	const std::lock_guard<std::mutex> lk(_mtx);
	if (_inited.load())
		return;
	// on_process_attach 가 이미 채웠으면 **그 경로를 그대로 쓴다.** 다시 만들면
	// 부팅 경로와 교체 경로가 다른 파일을 볼 수 있다.
	if (!g_paths.ok && !make_paths(self_path, g_paths))
		return;
	// 블랙리스트를 한 번만 복사해 온다 — 워커가 전역을 읽지 않게 해서 경합을 없앤다.
	_bad_ver = g_bad_ver;
	_bad_sha = g_bad_sha;
	_inited = true;
}

bool sherbet::update::controller::personalized() const
{
	// 개인화 빌드 보호: 공용 릴리스가 각인·전용 프리셋을 지우고 노드락을 조용히 끄는 것을 막는다.
	return sherbet::has_owner() || SHERBET_NODELOCK != 0;
}

void sherbet::update::controller::tick()
{
	if (!_inited.load())
		return;

	// 끝난 워커 회수. std::thread 에 재대입하기 전에 반드시 join 되어 있어야 한다.
	if (_worker_done.load())
	{
		join_worker();
		_worker_done = false;
	}

	if (personalized())
	{
		if (!_fetch_started.exchange(true))
			set_status(kStatusPersonal);
		return;
	}

	if (_fetch_started.load() || _busy.load() || _worker.joinable())
		return;

	_fetch_started = true;
	_busy = true;
	set_status(kStatusChecking);

	std::string bad_ver, bad_sha;
	{
		const std::lock_guard<std::mutex> lk(_mtx);
		bad_ver = _bad_ver;
		bad_sha = _bad_sha;
	}

	join_worker(); // ★ 규약: std::thread 대입 바로 앞
	_worker = std::thread([this, bad_ver, bad_sha]() {
		run_fetch(bad_ver, bad_sha);
		if (!_has_offer.load())
			set_status(""); // 실패는 전부 **조용한** '업데이트 없음' 이다("확인 중…" 을 남기지 않는다)
		_busy = false;
		_worker_done = true; // ★ 반드시 마지막
	});
}

void sherbet::update::controller::run_fetch(const std::string &bad_ver, const std::string &bad_sha)
{
	// ⚠️ **모든 실패는 조용한 '업데이트 없음' 이다.** 이 경로는 인증 컨트롤러의 상태를
	// 한 비트도 건드리지 않는다 — 24시간 오프라인 유예에 영향이 없어야 한다.
	const std::wstring path = utf8_to_wide(
		std::string(kManifestPath) + "?arch=" + kArch + "&cur=" + SHERBET_VERSION);
	if (path.empty())
		return;

	info found;
	for (int attempt = 0; attempt < 3 && !_stop.load(); ++attempt)
	{
		if (attempt > 0)
		{
			// 게임 시작 직후엔 네트워크가 아직 안 붙어 있을 수 있다. 15초 / 60초 뒤 재시도.
			// 100ms 슬라이스로 나눠 소멸(_stop)에 빠르게 반응한다(auth 폴러와 같은 패턴).
			const int slices = (attempt == 1) ? 150 : 600;
			for (int i = 0; i < slices && !_stop.load(); ++i)
				Sleep(100);
			if (_stop.load())
				return;
		}

		std::string resp;
		// ⚠️ bearer 는 반드시 nullptr — 미인증 엔드포인트다(스펙 §3.1).
		const int status = sherbet::http::get(kHost, path.c_str(), resp, nullptr);
		if (status != 200)
			continue; // 503/전송실패 — 재시도

		// arch echo 불일치·schema 불일치·URL 접두사 위반 등은 전부 여기서 걸린다.
		// 서버가 규약을 어긴 응답을 준 것이므로 재시도해도 결과가 같다 — 즉시 포기.
		const info u = parse_manifest(resp, kArch);
		if (!u.ok)
			return;
		found = u;
		break;
	}
	if (!found.ok)
		return;

	if (!should_offer(SHERBET_VERSION, found, bad_ver, bad_sha))
		return; // 같은 버전 / 구버전 / 블랙리스트 — 조용히 없음

	// ⚠️ 반드시 네임스페이스로 한정한다 — 멤버 이름(controller::is_mandatory)이 가린다.
	const bool mandatory = sherbet::update::is_mandatory(SHERBET_VERSION, found);
	{
		const std::lock_guard<std::mutex> lk(_mtx);
		_offer = found;
		_status.clear();
	}
	_mandatory = mandatory;
	_has_offer = true;

	reshade::log::message(reshade::log::level::info,
		"[sherbet-update] \xEC\x83\x88 \xEB\xB2\x84\xEC\xA0\x84 %s (\xED\x98\x84\xEC\x9E\xAC %s)%s",
		found.version.c_str(), SHERBET_VERSION, mandatory ? " [mandatory]" : "");
}

void sherbet::update::controller::begin_update()
{
	if (!_inited.load())
		return;
	if (personalized())
	{
		set_status(kStatusPersonal); // 개인화 빌드는 절대 자기교체하지 않는다
		return;
	}
	if (_busy.load() || _need_restart.load())
		return; // 연타 방지 / 이미 교체 끝

	// 끝난 워커를 먼저 회수한다. 남아 있으면 아래 대입이 std::terminate 다.
	if (_worker_done.load())
	{
		join_worker();
		_worker_done = false;
	}
	if (_worker.joinable())
		return; // 아직 도는 워커가 있다(페치 재시도 대기 중일 수 있다)

	info u;
	{
		const std::lock_guard<std::mutex> lk(_mtx);
		u = _offer;
	}
	if (!u.ok || !_has_offer.load())
		return;

	_cancel = false;
	_recv = 0;
	_total = 0;
	_busy = true;
	set_status(kStatusPrepare);

	join_worker(); // ★ 규약: std::thread 대입 바로 앞
	_worker = std::thread([this, u]() {
		run_swap(u);
		_busy = false;
		_worker_done = true; // ★ 반드시 마지막
	});
}

void sherbet::update::controller::cancel()
{
	_cancel = true;
}

void sherbet::update::controller::dismiss_session()
{
	_dismissed = true;
}

void sherbet::update::controller::clear_blacklist()
{
	// 스펙 §5.4 R13 — [그래도 다시 시도].
	// 자동 롤백이 오탐이었을 때(교체 rename 이 그 순간 AV 에 거부된 경우 등) 고객이
	// 클릭 1회로 블랙리스트를 풀 수 있게 한다. 이 장치가 있어서 자동 판정을 더
	// 정교하게 만들 이유가 사라진다.
	if (!_inited.load() || _busy.load())
		return;

	// 마커를 지우면 state=rolledback 도 함께 사라져 다음 부팅에 안전모드가 안 켜진다.
	// 다른 창이 교체 중이면 손대지 않는다.
	if (!take_mutex())
	{
		set_status(kStatusBusyOther);
		return;
	}
	DeleteFileW(g_paths.marker.c_str());
	release_mutex();

	{
		const std::lock_guard<std::mutex> lk(_mtx);
		_bad_ver.clear();
		_bad_sha.clear();
	}
	// 전역은 렌더 스레드 전용이다(on_process_attach 이후로는 워커가 읽지 않는다).
	g_bad_ver.clear();
	g_bad_sha.clear();
	// ⚠️ g_safe_mode 는 그대로 둔다 — 지금 매핑된 이미지는 여전히 문제를 일으킨
	// 그 바이너리다. 이번 세션의 효과를 다시 켜는 것이 아니라 '다시 제안받기' 만 푼다.

	_dismissed = false;
	_has_offer = false;
	_fetch_started = false; // 다음 tick() 이 다시 페치한다
	set_status(kStatusChecking);
}

bool sherbet::update::controller::has_offer() const
{
	return _has_offer.load() && !_dismissed.load();
}

bool sherbet::update::controller::is_mandatory() const
{
	return _mandatory.load() && _has_offer.load();
}

std::string sherbet::update::controller::offer_version() const
{
	const std::lock_guard<std::mutex> lk(_mtx);
	return _offer.version; // ⚠️ 값 복사
}

std::string sherbet::update::controller::offer_notes() const
{
	const std::lock_guard<std::mutex> lk(_mtx);
	return _offer.notes;
}

std::string sherbet::update::controller::status_text() const
{
	const std::lock_guard<std::mutex> lk(_mtx);
	return _status;
}

float sherbet::update::controller::progress() const
{
	const unsigned long long total = _total.load();
	if (total == 0)
		return 0.0f;
	const unsigned long long recv = _recv.load();
	if (recv >= total)
		return 1.0f;
	return static_cast<float>(static_cast<double>(recv) / static_cast<double>(total));
}

bool sherbet::update::controller::busy() const
{
	return _busy.load();
}

bool sherbet::update::controller::need_restart() const
{
	return _need_restart.load();
}

// ══════════════════════════════════════════════════════════════════════════
//  자기교체 — 스펙 §4.4 S1~S14
//
//  ⚠️ self 가 없는 유일한 구간은 S10~S11 사이(1ms 미만)다. 프록시 이름이면 그동안
//     게임은 System32 의 진짜 dxgi.dll 로 폴백해 켜진다.
//  ⚠️ AV·안티치트 표면 금지(§4.5): 자식 프로세스 ❌ / CreateRemoteThread ❌ /
//     자기 이미지 VirtualProtect·WriteProcessMemory ❌ / 새 DLL 을 이 프로세스에
//     LoadLibrary ❌ / MOVEFILE_DELAY_UNTIL_REBOOT ❌ / UAC 승격 ❌.
// ══════════════════════════════════════════════════════════════════════════

void sherbet::update::controller::progress_cb(void *ctx, unsigned long long received, unsigned long long total)
{
	// ⚠️ 4MB 다운로드면 약 2000회 불린다(읽기 버퍼 2048바이트). 아토믹 저장 둘 말고는
	// 아무것도 하지 않는다 — 락도, 할당도, ImGui 도 금지다(워커 스레드다).
	controller *const self = static_cast<controller *>(ctx);
	if (self == nullptr)
		return;
	self->_recv.store(received, std::memory_order_relaxed);
	if (total != 0)
		self->_total.store(total, std::memory_order_relaxed);
}

// _stop / _cancel 에 빠르게 반응하는 대기. 워커 스레드 전용.
bool sherbet::update::controller::wait_ms(unsigned int ms)
{
	for (unsigned int i = 0; i < ms / 100 + 1; ++i)
	{
		if (_stop.load() || _cancel.load())
			return false;
		Sleep(100);
	}
	return !_stop.load() && !_cancel.load();
}

void sherbet::update::controller::run_swap(const info &u)
{
	if (!g_paths.ok)
	{
		set_status(kStatusFailed);
		return;
	}
	// S1 — 네임드 뮤텍스 즉시 획득. 대기하지 않는다.
	// 실패 = 다른 게임 창이 교체 중이다. 그 창은 S10~S11 사이에 self 를 잠깐 없앨 수
	// 있으므로, 여기서 파일을 건드리면 서로의 교체를 망친다.
	if (!take_mutex())
	{
		set_status(kStatusBusyOther);
		return;
	}
	do_swap(u); // ★ 여기서 어떻게 빠져나가도 아래 한 줄이 반드시 돈다
	release_mutex();
}

void sherbet::update::controller::do_swap(const info &u)
{
	const paths &p = g_paths;

	// S2 — URL 전체 접두사 피닝. parse_manifest 가 이미 걸렀지만 여기서 다시 본다:
	// info 를 조립하는 다른 경로가 생기는 날을 위한 심층 방어다(호스트 화이트리스트가
	// 아니라 **전체 접두사** 피닝이 홈서버 단독 침해 방어의 유일한 근거다).
	if (!u.ok || !url_allowed(u.url) || !is_sha256_hex(u.sha256) ||
		u.size < (1ull << 20) || u.size > kMaxDownloadBytes)
	{
		set_status(kStatusBadUrl);
		return;
	}
	std::string host, path;
	if (!split_https_url(u.url, host, path))
	{
		set_status(kStatusBadUrl);
		return;
	}
	const std::wstring whost = utf8_to_wide(host);
	const std::wstring wpath = utf8_to_wide(path);
	if (whost.empty() || wpath.empty())
	{
		set_status(kStatusBadUrl);
		return;
	}

	// ⚠️ 파싱 안 되는 마커가 이미 있으면 **교체를 시작하지 않는다**(시뮬레이터 run_swap 과 동일).
	// 여기서 state=swapping 을 덮어쓰면 다른 writer 의 모르는 키를 통째로 잃고, 안 쓰면
	// 중단 복구가 불가능하다. 둘 중 안전한 쪽은 '시작하지 않는다'(디스크 무변화)다.
	boot_marker m;
	{
		std::string text;
		if (read_file(p.marker, text, kMaxMarkerBytes) && !parse_marker(text, m))
		{
			set_status(kStatusFailed);
			return;
		}
	}

	// ⚠️ exe 이름을 모르면 교체하지 않는다. 마커의 exe 가 비면 부팅 경로의 게이트2 가
	// 영원히 어긋나 새 빌드가 깨져도 자동 롤백이 **한 번도 돌지 않는다.**
	// 롤백 안전망 없이 설치하느니 업데이트를 미루는 쪽이 낫다.
	if (p.exe_name.empty())
	{
		set_status(kStatusFailed);
		return;
	}

	// S3 — 프리플라이트
	if (const char *why = preflight(p, u.size))
	{
		set_status(why);
		return;
	}

	// S4 — 잔재 정리와 재개. `.sherbet-new` 가 이미 있고 sha 가 맞으면 S5~S7 을 건너뛴다.
	bool staged = false;
	if (file_exists(p.newf))
	{
		std::string hex;
		if (hash_file(p.newf, hex) && hex == u.sha256)
			staged = true;
		else
			DeleteFileW(p.newf.c_str());
	}
	DeleteFileW(p.newpart.c_str()); // 묵은 .part 는 항상 버린다(재개 근거가 될 수 없다)

	if (!staged)
	{
		// S5 — 스트리밍 다운로드. bearer 는 없다(릴리스 자산은 공개다).
		_total.store(u.size);
		_recv.store(0);
		set_status(kStatusDownloading);

		bool got = false;
		for (int attempt = 0; attempt < 4 && !_stop.load(); ++attempt)
		{
			if (attempt > 0)
			{
				const unsigned int backoff[3] = { 200, 600, 1200 };
				if (!wait_ms(backoff[attempt - 1]))
					break;
			}
			const int st = sherbet::http::get_to_file(whost.c_str(), wpath.c_str(), p.newpart.c_str(),
				u.size, this, &progress_cb, &_cancel);
			if (_cancel.load() || _stop.load())
				break;
			if (st == 200)
			{
				got = true;
				break;
			}
		}
		if (_cancel.load())
		{
			DeleteFileW(p.newpart.c_str()); // get_to_file 이 이미 지웠지만 확실히 한다
			set_status(kStatusCancelled);
			return;
		}
		if (!got)
		{
			DeleteFileW(p.newpart.c_str());
			set_status(kStatusNetwork);
			return;
		}

		// S6 — 검증. 순서: 수신 크기 == size → sha256 → pe_check(앞 4KB).
		// ⚠️ 해시 불일치는 **일시적 오류가 아니다.** 재시도하지 않는다 — 같은 URL 이
		//    같은 잘못된 바이트를 줄 뿐이고, 재시도는 '언젠가 통과할' 확률만 만든다.
		set_status(kStatusVerifying);
		if (!verify_staged(p.newpart, u))
		{
			DeleteFileW(p.newpart.c_str());
			set_status(kStatusBadFile);
			return;
		}

		// S7 — 안전 정지 상태. 여기까지는 원본이 전혀 손상되지 않는다.
		// (REPLACE_EXISTING 은 우리 스테이징 파일에만 쓴다 — S10 의 self→.bak 과 다르다.)
		if (!MoveFileExW(p.newpart.c_str(), p.newf.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			DeleteFileW(p.newpart.c_str());
			set_status(kStatusFailed);
			return;
		}
	}

	// 여기부터는 취소를 받지 않는다 — S8~S13 은 수 ms 이고, 중간에 멈추면 오히려 위험하다.
	if (_cancel.load())
	{
		DeleteFileW(p.newf.c_str());
		set_status(kStatusCancelled);
		return;
	}

	// S8 — 묵은 `.sherbet-bak` 제거. READONLY 를 풀고 5회 재시도, 최종 실패면 `.bak.old` 로 밀어낸다.
	// ⚠️ 이 정리가 없으면 S10 의 rename(dwFlags=0)이 실패하거나, REPLACE_EXISTING 을 붙이고 싶어진다.
	//    묵은 `.bak` 을 살려 두면 '`.sherbet-bak` 은 마커가 비난하는 바이너리를 담지 않는다' 는
	//    성질이 무너져 restore_from_bak 이 불량 바이너리를 self 에 앉히게 된다.
	if (file_exists(p.bak))
	{
		SetFileAttributesW(p.bak.c_str(), FILE_ATTRIBUTE_NORMAL);
		for (int i = 0; i < 5; ++i)
		{
			if (DeleteFileW(p.bak.c_str()) || !file_exists(p.bak))
				break;
			Sleep(50);
		}
		if (file_exists(p.bak))
		{
			// ⚠️ 여기서는 REPLACE_EXISTING 이 **맞다**(S10 과 다르다).
			// 먼저 지우고 옮기면 밀어내기가 거부됐을 때 `.sherbet-bak.old` 를 이유 없이
			// 없앤 셈이 된다 — 잔재처럼 보이지만 한때 동작하던 진짜 바이너리이고,
			// self/.new/.bak 이 전부 사라진 디렉터리에 남은 마지막 사본일 수 있다.
			// 원자 교체면 실패해도 잃는 것이 없다. (시뮬레이터의 rm+mv 와 같은 순효과)
			MoveFileExW(p.bak.c_str(), p.bakold.c_str(), MOVEFILE_REPLACE_EXISTING); // 다음 시작의 정리 대상
		}
	}
	// S8 이 끝나고도 `.bak` 이 남아 있으면 S10 이 어차피 실패한다. **self 를 밀어내기 전에**
	// 멈춘다 — 디스크는 원본 그대로이고 `.sherbet-new` 만 남는다(다음 시작이 치운다).
	if (file_exists(p.bak))
	{
		set_status(kStatusFailed);
		return;
	}

	// S9 — 복구안내를 **먼저** 쓴다. 아래 S10 부터 self 가 사라지고, 그 구간에서 전원이
	// 나가면 고객에게 남는 단서는 이 파일 하나다. 문구는 make_recovery_note 하나에서만
	// 나오고 **실제 파일명**이 들어간다(고객이 프록시를 d3d11.dll 로 넣었을 수 있다).
	write_recovery_note(p);

	// ⚠️ tries 는 **물려받는다**(스펙 S9 는 tries 를 건드리지 않는다). 직전 롤백 마커면
	// 여기서 tries=2 가 그대로 따라오므로 아래 S13 의 tries=0 리셋이 필수다.
	// 모르는 키(unknown)도 위에서 읽은 마커 그대로 보존된다.
	m.state   = "swapping";
	m.version = u.version;
	m.prev    = SHERBET_VERSION;
	m.bak     = p.bak_name;
	m.exe     = p.exe_name; // ★ 부팅 경로 게이트2 가 읽는 값 — 같은 함수로 만든다
	m.sha     = u.sha256;
	if (!write_marker(p, m))
	{
		// 마커를 못 쓰면 중단 복구가 불가능해진다. self 를 건드리기 전에 멈춘다.
		set_status(kStatusFailed);
		return;
	}

	// S10 ★ self 가 사라지는 유일한 구간의 시작.
	// ⚠️ **dwFlags=0.** REPLACE_EXISTING 을 붙이면 묵은 `.bak`(= 롤백 재료)을 덮어쓴다.
	// ⚠️ MOVEFILE_DELAY_UNTIL_REBOOT 절대 금지 — PendingFileRenameOperations 는
	//    Defender 가 아는 악성 지표다.
	if (!MoveFileExW(p.self.c_str(), p.bak.c_str(), 0))
	{
		// 아직 아무것도 안 옮겼으므로 되돌릴 것도 없다. 마커는 swapping 인 채로 남고
		// 다음 시작의 startup_repair 가 staged → promote_pending 으로 닫는다(낡지만 무해).
		reshade::log::message(reshade::log::level::error,
			"[sherbet-update] S10 self -> .sherbet-bak \xEA\xB1\xB0\xEB\xB6\x80 (err=%lu)", GetLastError());
		set_status(kStatusFailed);
		return;
	}

	// S11 ★ 설치
	if (!MoveFileExW(p.newf.c_str(), p.self.c_str(), 0))
	{
		// 스펙 S11: "실패면 즉시 .bak → self 원복 5회". 같은 세션에서 도는 보상
		// 트랜잭션이다 — 이미지는 이미 매핑돼 있어 self 파일이 없어도 이 코드는 계속 돈다.
		// 이 원복이 없으면 다음 실행에 우리 DLL 이 **로드조차 되지 않아** 복구가 영영 안 돈다.
		bool undone = false;
		for (int i = 0; i < 5; ++i)
		{
			if (MoveFileExW(p.bak.c_str(), p.self.c_str(), 0))
			{
				undone = true;
				break;
			}
			Sleep(50);
		}
		reshade::log::message(reshade::log::level::error,
			"[sherbet-update] S11 .sherbet-new -> self \xEA\xB1\xB0\xEB\xB6\x80 — \xEC\x9B\x90\xEB\xB3\xB5 %s",
			undone ? "ok" : "FAILED");
		set_status(kStatusFailed);
		return;
	}

	// S12 — 사후검증. 이제 새 self 는 이 프로세스에 매핑돼 있지 않으므로 다시 읽어 해시할 수
	// 있다. AV 격리·변조를 여기서 잡는다.
	// ⚠️ hash_file 은 **매 호출 새 sha256_init** 으로 시작한다. S6 의 컨텍스트를 재사용하면
	//    sha256_final_hex 가 멱등하지 않아 여기서 **항상** 불일치가 나고, 멀쩡한 업데이트가
	//    매번 롤백된다.
	std::string post_hex;
	if (!hash_file(p.self, post_hex) || post_hex != u.sha256)
	{
		reshade::log::message(reshade::log::level::error,
			"[sherbet-update] S12 \xEC\x82\xAC\xED\x9B\x84\xEA\xB2\x80\xEC\xA6\x9D \xEB\xB6\x88\xEC\x9D\xBC\xEC\xB9\x98 — \xEC\xA6\x89\xEC\x8B\x9C \xEB\xA1\xA4\xEB\xB0\xB1");
		// S13 없이 곧바로 롤백한다. 이것이 **마커가 아직 swapping 인 채로 롤백이 시작되는
		// 유일한 경로**이고, 절차는 부팅 경로의 R8~R10 과 완전히 같다(같은 함수를 부른다).
		rollback_phase(p, m);
		{
			// 이번 세션의 재제안을 막는다. 전역(g_bad_*)은 렌더 스레드 소유라 건드리지 않고,
			// 디스크의 마커가 다음 부팅에 그 역할을 한다.
			const std::lock_guard<std::mutex> lk(_mtx);
			_bad_ver = m.bad_ver;
			_bad_sha = m.bad_sha;
		}
		_has_offer = false;
		set_status(kStatusFailed);
		return;
	}

	// S13 — 확정. ⚠️ **tries=0 을 반드시 함께 쓴다.**
	// 직전 롤백 마커의 tries=2 를 S9 가 물려받았으므로, 여기서 0 으로 되돌리지 않으면
	// 새로 깐 멀쩡한 빌드가 **첫 부팅에** 곧바로 롤백된다(decide_boot: 2+1 >= 2).
	m.state = "pending";
	m.tries = 0;
	write_marker(p, m);
	DeleteFileW(p.note.c_str()); // self 가 돌아왔으므로 복구안내는 더 필요 없다

	// S14
	_has_offer = false;
	_need_restart = true;
	set_status(kStatusDone);
	reshade::log::message(reshade::log::level::info,
		"[sherbet-update] \xEA\xB5\x90\xEC\xB2\xB4 \xEC\x99\x84\xEB\xA3\x8C: %s -> %s", SHERBET_VERSION, u.version.c_str());
}
