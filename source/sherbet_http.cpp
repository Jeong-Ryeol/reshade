/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_http.hpp"
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

	int request(const wchar_t *verb, const wchar_t *host, const wchar_t *path,
		const std::string *body, const char *content_type, std::string &out, const char *bearer)
	{
		out.clear();
		const scoped_handle session = InternetOpenW(L"Sherbet", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!session) return 0;

		const scoped_handle conn = InternetConnectW(session, host, INTERNET_DEFAULT_HTTPS_PORT,
			nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0);
		if (!conn) return 0;

		const DWORD flags = INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
		const scoped_handle req = HttpOpenRequestW(conn, verb, path, nullptr, nullptr, nullptr, flags, 0);
		if (!req) return 0;

		DWORD timeout = 5000; // 5초
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
		DWORD content_length = 0, clen = sizeof(content_length);
		const bool have_len = HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
			&content_length, &clen, nullptr) != FALSE;

		// 바디 읽기. InternetReadFile이 루프 중간에 false를 반환하면(네트워크 오류/타임아웃) 이는 절단이므로
		// EOF로 눙치지 않고 실패로 처리한다. 절단된 바디를 성공(200)으로 넘기면 잘린 셰이더 파일이 디스크에
		// 남아 컴파일 실패·드라이버(dxgi) 크래시로 이어진다 — 여러 파일을 연속 다운로드할 때 특히 잘 터진다.
		char buf[2048];
		DWORD read = 0;
		bool truncated = false;
		for (;;)
		{
			if (!InternetReadFile(req, buf, sizeof(buf), &read)) { truncated = true; break; } // 중간 오류 = 절단
			if (read == 0) break;                                                              // 정상 EOF
			out.append(buf, read);
		}
		if (have_len && out.size() != content_length)
			truncated = true; // 선언 길이와 실제 수신량 불일치 = 절단

		if (truncated)
		{
			out.clear();
			return 0; // 호출자는 status!=200 로 보고 실패 처리 → 잘린 파일을 저장하지 않는다
		}

		return static_cast<int>(status);
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
