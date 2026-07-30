/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 테마 선택 상태 — 순수 로직(ImGui 함수 호출 없음 → host 테스트 가능).
// sherbet_ui.cpp 는 ImGui 드로잉 전용이라 링크 없이 못 돌린다. 여기 있는 것은
// "무엇이 적용돼 있고 사용자가 무엇을 원했는가" 뿐이라 tools/sherbet_theme_state_test.cpp 가
// 그대로 컴파일해 검증한다.
#include "sherbet_ui.hpp"
#include "sherbet_owner.h"
#include "sherbet_license.hpp"
#include "sherbet_auth.hpp"
#include "sherbet_theme_json.hpp"
#include "sherbet_content_json.hpp"

#include <set>
#include <string>

namespace sherbet
{
	// 빌드 인자로 잘못된 테마 id 가 들어와도(오타 등) mint 로 안전 폴백 — 유료 테마가 잠긴 채 배송되는 사고 방지
	static const char *safe_default_id() { return find_theme(SHERBET_DEFAULT_THEME) != nullptr ? SHERBET_DEFAULT_THEME : "mint"; }
	// 지금 화면에 적용 중인 테마. 항상 레지스트리에 존재하는 id 여야 한다.
	static std::string s_active_id = safe_default_id();
	// 사용자가 "고른" 테마. 설정에 저장되는 값은 이쪽이다.
	//
	// 왜 둘로 나눴나: 서버 테마(deepdark 등)는 /content/me 가 도착해야 레지스트리에 등록된다.
	// 설정 로드 시점엔 아직 없으므로 적용할 수가 없는데, 예전엔 저장 경로가 "적용 중인 id"를
	// 그대로 써서 다음 save_config() 한 번이면 구매자가 고른 값이 mint 로 영구 파괴됐다.
	// 희망 id 를 따로 들고 있으면 (1) 콘텐츠 도착 후 복원할 수 있고 (2) 아직 못 푼 상태로
	// 저장돼도 값이 살아남는다.
	static std::string s_desired_id = s_active_id;
	// 서버(/content/me)가 내려준 엔타이틀 테마 id 집합. 기본(구매) 테마는 별도 처리(항상 열림).
	static std::set<std::string> s_entitled;
	// 서버 프리셋 목록(마켓 UI용)
	static std::vector<content_item> s_content_presets;
	// 서버가 내려준 잠금 기능 집합(예: "custompicture")
	static std::set<std::string> s_features;

	const theme &active_theme()
	{
		const theme *t = find_theme(s_active_id.c_str());
		return t != nullptr ? *t : default_theme();
	}
	const char *active_theme_id() { return s_active_id.c_str(); }
	const char *desired_theme_id() { return s_desired_id.c_str(); }

	// 화면에 적용 중인 테마만 바꾼다. 사용자의 희망(s_desired_id)은 건드리지 않는다.
	// 폴백 경로가 이걸 써야 "지금 못 쓰는 것"과 "사용자가 원한 적 없는 것"이 섞이지 않는다.
	static void apply_theme_now(const char *id)
	{
		if (find_theme(id) != nullptr)
			s_active_id = id;
	}

	// 설정 로드와 사용자 클릭이 공유하는 진입점 — 둘 다 "사용자가 고른 값"이다.
	// 아직 등록 안 된 서버 테마여도 희망 id 로는 반드시 기록해 둔다(저장/복원용).
	void set_active_theme(const char *id)
	{
		if (id == nullptr || id[0] == '\0')
			return;
		s_desired_id = id;
		apply_theme_now(id);
	}

	bool is_unlocked(const char *id)
	{
		if (!id) return false;
		if (!auth::enabled()) return true; // 개발 빌드: 전부 열림
		if (license::iequals(id, safe_default_id())) return true; // 기본(구매) 테마 항상 열림
		return s_entitled.count(id) > 0;
	}
	void mark_entitled(const char *id)
	{
		if (id && id[0] != '\0') s_entitled.insert(id);
	}
	bool has_feature(const char *name)
	{
		if (!name) return false;
		if (!auth::enabled()) return true; // 개발 빌드: 기능 전부 열림
		return s_features.count(name) > 0;
	}
	void clear_entitlements()
	{
		s_entitled.clear();
	}
	const std::vector<content_item> &content_presets() { return s_content_presets; }
	// /content/me 응답(JSON)을 반영: 동적 테마 재구성 + 엔타이틀 집합 재구성.
	// 렌더 스레드에서만 호출(레지스트리/엔타이틀은 렌더 루프가 읽음).
	void apply_content(const std::string &body)
	{
		const std::vector<parsed_theme> themes = parse_themes_manifest(body);
		clear_dynamic_themes();
		clear_entitlements();
		for (const parsed_theme &pt : themes)
		{
			if (pt.id.empty()) continue;
			if (pt.unlocked) mark_entitled(pt.id.c_str()); // 해제된 것만 잠금해제. 잠긴 건 진열만
			if (find_theme(pt.id.c_str()) == nullptr) // 내장에 없는 신규 → 동적 등록(잠겨도 마켓에 뜨게)
				add_dynamic_theme(pt);
		}
		// 설정에서 읽은 서버 테마는 이 시점에야 레지스트리에 들어온다 → 그때 복원한다.
		// 권한(is_unlocked)이 없으면 복원하지 않는다. 엔타이틀이 끊긴 구매자에게
		// 예전에 산 유료 테마를 공짜로 돌려주는 셈이 되기 때문. 희망 id 는 그대로 둔다
		// (다시 구매/역할 복구되면 그때 살아난다).
		if (s_desired_id != s_active_id && is_unlocked(s_desired_id.c_str()))
			apply_theme_now(s_desired_id.c_str());
		// 활성 테마가 사라진 동적 테마였다면 기본으로 폴백.
		// ⚠️ set_active_theme 이 아니라 apply_theme_now — 여기서 희망 id 를 덮어쓰면
		// 위에서 없앤 데이터 파괴 버그가 그대로 되살아난다.
		if (find_theme(active_theme_id()) == nullptr)
			apply_theme_now(safe_default_id());
		s_content_presets = parse_content_items(body, "presets");
		// 잠금 기능 집합 재구성(예: "custompicture")
		s_features.clear();
		for (const std::string &f : auth::json_string_array(body, "features"))
			if (!f.empty()) s_features.insert(f);
	}
}
