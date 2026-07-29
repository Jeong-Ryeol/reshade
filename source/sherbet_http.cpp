/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_http.hpp"
#include <chrono>
#include <Windows.h>
#include <WinInet.h>

namespace
{
	struct scoped_handle
	{
		HINTERNET h;
		scoped_handle(HINTERNET x) : h(x) {}
		~scoped_handle() { if (h) InternetCloseHandle(h); }
		operator HINTERNET() const { return h; }
	};

	// 바디 싱크. 문자열 수집과 파일 스트리밍이 **같은 절단 탐지 루프**를 쓰게 하는 유일한 이유다.
	// write 가 false 를 반환하면(디스크 오류/상한 초과/취소/시간 초과) 절단과 동일하게 취급한다.
	// total_hint = Content-Length(없으면 0).
	struct body_sink
	{
		void *ctx;
		bool (*write)(void *ctx, const char *data, DWORD len, unsigned long long total_hint);
	};

	constexpr DWORD kDefaultTimeoutMs = 5000;   // 5초 — 수십 KB fx 기준(기존 동작)

	// 반환 = HTTP 상태코드(0 = 전송 실패 또는 상태코드 판독 불가).
	// truncated 는 **바디가 불완전할 때만** true 가 된다. 전송 이전 단계의 실패(핸들 생성/전송 실패)는
	// truncated 를 건드리지 않고 0 만 반환한다 — 기존 request() 가 그 경우 out 을 비우지 않았기 때문에
	// (예: 상태코드 판독 실패 시 바디는 그대로 두고 0 반환) 이 구분을 없애면 호출자 동작이 바뀐다.
	int request_core(const wchar_t *verb, const wchar_t *host, const wchar_t *path,
		const std::string *body, const char *content_type, const char *bearer,
		DWORD timeout_ms, const body_sink &sink, bool &truncated)
	{
		const scoped_handle session = InternetOpenW(L"Sherbet", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!session) return 0;

		const scoped_handle conn = InternetConnectW(session, host, INTERNET_DEFAULT_HTTPS_PORT,
			nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
		if (!conn) return 0;

		const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
		const scoped_handle req = HttpOpenRequestW(conn, verb, path, nullptr, nullptr, nullptr, flags, 0);
		if (!req) return 0;

		DWORD timeout = timeout_ms;
		InternetSetOptionW(req, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionW(req, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionW(req, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

		std::string headers;
		if (content_type) { headers += "Content-Type: "; headers += content_type; headers += "\r\n"; }
		if (bearer) { headers += "Authorization: Bearer "; headers += bearer; headers += "\r\n"; }

		const void *body_ptr = body ? body->data() : nullptr;
		const DWORD body_len = body ? static_cast<DWORD>(body->size()) : 0;

		std::wstring wheaders(headers.begin(), headers.end());
		if (!HttpSendRequestW(req, headers.empty() ? nullptr : wheaders.c_str(),
			static_cast<DWORD>(wheaders.size()), const_cast<void *>(body_ptr), body_len))
			return 0;

		// 상태 코드. HttpSendRequestW 성공 후 HttpQueryInfoW가 실패하면 status는 0으로 유지됨.
		// WinInet의 상태라인 조회는 응답 수신 후 거의 항상 성공하지만, 실패 시 의도적으로
		// status=0으로 둬서 호출자가 "서버 사용 불가"로 처리하게 함(오프라인 은폐 경로로 라우팅됨, 잘못된 인증 절대 없음).
		DWORD status = 0, slen = sizeof(status);
		HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, nullptr);

		// 기대 바디 길이(Content-Length). 있으면 아래에서 수신량과 대조해 절단을 잡는다.
		// **없는 것은 실패가 아니다** — chunked 응답(GitHub 리다이렉트 종착지)이 정상적으로 존재한다.
		DWORD content_length = 0, clen = sizeof(content_length);
		const bool have_len = HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
			&content_length, &clen, nullptr) != FALSE;

		// 바디 읽기. InternetReadFile이 루프 중간에 false를 반환하면(네트워크 오류/타임아웃) 이는 절단이므로
		// EOF로 눙치지 않고 실패로 처리한다. 절단된 바디를 성공(200)으로 넘기면 잘린 셰이더 파일이 디스크에
		// 남아 컴파일 실패·드라이버(dxgi) 크래시로 이어진다 — 여러 파일을 연속 다운로드할 때 특히 잘 터진다.
		char buf[2048];
		DWORD read = 0;
		unsigned long long received = 0;
		for (;;)
		{
			if (!InternetReadFile(req, buf, sizeof(buf), &read)) { truncated = true; break; } // 중간 오류 = 절단
			if (read == 0) break;                                                              // 정상 EOF
			received += read;
			if (!sink.write(sink.ctx, buf, read, have_len ? content_length : 0ull)) { truncated = true; break; }
		}
		if (have_len && received != content_length)
			truncated = true; // 선언 길이와 실제 수신량 불일치 = 절단

		return static_cast<int>(status);
	}

	// 문자열 싱크 — 기존 out.append(buf, read) 그대로. 실패하지 않는다.
	bool string_sink_write(void *ctx, const char *data, DWORD len, unsigned long long)
	{
		static_cast<std::string *>(ctx)->append(data, len);
		return true;
	}

	int request(const wchar_t *verb, const wchar_t *host, const wchar_t *path,
		const std::string *body, const char *content_type, std::string &out, const char *bearer)
	{
		out.clear();

		const body_sink sink = { &out, &string_sink_write };
		bool truncated = false;
		const int status = request_core(verb, host, path, body, content_type, bearer,
			kDefaultTimeoutMs, sink, truncated);

		if (truncated)
		{
			out.clear();
			return 0; // 호출자는 status!=200 로 보고 실패 처리 → 잘린 파일을 저장하지 않는다
		}

		return status;
	}

	// ── 다운로드 전용(get_to_file) ─────────────────────────────────────────────────────
	constexpr unsigned long long kMaxDownloadBytes = 32ull * 1024 * 1024; // 32MiB 하드 상한
	constexpr DWORD kDownloadTimeoutMs = 20000;                           // 20초(연결/송신/수신)
	constexpr std::chrono::seconds kDownloadDeadline(300);                // 전체 5분 캡

	struct file_sink_state
	{
		HANDLE file;                        // 소유하지 않는다 — get_to_file 이 닫는다
		unsigned long long received;
		unsigned long long expect_size;     // 매니페스트 선언 크기(0 = 검사 안 함)
		const std::atomic<bool> *cancel;    // nullptr 가능
		void *ctx;
		void (*on_chunk)(void *, unsigned long long, unsigned long long); // nullptr 가능
		std::chrono::steady_clock::time_point start;
	};

	bool file_sink_write(void *ctx, const char *data, DWORD len, unsigned long long total_hint)
	{
		file_sink_state *const s = static_cast<file_sink_state *>(ctx);

		if (s->cancel != nullptr && s->cancel->load())
			return false; // 취소 — 호출자가 dest 를 지운다
		if (std::chrono::steady_clock::now() - s->start > kDownloadDeadline)
			return false; // 전체 5분 캡(수신 타임아웃에 걸리지 않을 만큼 느린 연결)

		s->received += len;
		// ★ 상한은 **루프 안에서** 검사한다. 루프가 끝난 뒤에 보면 이미 다 받은 뒤라 의미가 없다.
		if (s->received > kMaxDownloadBytes)
			return false;
		if (s->expect_size != 0 && s->received > s->expect_size)
			return false; // 선언보다 큰 응답은 우리 DLL 일 수 없다(대개 HTML 에러 페이지)

		DWORD written = 0;
		if (!WriteFile(s->file, data, len, &written, nullptr) || written != len)
			return false; // 디스크 가득/쓰기 오류

		if (s->on_chunk != nullptr)
		{
			const unsigned long long total = total_hint != 0 ? total_hint : s->expect_size;
			s->on_chunk(s->ctx, s->received, total);
		}
		return true;
	}
}

int sherbet::http::post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer)
{
	return request(L"POST", host, path, &json_body, "application/json", out, bearer);
}

int sherbet::http::get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer)
{
	return request(L"GET", host, path_query, nullptr, nullptr, out, bearer);
}

int sherbet::http::get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest,
	unsigned long long expect_size,
	void *ctx, void (*on_chunk)(void *, unsigned long long, unsigned long long),
	const std::atomic<bool> *cancel)
{
	if (dest == nullptr)
		return 0;
	if (cancel != nullptr && cancel->load())
		return 0; // 시작 전 취소 — 파일을 아예 만들지 않는다

	// 공유 금지(dwShareMode=0): 다른 창의 워커가 같은 .part 를 동시에 자르는 것을 OS 가 막는다.
	const HANDLE file = CreateFileW(dest, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return 0;

	file_sink_state state = {};
	state.file = file;
	state.received = 0;
	state.expect_size = expect_size;
	state.cancel = cancel;
	state.ctx = ctx;
	state.on_chunk = on_chunk;
	state.start = std::chrono::steady_clock::now();

	const body_sink sink = { &state, &file_sink_write };
	bool truncated = false;
	const int status = request_core(L"GET", host, path, nullptr, nullptr, nullptr,
		kDownloadTimeoutMs, sink, truncated);

	// 선언 크기와 정확히 일치하지 않으면 실패로 본다(expect_size==0 이면 검사 안 함).
	// Content-Length 가 없어도(chunked) 이 검사는 살아 있다 — 다만 최종 게이트는 호출자의 sha256 이다.
	const bool size_ok = (expect_size == 0) || (state.received == expect_size);

	// 핸들을 먼저 닫는다 — 열린 채로는 DeleteFileW 가 실패해 부분 파일이 남는다.
	CloseHandle(file);

	if (truncated || status != 200 || !size_ok)
	{
		DeleteFileW(dest); // 어떤 실패 경로에서도 부분 파일을 남기지 않는다
		return (truncated || !size_ok) ? 0 : status;
	}

	return status;
}
