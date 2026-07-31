/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "sherbet_theme.hpp"
#include "sherbet_content_json.hpp"
#include "sherbet_xhmarket.hpp"

namespace sherbet
{
	// 레이아웃 상수 (runtime_gui.cpp 와 rail_button 이 공유)
	constexpr float rail_width = 66.0f;
	constexpr float rail_button_size = 44.0f;
	constexpr float header_height = 40.0f;

	// color 의 RGB 는 유지하고 알파(0..255)만 교체
	inline ImU32 with_alpha(ImU32 color, ImU32 alpha) { return (color & 0x00FFFFFF) | (alpha << 24); }

	// 현재 런타임에서 활성화된 테마 (초기값 = SHERBET_DEFAULT_THEME)
	const theme &active_theme();
	const char *active_theme_id();
	// 사용자가 고른 테마 id. 아직 레지스트리에 없는 서버 테마(=/content/me 미도착)면
	// active_theme_id() 와 다를 수 있다. **설정에 저장할 값은 반드시 이쪽**이다.
	// active 를 저장하면 콘텐츠 도착 전 save_config() 한 번에 구매자 선택이 영구 파괴된다.
	const char *desired_theme_id();
	// 사용자 선택(설정 로드 / 테마 카드 클릭). 희망 id 를 기록하고, 지금 적용 가능하면 적용한다.
	void set_active_theme(const char *id);

	bool is_unlocked(const char *id);
	bool has_feature(const char *name);   // 서버가 내려준 잠금 기능 보유 여부(예: "custompicture")
	void mark_entitled(const char *id);   // 서버가 내려준 엔타이틀 테마 id 등록
	void clear_entitlements();            // 재페치 전 초기화
	void apply_content(const std::string &body); // /content/me 응답(JSON) 반영
	const std::vector<content_item> &content_presets(); // 서버가 내려준 프리셋 목록(마켓 UI)
	// 서버가 내려준 조준점 목록(마켓 UI). 잠긴 항목은 코드 없이 이름만 들어 있다.
	// /content/me 가 도착할 때 한 번 파싱되고, 그 뒤로는 매 프레임 읽기만 한다.
	const std::vector<xhmarket::entry> &content_crosshairs();

	// 테마 색/라운드/간격을 ImGui 스타일에 적용 (매 프레임 또는 테마 변경 시 호출)
	void apply_style(ImGuiStyle &style, const theme &t);

	// 그라디언트 + 부드럽게 움직이는 오로라 블롭 배경. rounding>0 이면 둥근 모서리 패널.
	void draw_background(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time, float rounding = 0.0f);
	// 테마 파티클(스파클/하트/잎)을 위로 떠오르게
	void draw_particles(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, const theme &t, float time);
	// 사각형 뒤 부드러운 글로우
	void draw_glow(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max, ImU32 glow_color);

	// 필형 토글 스위치. 값이 변경되면 true 반환.
	bool toggle(const char *label, bool *v);

	// 라운드 패널 차일드 시작/종료(테마 panel 배경 + border + 패딩). height 0 = 자동.
	void begin_card(const char *id, float height = 0.0f);
	void end_card();
	// pill_button 이 쓰는 FramePadding. 호출부가 버튼 폭을 **미리 계산**할 때 같은 값을 써야
	// 하므로 상수로 승격했다(호출부에 14 를 다시 하드코딩하면 같은 실수가 반복된다).
	constexpr ImVec2 pill_padding(14.0f, 7.0f);
	// 필형 버튼. 클릭 시 true.
	bool pill_button(const char *label, bool active);
	// 네비게이션 레일용 44x44 라운드 아이콘 버튼(활성 시 accent 배경+글로우). 클릭 시 true.
	// idle_col != 0 이면 **비활성** 상태의 아이콘 색을 지정한다(브랜드 로고를 선택 표시 없이
	// 항상 액센트로 살리기 위함).
	bool rail_button(const char *id, const char *icon, bool active, ImU32 idle_col = 0);

	// 상태 색(성공/경고/오류). 고정 파스텔은 라이트 테마(딸기)의 흰 카드 위에서 대비 1.6:1 로
	// 사라진다 — 테마 본문색 명도로 라이트/다크를 판정해 두 벌 중 하나를 준다.
	// ⚠️ 새 테마를 추가할 땐 t.text 를 배경과 확실히 대비되게 잡을 것. 본문색과 배경이 둘 다
	//    중간 명도면 이 휴리스틱이 흔들린다.
	enum class status { good, warn, bad };
	ImVec4 status_color(status s);

	// 마켓 카드 안 조준점 미리보기. 사각형 [min,max] 안 중앙에 **실제 픽셀 크기**로 그리고
	// 넘치면 잘라낸다. 기하는 전부 crosshair::build_crosshair 가 계산한다 —
	// 여기에는 산술이 없다(에임 탭 미리보기와 같은 규칙).
	// scratch 는 호출자가 들고 있는 재사용 버퍼다(카드마다 새로 할당하지 않기 위해).
	void draw_crosshair_preview(ImDrawList *dl, const ImVec2 &min, const ImVec2 &max,
		const crosshair::layer &layer, std::vector<crosshair::quad> &scratch);
}
