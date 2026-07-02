# SHERBET Plan 1 — Foundation & Rebrand Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** crosire ReShade 위에 SHERBET 리브랜드의 뼈대를 심는다 — 테마 데이터 구조, 구매자 개인화 상수, 빌드 배선, 스플래시/창 리브랜드. 끝나면 CI가 green이고 오버레이 스플래시가 "Sherbet by 정렬"로 뜬다(레이아웃은 아직 원본).

**Architecture:** 신규 소스 3개(`sherbet_owner.h`, `sherbet_theme.hpp`, `sherbet_themes.cpp`)를 추가하고 `ReShade.vcxproj`(CI 빌드 기준)와 `CMakeLists.txt`(보조)에 등록한다. `runtime_gui.cpp`의 스플래시 텍스트만 최소 수정해 리브랜드를 확인한다. 실제 스킨/레이아웃은 Plan 2 이후.

**Tech Stack:** C++17, MSVC(VS2022), ImGui(ReShade 번들), GitHub Actions(windows-latest).

## Global Constraints

- 외부 라이브러리 추가 금지. ImGui는 `deps/imgui`만.
- 이모지 렌더 금지. 아이콘은 ForkAwesome, 도형은 ImDrawList (이 플랜에선 아직 미사용).
- 핵심 기능 로직(이펙트/프리셋/렌더러) 수정 금지.
- 커밋 메시지에 Claude 저작권/공동저자 문구 넣지 않음.
- 디스코드 URL: `https://discord.gg/5NGR7XVFta`. 제작자명: `정렬`.
- 검증 = ① CI green(32/64비트 msbuild) ② 마일스톤 스크린샷. 빨간 CI 위에 다음 태스크 쌓지 않기.
- 테마 6종: mint, peach, pink, rainbow, lavender, noir. 팔레트 값은 `~/reshade-design/preview.html`의 CSS 변수에서 그대로 옮긴다(아래 Task 2에 전량 기재).

---

### Task 1: 구매자 개인화 상수 헤더 (`sherbet_owner.h`)

**Files:**
- Create: `source/sherbet_owner.h`

**Interfaces:**
- Produces:
  - 매크로 `SHERBET_OWNER` — `const char*` UTF-8 리터럴, 구매자 닉(기본 `""`)
  - 매크로 `SHERBET_ORDER_NO` — `const char*`, 주문번호(기본 `""`)
  - 매크로 `SHERBET_DEFAULT_THEME` — `const char*`, 기본 열린 테마 id(기본 `"mint"`)
  - 인라인 함수 `bool sherbet::has_owner()` — `SHERBET_OWNER`가 비어있지 않으면 true

- [ ] **Step 1: 헤더 작성**

Create `source/sherbet_owner.h`:

```cpp
/*
 * SHERBET — 구매자 개인화 상수.
 * 빌드 시 CI(workflow_dispatch)가 이 파일의 #define 값을 덮어써 주문별 빌드를 만든다.
 * 값이 비어 있으면(기본) 데모 빌드로 취급되어 개인화 문구가 숨겨진다.
 */
#pragma once

#include <cstring>

#ifndef SHERBET_OWNER
#define SHERBET_OWNER "" // 구매자 닉네임 (UTF-8)
#endif
#ifndef SHERBET_ORDER_NO
#define SHERBET_ORDER_NO "" // 주문번호
#endif
#ifndef SHERBET_DEFAULT_THEME
#define SHERBET_DEFAULT_THEME "mint" // 기본으로 열려 있는 테마 id
#endif

namespace sherbet
{
	inline bool has_owner()
	{
		return SHERBET_OWNER[0] != '\0';
	}
}
```

- [ ] **Step 2: 컴파일 sanity (헤더 단독 파싱)**

Run: `cd ~/reshade && clang -std=c++17 -fsyntax-only -x c++ source/sherbet_owner.h`
Expected: 에러 없이 종료(경고 무방). Mac에 clang 있음.

- [ ] **Step 3: Commit**

```bash
cd ~/reshade
git add source/sherbet_owner.h
git commit -m "feat(sherbet): 구매자 개인화 상수 헤더 추가"
```

---

### Task 2: 테마 데이터 구조 + 6종 팔레트 (`sherbet_theme.hpp`, `sherbet_themes.cpp`)

**Files:**
- Create: `source/sherbet_theme.hpp`
- Create: `source/sherbet_themes.cpp`

**Interfaces:**
- Consumes: `SHERBET_DEFAULT_THEME` (Task 1)
- Produces:
  - `struct sherbet::theme` — 필드: `id`(const char*), `display_name`(const char*), 색 `ImU32 bg0,bg1,bg2,panel,panel_alt,chip,border,text,text_dim,accent,accent2,glow`, `particle_shape`(enum), `bool hue_cycle`
  - `enum class sherbet::particle { spark, heart, leaf, petal }`
  - `const theme &sherbet::default_theme()` — `SHERBET_DEFAULT_THEME` id에 해당하는 테마 반환(못 찾으면 mint)
  - `const theme *sherbet::find_theme(const char *id)` — id로 조회(없으면 nullptr)
  - `const theme *sherbet::all_themes(size_t &count)` — 전체 배열 + 개수

주의: `ImU32`는 ImGui 색(0xAABBGGRR 패킹). 시안 CSS의 `#RRGGBB`는 `IM_COL32(R,G,B,A)`로 변환해 기재한다. 아래 값은 preview.html에서 변환 완료된 것.

- [ ] **Step 1: 헤더 작성**

Create `source/sherbet_theme.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <imgui.h>

namespace sherbet
{
	enum class particle { spark, heart, leaf, petal };

	struct theme
	{
		const char *id;
		const char *display_name;
		ImU32 bg0, bg1, bg2;      // 배경 그라디언트 스톱
		ImU32 panel, panel_alt;   // 카드/패널
		ImU32 chip, border;       // 칩/테두리
		ImU32 text, text_dim;     // 본문/흐린 텍스트
		ImU32 accent, accent2;    // 강조(그라디언트 2색)
		ImU32 glow;               // 글로우(알파 포함)
		particle particle_shape;
		bool hue_cycle;           // rainbow 전용 색상 순환
	};

	const theme &default_theme();
	const theme *find_theme(const char *id);
	const theme *all_themes(std::size_t &count);
}
```

- [ ] **Step 2: 테마 테이블 작성**

Create `source/sherbet_themes.cpp`:

```cpp
#include "sherbet_theme.hpp"
#include "sherbet_owner.h"
#include <cstring>

namespace sherbet
{
	// 값 출처: docs 시안 preview.html CSS 변수 → IM_COL32(R,G,B,A)
	static const theme s_themes[] = {
		{ "mint", "Mint Soda",
			IM_COL32(0x0a,0x1f,0x1a,255), IM_COL32(0x0f,0x2e,0x24,255), IM_COL32(0x12,0x3a,0x2c,255),
			IM_COL32(0x12,0x2e,0x26,184), IM_COL32(0x18,0x3a,0x2f,217),
			IM_COL32(0x13,0x42,0x33,255), IM_COL32(0x5d,0xf0,0xc0,64),
			IM_COL32(0xea,0xff,0xf6,255), IM_COL32(0x8f,0xbf,0xae,255),
			IM_COL32(0x5d,0xf0,0xc0,255), IM_COL32(0xa7,0xff,0xe3,255),
			IM_COL32(0x5d,0xf0,0xc0,115), particle::leaf, false },
		{ "peach", "Peach Punch",
			IM_COL32(0x22,0x13,0x10,255), IM_COL32(0x33,0x19,0x0f,255), IM_COL32(0x3f,0x21,0x13,255),
			IM_COL32(0x34,0x1d,0x14,184), IM_COL32(0x42,0x25,0x19,217),
			IM_COL32(0x4a,0x2a,0x1a,255), IM_COL32(0xff,0xab,0x7d,72),
			IM_COL32(0xff,0xf2,0xea,255), IM_COL32(0xcf,0xa0,0x8c,255),
			IM_COL32(0xff,0xab,0x7d,255), IM_COL32(0xff,0xd4,0xb8,255),
			IM_COL32(0xff,0xab,0x7d,115), particle::heart, false },
		{ "pink", "Pink Crush",
			IM_COL32(0x23,0x0f,0x1c,255), IM_COL32(0x33,0x12,0x2a,255), IM_COL32(0x41,0x17,0x33,255),
			IM_COL32(0x38,0x14,0x2d,184), IM_COL32(0x46,0x1a,0x38,217),
			IM_COL32(0x4d,0x1c,0x3b,255), IM_COL32(0xff,0x7e,0xb6,77),
			IM_COL32(0xff,0xee,0xf7,255), IM_COL32(0xd1,0x93,0xb4,255),
			IM_COL32(0xff,0x7e,0xb6,255), IM_COL32(0xff,0xc2,0xdd,255),
			IM_COL32(0xff,0x7e,0xb6,128), particle::heart, false },
		{ "rainbow", "Prism Pop",
			IM_COL32(0x0d,0x0d,0x14,255), IM_COL32(0x14,0x13,0x24,255), IM_COL32(0x1a,0x10,0x30,255),
			IM_COL32(0x18,0x16,0x28,191), IM_COL32(0x20,0x1c,0x34,224),
			IM_COL32(0x24,0x1f,0x45,255), IM_COL32(0xb4,0xa0,0xff,77),
			IM_COL32(0xf4,0xf2,0xff,255), IM_COL32(0x9d,0x97,0xbd,255),
			IM_COL32(0x8b,0xe9,0xfd,255), IM_COL32(0xff,0x79,0xc6,255),
			IM_COL32(0x8b,0xe9,0xfd,102), particle::spark, true },
		{ "lavender", "Lavender Dream",
			IM_COL32(0x16,0x13,0x2a,255), IM_COL32(0x1d,0x18,0x38,255), IM_COL32(0x24,0x1d,0x46,255),
			IM_COL32(0x20,0x1b,0x3a,191), IM_COL32(0x29,0x22,0x48,224),
			IM_COL32(0x2c,0x24,0x58,255), IM_COL32(0xb8,0xa7,0xff,72),
			IM_COL32(0xf1,0xed,0xff,255), IM_COL32(0xa7,0x9c,0xd0,255),
			IM_COL32(0xb8,0xa7,0xff,255), IM_COL32(0xe0,0xd7,0xff,255),
			IM_COL32(0xb8,0xa7,0xff,115), particle::spark, false },
		{ "noir", "Noir Gold",
			IM_COL32(0x0b,0x0b,0x0d,255), IM_COL32(0x12,0x11,0x14,255), IM_COL32(0x19,0x16,0x18,255),
			IM_COL32(0x16,0x14,0x16,204), IM_COL32(0x1e,0x1b,0x1c,230),
			IM_COL32(0x24,0x20,0x19,255), IM_COL32(0xe8,0xc8,0x77,72),
			IM_COL32(0xf5,0xef,0xe2,255), IM_COL32(0x9c,0x92,0x7e,255),
			IM_COL32(0xe8,0xc8,0x77,255), IM_COL32(0xf7,0xe5,0xb5,255),
			IM_COL32(0xe8,0xc8,0x77,102), particle::spark, false },
	};
	static const std::size_t s_theme_count = sizeof(s_themes) / sizeof(s_themes[0]);

	const theme *all_themes(std::size_t &count)
	{
		count = s_theme_count;
		return s_themes;
	}

	const theme *find_theme(const char *id)
	{
		if (id == nullptr)
			return nullptr;
		for (std::size_t i = 0; i < s_theme_count; ++i)
			if (std::strcmp(s_themes[i].id, id) == 0)
				return &s_themes[i];
		return nullptr;
	}

	const theme &default_theme()
	{
		const theme *t = find_theme(SHERBET_DEFAULT_THEME);
		return t != nullptr ? *t : s_themes[0];
	}
}
```

- [ ] **Step 3: 컴파일 sanity (ImGui 헤더 경로 포함 파싱)**

Run:
```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -Ideps/imgui source/sherbet_themes.cpp
```
Expected: 에러 없이 종료. (`deps/imgui/imgui.h` 존재 확인 후. 없으면 `find deps -name imgui.h`로 경로 조정하고 그 경로로 `-I`.)

- [ ] **Step 4: Commit**

```bash
cd ~/reshade
git add source/sherbet_theme.hpp source/sherbet_themes.cpp
git commit -m "feat(sherbet): 테마 구조체 + 6종 팔레트 테이블 추가"
```

---

### Task 3: 빌드 시스템에 신규 소스 등록 (vcxproj + CMake)

**Files:**
- Modify: `ReShade.vcxproj` (CI 빌드 기준)
- Modify: `ReShade.vcxproj.filters`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1·2의 신규 파일
- Produces: 신규 소스가 ReShade DLL 빌드 대상에 포함됨

**주의:** CI(`build.yml`)는 `msbuild ReShade.sln`을 쓰므로 **vcxproj 등록이 필수**다. CMake는 보조.

- [ ] **Step 1: vcxproj에서 기존 소스 등록 형식 확인**

Run: `cd ~/reshade && grep -n 'runtime_gui.cpp\|sherbet\|ClCompile Include' ReShade.vcxproj | head`
Expected: `<ClCompile Include="source\runtime_gui.cpp" />` 형태 확인.

- [ ] **Step 2: `ReShade.vcxproj`에 `.cpp` 추가**

`source\runtime_gui.cpp`의 `<ClCompile>` 줄 바로 아래에 추가:

```xml
    <ClCompile Include="source\sherbet_themes.cpp" />
```

그리고 헤더 그룹(`<ClInclude>`들 사이, 예: `runtime.hpp` 근처)에 추가:

```xml
    <ClInclude Include="source\sherbet_owner.h" />
    <ClInclude Include="source\sherbet_theme.hpp" />
```

- [ ] **Step 3: `ReShade.vcxproj.filters`에 동일 항목 추가**

`runtime_gui.cpp` 필터 항목을 찾아 같은 `<Filter>`(예: `Source Files`)로 세 파일을 추가:

```xml
    <ClCompile Include="source\sherbet_themes.cpp">
      <Filter>Source Files</Filter>
    </ClCompile>
    <ClInclude Include="source\sherbet_owner.h">
      <Filter>Header Files</Filter>
    </ClInclude>
    <ClInclude Include="source\sherbet_theme.hpp">
      <Filter>Header Files</Filter>
    </ClInclude>
```

(먼저 `grep -n 'runtime_gui.cpp' ReShade.vcxproj.filters`로 실제 Filter 이름을 확인해 맞춘다.)

- [ ] **Step 4: `CMakeLists.txt`에 추가**

`source/runtime_gui.cpp` 줄 아래에:

```cmake
  source/sherbet_owner.h
  source/sherbet_theme.hpp
  source/sherbet_themes.cpp
```

- [ ] **Step 5: XML 유효성 확인**

Run: `cd ~/reshade && python3 -c "import xml.dom.minidom as m; m.parse('ReShade.vcxproj'); m.parse('ReShade.vcxproj.filters'); print('XML OK')"`
Expected: `XML OK`

- [ ] **Step 6: Commit + push + CI 확인**

```bash
cd ~/reshade
git add ReShade.vcxproj ReShade.vcxproj.filters CMakeLists.txt
git commit -m "build(sherbet): 신규 소스 vcxproj/cmake 등록"
git push -u origin sherbet-base
gh run watch --exit-status
```
Expected: 워크플로 `build`가 32/64비트 모두 green. (신규 파일은 아직 아무 데서도 include 안 되지만 컴파일 대상에 포함되어 링크됨.)

주의: push 전 `.github/workflows/build.yml`이 `sherbet-base` 브랜치에서 트리거되는지 확인. 트리거 조건이 `main`뿐이면 Task 5에서 고치므로, 이 단계에서 CI가 안 돌면 Task 5를 먼저 수행 후 되돌아온다.

---

### Task 4: 스플래시 + 창 제목 리브랜드 ("Sherbet by 정렬")

**Files:**
- Modify: `source/runtime_gui.cpp` (스플래시 텍스트, 약 1069행 `ImGui::TextUnformatted("ReShade " VERSION_STRING_PRODUCT);`)

**Interfaces:**
- Consumes: `sherbet::default_theme()` (Task 2), `SHERBET_OWNER`/`sherbet::has_owner()` (Task 1)
- Produces: 없음(표시 변경만)

- [ ] **Step 1: include 추가**

`source/runtime_gui.cpp` 상단 include 블록(예: `#include "fonts/forkawesome.inl"` 근처, 파일 19행 부근)에 추가:

```cpp
#include "sherbet_theme.hpp"
#include "sherbet_owner.h"
```

- [ ] **Step 2: 스플래시 제목 문구 교체**

찾기: `source/runtime_gui.cpp`에서
```cpp
			ImGui::TextUnformatted("ReShade " VERSION_STRING_PRODUCT);
```
교체:
```cpp
			// SHERBET 리브랜드 스플래시 제목
			if (sherbet::has_owner())
				ImGui::Text("Sherbet %s \xC2\xB7 %s\xEB\x8B\x98\xEC\x9D\x84 \xEC\x9C\x84\xED\x95\x9C \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80", sherbet::default_theme().display_name, SHERBET_OWNER);
			else
				ImGui::Text("Sherbet %s \xC2\xB7 by \xEC\xA0\x95\xEB\xA0\xAC", sherbet::default_theme().display_name);
```
(위 UTF-8 바이트열: `님을 위한 커스텀`, `정렬`. 소스 인코딩 이슈를 피하려 이스케이프 사용. 에디터가 UTF-8을 확실히 저장한다면 한글 리터럴 직접 사용도 가능.)

- [ ] **Step 3: 업데이트 안내 URL 문구 무해화(선택, 브랜드 노출 최소화)**

찾기: `ImGui::Text(_("Visit %s for news, updates, effects and discussion."), "https://reshade.me");`
교체:
```cpp
			ImGui::Text(_("Visit %s for news, updates, effects and discussion."), "https://discord.gg/5NGR7XVFta");
```
(reshade.me 노출 제거, 디스코드로 대체. 기능 영향 없음 — 단순 텍스트.)

- [ ] **Step 4: 컴파일 sanity (문법만)**

Run: `cd ~/reshade && clang -std=c++17 -fsyntax-only -Ideps/imgui -Isource source/sherbet_themes.cpp`
Expected: OK. (runtime_gui.cpp 전체는 Mac에서 의존성 때문에 단독 파싱 불가 — CI로 검증.)

- [ ] **Step 5: Commit + push + CI 확인**

```bash
cd ~/reshade
git add source/runtime_gui.cpp
git commit -m "feat(sherbet): 스플래시/링크 리브랜드 (Sherbet by 정렬)"
git push
gh run watch --exit-status
```
Expected: CI green.

---

### Task 5: CI 워크플로를 sherbet-base에서 돌게 + 데모 아티팩트 이름

**Files:**
- Modify: `.github/workflows/build.yml`

**Interfaces:**
- Consumes: 없음
- Produces: `sherbet-base` push 시 CI 트리거, 아티팩트 이름 `Sherbet_Demo_*`

- [ ] **Step 1: 트리거 브랜치 추가**

`.github/workflows/build.yml`에서:
```yaml
on:
  push:
    branches:
      - main
```
교체:
```yaml
on:
  push:
    branches:
      - main
      - sherbet-base
  workflow_dispatch:
```
(`workflow_dispatch`는 Plan 6의 주문 빌드에서 입력 칸을 붙일 자리. 지금은 수동 실행만 가능하게 열어둠.)

- [ ] **Step 2: 아티팩트 이름 리브랜드**

`Upload ReShade (64-bit)` 스텝의 `name: ReShade (64-bit)` →
```yaml
          name: Sherbet_Demo (64-bit)
```
32-bit도 동일하게 `Sherbet_Demo (32-bit)`. (경로 `path:`는 유지 — DLL 산출 위치는 그대로 `ReShade64.dll`.)

- [ ] **Step 3: YAML 유효성 확인**

Run: `cd ~/reshade && python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build.yml')); print('YAML OK')"`
Expected: `YAML OK`

- [ ] **Step 4: Commit + push + CI 확인**

```bash
cd ~/reshade
git add .github/workflows/build.yml
git commit -m "ci(sherbet): sherbet-base 트리거 + 데모 아티팩트 이름"
git push
gh run watch --exit-status
```
Expected: CI green, 아티팩트가 `Sherbet_Demo (64-bit)` 이름으로 업로드됨.

---

### Task 6: 마일스톤 시각 확인 (Windows 필요 — 사용자 수행)

**Files:** 없음(검증 태스크)

- [ ] **Step 1: 아티팩트 다운로드**

`gh run download`(또는 Actions 웹 UI)로 최신 `Sherbet_Demo (64-bit)` 다운로드.

- [ ] **Step 2: 게임/테스트앱에 적용**

64비트 게임 폴더에 `ReShade64.dll`을 적절한 이름(예: `dxgi.dll`)으로 넣고 실행.

- [ ] **Step 3: 스플래시 확인**

Expected: 로딩 시 좌상단 스플래시가 **"Sherbet Mint Soda · by 정렬"** 로 표시(레이아웃/색은 아직 원본 ReShade). 확인되면 Plan 1 완료 → Plan 2 진행.

---

## Self-Review 메모 (플랜 작성자 확인)

- **스펙 커버리지:** 이 플랜은 스펙 §2(브랜딩 일부), §3(단일 base 빌드인자 뼈대), §4(신규 파일 중 owner/theme), §9(CI 트리거·workflow_dispatch 자리)를 커버. 스킨/레이아웃/마켓/라이선스/OSD는 Plan 2~6에서.
- **플레이스홀더:** 없음. 모든 색값·코드·명령 구체 기재.
- **타입 일관성:** `sherbet::theme`/`default_theme()`/`has_owner()`/`find_theme()`/`all_themes()` 시그니처가 Task 1·2에서 정의되고 Task 4에서 동일하게 사용됨. Plan 2 이후가 이들을 Consumes로 참조.
- **알려진 리스크:** ImGui include 경로(`deps/imgui`)가 실제와 다를 수 있음 → Task 2 Step 3에서 `find deps -name imgui.h`로 확인 지시. vcxproj Filter 이름이 표준과 다를 수 있음 → Task 3에서 grep으로 확인 지시.
