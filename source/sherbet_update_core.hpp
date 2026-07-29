/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — 순수 로직(플랫폼 비의존).
// WinInet/스레드/ImGui/std::filesystem 은 여기 넣지 않는다. sherbet_update.cpp 가 글루를 맡는다.
// 판정 로직 100% 를 여기 몰아넣어 맥 clang 으로 실제 단위테스트한다(tools/sherbet_update_test.cpp).
#pragma once

#include <string>
#include <cstddef>
#include <cstdint>

namespace sherbet
{
	namespace update
	{
		struct version3 { unsigned major = 0, minor = 0, patch = 0; };

		// "major.minor.patch" 정확히 3필드. 공백·접두사·잔여물 전부 거부.
		// 실패하면 false 를 반환하고 out 을 건드리지 않는다(호출자가 '업데이트 없음' 으로 처리).
		inline bool parse_version(const std::string &s, version3 &out)
		{
			version3 v;
			unsigned *field[3] = { &v.major, &v.minor, &v.patch };
			std::size_t i = 0;
			for (int f = 0; f < 3; ++f)
			{
				if (f > 0)
				{
					if (i >= s.size() || s[i] != '.') return false;
					++i;
				}
				const std::size_t start = i;
				unsigned long long acc = 0;
				while (i < s.size() && s[i] >= '0' && s[i] <= '9')
				{
					acc = acc * 10 + static_cast<unsigned>(s[i] - '0');
					if (acc > 999999ULL) return false; // 거대 정수 거부(오버플로 방지)
					++i;
				}
				if (i == start) return false;          // 숫자가 하나도 없음
				*field[f] = static_cast<unsigned>(acc);
			}
			if (i != s.size()) return false;           // 뒤에 잔여물(공백 포함)
			out = v;
			return true;
		}

		// 정수 3필드 사전식. 문자열 비교를 쓰면 "1.10.0" < "1.9.0" 이 되므로 반드시 이걸 쓴다.
		inline int version_cmp(const version3 &a, const version3 &b)
		{
			if (a.major != b.major) return a.major < b.major ? -1 : 1;
			if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
			if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
			return 0;
		}

		// ── SHA-256 (FIPS 180-4) ───────────────────────────────────────────
		// bcrypt.lib 를 새로 링크하지 않는다: ReShade.vcxproj 에 AdditionalDependencies 가
		// 하나도 없어 링크 성공을 7분 CI 왕복으로만 확인할 수 있고, BCrypt 는 DllMain 에서
		// 호출하는 것이 안전하지 않다(CNG 공급자 테이블·레지스트리·힙). 벤더링하면
		// 링크 의존성 0 이고 맥에서 NIST 벡터로 검증된다.
		struct sha256_ctx
		{
			std::uint32_t h[8];
			std::uint64_t total;        // 지금까지 넣은 총 바이트 수
			unsigned char buf[64];
			std::size_t buflen;
		};

		namespace detail
		{
			inline std::uint32_t ror32(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

			inline const std::uint32_t *sha256_k()
			{
				static const std::uint32_t K[64] = {
					0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
					0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
					0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
					0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
					0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
					0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
					0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
					0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
				};
				return K;
			}

			inline void sha256_block(std::uint32_t h[8], const unsigned char p[64])
			{
				const std::uint32_t *K = sha256_k();
				std::uint32_t w[64];
				for (int i = 0; i < 16; ++i)
					w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
					       (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
					       (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
					        static_cast<std::uint32_t>(p[i * 4 + 3]);
				for (int i = 16; i < 64; ++i)
				{
					const std::uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^ (w[i - 15] >> 3);
					const std::uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^ (w[i - 2] >> 10);
					w[i] = w[i - 16] + s0 + w[i - 7] + s1;
				}
				std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
				std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
				for (int i = 0; i < 64; ++i)
				{
					const std::uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
					const std::uint32_t ch = (e & f) ^ (~e & g);
					const std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
					const std::uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
					const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
					const std::uint32_t t2 = S0 + maj;
					hh = g; g = f; f = e; e = d + t1;
					d = c; c = b; b = a; a = t1 + t2;
				}
				h[0] += a; h[1] += b; h[2] += c; h[3] += d;
				h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
			}
		}

		inline void sha256_init(sha256_ctx &c)
		{
			c.h[0] = 0x6a09e667u; c.h[1] = 0xbb67ae85u; c.h[2] = 0x3c6ef372u; c.h[3] = 0xa54ff53au;
			c.h[4] = 0x510e527fu; c.h[5] = 0x9b05688cu; c.h[6] = 0x1f83d9abu; c.h[7] = 0x5be0cd19u;
			c.total = 0;
			c.buflen = 0;
		}

		inline void sha256_update(sha256_ctx &c, const void *data, std::size_t n)
		{
			const unsigned char *p = static_cast<const unsigned char *>(data);
			c.total += n;
			while (n > 0)
			{
				const std::size_t space = 64 - c.buflen;
				const std::size_t take = (n < space) ? n : space;
				for (std::size_t i = 0; i < take; ++i) c.buf[c.buflen + i] = p[i];
				c.buflen += take; p += take; n -= take;
				if (c.buflen == 64) { detail::sha256_block(c.h, c.buf); c.buflen = 0; }
			}
		}

		inline std::string sha256_final_hex(sha256_ctx &c)
		{
			const std::uint64_t bits = c.total * 8;
			unsigned char pad = 0x80;
			sha256_update(c, &pad, 1);
			pad = 0x00;
			while (c.buflen != 56) sha256_update(c, &pad, 1); // total 이 늘지만 bits 는 위에서 확정됨
			unsigned char len[8];
			for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xff);
			sha256_update(c, len, 8);

			static const char *hex = "0123456789abcdef";
			std::string out;
			out.reserve(64);
			for (int i = 0; i < 8; ++i)
				for (int b = 3; b >= 0; --b)
				{
					const unsigned char byte = static_cast<unsigned char>((c.h[i] >> (b * 8)) & 0xff);
					out += hex[byte >> 4];
					out += hex[byte & 0x0f];
				}
			return out;
		}

		// 매니페스트의 sha256 필드 형식 검사 — 소문자 64hex 만 통과.
		inline bool is_sha256_hex(const std::string &s)
		{
			if (s.size() != 64) return false;
			for (char ch : s)
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
			return true;
		}

		// ⚠️ 리터럴 접두사. 변경하려면 스펙 §3.4 를 먼저 읽을 것.
		// 호스트만 화이트리스트하면 github.com 은 누구나 릴리스를 올릴 수 있는 멀티테넌트
		// 호스트라 방어가 되지 않는다. 전체 접두사 피닝이 홈서버 단독 침해 방어의 유일한 근거다.
		inline const char *update_url_prefix()
		{
			return "https://github.com/Jeong-Ryeol/reshade/releases/download/";
		}

		inline bool url_allowed(const std::string &url)
		{
			const std::string prefix = update_url_prefix();
			if (url.size() <= prefix.size()) return false;          // 접두사만 있고 파일명 없음
			if (url.compare(0, prefix.size(), prefix) != 0) return false;
			if (url.find("..") != std::string::npos) return false;  // 경로 조작
			if (url.find('@') != std::string::npos) return false;   // userinfo 로 호스트 위장
			// 퍼센트 인코딩된 경로 조작. WinInet 은 INTERNET_FLAG_NO_ESCAPE 없이 URL 을
			// 정규화하므로 %2e%2e 는 디코딩 후 ../ 로 축약되어 접두사 피닝을 우회한다.
			if (url.find("%2e") != std::string::npos || url.find("%2E") != std::string::npos ||
				url.find("%2f") != std::string::npos || url.find("%2F") != std::string::npos)
				return false;
			// 제어문자. CR/LF 가 HttpOpenRequestW 의 오브젝트명에 실리면 헤더 주입이 된다.
			for (unsigned char c : url)
				if (c < 0x20 || c == 0x7f) return false;
			return true;
		}

		// "https://host/path…" → host, "/path…". https 전용.
		inline bool split_https_url(const std::string &url, std::string &host, std::string &path)
		{
			const std::string scheme = "https://";
			if (url.compare(0, scheme.size(), scheme) != 0) return false;
			const std::size_t slash = url.find('/', scheme.size());
			if (slash == std::string::npos) return false;           // 경로 없음
			const std::string h = url.substr(scheme.size(), slash - scheme.size());
			if (h.empty()) return false;
			host = h;
			path = url.substr(slash);
			return true;
		}
	}
}
