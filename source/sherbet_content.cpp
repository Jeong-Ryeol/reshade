/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "sherbet_content.hpp"
#include "sherbet_http.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	constexpr wchar_t kHost[] = L"wonryeol.asuscomm.com";
	constexpr wchar_t kContentPath[] = L"/sherbet-auth/content/me";
	const std::string kFilePathPrefix = "/sherbet-auth/content/file/";

	std::filesystem::path cache_path(const std::string &config_dir_utf8)
	{
		return std::filesystem::u8path(config_dir_utf8) / L"sherbet.themes";
	}
}

bool sherbet::content::fetch(const std::string &bearer, const std::string &config_dir_utf8, std::string &out_body)
{
	std::string resp;
	const int status = sherbet::http::get(kHost, kContentPath, resp, bearer.empty() ? nullptr : bearer.c_str());
	if (status != 200 || resp.empty())
		return false;
	out_body = resp;
	std::ofstream out(cache_path(config_dir_utf8), std::ios::trunc | std::ios::binary);
	if (out.is_open())
		out << resp;
	return true;
}

bool sherbet::content::load_cached(const std::string &config_dir_utf8, std::string &out_body)
{
	std::ifstream in(cache_path(config_dir_utf8), std::ios::binary);
	if (!in.is_open())
		return false;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (text.empty())
		return false;
	out_body = std::move(text);
	return true;
}

bool sherbet::content::fetch_file(const std::string &bearer, const std::string &item_id, const std::string &dest_path_utf8)
{
	const std::string path = kFilePathPrefix + item_id;      // item_id 는 서버 매니페스트의 불투명 키(ASCII)
	const std::wstring wpath(path.begin(), path.end());
	std::string resp;
	const int status = sherbet::http::get(kHost, wpath.c_str(), resp, bearer.empty() ? nullptr : bearer.c_str());
	if (status != 200 || resp.empty())
		return false; // 절단/오류 응답은 http::get 이 status!=200 로 신호 → 여기서 저장 안 함
	const std::filesystem::path dest = std::filesystem::u8path(dest_path_utf8);
	std::error_code ec;
	std::filesystem::create_directories(dest.parent_path(), ec); // 실패해도 아래 open 에서 재판정

	// 임시파일에 먼저 쓰고, 완전히 성공했을 때만 최종 이름으로 rename 한다. 이렇게 하면
	// 중간에 쓰기가 깨져도 잘린 파일이 최종 이름(예: 지상샤픈.fx)으로 남지 않는다 —
	// 잘린 셰이더가 남으면 다음 불러오기 때 '이미 있음'으로 건너뛰어 계속 컴파일 실패/크래시.
	const std::filesystem::path tmp = dest.parent_path() / (dest.filename().wstring() + L".part");
	{
		std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
		if (!out.is_open())
			return false;
		out.write(resp.data(), static_cast<std::streamsize>(resp.size()));
		out.flush();
		if (!out.good()) // 디스크 쓰기 실패 → 임시파일 버리고 실패 반환
		{
			out.close();
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
	std::filesystem::remove(dest, ec);          // 기존 파일 있으면 교체 위해 제거(rename 이식성)
	std::filesystem::rename(tmp, dest, ec);
	if (ec)
	{
		std::filesystem::remove(tmp, ec);
		return false;
	}
	return true;
}
