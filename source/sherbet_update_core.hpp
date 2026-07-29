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
#include <vector>
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
			// ⚠️ **ASCII 인쇄가능 문자만 통과시킨다**(0x20 ~ 0x7e).
			// - 제어문자: CR/LF 가 HttpOpenRequestW 의 오브젝트명에 실리면 헤더 주입이 된다.
			// - 0x7f 이상(= UTF-8 선행/후속 바이트): **베스트핏 매핑 우회를 막는 유일한 방어다.**
			//   글루는 URL 을 언젠가 wchar_t 로 바꿔 WinInet 에 넘긴다. 그 변환이 CP_UTF8 이
			//   아니라 ANSI(CP_ACP)로 이뤄지면 Windows 의 "best-fit" 매핑이 전각 문자를
			//   ASCII 로 접는다: U+FF0E U+FF0E U+FF0F(．．／) → "../". 그러면 위의 ".." 검사와
			//   접두사 피닝을 **통과한 뒤에** 경로 조작이 되살아난다. 실측 페이로드:
			//     ".../releases/download/" "\xEF\xBC\x8E\xEF\xBC\x8E\xEF\xBC\x8F" "attacker/evil.dll"
			//   "글루가 CP_UTF8 을 쓰면 된다" 에 보안을 걸지 않는다 — 그 규약은 테스트가 없는
			//   곳(Phase 3 의 .cpp)에 있고, 이 리포의 관행(sherbet_content.cpp 의
			//   std::wstring(p.begin(), p.end()))은 정확히 그 규약을 어기는 형태다.
			//   우리 릴리스 URL 은 구조상 전부 ASCII 다:
			//     https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-<x.y.z>/ReShade{64,32}.dll
			//   따라서 비ASCII 를 통째로 거부해도 잃는 정당한 URL 이 없다.
			for (unsigned char c : url)
				if (c < 0x20 || c >= 0x7f) return false;
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

			// 평면 JSON 에 `"key"` 가 있기만 한지. has_unique_keys 를 먼저 통과한 바디에서만
			// 의미가 있다(거기서 등장 횟수가 1 이하로 보장된다).
			inline bool has_key(const std::string &body, const char *key)
			{
				return body.find(std::string("\"") + key + "\"") != std::string::npos;
			}

			// 중요한 키가 바디 전체에서 두 번 이상 나오면 매니페스트를 통째로 거부한다.
			// sherbet::auth::json_string 은 바디 전체에서 `"key"` 의 '첫 등장' 을 찾는다.
			// 따라서 다른 값(예: notes) 안에 `"url":"…"` 처럼 생긴 텍스트가 앞서 있으면
			// 그쪽이 잡혀 진짜 url/sha256/size 를 통째로 가린다. 가짜 삼총사를 자체 정합적으로
			// (같은 리포 접두사 + 64hex + 범위 안 크기) 심으면 뒤따르는 검사가 전부 통과한다.
			// 중복 키(첫 값 우선)도 같은 메커니즘이라 함께 막힌다.
			// json.dumps 는 `"` 를 `\"` 로 이스케이프해 이 needle 을 깨뜨리지만, 매니페스트
			// 라우트는 아직 없고 손으로 조립한 JSON 은 이스케이프하지 않는다.
			// 서버가 이스케이프해 준다는 가정에 클라이언트 보안을 걸지 않는다.
			// ⚠️ needle 은 앞뒤 따옴표를 포함한다. 그래서 "version" 은 "min_version" 안에서
			// 잡히지 않는다(version 앞 글자가 " 가 아니라 _). 이 성질은 테스트로 못 박혀 있다.
			inline bool has_unique_keys(const std::string &body)
			{
				static const char *const keys[] = {
					"\"schema\"", "\"arch\"", "\"version\"", "\"min_version\"",
					"\"sha256\"", "\"url\"", "\"size\"", "\"allow_downgrade\""
				};
				for (const char *const k : keys)
				{
					const std::string needle = k;
					std::size_t seen = 0, i = 0;
					for (;;)
					{
						const std::size_t at = body.find(needle, i);
						if (at == std::string::npos) break;
						if (++seen > 1) return false;
						i = at + needle.size();
					}
				}
				return true;
			}
		}

		// 서버 응답(평면 JSON)을 판정 가능한 형태로. 하나라도 규약을 어기면 ok=false.
		// arch 는 컴파일 타임 결정값("x64" 또는 "x86")을 넘긴다.
		// ⚠️ 거부는 전부 `return info()` — 부분적으로 채워진 객체를 절대 내보내지 않는다.
		// ok 를 확인하지 않고 u.url / u.size 를 읽는 호출자가 생기면(Task 7·8) 검증에 실패한
		// 공격자 URL 을 그대로 쓰게 되기 때문이다. 실패는 항상 '완전히 빈 info' 다.
		inline info parse_manifest(const std::string &body, const char *arch)
		{
			info u;
			if (body.empty() || arch == nullptr) return info();
			// 키 유일성이 첫 관문 — 다른 값 안에 숨은 가짜 "url"/"sha256"/"size" 가 진짜를
			// 가리는 것을 막는다. 아래 has_key 도 이 보장 위에서만 성립한다.
			if (!detail::has_unique_keys(body)) return info();

			std::string s;
			// schema
			if (!sherbet::auth::json_string(body, "schema", s) || s != "1") return info();
			// arch echo — 서버가 다른 아키텍처를 줬으면 절대 설치하지 않는다
			if (!sherbet::auth::json_string(body, "arch", s) || s != arch) return info();
			// version
			if (!sherbet::auth::json_string(body, "version", u.version)) return info();
			version3 vp;
			if (!parse_version(u.version, vp)) return info();
			// min_version 은 선택 — 키가 없으면 빈 문자열로 두고 is_mandatory 가 false 를 낸다(스펙 §3.5).
			// 다만 "키가 있는데 형식이 틀렸다"(문자열이 아니거나 파싱 불가)는 version 과 똑같이
			// 매니페스트 전체를 거부한다. 스펙 §3.3 이 size 와 min_version 을 같은 이유로 문자열
			// 타입으로 못 박았으므로 강제도 대칭이어야 한다 — 여기서 조용히 넘기면 str() 을
			// 빠뜨린 라우트가 강제 업데이트를 소리 없이 무력화하고, Task 7 은 그 사실을 복구할
			// 방법이 없다(정보가 여기서 파괴된다).
			// '형식은 맞지만 값이 과한' 9.9.9 는 계속 통과한다 — 그건 §3.5 의 판단 영역이다.
			if (detail::has_key(body, "min_version"))
			{
				version3 mvp;
				if (!sherbet::auth::json_string(body, "min_version", s)) return info();
				if (!parse_version(s, mvp)) return info();
				u.min_version = s;
			}
			// sha256
			if (!sherbet::auth::json_string(body, "sha256", u.sha256)) return info();
			if (!is_sha256_hex(u.sha256)) return info();
			// url
			if (!sherbet::auth::json_string(body, "url", u.url)) return info();
			if (!url_allowed(u.url)) return info();
			// size — 서버가 문자열로 직렬화한다(숫자로 내면 json_string 이 못 읽는다)
			if (!sherbet::auth::json_string(body, "size", s)) return info();
			if (!detail::parse_u64(s, u.size)) return info();
			if (u.size < (1ULL << 20) || u.size > (32ULL << 20)) return info(); // 1MiB ~ 32MiB
			// 선택 필드 — 표시 전용이다.
			// ⚠️ json_string 의 언이스케이프는 모르는 \X 를 X 로 흘린다(가 → uAC00, \t → t).
			// notes/notice 는 이스케이프가 원문대로 복원되지 않으므로 화면에 보여주는 데만 쓰고
			// 파싱·비교·경로 조합에는 절대 쓰지 않는다.
			sherbet::auth::json_string(body, "notes", u.notes);
			sherbet::auth::json_string(body, "notice", u.notice);
			u.allow_downgrade = (sherbet::auth::json_bool_or_null(body, "allow_downgrade") == 1);

			u.ok = true;
			return u;
		}

		// ── 파일 이름 규약 (스펙 §4.3) ─────────────────────────────────────
		// ⚠️ **여기가 유일한 정의처다.** 접미사와 복구안내 문구를 글루(.cpp)나 테스트에
		// 다시 타이핑하면, 시뮬레이터(tools/sherbet_swap_sim.cpp)가 자기 사본을 검증하는
		// 꼴이 되어 제품에 대해 아무것도 못 박지 못한다 — 제품이 ".sherbet-backup" 을 쓰고
		// 시뮬레이터가 ".sherbet-bak" 을 써도 초록불이다. 그래서 시뮬레이터는 이 함수들을
		// 소비한다. 이름을 바꾸려면 여기만 고치면 되고, 고치는 순간 시뮬레이터가 따라온다.
		//
		// self(= 지금 이 DLL 의 파일 이름)는 설치마다 다르다(dxgi.dll / d3d11.dll / opengl32.dll…).
		// 상수로 둘 수 없고, 항상 g_reshade_dll_path.filename() 에서 온다.
		inline const char *suffix_new_part() { return ".sherbet-new.part"; } // 검증 전 스트리밍 대상
		inline const char *suffix_new()      { return ".sherbet-new"; }      // 검증 통과한 새 바이너리
		inline const char *suffix_bak()      { return ".sherbet-bak"; }      // 교체 전 self(롤백 재료)
		inline const char *suffix_bak_old()  { return ".sherbet-bak.old"; }  // 못 지운 묵은 .bak
		inline const char *suffix_failed()   { return ".sherbet-failed"; }   // §5.4 R9 증거 1개
		inline const char *marker_name()     { return "sherbet.update"; }

		// ⚠️ 파일명에 비ASCII 가 들어간다. Phase 3 이 이 이름으로 CreateFileW 를 부를 때는
		// 반드시 MultiByteToWideChar(CP_UTF8, …) 를 쓸 것 —
		// std::wstring(s.begin(), s.end()) 는 UTF-8 바이트를 그대로 wchar 로 늘려 깨진
		// 이름의 파일을 만든다(그러면 고객에게 남는 유일한 단서가 읽을 수 없는 이름이 된다).
		inline const char *recovery_note_name() { return "Sherbet-\xEB\xB3\xB5\xEA\xB5\xAC\xEC\x95\x88\xEB\x82\xB4.txt"; } // "Sherbet-복구안내.txt"

		// §4.4 S9 는 복구안내에 **실제 파일명**을 넣으라고 못 박는다 — 고객이 프록시를
		// dxgi.dll 이 아니라 d3d11.dll/opengl32.dll 로 넣었을 수 있고, 그 이름을 모르면
		// 안내문이 무용지물이다. 기록 지점이 둘(§4.4 S9 = 교체 전, §5.4 R9-pre = 롤백 전)이라
		// 문구가 갈라지지 않도록 여기서만 만든다.
		// ⚠️ 두 이름을 **모두** 넣어야 한다. 백업 이름만 적힌 안내문은 "무엇으로 이름을
		// 바꿔야 하는지" 를 빠뜨리는데, "dxgi.dll.sherbet-bak" 안에 "dxgi.dll" 이 들어 있어서
		// 단순 부분문자열 검사로는 그 결함이 잡히지 않는다(시뮬레이터의 mentions_file 참고).
		inline std::string make_recovery_note(const std::string &self_name, const std::string &bak_name)
		{
			return std::string("[Sherbet \xEB\xB3\xB5\xEA\xB5\xAC \xEC\x95\x88\xEB\x82\xB4] ") + self_name + // "[Sherbet 복구 안내] "
				" \xEC\x9D\xB4(\xEA\xB0\x80) \xEC\x97\x86\xEC\x9C\xBC\xEB\xA9\xB4 " + bak_name +             // " 이(가) 없으면 "
				" \xEC\x9D\x98 \xEC\x9D\xB4\xEB\xA6\x84\xEC\x9D\x84 " + self_name +                          // " 의 이름을 "
				" \xEB\xA1\x9C \xEB\xB0\x94\xEA\xBE\xB8\xEB\xA9\xB4 \xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4.";  // " 로 바꾸면 됩니다."
		}

		// ── 부팅 마커 ──────────────────────────────────────────────────────
		// <DLL 과 같은 폴더>/sherbet.update. sherbet.auth 와 같은 key=value 줄 포맷.
		// writer 가 셋(교체 워커 / 렌더 스레드 / 다른 프로세스의 attach)이라
		// 모르는 키를 반드시 보존해야 서로의 필드를 지우지 않는다.
		struct boot_marker
		{
			std::string state;     // pending | swapping | rolledback | rollback_failed
			std::string version;   // 이 마커가 서술하는 바이너리
			std::string prev;
			std::string bak;
			std::string exe;       // 교체 당시 대상 실행파일 이름
			std::string sha;
			std::string bad_ver;
			std::string bad_sha;
			int tries = 0;
			std::vector<std::string> unknown; // 모르는 줄 원문(그대로 되돌려 쓴다)
		};

		// 손상된 마커의 tries 상한. static_cast<int>(2^64-1) 은 -1 이 되고, 음수 tries 는
		// 롤백을 영원히 미룬다 — 깨진 게임에 갇히는 쪽이 훨씬 나쁘므로 위로 클램프한다.
		inline int marker_tries_cap() { return 1000000; }

		namespace detail
		{
			// 값 하나를 마커 파일에 안전하게 쓸 수 있는 형태로 만든다.
			// ⚠️ 줄 단위 포맷이라 값 안의 CR/LF 는 그 자체로 새 key=value 줄이 된다.
			// exe 는 g_target_executable_path.filename() — 파일시스템에서 온 임의 문자열이다.
			// 실측: exe = "evil\nstate=rolledback" 을 날것으로 쓰면 라운드트립 후 state 가
			// rolledback 이 되어 decide_boot 이 rollback → none 으로 뒤집힌다(= 롤백 소멸).
			// 지우지 않고 공백으로 '치환' 한다: 양옆 토큰이 붙어 다른 의미가 되는 일이 없고,
			// 사람이 파일을 열어 봤을 때 무엇이 소독됐는지 보인다.
			// 뒤쪽 공백류는 마저 잘라낸다 — parse_marker 가 어차피 자르므로, 여기서 안 자르면
			// serialize → parse → serialize 가 고정점이 아니게 된다.
			inline std::string marker_value(const std::string &v)
			{
				std::string o = v;
				for (char &c : o)
					if (c == '\n' || c == '\r') c = ' ';
				while (!o.empty() && (o.back() == ' ' || o.back() == '\t')) o.pop_back();
				return o;
			}

			// 줄 앞뒤의 [ \t\r] 를 잘라낸다. 앞쪽을 안 자르면 "  state=pending" 이 모르는
			// 줄로 밀려나 마커 전체가 '없음' 이 되고, 뒤쪽에서 탭을 안 자르면 state 가
			// "pending\t" 가 되어 parse 는 성공하는데 decide_boot 만 조용히 none 이 된다.
			// 값 '안' 은 건드리지 않는다 — 파일명에 공백이 정당하게 들어간다("Grand Theft Auto V.exe").
			inline void marker_trim(std::string &s)
			{
				std::size_t b = 0, e = s.size();
				while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
				while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
				s = s.substr(b, e - b);
			}

			// 값이 소독 후 비면 줄 자체를 쓰지 않는다. "key=" 를 남기면 파서가 빈 문자열을
			// 돌려주므로 다음 serialize 가 그 줄을 빼버려 고정점이 깨진다.
			inline void marker_line(std::string &o, const char *key, const std::string &raw)
			{
				const std::string v = marker_value(raw);
				if (v.empty()) return;
				o += key; o += '='; o += v; o += '\n';
			}
		}

		// ⚠️ 전제조건: m.state 는 비어 있으면 안 된다. state 가 빈 마커는 "state=\ntries=0\n"
		// 으로 직렬화되는데 parse_marker 는 그것을 거부하므로 파일이 영구히 '마커 없음' 이
		// 되고, 그 파일을 나중에 다시 쓰는 쪽은 unknown(다른 writer 의 필드)을 통째로 잃는다.
		// 마커를 없애려면 빈 state 로 쓰지 말고 파일을 지워라.
		inline std::string serialize_marker(const boot_marker &m)
		{
			std::string o;
			o += "state=" + detail::marker_value(m.state) + "\n";
			detail::marker_line(o, "version", m.version);
			detail::marker_line(o, "prev",    m.prev);
			detail::marker_line(o, "bak",     m.bak);
			detail::marker_line(o, "exe",     m.exe);
			detail::marker_line(o, "sha",     m.sha);
			detail::marker_line(o, "bad_ver", m.bad_ver);
			detail::marker_line(o, "bad_sha", m.bad_sha);
			o += "tries=" + std::to_string(m.tries) + "\n";
			// 모르는 줄은 항상 마지막에, 읽은 순서 그대로. 순서가 흔들리면 세 writer 가
			// 서로의 파일을 끝없이 다시 쓴다. 이것도 소독한다 — 손으로 채워 넣는 호출자가
			// 생기면 위조 경로가 다시 열린다.
			for (const std::string &line : m.unknown)
			{
				const std::string v = detail::marker_value(line);
				if (!v.empty()) o += v + "\n";
			}
			return o;
		}

		// 중복 키 정책: **마지막 값이 이긴다**(모든 키 동일). 단 tries 만 예외적으로
		// '정수가 아닌 줄은 무시' 한다 — 손상된 한 줄이 앞서 읽은 진짜 카운트를 0 으로
		// 지우면 롤백이 통째로 사라지기 때문이다(유효한 tries 가 하나도 없으면 그대로 0).
		inline bool parse_marker(const std::string &text, boot_marker &out)
		{
			boot_marker t;
			std::size_t i = 0;
			while (i < text.size())
			{
				const std::size_t eol = text.find('\n', i);
				std::string line = text.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
				i = (eol == std::string::npos) ? text.size() : eol + 1;
				detail::marker_trim(line);
				if (line.empty()) continue;
				const std::size_t eq = line.find('=');
				if (eq == std::string::npos || eq == 0) { t.unknown.push_back(line); continue; }
				std::string key = line.substr(0, eq);
				detail::marker_trim(key);              // "tries =3" 도 tries 로 읽는다
				const std::string val = line.substr(eq + 1);
				if      (key == "state")   t.state = val;
				else if (key == "version") t.version = val;
				else if (key == "prev")    t.prev = val;
				else if (key == "bak")     t.bak = val;
				else if (key == "exe")     t.exe = val;
				else if (key == "sha")     t.sha = val;
				else if (key == "bad_ver") t.bad_ver = val;
				else if (key == "bad_sha") t.bad_sha = val;
				else if (key == "tries")
				{
					// 비정수는 '무시'(0 으로 덮어쓰지 않는다 — 위 정책 주석 참고).
					// 정수지만 int 를 넘으면 클램프 — 캐스팅이 감기면 음수가 되고
					// 음수는 롤백을 막는다(고객이 깨진 게임에 갇힌다).
					unsigned long long n = 0;
					if (detail::parse_u64(val, n))
						t.tries = (n > static_cast<unsigned long long>(marker_tries_cap()))
							? marker_tries_cap() : static_cast<int>(n);
				}
				else t.unknown.push_back(line); // 모르는 키는 원문 보존
			}
			if (t.state.empty()) return false;  // 실패 시 out 은 건드리지 않는다
			out = t;
			return true;
		}

		enum class boot_action { none, count, rollback };

		// 스펙 §5.4 '2회 연속 부팅 실패' 의 정확한 정의.
		//   m.tries = 마커 파일에 적혀 있는, 지금까지 관찰된 부팅 실패 횟수.
		//
		// ⚠️ 계약: **디스크에서 읽은 그대로의 마커**를 넘겨야 한다.
		// R6 로 디스크에 tries+1 을 써도 이 함수에는 읽은 그대로의 마커를 넘긴다 —
		// 구조체의 tries 를 먼저 증가시키면 임계값이 절반이 된다.
		// R6 가 증가분을 판정 '전에' 디스크에 쓰라고 하는 것은 쓰기 전에 크래시하면
		// 카운트가 안 늘어 미탐이 나기 때문이고, 그것은 파일 내용에 대한 요구일 뿐이다.
		// 디스크에 쓰는 값과 이 함수에 넘기는 값은 의도적으로 다르다:
		//   디스크 = tries + 1,  인자 = tries(읽은 값).
		// 인자까지 증가시키면 첫 번째 부팅 실패가 곧바로 rollback 이 되어, 알트탭이나
		// 드라이버 결함 한 번에 멀쩡한 설치가 되돌아간다. test_decide_boot 에 이 오용의
		// 결과를 실행 가능한 형태로 박아 두었다.
		//
		// 판정: 이번 부팅이 max_tries 번째 실패가 되는 순간 롤백 → tries + 1 >= max_tries.
		// max_tries 기본값 2 → tries 가 1 인 마커로 부팅한 것이 곧 '2회 연속 실패' 다.
		// pending 이 아닌 상태는 전부 none: rolledback/rollback_failed 를 세면 이미 되돌린
		// 사용자를 또 되돌리고, swapping(교체 중단)은 startup_repair 가 따로 다룬다.
		inline boot_action decide_boot(const boot_marker &m, int max_tries = 2)
		{
			if (m.state != "pending") return boot_action::none;
			// long long 으로 더한다 — tries 가 INT_MAX 면 int 덧셈은 부호 오버플로(UB)다.
			// 마커는 디스크에서 오고 세 프로세스가 쓴다. 판정이 UB 로 흔들리면 롤백이 사라진다.
			const long long next = static_cast<long long>(m.tries) + 1;
			return (next >= static_cast<long long>(max_tries)) ? boot_action::rollback : boot_action::count;
		}

		// 배너를 띄울 것인가. 블랙리스트는 (버전, sha) 쌍으로 본다 —
		// 같은 번호로 고쳐 재배포하면 sha 가 달라 자동으로 다시 제안된다.
		// 비어 있는 반쪽은 와일드카드다: 롤백 코드가 한쪽만 적고 죽었다면 '어떤 빌드인지
		// 확실치 않지만 나빴다' 는 뜻이고, 그때 다시 제안해 재브릭시키는 쪽이 업데이트를
		// 한 번 놓치는 것보다 훨씬 나쁘다. 둘 다 비어 있으면 블랙리스트 자체가 없는 것이다.
		inline bool should_offer(const std::string &cur, const info &u,
			const std::string &bad_ver, const std::string &bad_sha)
		{
			// ⚠️ ok 가드가 유일한 방어다. parse_manifest 는 거부 시 완전히 빈 info() 를
			// 돌려주지만 그 '비어 있음' 에 기대면 안 된다(필드가 하나 늘면 무너진다).
			if (!u.ok) return false;
			version3 a, b;
			if (!parse_version(cur, a)) return false;   // 내 버전을 모르면 아무것도 하지 않는다
			if (!parse_version(u.version, b)) return false;
			if (!bad_ver.empty() || !bad_sha.empty())
			{
				const bool ver_hit = bad_ver.empty() || u.version == bad_ver;
				const bool sha_hit = bad_sha.empty() || u.sha256 == bad_sha;
				if (ver_hit && sha_hit) return false;
			}
			const int c = version_cmp(b, a);
			if (c > 0) return true;                     // 서버가 더 최신
			if (c < 0) return u.allow_downgrade;        // 킬스위치 강등
			return false;                               // 같음
		}

		// 필수 업데이트 표시 여부. 값 없음/파싱 실패면 반드시 false —
		// 오타 한 번(예: 9.9.9)으로 전 고객 UI 를 잠그면 안 된다(스펙 §3.5).
		// parse_manifest 가 이미 '키는 있는데 파싱 불가' 인 매니페스트를 통째로 거부하므로
		// ok=true 인 info 의 min_version 은 빈 문자열이거나 파싱 가능하다. 아래 두 가드는
		// 그 위의 심층 방어다 — 다른 경로가 info 를 조립하게 되는 날을 위해 남긴다.
		inline bool is_mandatory(const std::string &cur, const info &u)
		{
			if (!u.ok || u.min_version.empty()) return false;
			version3 a, m;
			if (!parse_version(cur, a)) return false;
			if (!parse_version(u.min_version, m)) return false;
			return version_cmp(a, m) < 0;
		}

		// ── 교체 중단 복구 상태머신 (스펙 §4.4 S7~S13 / §5.4 R3~R10) ───────
		// 맥에서는 실제 파일 교체를 검증할 수 없으므로 판정을 순수 함수로 뽑아
		// tools/sherbet_swap_sim.cpp 가 가짜 파일시스템 위에서 모든 중단 지점에
		// 전원차단을 주입하며 전수 검사한다. 판정을 글루(.cpp)에 흩뿌리면 그 검사가
		// 통째로 불가능해진다 — 새 판정을 여기 밖에 쓰지 말 것.
		//
		// ⚠️ 이 상태머신은 **파일 존재 여부만** 본다. 부팅 초기 경로(§5.2)는 원시 Win32 만
		// 쓸 수 있어(CRT·std::filesystem·new·crypto 금지) self 를 해시해 '지금 이게 옛
		// 바이너리냐 새 바이너리냐' 를 물을 수 없다. 그래서 판정은 다음 한 줄에만 기댄다:
		//     self 가 있으면 그건 **유효한 DLL 이다**(교체 경로가 self 자리에 놓는 것은
		//     S6 에서 sha256+pe_check 를 통과한 파일뿐이고, rename 은 원자적이다).
		// **어느 버전인지는 모른다.** (self 있음 + .bak 있음 + swapping) 이면 S11 이
		// 끝났다는 뜻이라 새 바이너리일 '가능성이 높지만', AV 가 .bak 을 격리해 가면 같은
		// 모양이 나온다. 그래서 이 상태머신은 버전을 아는 척하지 않고, 버전을 아는 척해야
		// 하는 판정(부팅 실패 카운트)은 전부 §5.2 게이트1(marker.version == 지금 매핑된
		// SHERBET_VERSION)에 넘긴다. **게이트1 은 exe 오탐 방지용이 아니라 크래시 정합성의
		// 마지막 방어선이다 — 빼면 여기서 남을 수 있는 낡은 마커가 곧바로 해롭게 변한다.**
		//
		// ⚠️ 마커 쓰기 순서 규칙 — 복구를 구현할 때 반드시 지킬 것(시뮬레이터가 못 박는다):
		//     **self 가 없는 동안에는 마커를 먼저 쓰고 파일을 나중에 옮긴다.**
		// self 가 없는 구간에서는 순서로 잃을 게 없다(어차피 DLL 이 없어 이번 부팅엔 우리
		// 코드가 아예 안 돈다). 반대로 파일을 먼저 옮기고 마커 쓰기 직전에 끊기면
		// (self 있음 + .bak 없음 + pending) 이 되는데, 이건 '정상적으로 새 버전이 깔려
		// 확정 대기 중' 과 디스크상 구별이 불가능해서 **블랙리스트(bad_ver/bad_sha)가 통째로
		// 증발한다** — 방금 되돌린 불량 빌드를 그대로 다시 제안하게 된다.
		// (§5.4 R10 이 마커를 맨 뒤에 두는 것처럼 읽히지만, R9 가 self 를 .sherbet-failed 로
		//  밀어낸 **뒤**라면 그 시점부터는 이 규칙이 우선한다.)
		//
		// ⚠️ self 가 없는 갈래는 '다음 부팅' 에는 안 돈다 — self 가 곧 이 DLL 이라 없으면
		// 로드조차 안 되고 게임은 System32 의 진짜 dxgi.dll 로 폴백해 켜진다(§4.4 각주).
		// 이 갈래의 실사용자는 (a) **같은 세션의 교체 워커**(이미지는 이미 매핑돼 있어 파일이
		// 사라져도 계속 돈다 — S11 실패 원복이 이것이다), (b) 사람이 손으로 하는 §4.4 S9
		// '복구안내' 절차, (c) 훗날의 외부 복구 도구다. 그래서 여기서도 전부 정의해 둔다.
		//
		// ⚠️ 그래서 **self 를 없앨 수 있는 모든 절차는 그 전에 `Sherbet-복구안내.txt` 를
		// 먼저 써야 한다**(§4.4 S9 = 교체 전, §5.4 R9-pre = 롤백 전). self 가 없는 채로
		// 세션이 끝나면 자동 복구가 영영 안 도는데, 그때 이 파일이 없으면 고객에게는
		// 제품이 아무 설명 없이 사라진 것으로 보인다. 그리고 self 가 없는 동안에는
		// 이 파일도, 어떤 DLL 사본(.sherbet-new/.sherbet-bak/.sherbet-bak.old)도 지우지
		// 않는다 — `.sherbet-bak.old` 는 잔재처럼 보이지만 한때 동작하던 진짜 바이너리다.
		//
		// ⚠️ **`.sherbet-bak` 은 마커가 비난하는(version/bad_ver) 바이너리를 절대 담지
		// 않는다.** S10 이 `.bak` 에 넣는 것은 '교체 **전**의 self'(지금 돌고 있는 바이너리)
		// 이고 S9/R10 이 version/bad_ver 에 적는 것은 '교체 **후**의 버전' 이라, 구조상 서로
		// 다른 파일이다. 이 성질은 restore_from_bak 이 '되돌리면 옛 바이너리가 된다' 고
		// 말할 수 있는 유일한 근거다 — 깨지면 불량 바이너리를 self 에 앉히면서 마커에는
		// rolledback 을 적게 된다. S10 에 REPLACE_EXISTING 을 붙이거나 S8 을 건너뛰어
		// 묵은 `.bak` 을 살려 두면 이 성질이 무너진다.
		enum class disk_state
		{
			normal,             // self 있음, 교체 중 아님, .bak 없음 → 할 일 없음
			staged,             // self 있음 + state==swapping
			                    //   = 교체가 확정(S13) 전에 끊겼다. self 는 유효한 DLL 이지만
			                    //     옛 것인지 새 것인지는 알 수 없다(위 ⚠️ 참고).
			swapping_lost_self, // self 없음 + .bak 있음 → 옛 바이너리로 되돌릴 수 있다
			resume_from_new,    // self 없음 + .bak 없음 + .sherbet-new 있음
			                    //   → .new 를 앉히는 것 말고 DLL 을 되살릴 방법이 없다
			rollback_ready,     // self 있음 + .bak 있음 + 교체 중 아님
			                    //   = 정상. 되돌릴 재료가 있을 뿐이고 롤백 판정은 decide_boot 담당.
			broken_no_dll       // self / .bak / .new 전부 없음 → 재료가 하나도 없다
		};

		// ⚠️ 전순함수여야 한다: (has_self, has_new, has_bak) 8가지 × 모든 마커 상태가
		// 빠짐없이 하나의 disk_state 로 간다. 시뮬레이터가 전수로 단언한다.
		inline disk_state classify(bool has_self, bool has_new, bool has_bak, const boot_marker &m)
		{
			// self 부재가 최우선이다. 마커가 rolledback 이라고 해서 여기서 먼저 리턴하면
			// (스펙 R4 의 문면대로) DLL 이 없는 디렉터리를 그대로 두게 된다 — R4 의
			// '마커를 지우지 않는다' 는 파일 복구를 하지 말라는 뜻이 아니다.
			if (!has_self)
			{
				if (has_bak) return disk_state::swapping_lost_self; // §5.4 R3: 옛 것 우선
				if (has_new) return disk_state::resume_from_new;    // 남은 유일한 소스
				return disk_state::broken_no_dll;
			}
			if (m.state == "swapping") return disk_state::staged;
			if (has_bak) return disk_state::rollback_ready;
			return disk_state::normal;
		}

		// ⚠️ 각 동작이 요구하는 '소스 파일' 은 classify 가 그 상태를 낸 시점에 반드시
		// 존재한다(restore_from_bak↔.bak, restore_from_new↔.new, promote_pending↔self).
		// 없는 파일을 옮기라고 시키면 rename 이 조용히 실패해 self 가 영영 안 돌아온다 —
		// 시뮬레이터가 8×마커상태 전수로 이걸 단언한다.
		enum class repair_action
		{
			none,             // 아무것도 하지 않는다
			promote_pending,  // 교체 확정: state=pending, tries=0 (파일은 건드리지 않는다)
			restore_from_bak, // rolledback 기록 **후** .bak → self (§5.4 R3)
			restore_from_new, // pending 기록 **후** .new → self (남은 소스가 그것뿐이다)
			give_up           // 재료 없음. rollback_failed 만 남기고 조용히 계속 실행(§5.4 R8)
		};

		// ⚠️ **state=pending 을 쓰는 쪽은 반드시 tries=0 을 함께 쓴다.**
		// (S13 / promote_pending / restore_from_new — 셋 다 예외 없이)
		// tries 는 '지금 pending 인 그 바이너리' 의 부팅 실패 횟수다. 롤백 직후의 마커는
		// tries=2 로 남아 있고(§5.4 R9/R10 은 tries 를 되돌리지 않는다), S9 는 state/version
		// 만 갈아끼우고 tries 를 그대로 물려받는다. 그래서 여기서 0 으로 되돌리지 않으면
		// **새로 깐 멀쩡한 빌드가 첫 부팅에 곧바로 롤백된다**(decide_boot: 2+1 >= 2).
		// tries 를 증가시키는 유일한 지점은 §5.4 R6 이다.
		//
		// ⚠️ restore_from_new 의 마커 규칙 — Phase 3 이 여기만 보고 구현할 수 있게 못 박는다:
		//   **state=pending / tries=0 으로 확정하고, bad_ver/bad_sha 는 그대로 둔다.**
		// .new 를 앉히는 것은 선택이 아니라 '남은 유일한 DLL 소스' 라서다. 그런데 마커가
		// 그 파일을 서술하고 있다는 보장은 없다(S9 전이면 마커는 아직 .new 를 모른다).
		// 그렇다고 rolledback/rollback_failed 를 남겨 두면 §5.4 R11 안전모드가 켜져 **방금
		// 되살린 바이너리로 효과가 통째로 꺼지고** '되돌렸습니다' 라는 거짓 배너가 뜬다.
		// pending 으로 두면 최악이 '게이트1 이 걸러 내는 낡은 마커' 다 — staged 와 같은 저울질.
		// 블랙리스트(bad_ver/bad_sha)는 상태가 아니라 사실이므로 지우지 않는다.
		// ⚠️ 따라서 글루는 bad_ver/bad_sha 를 **state 와 무관하게** 읽어 전역에 담아야 한다.
		//    §5.4 R4 는 rolledback 일 때만 읽으라고 읽히지만, 그러면 이 경로에서 블랙리스트가
		//    한 세션 동안 적용되지 않아 방금 문제를 일으킨 빌드를 다시 제안하게 된다.

		// staged → promote_pending 인 이유(대안을 고르지 않은 근거를 남긴다):
		// staged 는 '새 버전이 깔렸는데 확정만 못 했다' 와 'S10 전에 끊겨 아직 옛 버전이다'
		// 두 가지를 다 포함한다. 디스크만 봐서는 구별할 수 없으므로 **틀렸을 때 덜 해로운
		// 쪽**을 고른다.
		//   promote_pending 이 틀린 경우 → 마커가 '1.4.0 이 깔림' 이라고 적는데 실제로는
		//     1.3.0 이다. 게이트1 이 버전 불일치를 잡아 부팅 카운트도 롤백도 일어나지 않고,
		//     classify 는 그 마커를 normal/rollback_ready(=none)로만 보낸다. **무해하게 낡은
		//     마커**로 남았다가 다음 교체가 덮어쓴다. 블랙리스트도 안 건드린다.
		//   rolledback 을 적는 대안이 틀린 경우 → 실제로는 새 버전이 잘 깔렸는데 §5.4 R11 의
		//     안전모드가 켜져 **효과가 통째로 꺼지고**(고객이 산 기능이 사라진다) 멀쩡한
		//     버전이 블랙리스트된다. 게다가 전원이 나갔을 뿐인 고객에게 '되돌렸습니다' 라고
		//     거짓말을 한다.
		// 전자는 조용히 낡은 값, 후자는 소리나는 기능 상실이다. 그래서 promote_pending.
		inline repair_action decide_repair(disk_state s)
		{
			switch (s)
			{
			case disk_state::staged:             return repair_action::promote_pending;
			case disk_state::swapping_lost_self: return repair_action::restore_from_bak;
			case disk_state::resume_from_new:    return repair_action::restore_from_new;
			case disk_state::broken_no_dll:      return repair_action::give_up;
			case disk_state::rollback_ready:     return repair_action::none;
			case disk_state::normal:             return repair_action::none;
			}
			return repair_action::none;
		}
	}
}
