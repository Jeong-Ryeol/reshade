/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// host 테스트: clang++ -std=c++17 -Wall -Isource -Ideps/imgui tools/sherbet_theme_state_test.cpp -o /tmp/ts && /tmp/ts
//
// 재현하는 고객 버그: "게임을 껐다 켜면 테마가 매번 민트로 돌아온다."
// 서버가 내려주는 테마(deepdark 등)는 /content/me 가 도착해야 레지스트리에 등록된다.
// 설정 로드는 그보다 먼저 일어나므로 find_theme 이 실패하고, 예전 set_active_theme 은
// 조용히 아무것도 안 했다. 게다가 저장 경로가 "적용 중인 id"를 쓰는 바람에 save_config()
// 한 번이면 구매자가 고른 값이 mint 로 **영구 파괴**됐다.
//
// ⚠️ sherbet_themes.cpp 를 먼저 include 한다. sherbet_theme_state.cpp 의 s_active_id 초기화가
// find_theme 을 호출하는데, 그게 읽는 s_dynamic 이 먼저 생성돼 있어야 하기 때문(정적 초기화 순서).
#include "../source/sherbet_themes.cpp"
#include "../source/sherbet_theme_state.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// sherbet_auth.cpp 는 WinInet 의존이라 host 에서 못 쓴다. 엔타이틀 게이트를 실제로
// 밟으려면 auth::enabled() 가 true 여야 하므로(배포 빌드) 여기서 직접 정의한다.
namespace sherbet
{
	namespace auth
	{
		static bool g_auth_enabled = true;
		bool enabled() { return g_auth_enabled; }
	}
}

using namespace sherbet;

// ── runtime_gui.cpp 의 설정 로드/저장 두 줄을 그대로 흉내낸다 ───────────────────
// (그 파일은 Windows 전용이라 host 에서 컴파일할 수 없다. 두 줄이 바뀌면 여기도 바꿀 것.)
//   load: { std::string s; config.get("SHERBET", "ActiveTheme", s); if (!s.empty()) sherbet::set_active_theme(s.c_str()); }
//   save: config.set("SHERBET", "ActiveTheme", std::string(sherbet::desired_theme_id()));
static void config_load(const std::string &saved)
{
	if (!saved.empty()) set_active_theme(saved.c_str());
}
static std::string config_save()
{
	return std::string(desired_theme_id());
}

// 프로세스 재시작 — 정적 상태를 프로세스 시작 시점 값으로 되돌린다.
// (같은 TU 안이라 파일 static 에 직접 접근할 수 있다.)
static void restart()
{
	clear_dynamic_themes();
	clear_entitlements();
	s_features.clear();
	s_content_presets.clear();
	s_active_id = "mint";
	s_desired_id = "mint";
	auth::g_auth_enabled = true;
}

// /content/me 바디 한 조각. unlocked=false 면 "진열은 되지만 권한 없음"(마켓 카드).
static std::string theme_entry(const char *id, bool unlocked)
{
	return std::string("{\"id\":\"") + id + "\",\"display_name\":\"" + id + "\"," +
		"\"unlocked\":" + (unlocked ? "true" : "false") + ",\"particle\":\"spark\"," +
		"\"colors\":{\"bg0\":\"#0a1f1aff\",\"bg1\":\"#0f2e24ff\",\"bg2\":\"#123a2cff\"," +
		"\"panel\":\"#122e26b8\",\"panel_alt\":\"#183a2fd9\",\"chip\":\"#134233ff\"," +
		"\"border\":\"#5df0c040\",\"text\":\"#eafff6ff\",\"text_dim\":\"#8fbfaeff\"," +
		"\"accent\":\"#5df0c0ff\",\"accent2\":\"#a7ffe3ff\",\"glow\":\"#5df0c073\"}}";
}
static std::string manifest(const std::string &entries)
{
	return "{\"themes\":[" + entries + "],\"presets\":[],\"features\":[]}";
}
static const std::string kDeepdarkUnlocked = manifest(theme_entry("deepdark", true));
static const std::string kDeepdarkLocked = manifest(theme_entry("deepdark", false));
static const std::string kNoThemes = manifest("");

static bool active_is(const char *id)
{
	// id 문자열뿐 아니라 실제로 적용되는 theme 구조체까지 일치하는지 본다.
	return std::strcmp(active_theme_id(), id) == 0 && std::strcmp(active_theme().id, id) == 0;
}

// ── 1. 내장 테마: 즉시 적용되고, 나중에 콘텐츠가 와도 그대로 ────────────────────
static void test_builtin_applies_immediately()
{
	restart();
	config_load("peach"); // 내장이라 로드 시점에 이미 레지스트리에 있다
	assert(active_is("peach"));
	assert(config_save() == "peach");

	apply_content(kDeepdarkUnlocked); // 서버 콘텐츠가 도착해도 사용자 선택을 건드리면 안 된다
	assert(active_is("peach"));
	assert(config_save() == "peach");

	restart();
	config_load("noir");
	assert(active_is("noir"));
	apply_content(kNoThemes);
	assert(active_is("noir"));
	assert(config_save() == "noir");
}

// ── 2. 서버 테마 + 콘텐츠 도착 → 복원 (이 스위트의 핵심: 고객이 신고한 버그) ────
static void test_server_theme_restored_when_content_arrives()
{
	restart();
	// 게임 재시작: 설정엔 deepdark 가 있지만 아직 /content/me 가 안 왔다.
	config_load("deepdark");
	assert(active_is("mint"));               // 적용은 못 한다(등록 전) — 여기까진 정상
	assert(config_save() == "deepdark");     // ★ 그래도 구매자의 선택은 살아 있어야 한다

	apply_content(kDeepdarkUnlocked);        // 로그인 후 콘텐츠 도착
	assert(active_is("deepdark"));           // ★ 이 시점에 복원돼야 한다 (버그: 안 됐다)
	assert(config_save() == "deepdark");
}

// ── 3. 콘텐츠가 끝내 안 옴 → 폴백하되 희망 id 는 저장 경로에서 살아남는다 ────────
static void test_desired_survives_when_content_never_arrives()
{
	restart();
	config_load("deepdark");
	assert(active_is("mint"));
	// 오프라인이라 콘텐츠가 없는 채로 다른 설정을 바꿔 save_config() 가 여러 번 돈다.
	const std::string saved_1 = config_save();
	const std::string saved_2 = config_save();
	assert(saved_1 == "deepdark" && saved_2 == "deepdark"); // ★ mint 로 덮이면 영구 파괴

	// 그 상태로 또 재시작해도 선택이 이어지고, 마침내 콘텐츠가 오면 복원된다.
	restart();
	config_load(saved_2);
	assert(active_is("mint"));
	apply_content(kDeepdarkUnlocked);
	assert(active_is("deepdark"));
	assert(config_save() == "deepdark");
}

// ── 4. 엔타이틀 만료 → 복원하지 않는다(유료 테마 무상 제공 금지) ────────────────
static void test_lapsed_entitlement_is_not_restored()
{
	restart();
	config_load("deepdark");
	apply_content(kDeepdarkLocked); // 진열은 되지만 unlocked=false
	assert(!is_unlocked("deepdark"));
	assert(active_is("mint"));           // ★ 복원 금지
	assert(config_save() == "deepdark"); // 희망은 남긴다 — 다시 구매하면 살아난다

	// 아예 매니페스트에서 빠진 경우(역할 회수)도 마찬가지.
	restart();
	config_load("deepdark");
	apply_content(kNoThemes);
	assert(active_is("mint"));
	assert(config_save() == "deepdark");

	// 나중에 권한이 돌아오면 그때 복원.
	apply_content(kDeepdarkUnlocked);
	assert(active_is("deepdark"));

	// 개발 빌드(auth 꺼짐)에서는 전부 열려 있으므로 그대로 복원된다.
	restart();
	auth::g_auth_enabled = false;
	config_load("deepdark");
	apply_content(kDeepdarkLocked);
	assert(active_is("deepdark"));
}

// ── 5. 사용자가 직접 고르면 희망 id 가 갱신된다 ─────────────────────────────────
static void test_user_pick_updates_desired()
{
	restart();
	config_load("deepdark");
	apply_content(kDeepdarkUnlocked);
	assert(active_is("deepdark"));

	set_active_theme("peach"); // 테마 카드 클릭(runtime_gui.cpp: set_active_theme + save_config)
	assert(active_is("peach"));
	assert(config_save() == "peach");

	set_active_theme("deepdark"); // 다시 서버 테마로
	assert(active_is("deepdark"));
	assert(config_save() == "deepdark");

	// 빈/널 id 는 무시 — 희망 id 를 망가뜨리지 않는다.
	set_active_theme("");
	set_active_theme(nullptr);
	assert(active_is("deepdark") && config_save() == "deepdark");

	// 존재하지 않는 id 를 골라도 적용은 안 되지만 희망으로는 기록된다(서버 테마와 구분 불가).
	restart();
	set_active_theme("nosuchtheme");
	assert(active_is("mint"));
	assert(config_save() == "nosuchtheme");
}

// ── 6. 기존 폴백("활성 테마가 사라졌다")은 그대로, 희망 id 는 안 건드린다 ────────
static void test_vanished_active_falls_back_without_clobbering_desired()
{
	restart();
	config_load("deepdark");
	apply_content(kDeepdarkUnlocked);
	assert(active_is("deepdark"));

	// 재페치했더니 deepdark 가 사라졌다 → clear_dynamic_themes 로 활성 테마가 증발한다.
	apply_content(kNoThemes);
	assert(active_is("mint"));           // ★ 폴백은 여전히 동작해야 한다
	assert(config_save() == "deepdark"); // ★ 폴백이 희망 id 를 덮어쓰면 안 된다

	// 다음 페치에서 다시 나타나면 복원.
	apply_content(kDeepdarkUnlocked);
	assert(active_is("deepdark"));
	assert(config_save() == "deepdark");
}

// ── 7. 잡다한 회귀: 엔타이틀/기능 집합이 페치마다 재구성된다 ─────────────────────
static void test_entitlement_and_features_rebuild()
{
	restart();
	assert(is_unlocked("mint"));   // 기본(구매) 테마는 항상 열림
	assert(!is_unlocked("peach")); // 서버가 말하기 전엔 잠김
	assert(!is_unlocked(nullptr));
	assert(!has_feature("custompicture"));

	apply_content("{\"themes\":[],\"presets\":[],\"features\":[\"custompicture\"]}");
	assert(has_feature("custompicture"));
	apply_content(kNoThemes);
	assert(!has_feature("custompicture")); // 재페치 시 초기화

	apply_content(kDeepdarkUnlocked);
	assert(is_unlocked("deepdark"));
	apply_content(kDeepdarkLocked);
	assert(!is_unlocked("deepdark"));
}

int main()
{
	test_builtin_applies_immediately();
	test_server_theme_restored_when_content_arrives();
	test_desired_survives_when_content_never_arrives();
	test_lapsed_entitlement_is_not_restored();
	test_user_pick_updates_desired();
	test_vanished_active_falls_back_without_clobbering_desired();
	test_entitlement_and_features_rebuild();
	std::puts("sherbet_theme_state_test: ALL PASS");
	return 0;
}
