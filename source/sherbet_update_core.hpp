/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 자동 업데이트 — 순수 로직(플랫폼 비의존).
// WinInet/스레드/ImGui/std::filesystem 은 여기 넣지 않는다. sherbet_update.cpp 가 글루를 맡는다.
// 판정 로직 100% 를 여기 몰아넣어 맥 clang 으로 실제 단위테스트한다(tools/sherbet_update_test.cpp).
#pragma once

#include "sherbet_auth_core.hpp"

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

		namespace detail
		{
			// pe_check 의 경계 판정만 떼어낸 순수 술어. 역참조가 없어 어떤 플랫폼에서도
			// 결정적으로 단위테스트할 수 있다 — 잘못된 판정이 크래시로 드러나기를 기대하지 않는다.
			// e_lfanew + 24 를 계산하지 않는다: uint32_t 든 32비트 size_t 든 감기기 때문에,
			// len 을 먼저 가드하고 뺄셈으로 비교한다.
			inline bool pe_bounds_ok(std::uint32_t e_lfanew, std::size_t len)
			{
				if (len < 24) return false;              // len - 24 언더플로 방지
				if (e_lfanew < 0x40) return false;       // DOS 헤더 안을 가리키는 값은 무효
				return static_cast<std::size_t>(e_lfanew) <= len - 24;
			}
		}

		// 다운로드한 파일의 앞부분이 우리 아키텍처의 유효한 DLL 인지 본다.
		// sha256 은 '바이트가 온전한가' 만 보고 '무엇인가' 는 못 본다. x64 슬롯에 32비트 DLL 을
		// 넣는 운영 실수 한 번이면 다음 실행에 ERROR_BAD_EXE_FORMAT 으로 롤백 코드조차 안 돈다.
		inline bool pe_check(const unsigned char *head, std::size_t len, bool want_x64)
		{
			if (head == nullptr || len < 0x40) return false;
			if (head[0] != 'M' || head[1] != 'Z') return false;
			const std::uint32_t e_lfanew =
				static_cast<std::uint32_t>(head[0x3c]) |
				(static_cast<std::uint32_t>(head[0x3d]) << 8) |
				(static_cast<std::uint32_t>(head[0x3e]) << 16) |
				(static_cast<std::uint32_t>(head[0x3f]) << 24);
			// COFF 헤더는 서명 4바이트 + 20바이트. Characteristics 는 서명 기준 +22.
			// ⚠️ 여기서 `e_lfanew + 24 > len` 을 직접 쓰면 안 된다. e_lfanew 는 uint32 라 덧셈이
			// 32비트에서 랩어라운드해(0xFFFFFFFF + 24 == 23) 경계검사를 통과하고
			// head + 0xFFFFFFFF 를 역참조한다. 이 4바이트는 네트워크에서 온 파일이 통째로
			// 고르는 값이다. size_t 로 캐스팅한 덧셈도 32비트 빌드(ReShade32)에선 여전히 랩한다.
			// 판정은 detail::pe_bounds_ok 에 있다 — 역참조가 없어 결정적으로 단위테스트된다.
			if (!detail::pe_bounds_ok(e_lfanew, len)) return false;
			const unsigned char *nt = head + e_lfanew;
			if (nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0) return false;
			const std::uint16_t machine =
				static_cast<std::uint16_t>(nt[4] | (nt[5] << 8));
			const std::uint16_t characteristics =
				static_cast<std::uint16_t>(nt[22] | (nt[23] << 8));
			const std::uint16_t want = want_x64 ? 0x8664 : 0x014c;
			if (machine != want) return false;
			if ((characteristics & 0x2000) == 0) return false; // IMAGE_FILE_DLL
			return true;
		}

		struct info
		{
			bool ok = false;
			std::string version, url, sha256, min_version, notes, notice;
			unsigned long long size = 0;
			bool allow_downgrade = false;
		};

		namespace detail
		{
			// "4312576" → 4312576. 숫자만 허용, 빈 문자열·오버플로 거부.
			inline bool parse_u64(const std::string &s, unsigned long long &out)
			{
				if (s.empty() || s.size() > 20) return false;
				unsigned long long acc = 0;
				for (char c : s)
				{
					if (c < '0' || c > '9') return false;
					if (acc > (0xffffffffffffffffULL - static_cast<unsigned>(c - '0')) / 10) return false;
					acc = acc * 10 + static_cast<unsigned>(c - '0');
				}
				out = acc;
				return true;
			}
		}

		// 서버 응답(평면 JSON)을 판정 가능한 형태로. 하나라도 규약을 어기면 ok=false.
		// arch 는 컴파일 타임 결정값("x64" 또는 "x86")을 넘긴다.
		inline info parse_manifest(const std::string &body, const char *arch)
		{
			info u;
			if (body.empty() || arch == nullptr) return u;

			std::string s;
			// schema
			if (!sherbet::auth::json_string(body, "schema", s) || s != "1") return u;
			// arch echo — 서버가 다른 아키텍처를 줬으면 절대 설치하지 않는다
			if (!sherbet::auth::json_string(body, "arch", s) || s != arch) return u;
			// version
			if (!sherbet::auth::json_string(body, "version", u.version)) return u;
			version3 probe;
			if (!parse_version(u.version, probe)) return u;
			// min_version 은 선택. 없거나 파싱 불가면 빈 문자열로 두고 is_mandatory 가 false 를 낸다.
			if (sherbet::auth::json_string(body, "min_version", s) && parse_version(s, probe))
				u.min_version = s;
			// sha256
			if (!sherbet::auth::json_string(body, "sha256", u.sha256)) return u;
			if (!is_sha256_hex(u.sha256)) return u;
			// url
			if (!sherbet::auth::json_string(body, "url", u.url)) return u;
			if (!url_allowed(u.url)) return u;
			// size — 서버가 문자열로 직렬화한다(숫자로 내면 json_string 이 못 읽는다)
			if (!sherbet::auth::json_string(body, "size", s)) return u;
			if (!detail::parse_u64(s, u.size)) return u;
			if (u.size < (1ULL << 20) || u.size > (32ULL << 20)) return u; // 1MiB ~ 32MiB
			// 선택 필드
			sherbet::auth::json_string(body, "notes", u.notes);
			sherbet::auth::json_string(body, "notice", u.notice);
			u.allow_downgrade = (sherbet::auth::json_bool_or_null(body, "allow_downgrade") == 1);

			u.ok = true;
			return u;
		}
	}
}
