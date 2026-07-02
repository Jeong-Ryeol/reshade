# SHERBET Plan 6 — About, Owner Builds & Theme Market Implementation Plan

> REQUIRED SUB-SKILL: subagent-driven-development. Steps use `- [ ]`.

**Goal:** 제품의 사업성 마무리 — About 탭(정렬 크레딧·디스코드·구매자 표기·무단배포 경고), 주문별 개인화 빌드 워크플로(CI), 테마 마켓(런타임 테마 전환 + 잠금/언락코드).

**Architecture:** ForkAwesome + sherbet_ui 위젯 재사용. 런타임 활성 테마 상태를 sherbet_ui에 두고 모든 default_theme() 사용처를 active_theme()로 전환. 오프라인 언락코드는 임베드 시크릿 + 경량 해시(FNV)로 검증(스펙이 완벽보호 불가를 인정). 노드락(HWID)·프리셋 마켓은 후속(Plan 7)로 명시.

## Global Constraints

- ImGui 1.92.5 + IMGUI_DISABLE_OBSOLETE_FUNCTIONS. obsolete 금지. sherbet_ui 로컬검증 `clang ... -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp` exit 0.
- runtime_gui.cpp Mac 빌드 불가 → 정독 + CI. 이펙트/프리셋 로직 보존.
- 외부 라이브러리 금지. 이모지 금지. 커밋 Claude/AI 문구 금지.
- 디스코드 `https://discord.gg/5NGR7XVFta`. 제작자 `정렬`.
- 검증: 플랜 끝 push 1회 + CI green.

---

### Task 1: About 탭 리브랜드 (정렬 크레딧·디스코드·구매자·경고)

**Files:** Modify `source/runtime_gui.cpp` (`draw_gui_about`, 3182~)

**설명:** 기존 About 내용(ReShade 버전/링크/오픈소스 라이선스) 중 상단에 Sherbet 크레딧 블록을 추가. 오픈소스 라이선스 고지는 보존(BSD 의무). 리쉐이드 브랜드 상단 노출만 정리.

- [ ] **Step 1: 크레딧 블록 삽입**

`draw_gui_about()` 함수 본문 맨 앞(첫 문장 출력 전)에 삽입:
```cpp
	// SHERBET 크레딧 블록
	sherbet::begin_card("##about_credit");
	ImGui::PushFont(_sherbet_title_font, 0.0f);
	ImGui::Text("Sherbet %s", sherbet::active_theme().display_name);
	ImGui::PopFont();
	ImGui::TextUnformatted("\xEC\xA0\x95\xEB\xA0\xAC\xEC\x9D\xB4 \xEB\xA7\x8C\xEB\x93\xA0 \xEC\xBB\xA4\xEC\x8A\xA4\xED\x85\x80 \xEB\xA6\xAC\xEC\x89\x90\xEC\x9D\xB4\xEB\x93\x9C"); // "정렬이 만든 커스텀 리쉐이드"
	ImGui::Spacing();
	ImGui::TextLinkOpenURL(ICON_FK_COMMENTS "  \xEB\x94\x94\xEC\x8A\xA4\xEC\xBD\x94\xEB\x93\x9C \xEC\xB0\xB8\xEC\x97\xAC", "https://discord.gg/5NGR7XVFta"); // "디스코드 참여"
	if (sherbet::has_owner())
	{
		ImGui::Spacing();
		ImGui::Text(ICON_FK_CHECK "  \xEB\x93\xB1\xEB\xA1\x9D \xEC\x86\x8C\xEC\x9C\xA0\xEC\x9E\x90 : %s", SHERBET_OWNER); // "등록 소유자 :"
		if (SHERBET_ORDER_NO[0] != '\0')
			ImGui::Text("   \xEC\xA3\xBC\xEB\xAC\xB8 #%s", SHERBET_ORDER_NO); // "주문 #"
	}
	sherbet::end_card();

	sherbet::begin_card("##about_warn");
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.7f, 1.0f));
	ImGui::TextWrapped(ICON_FK_EXCLAMATION_TRIANGLE " \xEC\x9D\xB4 \xEB\xB9\x8C\xEB\x93\x9C\xEB\x8A\x94 \xEC\xA0\x95\xEB\xA0\xAC\xEC\x9D\xB4 \xEC\xA0\x9C\xEC\x9E\x91\xED\x95\x9C \xEA\xB5\xAC\xEB\xA7\xA4\xEC\x9E\x90 \xEC\xA0\x84\xEC\x9A\xA9 \xEB\xB9\x8C\xEB\x93\x9C\xEC\x9E\x85\xEB\x8B\x88\xEB\x8B\xA4. \xEB\xAC\xB4\xEB\x8B\xA8 \xEB\xB0\xB0\xED\x8F\xAC\xC2\xB7\xEA\xB3\xB5\xEC\x9C\xA0 \xEC\x8B\x9C \xEB\xB8\x94\xEB\x9E\x99\xEB\xA6\xAC\xEC\x8A\xA4\xED\x8A\xB8 \xEC\xB6\x94\xEA\xB0\x80 \xEB\xB0\x8F \xED\x8C\x8C\xEC\x9D\xBC \xEC\x9E\xA0\xEA\xB8\x88 \xEC\xA1\xB0\xEC\xB9\x98\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4.");
	ImGui::PopStyleColor();
	sherbet::end_card();
	ImGui::Spacing();
```
(UTF-8 이스케이프 = "정렬이 만든 커스텀 리쉐이드", "디스코드 참여", "등록 소유자 :", "주문 #", 경고문. `ICON_FK_COMMENTS`/`ICON_FK_CHECK`/`ICON_FK_EXCLAMATION_TRIANGLE`는 forkawesome.h 존재 여부 확인 후 사용 — 없으면 `ICON_FK_COMMENT`/`ICON_FK_OK`/`ICON_FK_WARNING` 등 존재하는 것으로 대체.)

주의: `ImGui::PushFont(_sherbet_title_font, 0.0f)` — 1.92 PushFont는 (ImFont*, float) 시그니처. `_sherbet_title_font`가 nullptr여도 안전(기본 폰트). `TextLinkOpenURL(label, url)` 2인자 오버로드 존재 확인(3472행에서 사용됨).

- [ ] **Step 2: 아이콘 상수 확인 + Commit(push 안 함)**

`grep -n "ICON_FK_COMMENTS\|ICON_FK_COMMENT\|ICON_FK_CHECK\|ICON_FK_EXCLAMATION_TRIANGLE\|ICON_FK_WARNING\|ICON_FK_OK" res/fonts/forkawesome.h` 로 존재하는 이름 확인 후 코드 반영.
```bash
git add source/runtime_gui.cpp
git commit -m "feat(sherbet): About 탭 크레딧·디스코드·구매자·경고"
```

---

### Task 2: 주문별 개인화 빌드 워크플로 (CI)

**Files:** Modify `.github/workflows/build.yml`

- [ ] **Step 1: workflow_dispatch 입력 추가**

`on:` 블록의 `workflow_dispatch:`를 입력 포함으로 확장:
```yaml
  workflow_dispatch:
    inputs:
      owner:
        description: '구매자 닉네임 (파일 곳곳 각인)'
        required: false
        default: ''
      order_no:
        description: '주문번호'
        required: false
        default: ''
      default_theme:
        description: '기본 테마 (mint/peach/pink/rainbow/lavender/noir)'
        required: false
        default: 'mint'
```

- [ ] **Step 2: 빌드 전 sherbet_owner.h 덮어쓰기 스텝 추가**

`Set up MSBuild` 스텝 다음, `Build ReShade (32-bit)` 스텝 앞에 추가:
```yaml
      - name: Apply Sherbet owner
        if: github.event_name == 'workflow_dispatch'
        shell: pwsh
        run: |
          $owner = '${{ github.event.inputs.owner }}'
          $order = '${{ github.event.inputs.order_no }}'
          $theme = '${{ github.event.inputs.default_theme }}'
          $content = @"
          #pragma once
          #include <cstring>
          #ifndef SHERBET_OWNER
          #define SHERBET_OWNER "$owner"
          #endif
          #ifndef SHERBET_ORDER_NO
          #define SHERBET_ORDER_NO "$order"
          #endif
          #ifndef SHERBET_DEFAULT_THEME
          #define SHERBET_DEFAULT_THEME "$theme"
          #endif
          namespace sherbet { inline bool has_owner() { return SHERBET_OWNER[0] != '\0'; } }
          "@
          Set-Content -Path source/sherbet_owner.h -Value $content -Encoding UTF8
```
주의: 실제 `source/sherbet_owner.h`의 내용/네임스페이스와 정확히 일치해야 함(빌드 깨짐 방지). 현재 파일을 읽어 동일 구조로 작성.

- [ ] **Step 3: 아티팩트 이름에 owner 반영(선택)**

64-bit 업로드 스텝 `name:`을 워크플로 디스패치 시 구분되게(예: `Sherbet_${{ github.event.inputs.default_theme }}_${{ github.event.inputs.owner }} (64-bit)`) — 단, 빈 값일 때도 유효한 이름이어야 함. 간단히 유지하려면 이 스텝은 생략 가능.

- [ ] **Step 4: YAML 검증 + Commit(push 안 함)**

```bash
cd ~/reshade && python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build.yml')); print('YAML OK')"
git add .github/workflows/build.yml
git commit -m "ci(sherbet): 주문별 개인화 빌드 워크플로 입력"
```

---

### Task 3: 런타임 활성 테마 + 테마 마켓

**Files:** Modify `source/sherbet_ui.hpp`/`.cpp`, `source/runtime_gui.cpp`, `source/runtime.hpp`

**Interfaces:**
- Produces (sherbet_ui):
  - `const theme &active_theme();` `void set_active_theme(const char *id);` `const char *active_theme_id();`
  - `bool is_unlocked(const char *id);` `void unlock_theme(const char *id);` `std::string unlocked_csv();` `void load_unlocked_csv(const char *csv);`
  - `bool check_theme_code(const char *id, const char *code);` (오프라인 검증)

- [ ] **Step 1: 활성 테마 + 언락 상태 (sherbet_ui)**

`sherbet_ui.cpp`에 (상단 `#include <string>`, `<set>` 추가):
```cpp
	static std::string s_active_id = SHERBET_DEFAULT_THEME;
	static std::set<std::string> s_unlocked = { SHERBET_DEFAULT_THEME };

	const theme &active_theme()
	{
		const theme *t = find_theme(s_active_id.c_str());
		return t != nullptr ? *t : default_theme();
	}
	const char *active_theme_id() { return s_active_id.c_str(); }
	void set_active_theme(const char *id) { if (find_theme(id)) s_active_id = id; }
	bool is_unlocked(const char *id) { return id && (s_unlocked.count(SHERBET_DEFAULT_THEME) , s_unlocked.count(id) > 0); }
	void unlock_theme(const char *id) { if (find_theme(id)) s_unlocked.insert(id); }
	std::string unlocked_csv() { std::string o; for (const auto &s : s_unlocked) { if (!o.empty()) o += ','; o += s; } return o; }
	void load_unlocked_csv(const char *csv)
	{
		if (!csv) return; std::string cur; for (const char *p = csv; ; ++p) {
			if (*p == ',' || *p == '\0') { if (!cur.empty() && find_theme(cur.c_str())) s_unlocked.insert(cur); cur.clear(); if (*p == '\0') break; }
			else cur += *p; }
	}
	// 오프라인 언락코드: FNV-1a(SECRET:id) → "SHRB-XXXX-XXXX"
	static unsigned int fnv1a(const char *s) { unsigned int h = 2166136261u; for (; *s; ++s) { h ^= (unsigned char)*s; h *= 16777619u; } return h; }
	bool check_theme_code(const char *id, const char *code)
	{
		if (!id || !code) return false;
		static const char *SECRET = "sherbet-by-jeongryeol-2026";
		char buf[128]; snprintf(buf, sizeof(buf), "%s:theme:%s", SECRET, id);
		unsigned int h = fnv1a(buf);
		char expect[16]; snprintf(expect, sizeof(expect), "SHRB-%04X-%04X", (h >> 16) & 0xFFFF, h & 0xFFFF);
		// 대소문자 무시 비교
		for (int i = 0; expect[i] || code[i]; ++i) { char a = expect[i], b = code[i]; if (a >= 'a' && a <= 'z') a -= 32; if (b >= 'a' && b <= 'z') b -= 32; if (a != b) return false; }
		return true;
	}
```
hpp에 선언 추가. 로컬검증 clang exit 0.

- [ ] **Step 2: default_theme() 사용처를 active_theme()로 전환**

`source/sherbet_ui.cpp`의 위젯 4곳(`toggle`,`begin_card`,`pill_button`,`rail_button`의 `const theme &t = default_theme();`) → `active_theme()`. `source/runtime_gui.cpp`의 apply_style/draw_background/draw_particles 호출(1345,1390,1391) → `sherbet::active_theme()`. 스플래시(1100,1102)와 VR 타이틀은 `default_theme()` 유지(빌드 정체성 = 구매 테마). 로컬검증 clang exit 0.

- [ ] **Step 3: 활성/언락 상태 config 저장 (runtime)**

`source/runtime.hpp`에 불필요(상태는 sherbet_ui static). config 로드/저장에서 sherbet 함수 호출:
- 로드부(load_config, `_fps_pos` get 근처)에 추가:
```cpp
	{ std::string s; config.get("SHERBET", "ActiveTheme", s); if (!s.empty()) sherbet::set_active_theme(s.c_str());
	  std::string u; config.get("SHERBET", "Unlocked", u); sherbet::load_unlocked_csv(u.c_str()); }
```
- 저장부에 추가:
```cpp
	config.set("SHERBET", "ActiveTheme", std::string(sherbet::active_theme_id()));
	config.set("SHERBET", "Unlocked", sherbet::unlocked_csv());
```
(`config.get(section,key,std::string&)` 오버로드 존재 확인 — ReShade ini_file은 문자열 get 지원.)

- [ ] **Step 4: 테마 마켓 UI (draw_gui_market 교체)**

Task 1의 스텁 `draw_gui_market()` 본문을 교체:
```cpp
void reshade::runtime::draw_gui_market()
{
	ImGui::PushFont(_sherbet_title_font, 0.0f);
	ImGui::TextUnformatted(ICON_FK_SHOPPING_CART "  Theme Market");
	ImGui::PopFont();
	ImGui::TextUnformatted("\xEC\x98\xA4\xEB\xB2\x84\xEB\xA0\x88\xEC\x9D\xB4 \xED\x85\x8C\xEB\xA7\x88. \xEC\x9E\xA0\xEA\xB8\xB4 \xED\x85\x8C\xEB\xA7\x88\xEB\x8A\x94 \xEC\x96\xB8\xEB\xA0\x9D\xEC\xBD\x94\xEB\x93\x9C\xEB\xA1\x9C \xED\x95\xB4\xEC\xA0\x9C\xED\x95\xB4\xEC\x9A\x94."); // "오버레이 테마. 잠긴 테마는 언락코드로 해제해요."
	ImGui::Spacing();

	std::size_t count = 0; const sherbet::theme *all = sherbet::all_themes(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const sherbet::theme &th = all[i];
		const bool unlocked = sherbet::is_unlocked(th.id);
		const bool active = std::strcmp(th.id, sherbet::active_theme_id()) == 0;
		ImGui::PushID((int)i);
		sherbet::begin_card("##theme_card");
		// 색 미리보기 바
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float bw = ImGui::GetContentRegionAvail().x;
		dl->AddRectFilledMultiColor(p, ImVec2(p.x + bw, p.y + 26.0f), th.bg1, th.accent, th.accent, th.bg1);
		ImGui::Dummy(ImVec2(bw, 30.0f));
		ImGui::Text("%s", th.display_name);
		if (active)
			ImGui::TextDisabled("%s", ICON_FK_CHECK " \xEC\x82\xAC\xEC\x9A\xA9 \xEC\xA4\x91"); // "사용 중"
		else if (unlocked)
		{
			if (sherbet::pill_button(ICON_FK_CHECK "  \xEC\xA0\x81\xEC\x9A\xA9", false)) // "적용"
			{ sherbet::set_active_theme(th.id); save_config(); }
		}
		else
		{
			ImGui::TextDisabled("%s", ICON_FK_LOCK " \xEC\x9E\xA0\xEA\xB9\x80"); // "잠김"
		}
		sherbet::end_card();
		ImGui::PopID();
	}

	ImGui::Spacing();
	// 언락코드 입력
	static char code_buf[32] = "";
	ImGui::SetNextItemWidth(220.0f);
	ImGui::InputTextWithHint("##unlock", "SHRB-XXXX-XXXX", code_buf, sizeof(code_buf));
	ImGui::SameLine();
	if (sherbet::pill_button(ICON_FK_KEY "  \xED\x95\xB4\xEC\xA0\x9C", true)) // "해제"
	{
		for (std::size_t i = 0; i < count; ++i)
			if (sherbet::check_theme_code(all[i].id, code_buf))
			{ sherbet::unlock_theme(all[i].id); code_buf[0] = '\0'; save_config(); break; }
	}
}
```
주의: `InputTextWithHint`/`SetNextItemWidth`/`PushID` 표준. `all_themes`/`check_theme_code` 등 Task3 Step1 함수. `#include <cstring>`(strcmp) 필요 — runtime_gui.cpp에 이미 있음.

- [ ] **Step 5: 정독 + Commit(push 안 함)**

```bash
cd ~/reshade && clang -std=c++17 -fsyntax-only -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DIMGUI_DEFINE_MATH_OPERATORS -Ideps/imgui -Isource source/sherbet_ui.cpp
git add source/sherbet_ui.hpp source/sherbet_ui.cpp source/runtime_gui.cpp source/runtime.hpp
git commit -m "feat(sherbet): 런타임 활성 테마 + 테마 마켓(언락코드)"
```

---

### Task 4: 플랜 CI 검증 (컨트롤러)
- [ ] push + CI watch. 실패 시 첫 에러(대개 없는 ICON_FK 상수 / config.get 오버로드 / obsolete API) 픽스 후 재검증.

---

## 후속 (Plan 7, 이번 범위 밖)
- 노드락(HWID 첫 실행 고정 + sherbet.lic) — Windows API·파일 IO
- 프리셋 마켓 + 개인 세팅 이어받기 (res/presets 내장, 프리셋 로드 연동)
- 코드 생성기 `tools/sherbet_codegen`(테마 언락코드 발급) — Task3의 FNV 공식과 동일 시크릿으로 생성

## Self-Review 메모
- **커버리지:** 스펙 §2 크레딧, §8.2 개인화 각인(빌드 워크플로), §8.3.1 테마 마켓(잠금/언락/적용), §8.5 경고. 노드락·프리셋마켓은 후속 명시.
- **리스크:** default_theme→active_theme 전환 누락 시 테마 전환 미반영(기능 저하, 컴파일 OK). About/마켓 UTF-8 이스케이프 정확성. ICON_FK 상수 존재. config 문자열 get 오버로드.
- **타입 일관성:** active_theme/set_active_theme/is_unlocked/unlock_theme/check_theme_code/all_themes 시그니처 hpp/cpp/호출부 일치.
