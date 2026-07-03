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

		// 상태 코드
		DWORD status = 0, slen = sizeof(status);
		HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &slen, nullptr);

		// 바디 읽기(루프)
		char buf[2048];
		DWORD read = 0;
		while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
			out.append(buf, read);

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
