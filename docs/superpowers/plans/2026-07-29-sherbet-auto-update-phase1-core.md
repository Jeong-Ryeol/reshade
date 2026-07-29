# Sherbet 자동 업데이트 — 1단계: 버전 체계 + 순수 로직 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 자동 업데이트의 판정 로직 100%를 Windows 무의존 순수 헤더로 만들고, 맥에서 실행되는 단위테스트와 CI 테스트 잡으로 검증한다.

**Architecture:** `source/sherbet_update_core.hpp` 단일 헤더에 순수 로직(버전 비교·매니페스트 파싱·URL 피닝·SHA-256·PE 검사·부팅마커·상태머신)을 전부 담고, `tools/sherbet_update_test.cpp`가 맥 clang으로 이를 검증한다. Win32 글루(`sherbet_update.cpp`)와 UI는 이 계획 범위 밖(3단계)이며, 매핑된 DLL rename 가정에 의존하지 않는다.

**Tech Stack:** C++17 헤더 온리, `<cassert>` 기반 호스트 테스트(기존 `tools/sherbet_auth_test.cpp` 스타일), GitHub Actions ubuntu 잡, PowerShell(`update_version.ps1`)

**설계 스펙:** `docs/superpowers/specs/2026-07-29-sherbet-auto-update-design.md` (커밋 `2fb295dc`)

## Global Constraints

- **C++17.** `source/sherbet_update_core.hpp`는 Windows 헤더·스레드·ImGui·`std::filesystem`에 의존하지 않는다. 표준 라이브러리만 사용한다.
- **새 파일 헤더 주석은** `/* Copyright (C) 2026 정렬 (Jeong-Ryeol) / SPDX-License-Identifier: BSD-3-Clause */` 형식(기존 셔벗 파일과 동일).
- **커밋 메시지에 AI·클로드 저작권 문구 금지** (사용자 전역 규칙).
- **호스트 테스트 컴파일 명령(모든 태스크 공통):**
  `clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
- **`assert`를 쓰므로 `-DNDEBUG`를 절대 붙이지 않는다.**
- **기존 파서 재사용:** 매니페스트 파싱은 `sherbet_auth_core.hpp`의 `json_string`(22행) / `json_bool_or_null`(45행)만 쓴다. 새 JSON 파서를 만들지 않는다.
- **버전 문자열 형식은 `major.minor.patch` 3필드 고정.** 각 필드 최대 999999.
- **URL 피닝 접두사(리터럴, 변경 금지):** `https://github.com/Jeong-Ryeol/reshade/releases/download/`
- **이 계획은 `sherbet_update.cpp`(Win32 글루)·`runtime_gui.cpp`·서버·`release.yml`을 건드리지 않는다.** 3단계 계획에서 다룬다.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `source/sherbet_owner.h` (수정) | `SHERBET_VERSION` 상수 추가 |
| `source/dll_main.cpp` (수정) | `SherbetVersion` 익스포트 심볼 1줄 |
| `tools/update_version.ps1` (수정) | `git describe --match` 앵커 1줄 |
| `source/sherbet_update_core.hpp` (신규) | 순수 로직 전부 |
| `tools/sherbet_update_test.cpp` (신규) | 위 헤더의 호스트 단위테스트 |
| `tools/sherbet_swap_sim.cpp` (신규) | 가짜 파일시스템 위 교체 상태머신 수렴 검증 |
| `.github/workflows/build.yml` (수정) | ubuntu 호스트 테스트 잡 추가 |

---

## Task 1: 버전 상수와 익스포트 심볼

**Files:**
- Modify: `source/sherbet_owner.h`
- Modify: `source/dll_main.cpp:16` 부근
- Modify: `tools/update_version.ps1:17`

**Interfaces:**
- Consumes: 없음
- Produces: 매크로 `SHERBET_VERSION`(문자열 리터럴, 예 `"1.0.0"`). 이후 모든 태스크가 이 매크로를 참조한다.

- [ ] **Step 1: `sherbet_owner.h`에 버전 상수 추가**

`SHERBET_ONLINE_AUTH` 블록(37~39행) 바로 뒤, `namespace sherbet` 앞에 삽입:

```c
// Sherbet 자체 버전. 업스트림 리쉐이드 버전(res/version.h)과 완전히 별개다.
// res/version.h 는 git describe --tags 기반인데 sherbet-base 에는 조상 태그가 없어
// 항상 0.0.0.N 으로 떨어지고 32/64비트가 서로 달라 식별자로 쓸 수 없다.
//
// ⚠️ 릴리스 태그는 반드시 `sherbet-1.0.0` 형식(v 금지). `sherbet-v1.0.0` 은
//    tools/update_version.ps1 의 앵커 없는 -match 에 걸려 res/version.h 를 탈취한다.
// ⚠️ 이 값과 태그 접미사가 다르면 release.yml 이 빌드를 실패시킨다.
#ifndef SHERBET_VERSION
#define SHERBET_VERSION "1.0.0"
#endif
```

- [ ] **Step 2: 값이 들어갔는지 확인**

Run: `grep -n 'SHERBET_VERSION' source/sherbet_owner.h`
Expected: `#define SHERBET_VERSION "1.0.0"` 줄이 출력된다.

- [ ] **Step 3: `dll_main.cpp`에 익스포트 심볼 추가**

`dll_main.cpp:16`의 `ReShadeVersion` 익스포트를 찾는다. 그 **바로 아래 줄**에 추가하고, 같은 파일 상단 include 목록에 `#include "sherbet_owner.h"`가 없으면 추가한다:

```cpp
extern "C" __declspec(dllexport) const char *SherbetVersion = SHERBET_VERSION;
```

`#define`만으로는 어느 번역 단위에서도 참조되지 않으면 바이너리에 한 바이트도 남지 않는다. 익스포트 심볼이면 확실히 남고, 배포도구가 바이트에서 찾아 "릴리스에 엉뚱한 아티팩트가 올라갔다"를 잡는다.

- [ ] **Step 4: `update_version.ps1` 앵커 굳히기**

`tools/update_version.ps1:17`을 찾는다:

```powershell
elseif ($(git describe --tags) -match "v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?") {
```

다음으로 바꾼다:

```powershell
elseif ($(git describe --tags --match "v[0-9]*") -match "v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?") {
```

PowerShell `-match`는 앵커 없는 부분문자열 매칭이라, 누가 실수로 `sherbet-v1.4.0` 태그를 밀면 그 안의 `v1.4.0`에 걸려 리쉐이드 `res/version.h`를 탈취한다. `--match`로 업스트림 태그만 후보에 올려 2차 방어를 건다.

- [ ] **Step 5: 태그 규칙이 실제로 안전한지 검증**

Run:
```bash
cd ~/reshade && for t in "sherbet-1.4.0" "sherbet-v1.4.0" "v6.5.1"; do
  printf '%-16s ' "$t"
  pwsh -c "if ('$t' -match 'v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?') { 'MATCH (위험)' } else { 'no match (안전)' }" 2>/dev/null \
    || python3 -c "import re,sys; print('MATCH (위험)' if re.search(r'v(\d+)\.(\d+)\.(\d+)(-\d+-\w+)?', '$t') else 'no match (안전)')"
done
```
Expected:
```
sherbet-1.4.0    no match (안전)
sherbet-v1.4.0   MATCH (위험)
v6.5.1           MATCH (위험)
```
`sherbet-1.4.0`만 안전하다는 것이 태그 규칙의 근거다. 이 출력이 다르면 멈추고 스펙 §2.2를 다시 읽는다.

- [ ] **Step 6: 커밋**

```bash
git add source/sherbet_owner.h source/dll_main.cpp tools/update_version.ps1
git commit -m "Sherbet 자체 버전 상수 도입

res/version.h 는 git describe --tags 기반인데 sherbet-base 에 조상 태그가 없어
항상 0.0.0.N 이고 32/64비트가 서로 달라 자동 업데이트 식별자로 쓸 수 없다.
sherbet_owner.h 에 SHERBET_VERSION 을 두고 바이너리에는 익스포트 심볼로 박는다.
update_version.ps1 은 --match 로 업스트림 태그만 후보에 올려 오타 태그 방어."
```

---

## Task 2: 버전 파싱과 비교

**Files:**
- Create: `source/sherbet_update_core.hpp`
- Create: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 1의 `SHERBET_VERSION`
- Produces:
  - `struct sherbet::update::version3 { unsigned major, minor, patch; }`
  - `bool parse_version(const std::string &s, version3 &out)` — 실패 시 false, `out` 무변경
  - `int version_cmp(const version3 &a, const version3 &b)` — -1 / 0 / 1

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp` 생성:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 호스트(Mac/Linux) clang 로 빌드·실행하는 순수 로직 테스트. Windows 의존 없음.
// clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/t && /tmp/t
#include "sherbet_update_core.hpp"
#include <cassert>
#include <cstdio>

using namespace sherbet::update;

static void test_parse_version() {
	version3 v;
	assert(parse_version("1.4.0", v) && v.major == 1 && v.minor == 4 && v.patch == 0);
	assert(parse_version("0.0.0", v) && v.major == 0 && v.minor == 0 && v.patch == 0);
	assert(parse_version("10.20.30", v) && v.major == 10 && v.minor == 20 && v.patch == 30);

	// 거부 케이스
	assert(!parse_version("", v));
	assert(!parse_version("1.4", v));          // 필드 부족
	assert(!parse_version("1.4.0.1", v));      // 필드 초과
	assert(!parse_version("1.4.x", v));        // 숫자 아님
	assert(!parse_version("v1.4.0", v));       // 접두사
	assert(!parse_version(" 1.4.0", v));       // 앞 공백
	assert(!parse_version("1.4.0 ", v));       // 뒤 공백
	assert(!parse_version("1..0", v));         // 빈 필드
	assert(!parse_version("1.4.99999999", v)); // 거대 정수

	// 실패 시 out 이 오염되지 않아야 한다
	version3 keep{ 7, 7, 7 };
	assert(!parse_version("garbage", keep));
	assert(keep.major == 7 && keep.minor == 7 && keep.patch == 7);
}

static void test_version_cmp() {
	version3 a, b;
	assert(parse_version("1.4.0", a) && parse_version("1.4.0", b));
	assert(version_cmp(a, b) == 0);

	// 문자열 비교였다면 틀리는 케이스 — "1.10.0" < "1.9.0" 이 되어버린다
	assert(parse_version("1.10.0", a) && parse_version("1.9.0", b));
	assert(version_cmp(a, b) == 1);
	assert(version_cmp(b, a) == -1);

	assert(parse_version("2.0.0", a) && parse_version("1.99.99", b));
	assert(version_cmp(a, b) == 1);

	assert(parse_version("1.0.1", a) && parse_version("1.0.0", b));
	assert(version_cmp(a, b) == 1);
}

int main() {
	test_parse_version();
	test_version_cmp();
	std::printf("sherbet_update_core: ALL PASS\n");
	return 0;
}
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `fatal error: 'sherbet_update_core.hpp' file not found`

- [ ] **Step 3: 최소 구현**

`source/sherbet_update_core.hpp` 생성:

```cpp
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
	}
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "업데이트 버전 파싱·비교 순수 함수 추가

정수 3필드 사전식 비교. 문자열 비교면 1.10.0 < 1.9.0 이 되어 새 버전을
구버전으로 오판한다. 파싱 실패는 false 반환 + out 무변경으로 '업데이트 없음' 처리."
```

---

## Task 3: SHA-256 (벤더링)

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Modify: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 2의 헤더
- Produces:
  - `struct sherbet::update::sha256_ctx`
  - `void sha256_init(sha256_ctx &c)`
  - `void sha256_update(sha256_ctx &c, const void *data, std::size_t n)`
  - `std::string sha256_final_hex(sha256_ctx &c)` — 소문자 64자 hex
  - `bool is_sha256_hex(const std::string &s)` — 소문자 64hex 형식 검사

**왜 벤더링하는가:** `ReShade.vcxproj`에 `AdditionalDependencies`가 하나도 없다. `bcrypt.lib`를 새로 걸면 링크 성공 여부를 7분 CI 왕복으로만 알 수 있고, BCrypt는 CNG 공급자 테이블·레지스트리·힙을 건드려 **`DllMain`에서 호출하는 것이 안전하지 않다**. 벤더링하면 링크 의존성 0이고 맥에서 NIST 벡터로 검증된다.

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp`의 `test_version_cmp()` 뒤, `main()` 앞에 추가:

```cpp
static std::string sha_of(const std::string &s) {
	sha256_ctx c; sha256_init(c);
	sha256_update(c, s.data(), s.size());
	return sha256_final_hex(c);
}

static void test_sha256_nist() {
	// NIST 표준 벡터
	assert(sha_of("") ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	assert(sha_of("abc") ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	assert(sha_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	std::string million(1000000, 'a');
	assert(sha_of(million) ==
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

static void test_sha256_padding_boundaries() {
	// 패딩 버그가 사는 곳이 정확히 여기다. 길이별로 일괄 해시와 스트리밍 해시가 같아야 한다.
	const std::size_t lens[] = { 0, 1, 54, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 1000 };
	for (std::size_t n : lens) {
		std::string data;
		for (std::size_t i = 0; i < n; ++i) data += static_cast<char>('a' + (i % 26));
		const std::string once = sha_of(data);

		// 홀수 청크(1,7,13,…)로 나눠 넣어도 같은 결과여야 한다
		sha256_ctx c; sha256_init(c);
		std::size_t off = 0, chunk = 1;
		while (off < data.size()) {
			const std::size_t take = (data.size() - off < chunk) ? data.size() - off : chunk;
			sha256_update(c, data.data() + off, take);
			off += take;
			chunk = chunk * 2 + 5; // 1, 7, 19, 43, …
		}
		assert(sha256_final_hex(c) == once);
	}
}

static void test_is_sha256_hex() {
	assert(is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	assert(!is_sha256_hex(""));
	assert(!is_sha256_hex("E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855")); // 대문자
	assert(!is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b85"));  // 63자
	assert(!is_sha256_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b8555")); // 65자
	assert(!is_sha256_hex("g3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")); // hex 아님
}
```

`main()`에 호출 추가:

```cpp
	test_sha256_nist();
	test_sha256_padding_boundaries();
	test_is_sha256_hex();
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `use of undeclared identifier 'sha256_ctx'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp`의 `#include <cstddef>` 아래에 `#include <cstdint>`를 추가하고, `version_cmp` 뒤에 삽입:

```cpp
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
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 시스템 도구와 교차 검증**

Run:
```bash
printf 'abc' | shasum -a 256
printf '' | shasum -a 256
```
Expected: 각각 `ba7816bf…20015ad`, `e3b0c442…852b855` — Step 1 테스트의 기대값과 일치해야 한다. 직접 짠 SHA-256이 조용히 틀리면 전 고객이 업데이트 불능이 되므로 표준 도구로 한 번 더 확인한다.

- [ ] **Step 6: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "SHA-256 벤더링 구현 + NIST 벡터 테스트

bcrypt.lib 를 새로 링크하지 않는다: ReShade.vcxproj 에 AdditionalDependencies 가
없어 링크 성공을 CI 왕복으로만 알 수 있고, BCrypt 는 DllMain 에서 호출이 안전하지 않다.
NIST 4벡터 + 패딩 경계(55/56/63/64/119/120) + 홀수 청크 스트리밍 일치 검증."
```

---

## Task 4: URL 피닝과 분해

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Modify: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 3의 헤더
- Produces:
  - `bool url_allowed(const std::string &url)`
  - `bool split_https_url(const std::string &url, std::string &host, std::string &path)`

**왜 호스트만으로는 안 되는가:** `github.com`은 누구나 5초 만에 리포를 만들고 릴리스를 올릴 수 있는 멀티테넌트 호스트다. 호스트만 검사하면 조건이 "우리 릴리스에 올려야 한다"가 아니라 "아무 GitHub 릴리스에나 올리면 된다"로 무너진다. **URL 전체 접두사 피닝이 홈서버 단독 침해 방어의 유일한 근거다**(스펙 §3.4).

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp`에 추가:

```cpp
static void test_url_allowed() {
	const std::string good =
		"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll";
	assert(url_allowed(good));

	// 다른 소유자·리포 — 호스트만 검사했다면 통과해버리는 케이스
	assert(!url_allowed("https://github.com/attacker/evil/releases/download/x/ReShade64.dll"));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/other/releases/download/x/a.dll"));
	// 유사 호스트
	assert(!url_allowed("https://github.com.evil.kr/Jeong-Ryeol/reshade/releases/download/x/a.dll"));
	// 스킴
	assert(!url_allowed("http://github.com/Jeong-Ryeol/reshade/releases/download/x/a.dll"));
	// 경로 조작
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/../../../x.dll"));
	// userinfo 를 이용한 호스트 위장
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/@evil.kr/a.dll"));
	// 빈 문자열·접두사만
	assert(!url_allowed(""));
	assert(!url_allowed("https://github.com/Jeong-Ryeol/reshade/releases/download/"));
}

static void test_split_https_url() {
	std::string host, path;
	assert(split_https_url(
		"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll",
		host, path));
	assert(host == "github.com");
	assert(path == "/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll");

	assert(!split_https_url("http://github.com/a", host, path)); // https 아님
	assert(!split_https_url("https://github.com", host, path));  // 경로 없음
	assert(!split_https_url("", host, path));
}
```

`main()`에 추가:

```cpp
	test_url_allowed();
	test_split_https_url();
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `use of undeclared identifier 'url_allowed'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp`의 `is_sha256_hex` 뒤에 삽입:

```cpp
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
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "업데이트 URL 전체 접두사 피닝 추가

호스트만 화이트리스트하면 github.com 은 누구나 릴리스를 올릴 수 있는 멀티테넌트
호스트라 방어가 아니다. 홈서버가 단독으로 털려도 사용자를 공격자 에셋으로
보내지 못하게 하는 것은 전체 접두사 피닝뿐이다."
```

---

## Task 5: PE 헤더 검사

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Modify: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 4의 헤더
- Produces: `bool pe_check(const unsigned char *head, std::size_t len, bool want_x64)`

**왜 필요한가:** sha256만으로는 "유효한 내 아키텍처 DLL인가"를 전혀 못 본다. x64 슬롯에 32비트 DLL을 넣는 운영 실수 한 번이면, 다음 실행에 임포트가 `ERROR_BAD_EXE_FORMAT`으로 실패하고 **롤백 코드조차 안 도는 브릭**이 된다(스펙 §8).

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp`에 추가:

```cpp
// 최소한의 가짜 PE 헤더를 만든다. e_lfanew 는 0x80 에 둔다.
static std::string make_pe(unsigned short machine, unsigned short characteristics,
                           unsigned int e_lfanew = 0x80, bool mz = true, bool sig = true) {
	std::string b(0x200, '\0');
	if (mz) { b[0] = 'M'; b[1] = 'Z'; }
	b[0x3c] = static_cast<char>(e_lfanew & 0xff);
	b[0x3d] = static_cast<char>((e_lfanew >> 8) & 0xff);
	b[0x3e] = static_cast<char>((e_lfanew >> 16) & 0xff);
	b[0x3f] = static_cast<char>((e_lfanew >> 24) & 0xff);
	if (sig && e_lfanew + 24 <= b.size()) {
		b[e_lfanew] = 'P'; b[e_lfanew + 1] = 'E'; b[e_lfanew + 2] = '\0'; b[e_lfanew + 3] = '\0';
		b[e_lfanew + 4] = static_cast<char>(machine & 0xff);
		b[e_lfanew + 5] = static_cast<char>((machine >> 8) & 0xff);
		b[e_lfanew + 22] = static_cast<char>(characteristics & 0xff);
		b[e_lfanew + 23] = static_cast<char>((characteristics >> 8) & 0xff);
	}
	return b;
}

// make_pe 는 e_lfanew + 24 <= 버퍼크기 일 때만 PE 서명을 쓴다. 경계검사 자체를 시험하려면
// 버퍼 밖을 가리키는 e_lfanew 도 그대로 기록해야 하므로 여기서 직접 만든다.
// write_nt 를 켤 때는 호출자가 e_lfanew + 24 <= len 을 보장할 것.
static std::string raw_pe(std::size_t len, std::uint32_t e_lfanew, bool write_nt,
                          unsigned short machine = 0x8664, unsigned short characteristics = 0x2000) {
	std::string b(len, '\0');
	if (len >= 2) { b[0] = 'M'; b[1] = 'Z'; }
	if (len >= 0x40) {
		b[0x3c] = static_cast<char>(e_lfanew & 0xff);
		b[0x3d] = static_cast<char>((e_lfanew >> 8) & 0xff);
		b[0x3e] = static_cast<char>((e_lfanew >> 16) & 0xff);
		b[0x3f] = static_cast<char>((e_lfanew >> 24) & 0xff);
	}
	if (write_nt) {
		const std::size_t o = static_cast<std::size_t>(e_lfanew);
		b[o] = 'P'; b[o + 1] = 'E'; b[o + 2] = '\0'; b[o + 3] = '\0';
		b[o + 4] = static_cast<char>(machine & 0xff);
		b[o + 5] = static_cast<char>((machine >> 8) & 0xff);
		b[o + 22] = static_cast<char>(characteristics & 0xff);
		b[o + 23] = static_cast<char>((characteristics >> 8) & 0xff);
	}
	return b;
}

static void test_pe_check() {
	const unsigned short DLL = 0x2000; // IMAGE_FILE_DLL
	const std::string x64 = make_pe(0x8664, DLL);
	const std::string x86 = make_pe(0x014c, DLL);

	assert(pe_check(reinterpret_cast<const unsigned char *>(x64.data()), x64.size(), true));
	assert(pe_check(reinterpret_cast<const unsigned char *>(x86.data()), x86.size(), false));

	// 아키텍처 교차 — 이 한 줄이 유일한 브릭 시나리오를 막는다
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x86.data()), x86.size(), true));
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x64.data()), x64.size(), false));

	// DLL 비트 없음(EXE)
	const std::string exe = make_pe(0x8664, 0x0002);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(exe.data()), exe.size(), true));

	// MZ 아님
	const std::string nomz = make_pe(0x8664, DLL, 0x80, false);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(nomz.data()), nomz.size(), true));

	// PE 서명 없음
	const std::string nosig = make_pe(0x8664, DLL, 0x80, true, false);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(nosig.data()), nosig.size(), true));

	// e_lfanew 가 버퍼 밖
	const std::string far = make_pe(0x8664, DLL, 0x00100000);
	assert(!pe_check(reinterpret_cast<const unsigned char *>(far.data()), far.size(), true));

	// 버퍼가 너무 짧음
	assert(!pe_check(reinterpret_cast<const unsigned char *>(x64.data()), 8, true));
	assert(!pe_check(nullptr, 0, true));

	// ── e_lfanew 32비트 랩어라운드 ────────────────────────────────────────
	// `e_lfanew + 24 > len` 을 uint32 로 계산하면 0xFFFFFFFF + 24 == 23 이 되어
	// 경계검사를 통과하고 head + 0xFFFFFFFF 를 역참조한다(SIGSEGV 또는 무관한 메모리 읽기).
	// 입력 4바이트는 전부 공격자가 고르는 값이다. 아래 세 개는 크래시 없이 false 여야 한다.
	{
		const std::string w32 = raw_pe(0x40, 0xFFFFFFFFu, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}
	{
		// 랩 구간의 하한 — 0xFFFFFFE8 + 24 == 0
		const std::string w32 = raw_pe(0x40, 0xFFFFFFE8u, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}
	{
		const std::string w32 = raw_pe(0x40, 0xFFFFFFF0u, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(w32.data()), w32.size(), true));
	}

	// ── 경계 정확도: e_lfanew + 24 == len 은 통과, +1 이면 거부 ────────────
	// COFF 헤더에서 우리가 읽는 최대 오프셋이 +23 이므로 딱 24바이트면 충분하다.
	{
		const std::uint32_t e = 0x40;
		const std::string tight = raw_pe(e + 24, e, true, 0x8664, DLL); // len == e_lfanew + 24
		assert(pe_check(reinterpret_cast<const unsigned char *>(tight.data()), tight.size(), true));
		// 1바이트 부족(= e_lfanew + 24 == len + 1)이면 마지막 바이트를 못 읽으므로 거부
		assert(!pe_check(reinterpret_cast<const unsigned char *>(tight.data()), tight.size() - 1, true));
	}

	// len == 0 인데 포인터는 유효 — 널 검사와 별개로 길이만으로 걸러야 한다
	{
		const std::string any = raw_pe(0x40, 0x40, false);
		assert(!pe_check(reinterpret_cast<const unsigned char *>(any.data()), 0, true));
	}
}
```

`main()`에 `test_pe_check();` 추가.

> **경계검사 오버플로가 이 태스크의 핵심 위험이다.** `pe_check` 의 입력은 네트워크에서 받은
> 4MB 파일 그대로이고 `e_lfanew` 4바이트는 공격자가 통째로 고른다. 위 테스트 목록에
> `0xFFFFFFFF`·`0xFFFFFFE8`(랩 하한)·`0xFFFFFFF0`·정확 경계쌍(`e_lfanew + 24 == len` 통과,
> `== len + 1` 거부)·`len == 0` + 유효 포인터가 **반드시** 있어야 한다. 이것들이 없으면
> 32비트 덧셈 랩어라운드가 테스트를 다 통과한 채로 살아남는다.

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `use of undeclared identifier 'pe_check'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp`의 `split_https_url` 뒤에 삽입:

```cpp
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
			// ⚠️ `e_lfanew + 24 > len` 으로 쓰면 안 된다. e_lfanew 는 uint32 라 덧셈이 32비트에서
			// 랩어라운드해(0xFFFFFFFF + 24 == 23) 경계검사를 통과하고 head + 0xFFFFFFFF 를
			// 역참조한다. 이 4바이트는 네트워크에서 온 파일이 통째로 고르는 값이다.
			// size_t 로 캐스팅한 덧셈도 32비트 빌드(ReShade32)에선 여전히 랩한다. 반드시
			// 이미 넓혀진 len 쪽에서 뺀다. 위 `len < 0x40` 로 len >= 64 라 아래 가드는
			// 중복이지만, len - 24 가 언더플로하지 않음을 한 줄 안에서 증명해 둔다.
			if (len < 24) return false;
			if (e_lfanew < 0x40 || static_cast<std::size_t>(e_lfanew) > len - 24) return false;
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
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 실제 DLL로 교차 검증**

Run:
```bash
cd ~/reshade && gh run download 29590709842 -n "Sherbet_Demo (64-bit)" -D /tmp/sherbet64 2>/dev/null \
  && python3 -c "
d=open('/tmp/sherbet64/ReShade64.dll','rb').read(4096)
import struct
e=struct.unpack_from('<I', d, 0x3c)[0]
print('MZ:', d[:2]); print('e_lfanew:', hex(e)); print('PE sig:', d[e:e+4])
print('Machine:', hex(struct.unpack_from('<H', d, e+4)[0]), '(0x8664 이어야 함)')
print('Characteristics:', hex(struct.unpack_from('<H', d, e+22)[0]), '& 0x2000 =',
      hex(struct.unpack_from('<H', d, e+22)[0] & 0x2000))
"
```
Expected: `Machine: 0x8664`, `& 0x2000 = 0x2000`. 실제 배포 DLL이 우리 검사를 통과하는 형태인지 확인하는 단계다. 아티팩트가 만료됐으면 이 스텝은 건너뛰고 다음으로 간다.

- [ ] **Step 6: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "PE 헤더 검사 추가 — 아키텍처 오배포 브릭 방지

sha256 은 바이트 온전성만 보고 '무엇인가'는 못 본다. x64 슬롯에 32비트 DLL 을
넣는 운영 실수 한 번이면 다음 실행에 ERROR_BAD_EXE_FORMAT 으로 롤백 코드조차
안 도는 브릭이 된다. MZ/e_lfanew/PE00/Machine/IMAGE_FILE_DLL 을 교체 전 필수 게이트로."
```

---

## Task 6: 매니페스트 파싱

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Modify: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 5의 헤더, `sherbet_auth_core.hpp`의 `json_string` / `json_bool_or_null`
- Produces:
  - `struct sherbet::update::info { bool ok; std::string version, url, sha256, min_version, notes, notice; unsigned long long size; bool allow_downgrade; }`
  - `info parse_manifest(const std::string &body, const char *arch)`

**타입 규약(스펙 §3.3):** `json_string`은 **따옴표로 감싼 문자열만** 읽는다. 서버는 `schema`/`size`/`version`/`min_version`/`sha256`/`url`/`notes`/`notice`를 전부 문자열로 직렬화하고, `allow_downgrade`만 진짜 JSON 불리언으로 낸다.

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp`에 추가:

```cpp
static const char *kGoodManifest =
	"{\"schema\":\"1\",\"arch\":\"x64\",\"version\":\"1.4.0\",\"min_version\":\"1.0.0\","
	"\"allow_downgrade\":false,\"size\":\"4312576\","
	"\"sha256\":\"3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80\","
	"\"url\":\"https://github.com/Jeong-Ryeol/reshade/releases/download/sherbet-1.4.0/ReShade64.dll\","
	"\"notice\":\"\",\"notes\":\"OSD 가로 배치 추가\\n로그인 만료 버그 수정\"}";

static void test_parse_manifest_good() {
	const info u = parse_manifest(kGoodManifest, "x64");
	assert(u.ok);
	assert(u.version == "1.4.0");
	assert(u.min_version == "1.0.0");
	assert(u.size == 4312576ULL);
	assert(u.sha256 == "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80");
	assert(!u.allow_downgrade);
	assert(u.notes.find('\n') != std::string::npos); // \n 이스케이프가 실제 개행으로 풀렸는지
	assert(u.notice.empty());
}

static void test_parse_manifest_rejects() {
	// arch 불일치 — 서버가 x64 를 줬는데 우리가 x86 빌드
	assert(!parse_manifest(kGoodManifest, "x86").ok);

	// 빈 바디 / 잡음 / 503 바디
	assert(!parse_manifest("", "x64").ok);
	assert(!parse_manifest("not json at all", "x64").ok);
	assert(!parse_manifest("{\"error\":\"upstream_unavailable\"}", "x64").ok);

	std::string m;
	// schema 불일치
	m = kGoodManifest; m.replace(m.find("\"schema\":\"1\""), 12, "\"schema\":\"2\"");
	assert(!parse_manifest(m, "x64").ok);

	// sha256 이 64hex 아님
	m = kGoodManifest; m.replace(m.find("3f1c9a"), 6, "ZZZZZZ");
	assert(!parse_manifest(m, "x64").ok);

	// url 이 피닝 접두사 밖
	m = kGoodManifest;
	m.replace(m.find("Jeong-Ryeol/reshade"), 19, "attacker/evil00000");
	assert(!parse_manifest(m, "x64").ok);

	// size 범위 밖 (1MiB 미만)
	m = kGoodManifest; m.replace(m.find("\"4312576\""), 9, "\"1000\"  ");
	assert(!parse_manifest(m, "x64").ok);

	// size 가 문자열이 아니라 숫자 — json_string 이 못 읽으므로 거부되어야 한다
	m = kGoodManifest; m.replace(m.find("\"size\":\"4312576\""), 16, "\"size\":4312576  ");
	assert(!parse_manifest(m, "x64").ok);

	// version 파싱 불가
	m = kGoodManifest; m.replace(m.find("\"version\":\"1.4.0\""), 17, "\"version\":\"abc\"  ");
	assert(!parse_manifest(m, "x64").ok);
}

static void test_parse_manifest_key_confusion() {
	// "version" 이 "min_version" 에 오탐하면 안 된다.
	// json_string 은 따옴표를 포함한 needle 로 찾으므로 "version" 은 "min_version" 안의
	// version 에 걸리지 않는다(앞이 " 가 아니라 _ 이므로). 이 성질을 회귀로 못 박는다.
	const info u = parse_manifest(kGoodManifest, "x64");
	assert(u.version == "1.4.0" && u.min_version == "1.0.0");
}

static void test_parse_manifest_truncated() {
	// 잘린 입력 전수 — 파서가 범위를 넘어가 크래시하지 않아야 한다.
	const std::string full = kGoodManifest;
	for (std::size_t n = 0; n < full.size(); ++n) {
		const info u = parse_manifest(full.substr(0, n), "x64");
		(void)u; // ok 여부는 상관없다. 크래시하지 않는 것이 검사 대상.
	}
}
```

`main()`에 추가:

```cpp
	test_parse_manifest_good();
	test_parse_manifest_rejects();
	test_parse_manifest_key_confusion();
	test_parse_manifest_truncated();
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `use of undeclared identifier 'parse_manifest'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp` 상단 include에 `#include "sherbet_auth_core.hpp"`를 추가하고, `pe_check` 뒤에 삽입:

```cpp
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
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "매니페스트 파싱 추가 — 기존 평면 파서 재사용

sherbet_auth_core.hpp 의 json_string/json_bool_or_null 만 쓴다(새 파서 = 새 버그).
json_string 은 따옴표 문자열만 읽으므로 서버는 size/schema 를 문자열로 직렬화해야 하고,
숫자로 오면 거부되는 것을 테스트로 못 박았다. 잘린 입력 전수 크래시 검사 포함."
```

---

## Task 7: 부팅마커와 판정

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Modify: `tools/sherbet_update_test.cpp`

**Interfaces:**
- Consumes: Task 6의 헤더
- Produces:
  - `struct sherbet::update::boot_marker` — 필드 `state, version, prev, bak, exe, sha, bad_ver, bad_sha`(전부 `std::string`), `tries`(int), `unknown`(`std::vector<std::string>`, 모르는 줄 원문)
  - `std::string serialize_marker(const boot_marker &m)`
  - `bool parse_marker(const std::string &text, boot_marker &out)`
  - `enum class boot_action { none, count, rollback }`
  - `boot_action decide_boot(const boot_marker &m, int max_tries = 2)`
  - `bool should_offer(const std::string &cur, const info &u, const std::string &bad_ver, const std::string &bad_sha)`
  - `bool is_mandatory(const std::string &cur, const info &u)`

- [ ] **Step 1: 실패하는 테스트 작성**

`tools/sherbet_update_test.cpp` 상단 include에 `#include <string>`가 없으면 추가하고, 아래를 추가:

```cpp
static void test_marker_roundtrip() {
	boot_marker m;
	m.state = "pending"; m.version = "1.4.0"; m.prev = "1.3.0";
	m.bak = "dxgi.dll.sherbet-bak"; m.exe = "FiveM_b3095_GTAProcess.exe";
	m.sha = "3f1c9a4b5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80912a3b4c5d6e7f80";
	m.tries = 1;

	boot_marker back;
	assert(parse_marker(serialize_marker(m), back));
	assert(back.state == m.state && back.version == m.version && back.prev == m.prev);
	assert(back.bak == m.bak && back.exe == m.exe && back.sha == m.sha && back.tries == 1);
}

static void test_marker_preserves_unknown_keys() {
	// writer 가 셋(워커/렌더/다른 프로세스 attach)이라 모르는 키를 지우면 서로의 필드가 날아간다.
	const std::string text =
		"state=pending\nversion=1.4.0\nfuture_field=hello\ntries=1\nanother=42\n";
	boot_marker m;
	assert(parse_marker(text, m));
	const std::string out = serialize_marker(m);
	assert(out.find("future_field=hello") != std::string::npos);
	assert(out.find("another=42") != std::string::npos);
	assert(out.find("state=pending") != std::string::npos);
}

static void test_marker_tolerates_garbage() {
	boot_marker m;
	assert(parse_marker("state=pending\n\n= \nno-equals-here\ntries=notanumber\n", m));
	assert(m.state == "pending");
	assert(m.tries == 0); // 비정수는 0 으로
	boot_marker empty;
	assert(!parse_marker("", empty));       // state 없으면 실패
	assert(!parse_marker("tries=3\n", empty)); // state 없으면 실패
}

static void test_decide_boot() {
	boot_marker m;
	// 마커 없음(state 비어있음) → none
	assert(decide_boot(m) == boot_action::none);

	m.state = "pending"; m.tries = 0;
	assert(decide_boot(m) == boot_action::count);   // 0+1 = 1 < 2

	m.tries = 1;
	assert(decide_boot(m) == boot_action::rollback); // 1+1 = 2 >= 2

	m.tries = 5;
	assert(decide_boot(m) == boot_action::rollback);

	m.state = "rolledback"; m.tries = 0;
	assert(decide_boot(m) == boot_action::none);    // 블랙리스트 상태는 카운트하지 않는다

	m.state = "rollback_failed";
	assert(decide_boot(m) == boot_action::none);

	m.state = "swapping";
	assert(decide_boot(m) == boot_action::none);    // 교체 중단은 startup_repair 가 따로 다룬다

	// max_tries 조절
	boot_marker p; p.state = "pending"; p.tries = 1;
	assert(decide_boot(p, 3) == boot_action::count);
	p.tries = 2;
	assert(decide_boot(p, 3) == boot_action::rollback);
}

static void test_should_offer() {
	const info u = parse_manifest(kGoodManifest, "x64"); // version 1.4.0
	assert(u.ok);

	assert(should_offer("1.3.0", u, "", ""));   // 신버전 → 제안
	assert(!should_offer("1.4.0", u, "", ""));  // 같음 → 안 함
	assert(!should_offer("1.5.0", u, "", ""));  // 내가 더 최신 → 안 함(allow_downgrade=false)

	// 블랙리스트: (버전, sha) 쌍이 모두 일치할 때만 차단
	assert(!should_offer("1.3.0", u, "1.4.0", u.sha256));
	// 같은 번호로 고쳐 재배포하면 sha 가 달라 다시 제안되어야 한다
	assert(should_offer("1.3.0", u, "1.4.0", "0000000000000000000000000000000000000000000000000000000000000000"));
	// 버전만 다르면 차단하지 않는다
	assert(should_offer("1.3.0", u, "1.2.0", u.sha256));

	// 현재 버전이 파싱 불가면 제안하지 않는다(안전 측)
	assert(!should_offer("garbage", u, "", ""));

	// ok 가 아닌 매니페스트는 제안하지 않는다
	info bad;
	assert(!should_offer("1.0.0", bad, "", ""));
}

static void test_should_offer_downgrade() {
	std::string m = kGoodManifest;
	m.replace(m.find("\"allow_downgrade\":false"), 23, "\"allow_downgrade\":true ");
	const info d = parse_manifest(m, "x64");
	assert(d.ok && d.allow_downgrade);
	// 킬스위치: 이미 더 최신을 쓰는 사람도 권장 버전으로 되돌리도록 제안한다
	assert(should_offer("1.5.0", d, "", ""));
	assert(!should_offer("1.4.0", d, "", "")); // 같으면 여전히 안 함
}

static void test_is_mandatory() {
	const info u = parse_manifest(kGoodManifest, "x64"); // min_version 1.0.0
	assert(!is_mandatory("1.3.0", u));  // 1.3.0 >= 1.0.0
	assert(!is_mandatory("1.0.0", u));  // 경계: 같으면 필수 아님

	std::string m = kGoodManifest;
	m.replace(m.find("\"min_version\":\"1.0.0\""), 21, "\"min_version\":\"1.3.5\"");
	const info hi = parse_manifest(m, "x64");
	assert(hi.ok);
	assert(is_mandatory("1.3.0", hi));  // 1.3.0 < 1.3.5

	// min_version 없음 → 절대 필수 아님
	m = kGoodManifest;
	m.replace(m.find("\"min_version\":\"1.0.0\""), 21, "\"min_versionX\":\"1.0.0\"");
	const info nomin = parse_manifest(m, "x64");
	assert(nomin.ok && nomin.min_version.empty());
	assert(!is_mandatory("0.1.0", nomin));

	// 현재 버전 파싱 불가 → false
	assert(!is_mandatory("garbage", u));
	// ok 아님 → false
	info bad;
	assert(!is_mandatory("1.0.0", bad));
}
```

`main()`에 추가:

```cpp
	test_marker_roundtrip();
	test_marker_preserves_unknown_keys();
	test_marker_tolerates_garbage();
	test_decide_boot();
	test_should_offer();
	test_should_offer_downgrade();
	test_is_mandatory();
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test`
Expected: FAIL — `use of undeclared identifier 'boot_marker'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp` 상단 include에 `#include <vector>`를 추가하고, `parse_manifest` 뒤에 삽입:

```cpp
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

		inline std::string serialize_marker(const boot_marker &m)
		{
			std::string o;
			o += "state=" + m.state + "\n";
			if (!m.version.empty()) o += "version=" + m.version + "\n";
			if (!m.prev.empty())    o += "prev=" + m.prev + "\n";
			if (!m.bak.empty())     o += "bak=" + m.bak + "\n";
			if (!m.exe.empty())     o += "exe=" + m.exe + "\n";
			if (!m.sha.empty())     o += "sha=" + m.sha + "\n";
			if (!m.bad_ver.empty()) o += "bad_ver=" + m.bad_ver + "\n";
			if (!m.bad_sha.empty()) o += "bad_sha=" + m.bad_sha + "\n";
			o += "tries=" + std::to_string(m.tries) + "\n";
			for (const std::string &line : m.unknown) o += line + "\n";
			return o;
		}

		inline bool parse_marker(const std::string &text, boot_marker &out)
		{
			boot_marker t;
			std::size_t i = 0;
			while (i < text.size())
			{
				const std::size_t eol = text.find('\n', i);
				std::string line = text.substr(i, eol == std::string::npos ? std::string::npos : eol - i);
				i = (eol == std::string::npos) ? text.size() : eol + 1;
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
				if (line.empty()) continue;
				const std::size_t eq = line.find('=');
				if (eq == std::string::npos || eq == 0) { t.unknown.push_back(line); continue; }
				const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
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
					unsigned long long n = 0;
					t.tries = detail::parse_u64(val, n) ? static_cast<int>(n) : 0;
				}
				else t.unknown.push_back(line); // 모르는 키는 원문 보존
			}
			if (t.state.empty()) return false;
			out = t;
			return true;
		}

		enum class boot_action { none, count, rollback };

		// 스펙 §5.4. 확정사항 3 의 '2회 연속 부팅 실패' 와 정확히 일치한다.
		inline boot_action decide_boot(const boot_marker &m, int max_tries = 2)
		{
			if (m.state != "pending") return boot_action::none;
			return (m.tries + 1 >= max_tries) ? boot_action::rollback : boot_action::count;
		}

		// 배너를 띄울 것인가. 블랙리스트는 (버전, sha) 쌍으로 본다 —
		// 같은 번호로 고쳐 재배포하면 sha 가 달라 자동으로 다시 제안된다.
		inline bool should_offer(const std::string &cur, const info &u,
			const std::string &bad_ver, const std::string &bad_sha)
		{
			if (!u.ok) return false;
			version3 a, b;
			if (!parse_version(cur, a)) return false;   // 내 버전을 모르면 아무것도 하지 않는다
			if (!parse_version(u.version, b)) return false;
			if (!bad_ver.empty() && u.version == bad_ver && u.sha256 == bad_sha) return false;
			const int c = version_cmp(b, a);
			if (c > 0) return true;                     // 서버가 더 최신
			if (c < 0) return u.allow_downgrade;        // 킬스위치 강등
			return false;                               // 같음
		}

		// 필수 업데이트 표시 여부. 값 없음/파싱 실패면 반드시 false —
		// 오타 한 번(예: 9.9.9)으로 전 고객 UI 를 잠그면 안 된다(스펙 §3.5).
		inline bool is_mandatory(const std::string &cur, const info &u)
		{
			if (!u.ok || u.min_version.empty()) return false;
			version3 a, m;
			if (!parse_version(cur, a)) return false;
			if (!parse_version(u.min_version, m)) return false;
			return version_cmp(a, m) < 0;
		}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test`
Expected: `sherbet_update_core: ALL PASS`

- [ ] **Step 5: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_update_test.cpp
git commit -m "부팅마커 직렬화와 롤백·제안 판정 추가

마커는 writer 가 셋이라 모르는 키를 보존해야 서로의 필드를 지우지 않는다.
블랙리스트는 (버전, sha) 쌍이라 같은 번호로 고쳐 재배포하면 다시 제안된다.
is_mandatory 는 값 없음·파싱 실패에 반드시 false — 오타 하나로 전 고객 UI 를 잠그지 않는다."
```

---

## Task 8: 교체 상태머신 시뮬레이터

**Files:**
- Modify: `source/sherbet_update_core.hpp`
- Create: `tools/sherbet_swap_sim.cpp`

**Interfaces:**
- Consumes: Task 7의 헤더
- Produces:
  - `enum class disk_state { normal, staged, swapping_lost_self, rollback_ready, broken_no_dll }`
  - `disk_state classify(bool has_self, bool has_new, bool has_bak, const boot_marker &m)`
  - `enum class repair_action { none, promote_pending, restore_from_bak, restore_from_new, give_up }`
  - `repair_action decide_repair(disk_state s)`

**왜 이 태스크가 이 계획에서 가장 값어치 있는가:** 맥에서는 실제 파일 교체를 검증할 수 없다. 대신 상태머신을 순수 함수로 뽑아 **가짜 파일시스템 위에서 모든 중단 지점에 전원차단을 주입하고, 어느 지점에서 끊겨도 정상 상태로 수렴하는지**를 단언한다.

- [ ] **Step 1: 실패하는 시뮬레이터 작성**

`tools/sherbet_swap_sim.cpp` 생성:

```cpp
/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 가짜 파일시스템 위에서 교체 알고리즘(스펙 §4.4 S7~S13)을 돌리며 단계마다 '전원차단' 을
// 주입하고, 어느 지점에서 끊겨도 startup_repair 가 정상 상태로 수렴하는지 단언한다.
// clang++ -std=c++17 -Wall -Isource tools/sherbet_swap_sim.cpp -o /tmp/sim && /tmp/sim
#include "sherbet_update_core.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>

using namespace sherbet::update;

// 가짜 파일시스템: 경로 → 내용("OLD"/"NEW"/마커 텍스트)
using fs_t = std::map<std::string, std::string>;

static const char *SELF = "dxgi.dll";
static const char *NEWF = "dxgi.dll.sherbet-new";
static const char *BAKF = "dxgi.dll.sherbet-bak";
static const char *MARK = "sherbet.update";

static bool has(const fs_t &fs, const char *p) { return fs.count(p) != 0; }

static void mv(fs_t &fs, const char *from, const char *to) {
	auto it = fs.find(from);
	if (it == fs.end()) return;
	fs[to] = it->second;
	fs.erase(it);
}

// 교체 절차. cut == k 이면 k 번째 단계 직후 전원이 나간다(그 이후 단계는 실행 안 됨).
static void run_swap(fs_t &fs, int cut) {
	boot_marker m;
	int step = 0;
	// S7 스테이징: 다운로드가 끝나 .sherbet-new 가 놓인 상태
	fs[NEWF] = "NEW";                                   if (++step == cut) return;
	// S9 마커 기록
	m.state = "swapping"; m.version = "1.4.0"; m.prev = "1.3.0";
	m.bak = BAKF; m.exe = "GAME.exe"; m.sha = "deadbeef";
	fs[MARK] = serialize_marker(m);                     if (++step == cut) return;
	// S10 백업
	mv(fs, SELF, BAKF);                                 if (++step == cut) return;
	// S11 설치
	mv(fs, NEWF, SELF);                                 if (++step == cut) return;
	// S13 확정
	m.state = "pending"; m.tries = 0;
	fs[MARK] = serialize_marker(m);                     if (++step == cut) return;
}

// 다음 실행의 복구 경로. 스펙 §5.4 R3.
static void startup_repair(fs_t &fs) {
	boot_marker m;
	if (!has(fs, MARK) || !parse_marker(fs[MARK], m)) return;
	const disk_state s = classify(has(fs, SELF), has(fs, NEWF), has(fs, BAKF), m);
	switch (decide_repair(s)) {
	case repair_action::promote_pending:
		m.state = "pending"; m.tries = 0; fs[MARK] = serialize_marker(m);
		break;
	case repair_action::restore_from_bak:
		mv(fs, BAKF, SELF);
		m.state = "rolledback"; m.bad_ver = m.version; m.bad_sha = m.sha;
		fs[MARK] = serialize_marker(m);
		break;
	case repair_action::restore_from_new:
		mv(fs, NEWF, SELF);
		m.state = "pending"; m.tries = 0; fs[MARK] = serialize_marker(m);
		break;
	case repair_action::give_up:
		m.state = "rollback_failed"; fs[MARK] = serialize_marker(m);
		break;
	case repair_action::none:
		break;
	}
}

int main() {
	// 모든 중단 지점에 대해: 복구 후 반드시 self 가 존재해야 한다(= 게임이 켜진다).
	for (int cut = 1; cut <= 6; ++cut) {
		fs_t fs;
		fs[SELF] = "OLD";
		run_swap(fs, cut);
		startup_repair(fs);
		if (!has(fs, SELF)) {
			std::printf("FAIL: cut=%d 에서 복구 후 %s 가 없음\n", cut, SELF);
			return 1;
		}
		// 복구 후 내용은 OLD 또는 NEW 중 하나여야 한다(쓰레기가 남으면 안 됨)
		assert(fs[SELF] == "OLD" || fs[SELF] == "NEW");
	}

	// 중단 없이 완주한 경우 정상 상태여야 한다
	{
		fs_t fs; fs[SELF] = "OLD";
		run_swap(fs, 0);
		assert(fs[SELF] == "NEW");
		assert(has(fs, BAKF) && fs[BAKF] == "OLD");
		assert(!has(fs, NEWF));
		boot_marker m;
		assert(parse_marker(fs[MARK], m) && m.state == "pending" && m.tries == 0);
	}

	// .bak 이 사라진 상태에서 롤백을 시도하면 아무 파일도 건드리지 않아야 한다
	{
		fs_t fs;
		boot_marker m; m.state = "pending"; m.version = "1.4.0"; m.tries = 1;
		fs[MARK] = serialize_marker(m);
		fs[SELF] = "NEW";
		const disk_state s = classify(true, false, false, m);
		assert(decide_repair(s) != repair_action::restore_from_bak);
	}

	std::printf("sherbet_swap_sim: ALL PASS\n");
	return 0;
}
```

- [ ] **Step 2: 시뮬레이터가 실패하는지 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_swap_sim.cpp -o /tmp/sherbet_swap_sim`
Expected: FAIL — `use of undeclared identifier 'classify'`

- [ ] **Step 3: 최소 구현**

`sherbet_update_core.hpp`의 `is_mandatory` 뒤에 삽입:

```cpp
		// ── 교체 중단 복구 상태머신 ────────────────────────────────────────
		// 맥에서는 실제 파일 교체를 검증할 수 없으므로 판정을 순수 함수로 뽑아
		// tools/sherbet_swap_sim.cpp 가 가짜 파일시스템 위에서 전수 검사한다.
		enum class disk_state
		{
			normal,             // self 존재, 마커가 교체 중이 아님
			staged,             // .sherbet-new 만 놓인 안전 정지 상태
			swapping_lost_self, // 교체 도중 끊겨 self 가 없음
			rollback_ready,     // self 존재 + .bak 존재 (되돌릴 수 있음)
			broken_no_dll       // self 도 .bak 도 .new 도 없음
		};

		inline disk_state classify(bool has_self, bool has_new, bool has_bak, const boot_marker &m)
		{
			if (!has_self)
			{
				if (has_bak) return disk_state::swapping_lost_self;
				if (has_new) return disk_state::swapping_lost_self;
				return disk_state::broken_no_dll;
			}
			if (m.state == "swapping") return disk_state::staged;
			if (has_bak) return disk_state::rollback_ready;
			return disk_state::normal;
		}

		enum class repair_action { none, promote_pending, restore_from_bak, restore_from_new, give_up };

		inline repair_action decide_repair(disk_state s)
		{
			switch (s)
			{
			case disk_state::staged:             return repair_action::promote_pending;
			case disk_state::swapping_lost_self: return repair_action::restore_from_bak;
			case disk_state::broken_no_dll:      return repair_action::give_up;
			case disk_state::rollback_ready:     return repair_action::none;
			case disk_state::normal:             return repair_action::none;
			}
			return repair_action::none;
		}
```

**주의:** `swapping_lost_self`에서 `.bak`이 없고 `.new`만 있는 경우도 복구해야 한다. 시뮬레이터의 `startup_repair`가 `restore_from_bak`을 호출했는데 `.bak`이 없으면 `mv`가 아무것도 안 하므로 self가 계속 없다. Step 4에서 이게 드러나면 `classify`를 다음처럼 나눈다:

```cpp
			if (!has_self)
			{
				if (has_bak) return disk_state::swapping_lost_self;
				if (has_new) return disk_state::staged;   // .new 로부터 재개
				return disk_state::broken_no_dll;
			}
```
그리고 `decide_repair(staged)`가 self 부재 시 `restore_from_new`를 내도록 시그니처를 `decide_repair(disk_state s, bool has_self)`로 확장한다.

- [ ] **Step 4: 시뮬레이터 통과 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_swap_sim.cpp -o /tmp/sherbet_swap_sim && /tmp/sherbet_swap_sim`
Expected: `sherbet_swap_sim: ALL PASS`

FAIL이 나면 어느 `cut`에서 실패했는지 출력된다. Step 3의 주의 사항을 적용하고 다시 돌린다. **여기서 나오는 실패는 진짜 설계 구멍이므로 테스트를 느슨하게 고치지 말 것.**

- [ ] **Step 5: 기존 테스트 회귀 확인**

Run: `cd ~/reshade && clang++ -std=c++17 -Wall -Isource tools/sherbet_update_test.cpp -o /tmp/sherbet_update_test && /tmp/sherbet_update_test && clang++ -std=c++17 -Wall -Isource tools/sherbet_auth_test.cpp -o /tmp/sherbet_auth_test && /tmp/sherbet_auth_test`
Expected:
```
sherbet_update_core: ALL PASS
sherbet_auth_core parsing: ALL PASS
```

- [ ] **Step 6: 커밋**

```bash
git add source/sherbet_update_core.hpp tools/sherbet_swap_sim.cpp
git commit -m "교체 중단 복구 상태머신 + 가짜 파일시스템 시뮬레이터

맥에서 실제 파일 교체를 검증할 수 없으므로 판정을 순수 함수로 뽑고,
가짜 파일시스템 위에서 모든 중단 지점에 전원차단을 주입해 어느 지점에서 끊겨도
self 가 존재하는 상태로 수렴하는지 단언한다. .bak 이 없으면 self 를 먼저
밀어내지 않는 규칙도 여기서 검증된다."
```

---

## Task 9: CI 호스트 테스트 잡

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: Task 8까지의 테스트 3개(`sherbet_auth_test`, `sherbet_update_test`, `sherbet_swap_sim`)
- Produces: 없음(CI 게이트)

**왜 필요한가:** 현재 `build.yml`은 테스트를 **하나도 실행하지 않는다**(checkout/python/msbuild/upload뿐). 직접 짠 SHA-256이 조용히 틀리면 전 고객이 업데이트 불능이 된다. 호스트 테스트를 CI 잡으로 승격하는 것이 이 계획의 필수 마무리다.

- [ ] **Step 1: 현재 잡 구조 확인**

Run: `cd ~/reshade && grep -n 'jobs:\|^  [a-z-]*:\|runs-on' .github/workflows/build.yml | head -10`
Expected: `jobs:` 아래에 `build:` 하나, `runs-on: windows-latest`. 여기에 형제 잡을 추가한다.

- [ ] **Step 2: 테스트 잡 추가**

`.github/workflows/build.yml`의 `jobs:` 바로 아래, `build:` **앞**에 삽입(테스트가 먼저 보이게):

```yaml
  host-tests:
    runs-on: ubuntu-latest

    permissions:
      contents: read

    steps:
      - name: Checkout
        uses: actions/checkout@v6
        with:
          persist-credentials: false

      - name: 순수 로직 호스트 테스트
        run: |
          set -e
          for t in sherbet_auth_test sherbet_update_test sherbet_swap_sim; do
            echo "=== $t ==="
            clang++ -std=c++17 -Wall -Isource "tools/$t.cpp" -o "/tmp/$t"
            "/tmp/$t"
          done
```

`-DNDEBUG`를 붙이면 `assert`가 전부 사라져 테스트가 무조건 통과한다. 절대 붙이지 않는다.

- [ ] **Step 3: YAML 문법 확인**

Run: `cd ~/reshade && python3 -c "import yaml,sys; d=yaml.safe_load(open('.github/workflows/build.yml')); print('jobs:', list(d['jobs'].keys()))"`
Expected: `jobs: ['host-tests', 'build']`

- [ ] **Step 4: 잡이 하는 일을 로컬에서 그대로 재현**

Run:
```bash
cd ~/reshade && set -e && for t in sherbet_auth_test sherbet_update_test sherbet_swap_sim; do
  echo "=== $t ==="; clang++ -std=c++17 -Wall -Isource "tools/$t.cpp" -o "/tmp/$t"; "/tmp/$t"; done
```
Expected: 세 테스트 모두 `ALL PASS`. CI에서 처음 보지 말고 여기서 먼저 확인한다.

- [ ] **Step 5: 커밋하고 CI 확인**

```bash
git add .github/workflows/build.yml
git commit -m "CI 에 순수 로직 호스트 테스트 잡 추가

지금까지 build.yml 은 테스트를 하나도 실행하지 않았다(checkout/msbuild/upload 뿐).
직접 짠 SHA-256 이 조용히 틀리면 전 고객이 업데이트 불능이 되므로
ubuntu 잡에서 sherbet_auth_test / sherbet_update_test / sherbet_swap_sim 을 돌린다."
git push
```

- [ ] **Step 6: CI green 확인**

Run: `cd ~/reshade && sleep 60 && gh run list --workflow=build.yml --limit 1 --json databaseId,status,conclusion`
그다음 `gh run watch <databaseId>` 로 지켜본다.
Expected: `host-tests` 잡 success, `build` 잡도 기존대로 success. **`build` 잡이 깨졌다면 Task 1의 `dll_main.cpp` 수정이 원인일 가능성이 높으므로 그 로그부터 본다.**

---

## Self-Review

**1. 스펙 커버리지** — 이 계획이 다루는 스펙 절: §2(버전 체계) 전부, §3.3~3.5(파싱·거부·필수/강등 판정), §4.4의 상태머신 부분, §5.1·5.4의 마커·판정, §9의 1층 전부와 2층 일부. **다루지 않는 절(2·3단계 계획으로 이월):** §3.1~3.2(서버 라우터), §4.1~4.5의 실제 Win32 교체, §5.2~5.3(DllMain 훅·`on_present` 판정), §6의 글루·UI, §7(릴리스 파이프라인), §10(첫 배포).

**2. 플레이스홀더** — 없음. 모든 코드 스텝에 실제 코드가 있고, 모든 실행 스텝에 명령과 기대 출력이 있다.

**3. 타입 일관성** — `version3`/`parse_version`/`version_cmp`(Task 2) → `parse_manifest`(Task 6)·`should_offer`/`is_mandatory`(Task 7)에서 동일 이름으로 사용. `is_sha256_hex`(Task 3) → Task 6에서 사용. `url_allowed`(Task 4) → Task 6에서 사용. `detail::parse_u64`(Task 6) → Task 7 `parse_marker`에서 사용(정의가 앞서므로 순서 OK). `boot_marker`(Task 7) → Task 8 `classify`에서 사용. **`decide_repair`는 Task 8 Step 3의 주의 사항에 따라 시그니처가 확장될 수 있으며, 그 경우 시뮬레이터도 같이 고친다(같은 태스크 안이라 불일치가 남지 않는다).**

---

## 이 계획이 끝나면

- `source/sherbet_update_core.hpp` 완성 — 자동 업데이트의 판정 로직 100%
- 맥에서 즉시 돌아가는 테스트 2개 + CI 게이트
- `SHERBET_VERSION` 도입, 태그 규칙 확정, 오타 태그 2차 방어
- **아직 아무 동작도 하지 않는다.** 네트워크·파일·UI는 2·3단계에서 붙인다

**다음 계획:**
- **2단계** — 서버 `/update/manifest` 라우터 + `release.yml` + 배포도구(`sherbet_admin.py`). 윈도우 실험 불필요
- **3단계** — 클라 Win32 글루·UI·롤백. **매핑된 DLL rename 실험 통과가 전제**
