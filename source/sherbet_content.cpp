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
