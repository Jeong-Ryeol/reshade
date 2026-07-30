/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 발로란트급 조준점 — 순수 로직(플랫폼 비의존).
// 설계 근거: docs/superpowers/specs/2026-07-30-sherbet-valorant-crosshair-design.md
// 아래 주석의 §N 은 전부 그 스펙의 절 번호다.
//
// Windows·ImGui·파일 IO 는 여기 넣지 않는다. 그리기는 runtime_gui.cpp, 설정 저장은
// runtime.cpp 가 맡는다. **판정이 들어가는 것은 전부 여기 있다** — 이 헤더만 맥/리눅스
// clang 으로 실제 단위테스트할 수 있기 때문이다(tools/sherbet_crosshair_test.cpp).
//
// 이 파일의 존재 이유는 **공유 코드 호환성**이다. 발로란트 유저는 이미 코드를 주고받고
// 있고, 키 하나를 잘못 매핑하면 남의 조준점이 조용히 다른 모양으로 들어온다.
// 키 전표는 §2.3, 기본값 생략 규칙은 §2.4, 정규 출력 순서는 §2.7 을 그대로 옮겼다.
//
// ── 이 헤더가 담지 않는 것 ─────────────────────────────────────────────
//  * `bHideCrosshair`  — 공유 코드에 키가 없다(§1.2). Sherbet 자체 on/off 로 대체한다(§5).
//  * `bShowMinError`   — 공유 코드에 키가 없다(§1.3). 실유저 프로필 17개 전부 true 였으므로
//                        kShowMinError 상수로 고정하고 프로필에 담지 않는다.
//  * `bScaleToResolution` — §0.4. 발로란트 기본이 false 이고 코드에 담기지도 않는다.
//                        Sherbet 자체 토글(기본 OFF)로만 제공한다.
//  세 값은 프로필(=공유 코드로 표현되는 상태)이 아니므로 profile 에 넣지 않았다.
//  넣었다면 "직렬화되지 않는 필드" 가 생겨 왕복 동치(§ canonical)가 흐려진다.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace sherbet
{
	namespace crosshair
	{
		// ─────────────────────────────────────────────────────────────
		// 한계값
		// ─────────────────────────────────────────────────────────────

		// §2.6: **import 시 §1 의 UI 범위로 클램프하지 않는다.** UI 슬라이더는 §1 범위지만
		// 텍스트 입력으로 초과할 수 있고, 바깥선 길이 상한이 10인지 20인지는 아직 실물
		// 미확인이다(§6 #1). 안전 상한에서만 자른다.
		constexpr int kValueCap = 200;         // 픽셀 정수 안전 상한
		constexpr float kFloatCap = 1000.0f;   // 배율·투명도 안전 상한
		constexpr std::size_t kMaxCodeLen = 4096;

		// §3.4 휴지 상태 +4px(min error). 도(degree) 논증은 채택하지 않았다 — 4는 그냥 4px 다.
		constexpr int kMinErrorPx = 4;

		// §1.3 · §5. 코드 키가 없고 실유저 프로필 17개 전부 true 였다. 항상 true 로 취급한다.
		constexpr bool kShowMinError = true;
		// §0.4. 발로란트 기본이 false 다. Sherbet 도 기본은 백버퍼 픽셀 그대로 그린다.
		constexpr bool kScaleToResolutionDefault = false;

		// ─────────────────────────────────────────────────────────────
		// §6 실물 확인 대기 — 추측으로 채우지 않고 **한 곳에서 뒤집을 수 있게** 둔 것들
		//
		// 넷 다 "발로란트를 한 번 켜서 스크린샷을 확대하면 끝나는" 항목이다. 지금 값은
		// 스펙이 채택한 읽기이고, 근거 등급이 낮다는 것도 스펙이 명시하고 있다.
		// 실물 확인 후에는 **여기 상수 하나만** 바꾸면 렌더 전체가 따라온다.
		// ─────────────────────────────────────────────────────────────

		// §6 #3 · §0.3 #3 — 홀수 두께에서 어느 쪽이 1px 밀리는가.
		//   true (채택) : 좌·상 팔이 1px 더 자라 조준점이 **좌상단으로 0.5px** 스냅된다.
		//   false       : 우·하 팔이 밀려 우하단으로 스냅된다(정확한 거울상).
		// 근거가 VCRDB 단일 계보뿐이고 인게임 스크린샷으로 검증된 바 없다.
		// 확인법: `0;P;h;0;0t;3;0o;0;0l;6;1b;0` 스크린샷을 확대해 좌 팔이 우 팔보다
		// 왼쪽으로 1px 더 나가 있는지 본다.
		constexpr bool kOddThicknessShiftsTopLeft = true;

		// §6 #6 · §0.3 #18 — 휴지 상태 +4px(min error)의 게이트.
		//   firing_error (채택) : 그 라인의 f(bShowShootingError)가 꺼지면 +4 도 사라진다.
		//                         커뮤니티 렌더러 전부의 관행이다.
		//   always              : 게임의 실제 게이트가 라인 레벨 bShowMinError(기본 true,
		//                         공유 코드 키 없음)일 경우. 그러면 f;0 이어도 +4 가 남는다.
		// 확인법: `0f;1`(기본)과 `0f;0` 두 코드로 휴지 상태 중앙~선 안쪽 끝을 잰다.
		//   7px vs 3px → firing_error 가 맞다.   둘 다 7px → always 로 바꾼다.
		// ⚠️ 바꾸면 tools/sherbet_crosshair_test.cpp 의 test_helpers / test_example_c 가
		//    실패한다. 그건 정상이다 — 그 단언들이 지금 읽기를 못 박고 있다는 뜻이다.
		enum class min_error_gate { firing_error, always };
		constexpr min_error_gate kMinErrorGate = min_error_gate::firing_error;

		// §6 #4 · §0.3 #5 — 팔의 윤곽선을 언제 그리는가.
		//   true (채택) : 팔마다 [본체 → 자기 링] 1-pass. 뒤에 오는 팔의 링이 앞 팔의
		//                 본체를 덮을 수 있어 겹치는 자리가 더 진해진다. 모든 레퍼런스
		//                 렌더러가 이렇게 한다.
		//   false       : 그룹마다 [링 4개 전부 → 본체 4개 전부] 2-pass. 본체가 항상
		//                 모든 링 위에 온다.
		// 확인법: `0o;0` + `t;6` + `o;0.5` + `0t;2`. 팔 4개의 윤곽선이 중앙에서 만나는
		//   자리에 더 진한 십자 이음매가 보이면 1-pass, 균일하면 2-pass 다.
		constexpr bool kOutlineOnePass = true;

		// §6 #5 · §0.3 #6 — 중앙 점을 안쪽선 위에 그리는가 아래에 그리는가. 소스가 2:2 다.
		//   above_inner (채택) : inner → dot → outer (VCRDB + valoreye)
		//   below_all          : dot → inner → outer (genesy + iNiR)
		// 확인법: `d;1;z;6;a;0.5` + `0o;0;0t;2`. 반투명 점 아래로 안쪽선이 비쳐 보이면
		//   점이 위(above_inner), 안 보이면 아래(below_all)다.
		enum class dot_order { above_inner, below_all };
		constexpr dot_order kDotOrder = dot_order::above_inner;

		// §1 의 UI 슬라이더 범위. **파서는 이 범위를 강제하지 않는다**(위 주석 참고) —
		// 나중에 UI 를 붙일 때 슬라이더 한계로만 쓴다.
		struct int_range { int lo, hi; };
		struct float_range { float lo, hi; };

		constexpr int_range kUiOutlineThickness { 1, 6 };   // §1.2 min 이 0 이 아니라 1
		constexpr int_range kUiCenterDotSize { 1, 6 };
		constexpr int_range kUiColorIndex { 0, 8 };
		constexpr int_range kUiLineThickness { 0, 10 };
		constexpr int_range kUiInnerLineLength { 0, 20 };
		constexpr int_range kUiOuterLineLength { 0, 10 };   // ⚠️ inner 와 다르다(§1.3)
		constexpr int_range kUiLineLengthVertical { 0, 20 };
		constexpr int_range kUiInnerLineOffset { 0, 20 };
		constexpr int_range kUiOuterLineOffset { 0, 40 };   // ⚠️ inner 와 다르다(§1.3)
		constexpr float_range kUiOpacity { 0.0f, 1.0f };
		constexpr float_range kUiErrorScale { 0.0f, 3.0f };
		constexpr float_range kUiSniperDotSize { 0.0f, 4.0f };

		// ─────────────────────────────────────────────────────────────
		// 색
		// ─────────────────────────────────────────────────────────────

		struct rgba
		{
			std::uint8_t r = 255, g = 255, b = 255, a = 255;
		};
		inline bool operator==(const rgba &x, const rgba &y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; }
		inline bool operator!=(const rgba &x, const rgba &y) { return !(x == y); }

		struct rgb
		{
			std::uint8_t r = 255, g = 255, b = 255;
		};
		inline bool operator==(const rgb &x, const rgb &y) { return x.r == y.r && x.g == y.g && x.b == y.b; }
		inline bool operator!=(const rgb &x, const rgb &y) { return !(x == y); }

		// §1.5 프리셋 8종. colorNames 원문:
		//   ["White","Green","Yellow Green","Green Yellow","Yellow","Cyan","Pink","Red","Custom"]
		// ⚠️ 인덱스 3 은 #DFFF00 이지 #ADFF2F 가 **아니다** — 여기서 틀리는 파서가 많다(§2.10).
		constexpr rgb kPresets[8] = {
			{ 255, 255, 255 }, // 0 White        #FFFFFF
			{ 0,   255, 0   }, // 1 Green        #00FF00
			{ 127, 255, 0   }, // 2 Yellow Green #7FFF00
			{ 223, 255, 0   }, // 3 Green Yellow #DFFF00
			{ 255, 255, 0   }, // 4 Yellow       #FFFF00
			{ 0,   255, 255 }, // 5 Cyan         #00FFFF
			{ 255, 0,   255 }, // 6 Pink         #FF00FF
			{ 255, 0,   0   }, // 7 Red          #FF0000
		};
		constexpr int kCustomColorIndex = 8;

		// §1.5 · §0.3 #9. 공유 코드에 윤곽선 색 키가 **없다.** 그래서 import 되는 코드의
		// 윤곽선은 항상 검정이다. (게임 세이브의 outlineColor 는 완전한 RGBA 필드이고
		// 실덤프에 진파랑 사례가 있으므로 "고정 상수"라고 쓰면 틀린다 — 코드에 없을 뿐이다.)
		constexpr rgb kOutlineColor { 0, 0, 0 };

		// ─────────────────────────────────────────────────────────────
		// 파라미터 모델 (§1)
		// ─────────────────────────────────────────────────────────────

		enum class line_kind { inner, outer };

		// §1.3 Inner Lines(코드 접두사 '0') / Outer Lines(접두사 '1').
		// ⚠️ 기본값이 inner/outer 마다 다르다. 기본 생성자는 **inner** 기본값을 준다.
		//    outer 는 반드시 make_line(line_kind::outer) 로 만들어야 한다.
		struct line
		{
			bool show_lines = true;             // b
			int thickness = 2;                  // t
			int length = 6;                     // l   (outer 기본 2)
			int length_vertical = 6;            // v   (outer 기본 2)
			bool allow_vert_scaling = false;    // g   가로/세로 링크 해제
			int offset = 3;                     // o   (outer 기본 10)
			float opacity = 0.8f;               // a   (outer 기본 0.35)
			bool show_movement_error = false;   // m   (outer 기본 true) ⚠️
			float movement_error_scale = 1.0f;  // s
			bool show_shooting_error = true;    // f
			float firing_error_scale = 1.0f;    // e
		};

		inline line make_line(line_kind k)
		{
			line l;
			if (k == line_kind::outer)
			{
				l.length = 2;
				l.length_vertical = 2;
				l.offset = 10;
				l.opacity = 0.35f;
				l.show_movement_error = true; // 바깥선 이동 오차 기본 ON — 틀리는 파서가 많다(§2.10)
			}
			return l;
		}

		inline bool operator==(const line &x, const line &y)
		{
			return x.show_lines == y.show_lines && x.thickness == y.thickness && x.length == y.length &&
			       x.length_vertical == y.length_vertical && x.allow_vert_scaling == y.allow_vert_scaling &&
			       x.offset == y.offset && x.opacity == y.opacity && x.show_movement_error == y.show_movement_error &&
			       x.movement_error_scale == y.movement_error_scale && x.show_shooting_error == y.show_shooting_error &&
			       x.firing_error_scale == y.firing_error_scale;
		}
		inline bool operator!=(const line &x, const line &y) { return !(x == y); }

		// §1.2 Primary / ADS 레이어. 코드에서는 각각 'P' / 'A' 섹션이다.
		struct layer
		{
			int color_index = 0;                  // c  0..8 (8 = 커스텀 스와치)
			rgba custom_color;                    // u  RRGGBBAA, 기본 FFFFFFFF
			bool use_custom_color = false;        // b  bUseCustomColor — 실제 적용 여부
			bool has_outline = true;              // h
			int outline_thickness = 1;            // t  UI min 이 0 이 아니라 1
			float outline_opacity = 0.5f;         // o
			bool show_center_dot = false;         // d
			int center_dot_size = 2;              // z  정사각형 한 변의 길이(반지름 아님)
			float center_dot_opacity = 1.0f;      // a
			bool fade_with_firing_error = true;   // f
			bool show_spectated = true;           // s  Sherbet 무관 — 보관·재출력만
			bool fix_min_error = false;           // m  켜면 휴지 상태 +4px 제거(§3.4)
			line inner = make_line(line_kind::inner);
			line outer = make_line(line_kind::outer);
		};

		inline bool operator==(const layer &x, const layer &y)
		{
			return x.color_index == y.color_index && x.custom_color == y.custom_color &&
			       x.use_custom_color == y.use_custom_color && x.has_outline == y.has_outline &&
			       x.outline_thickness == y.outline_thickness && x.outline_opacity == y.outline_opacity &&
			       x.show_center_dot == y.show_center_dot && x.center_dot_size == y.center_dot_size &&
			       x.center_dot_opacity == y.center_dot_opacity &&
			       x.fade_with_firing_error == y.fade_with_firing_error && x.show_spectated == y.show_spectated &&
			       x.fix_min_error == y.fix_min_error && x.inner == y.inner && x.outer == y.outer;
		}
		inline bool operator!=(const layer &x, const layer &y) { return !(x == y); }

		// §1.4 Sniper Scope('S' 섹션). ⚠️ 여기만 크기가 정수가 아니라 float 이고,
		// 커스텀 색 키가 'u' 가 아니라 **'t'** 다. 둘 다 실수하기 딱 좋은 부분이다.
		// Sherbet 은 스나이퍼를 그리지 않지만 **파싱·보관·재출력은 한다**(§2.7 마지막).
		struct sniper
		{
			bool show_center_dot = true;      // d  ⚠️ 기본 true
			int color_index = 7;              // c  ⚠️ 기본 7(Red)
			bool use_custom_color = false;    // b
			rgba custom_color;                // t  ⚠️ 'u' 가 아니다
			float center_dot_size = 1.0f;     // s  ⚠️ float, 0..4
			float center_dot_opacity = 0.75f; // o  ⚠️ 기본 0.75
		};

		inline bool operator==(const sniper &x, const sniper &y)
		{
			return x.show_center_dot == y.show_center_dot && x.color_index == y.color_index &&
			       x.use_custom_color == y.use_custom_color && x.custom_color == y.custom_color &&
			       x.center_dot_size == y.center_dot_size && x.center_dot_opacity == y.center_dot_opacity;
		}
		inline bool operator!=(const sniper &x, const sniper &y) { return !(x == y); }

		// §2.7 규칙 8 — 파싱 때 만난 미지 키/미지 섹션을 원문 그대로 보관했다가 다시 내보낸다.
		// 미래 패치에서 키가 늘어도 Sherbet 을 거쳐간 코드가 값을 잃지 않는다.
		struct unknown_entry
		{
			std::string key, value;
		};
		inline bool operator==(const unknown_entry &x, const unknown_entry &y) { return x.key == y.key && x.value == y.value; }
		inline bool operator!=(const unknown_entry &x, const unknown_entry &y) { return !(x == y); }

		// marker: "" = 루트, "P"/"A"/"S" = 알려진 섹션, 그 외 1글자 대문자 = 미지 섹션.
		struct unknown_section
		{
			std::string marker;
			std::vector<unknown_entry> items;
		};
		inline bool operator==(const unknown_section &x, const unknown_section &y) { return x.marker == y.marker && x.items == y.items; }
		inline bool operator!=(const unknown_section &x, const unknown_section &y) { return !(x == y); }

		using unknown_store = std::vector<unknown_section>;

		// 프로필 = **공유 코드로 표현되는 상태 전부**, 그 이상도 이하도 아니다.
		struct profile
		{
			// §1.1 General(코드에서는 섹션 마커 없이 맨 앞)
			bool ads_copies_primary = true;    // p  ⚠️ 기본 true
			bool override_all_primary = false; // c
			bool advanced_options = false;     // s

			layer primary; // P
			layer ads;     // A
			sniper snipe;  // S

			unknown_store unknowns;
		};

		inline bool operator==(const profile &x, const profile &y)
		{
			return x.ads_copies_primary == y.ads_copies_primary && x.override_all_primary == y.override_all_primary &&
			       x.advanced_options == y.advanced_options && x.primary == y.primary && x.ads == y.ads &&
			       x.snipe == y.snipe && x.unknowns == y.unknowns;
		}
		inline bool operator!=(const profile &x, const profile &y) { return !(x == y); }

		// ─────────────────────────────────────────────────────────────
		// 순수 헬퍼
		// ─────────────────────────────────────────────────────────────

		inline int clamp_int(int v) { return v < 0 ? 0 : (v > kValueCap ? kValueCap : v); }
		inline float clamp_float(double v) { return v < 0.0 ? 0.0f : (v > kFloatCap ? kFloatCap : static_cast<float>(v)); }
		inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

		// §2.5 float 출력 관례(소수점 최대 3자리)가 도달할 수 있는 값으로 맞춘다.
		// 파싱에는 정밀도 제약이 없지만(§0.3 #14) 출력은 3자리이므로, 왕복 동치를
		// 정의하려면 이 반올림이 필요하다. 음수는 코드 문법상 존재할 수 없다(§2.6 문자 검사).
		inline float round_milli(float v)
		{
			if (v < 0.0f)
				return 0.0f;
			if (v > kFloatCap)
				v = kFloatCap;
			const long long milli = static_cast<long long>(static_cast<double>(v) * 1000.0 + 0.5);
			return static_cast<float>(static_cast<double>(milli) / 1000.0);
		}

		// §2.8 적용 판정: c;8 **또는** b;1. 두 경로를 다 처리해야 한다 — 게임 세이브에는
		// 팔레트 인덱스 자체가 없으므로 외부 툴이 만든 코드는 b;1 만 가질 수 있다.
		inline bool uses_custom_color(const layer &L) { return L.color_index == kCustomColorIndex || L.use_custom_color; }
		inline bool uses_custom_color(const sniper &S) { return S.color_index == kCustomColorIndex || S.use_custom_color; }

		// 프리셋 인덱스 → RGB. **전역 함수(total)** 다 — 범위 밖 인덱스는 클램프하지 않고
		// 그대로 보관하므로(§2.6) 여기서 White 로 떨어뜨린다.
		inline rgb preset_color(int index)
		{
			if (index < 0 || index >= static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0])))
				return kPresets[0];
			return kPresets[index];
		}

		// §2.8 알파 주의: 커스텀 색의 알파는 **읽어서 보관하고 export 때 되돌려 주되
		// 렌더링에는 쓰지 않는다.** 발로란트는 opacity 키들로 투명도를 제어하므로
		// 여기서 또 곱하면 두 번 어두워진다. 그래서 반환 타입이 rgba 가 아니라 rgb 다.
		inline rgb resolve_color(const layer &L)
		{
			if (uses_custom_color(L))
				return rgb { L.custom_color.r, L.custom_color.g, L.custom_color.b };
			return preset_color(L.color_index);
		}
		inline rgb resolve_color(const sniper &S)
		{
			if (uses_custom_color(S))
				return rgb { S.custom_color.r, S.custom_color.g, S.custom_color.b };
			return preset_color(S.color_index);
		}

		// §3.6 의 `(int)(opacity * 255.f)` 를 그대로 옮긴다(반올림이 아니라 절삭이다).
		inline std::uint8_t alpha_from_opacity(float opacity)
		{
			return static_cast<std::uint8_t>(static_cast<int>(clamp01(opacity) * 255.0f));
		}

		// §3.2 세로 팔의 길이. g==1 이면 v, g==0 이면 l 이다.
		// (v 는 g 와 무관하게 저장·출력되지만 **렌더링에는 g 가 켜졌을 때만 쓰인다**.)
		inline int effective_vertical_length(const line &L)
		{
			return L.allow_vert_scaling ? L.length_vertical : L.length;
		}

		// §3.4 휴지 상태의 유효 오프셋. 동적 오차(§4)를 더하기 전 값이다.
		//   그 라인의 Firing Error 가 켜져 있고 프로필의 m(fix_min_error)이 꺼져 있으면 +4px.
		//   inner/outer 가 **각자 독립적으로** 받는다 → 기본 조준점은 3+4=7 / 10+4=14.
		//   +4 는 발사 오차 배율로 곱해지지 않는다. 불리언만 본다.
		// ⚠️ 게이트를 f 로 두는 것은 커뮤니티 렌더러 전부의 관행이고 **근사임이 명시돼 있다**
		//    (§0.3 #18 / §6 #6). 게임의 실제 게이트는 코드 키가 없는 bShowMinError 일 수 있다.
		//    → kMinErrorGate 하나로 뒤집는다.
		inline int resting_offset(const line &L, const layer &owner)
		{
			const bool gated = (kMinErrorGate == min_error_gate::always) ? true : L.show_shooting_error;
			return clamp_int(L.offset) + ((gated && !owner.fix_min_error) ? kMinErrorPx : 0);
		}

		// §3.4 유효 오프셋 = 휴지 오프셋 + §4 의 동적 오차(정수 반올림).
		// err_px 는 태스크 4(오차 애니메이션)가 넘긴다. 정적 조준점에서는 0 이다.
		// 정수 반올림 때문에 확장이 1px 계단으로 움직인다 — 의도한 것이다(§4.3).
		inline int effective_offset(const line &L, const layer &owner, float err_px)
		{
			int off = resting_offset(L, owner);
			if (err_px > 0.0f)
				off += static_cast<int>(err_px + 0.5f); // err_px >= 0 이므로 floor(x+0.5) 와 같다
			return off;
		}

		// §2.6 파싱 후처리. **파싱된 원본 값은 파괴하지 않는다** — 고급 옵션이 꺼져 있다고
		// 파싱 단계에서 ads = primary 를 대입해 버리면, 유저가 다시 켰을 때 원래 ADS 설정이
		// 날아가고 export 도 원본과 달라진다. 그래서 *사용 시점*의 계산으로 둔다.
		inline const layer &effective_ads(const profile &p)
		{
			return (p.ads_copies_primary || !p.advanced_options) ? p.primary : p.ads;
		}

		// ─────────────────────────────────────────────────────────────
		// 공유 코드 (§2)
		// ─────────────────────────────────────────────────────────────

		enum class section { root, primary, ads, sniper, unknown };

		// 코드 **전체**를 거부하는 사유. §2.6 "부분 적용은 없다" —
		// 아래 중 하나에 걸리면 out 은 한 글자도 건드리지 않는다.
		enum class parse_error
		{
			ok = 0,
			empty,        // trim 후 빈 문자열
			too_long,     // kMaxCodeLen 초과
			illegal_char, // [0-9A-Za-z;.] 이외의 문자(공백·'-'·':' …)
			bad_prefix,   // tokens[0] != "0" (§2.1 맨 앞 '0' 은 고정 접두 토큰)
			empty_token,  // ";;" 또는 끝의 ';' — 키/값 자리가 비었다
			dangling_key, // 값이 없는 키로 코드가 끝났다
		};

		struct parse_report
		{
			parse_error error = parse_error::ok;
			int unknown_items = 0; // 미지 키 + 미지 섹션 안의 키. 보관은 되지만 해석은 안 된다
			int bad_values = 0;    // 타입이 안 맞아 무시한 키(그 키만 기본값 유지)
			bool ok() const { return error == parse_error::ok; }
		};

		namespace detail
		{
			inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

			// §2.6 2) 게임이 쓰는 정규식 형태 ^0[a-zA-Z0-9;.]*$ 와 같은 문자 집합.
			// '-' 가 빠져 있는 것이 중요하다 — 코드에는 음수가 애초에 못 들어온다.
			inline bool is_code_char(char c)
			{
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == ';' || c == '.';
			}
			inline bool is_marker_token(const std::string &t) { return t.size() == 1 && t[0] >= 'A' && t[0] <= 'Z'; }

			// 순수 10진 정수. 소수점이 있으면 실패한다 → §2.6 "int 필드에 소수(0l;4.5)는
			// **그 키만 무시**". 자릿수가 터무니없어도 UB 없이 안전 상한에서 포화한다.
			inline bool parse_uint(const std::string &s, int &out)
			{
				if (s.empty())
					return false;
				long long v = 0;
				for (char c : s)
				{
					if (c < '0' || c > '9')
						return false;
					if (v <= kValueCap)
						v = v * 10 + (c - '0');
					if (v > kValueCap)
						v = kValueCap; // §2.6 "안전 상한에서만 잘라낸다"
				}
				out = static_cast<int>(v);
				return true;
			}

			// 10^n (n <= 18 은 double 로 정확하다).
			inline double pow10(int n)
			{
				double p = 1.0;
				for (int i = 0; i < n; ++i)
					p *= 10.0;
				return p;
			}

			// float 값. 부호·지수 표기는 없다(문자 집합상 '-'/'+' 는 코드에 못 들어오고,
			// "1e2" 는 여기서 실패해 그 키만 기본값으로 남는다).
			// **가수를 하나로 모아 마지막에 한 번만 나눈다** — 그래야 "1.02" 와 "1020/1000"
			// 이 비트 단위로 같은 double 이 되어 round_milli 와의 왕복이 정확해진다.
			inline bool parse_number(const std::string &s, double &out)
			{
				std::size_t i = 0;
				bool any = false, saturated = false;
				unsigned long long mant = 0;
				int frac_digits = 0;

				for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
				{
					any = true;
					if (mant < 100000000000000000ull)
						mant = mant * 10 + static_cast<unsigned>(s[i] - '0');
					else
						saturated = true;
				}
				if (i < s.size() && s[i] == '.')
				{
					++i;
					for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i)
					{
						any = true;
						if (mant < 100000000000000000ull)
						{
							mant = mant * 10 + static_cast<unsigned>(s[i] - '0');
							++frac_digits;
						}
					}
				}
				if (!any || i != s.size())
					return false;

				out = saturated ? 1e18 : static_cast<double>(mant) / pow10(frac_digits);
				return true;
			}

			inline int hex_digit(char c)
			{
				if (c >= '0' && c <= '9')
					return c - '0';
				if (c >= 'A' && c <= 'F')
					return c - 'A' + 10;
				if (c >= 'a' && c <= 'f')
					return c - 'a' + 10;
				return -1;
			}

			// §2.6 "6자리 HEX(u;FFFFFF) → 뒤에 FF 를 붙여 RRGGBBAA 로". (§6 #10 실물 확인 대상)
			inline bool parse_hex(const std::string &s, rgba &out)
			{
				if (s.size() != 6 && s.size() != 8)
					return false;
				std::uint8_t v[4] = { 255, 255, 255, 255 };
				for (std::size_t i = 0; i + 1 < s.size(); i += 2)
				{
					const int hi = hex_digit(s[i]), lo = hex_digit(s[i + 1]);
					if (hi < 0 || lo < 0)
						return false;
					v[i / 2] = static_cast<std::uint8_t>(hi * 16 + lo);
				}
				out.r = v[0];
				out.g = v[1];
				out.b = v[2];
				out.a = (s.size() == 8) ? v[3] : static_cast<std::uint8_t>(255);
				return true;
			}

			// §2.5 hex 는 대문자 8자리.
			inline std::string hex_string(const rgba &c)
			{
				static const char *D = "0123456789ABCDEF";
				const std::uint8_t v[4] = { c.r, c.g, c.b, c.a };
				std::string s(8, '0');
				for (int i = 0; i < 4; ++i)
				{
					s[i * 2] = D[v[i] >> 4];
					s[i * 2 + 1] = D[v[i] & 0x0F];
				}
				return s;
			}

			inline std::string int_string(int v) { return std::to_string(clamp_int(v)); }
			inline std::string bool_string(bool v) { return v ? "1" : "0"; }

			// §2.4 "기본값과 같은 항목은 생략" 의 '같다' 는 **코드가 표현할 수 있는 정밀도**
			// 에서 따진다. 0.8004 는 코드에 "0.8" 로 실려 기본값과 구분되지 않으므로 생략해야
			// 한다 — raw 비교로 내보내면 다시 읽었을 때 생략되어 export 가 두 번 흔들린다.
			inline bool int_differs(int a, int b) { return clamp_int(a) != clamp_int(b); }
			inline bool float_differs(float a, float b) { return round_milli(a) != round_milli(b); }

			// §2.5 float: 소수점 최대 3자리, 뒤 0 제거. "1.000" → "1", "0.020" → "0.02".
			// snprintf("%.3f") 를 쓰지 않는다 — 게임 프로세스의 로케일이 LC_NUMERIC=de_DE 면
			// 소수점이 ',' 로 나와 그 코드는 §2.6 의 문자 검사에서 통째로 거부된다.
			inline std::string float_string(float v)
			{
				if (v < 0.0f)
					v = 0.0f;
				if (v > kFloatCap)
					v = kFloatCap;
				const long long milli = static_cast<long long>(static_cast<double>(v) * 1000.0 + 0.5);
				std::string s = std::to_string(milli / 1000);
				int frac = static_cast<int>(milli % 1000);
				if (frac != 0)
				{
					char buf[4] = { static_cast<char>('0' + frac / 100), static_cast<char>('0' + (frac / 10) % 10),
						            static_cast<char>('0' + frac % 10), '\0' };
					std::string f(buf);
					while (!f.empty() && f.back() == '0')
						f.pop_back();
					s += '.';
					s += f;
				}
				return s;
			}

			// 손으로 만든 unknown 이 코드 문법을 깨뜨리지 못하게 막는다.
			// (파서가 만든 것은 언제나 유효하다. 이건 호출 측 방어다.)
			inline bool valid_unknown_token(const std::string &t)
			{
				if (t.empty())
					return false;
				for (char c : t)
					if (!is_code_char(c) || c == ';')
						return false;
				return true;
			}
			inline bool valid_unknown_key(const std::string &k)
			{
				// 1글자 대문자는 키 자리에서 섹션 마커로 읽히므로 키가 될 수 없다.
				return valid_unknown_token(k) && !is_marker_token(k);
			}
			inline bool valid_unknown_marker(const std::string &m)
			{
				return is_marker_token(m) && m != "P" && m != "A" && m != "S";
			}

			inline std::vector<unknown_entry> &unknown_bucket(unknown_store &u, const std::string &marker)
			{
				for (unknown_section &s : u)
					if (s.marker == marker)
						return s.items;
				u.push_back(unknown_section { marker, {} });
				return u.back().items;
			}
			inline void set_bool(bool &dst, const std::string &v, parse_report &rep)
			{
				int i = 0;
				if (parse_uint(v, i))
					dst = (i != 0);
				else
					++rep.bad_values;
			}
			inline void set_int(int &dst, const std::string &v, parse_report &rep)
			{
				int i = 0;
				if (parse_uint(v, i))
					dst = i;
				else
					++rep.bad_values;
			}
			inline void set_float(float &dst, const std::string &v, parse_report &rep)
			{
				double d = 0.0;
				if (parse_number(v, d))
					dst = clamp_float(d);
				else
					++rep.bad_values;
			}
			inline void set_hex(rgba &dst, const std::string &v, parse_report &rep)
			{
				rgba c;
				if (parse_hex(v, c))
					dst = c;
				else
					++rep.bad_values;
			}

			// §2.3 라인 그룹 키. **'0l' 은 두 글자 통짜 키다** — "섹션 0 의 키 l" 로 쪼개
			// 읽으면 안쪽선 값이 전부 조용히 사라진다(§2.10, RazorReaper 가 고친 버그).
			inline bool apply_line(line &L, char k, const std::string &v, parse_report &rep)
			{
				switch (k)
				{
				case 'b': set_bool(L.show_lines, v, rep); return true;
				case 't': set_int(L.thickness, v, rep); return true;
				case 'l': set_int(L.length, v, rep); return true;
				case 'v': set_int(L.length_vertical, v, rep); return true;
				case 'g': set_bool(L.allow_vert_scaling, v, rep); return true;
				case 'o': set_int(L.offset, v, rep); return true;
				case 'a': set_float(L.opacity, v, rep); return true;
				case 'm': set_bool(L.show_movement_error, v, rep); return true;
				case 's': set_float(L.movement_error_scale, v, rep); return true;
				case 'f': set_bool(L.show_shooting_error, v, rep); return true;
				case 'e': set_float(L.firing_error_scale, v, rep); return true;
				default: return false;
				}
			}

			// §2.3 P/A 섹션. 같은 글자가 섹션마다 다른 뜻이라 섹션별로 분리해 둔다.
			inline bool apply_layer(layer &L, const std::string &k, const std::string &v, parse_report &rep)
			{
				if (k.size() == 2 && (k[0] == '0' || k[0] == '1'))
					return apply_line(k[0] == '0' ? L.inner : L.outer, k[1], v, rep);
				if (k.size() != 1)
					return false;
				switch (k[0])
				{
				case 'c': set_int(L.color_index, v, rep); return true;
				case 'u': set_hex(L.custom_color, v, rep); return true;
				case 'b': set_bool(L.use_custom_color, v, rep); return true;
				case 'h': set_bool(L.has_outline, v, rep); return true;
				case 't': set_int(L.outline_thickness, v, rep); return true;
				case 'o': set_float(L.outline_opacity, v, rep); return true;
				case 'd': set_bool(L.show_center_dot, v, rep); return true;
				case 'z': set_int(L.center_dot_size, v, rep); return true;
				case 'a': set_float(L.center_dot_opacity, v, rep); return true;
				case 'f': set_bool(L.fade_with_firing_error, v, rep); return true;
				case 's': set_bool(L.show_spectated, v, rep); return true;
				case 'm': set_bool(L.fix_min_error, v, rep); return true;
				default: return false;
				}
			}

			// §2.3 S 섹션. ⚠️ 커스텀 색이 'u' 가 아니라 't', 크기 's' 는 float 이다.
			inline bool apply_sniper(sniper &S, const std::string &k, const std::string &v, parse_report &rep)
			{
				if (k.size() != 1)
					return false;
				switch (k[0])
				{
				case 'd': set_bool(S.show_center_dot, v, rep); return true;
				case 'c': set_int(S.color_index, v, rep); return true;
				case 'b': set_bool(S.use_custom_color, v, rep); return true;
				case 't': set_hex(S.custom_color, v, rep); return true;
				case 's': set_float(S.center_dot_size, v, rep); return true;
				case 'o': set_float(S.center_dot_opacity, v, rep); return true;
				default: return false;
				}
			}

			// §1.1 루트. 라인 키는 없다.
			inline bool apply_root(profile &p, const std::string &k, const std::string &v, parse_report &rep)
			{
				if (k.size() != 1)
					return false;
				switch (k[0])
				{
				case 'p': set_bool(p.ads_copies_primary, v, rep); return true;
				case 'c': set_bool(p.override_all_primary, v, rep); return true;
				case 's': set_bool(p.advanced_options, v, rep); return true;
				default: return false;
				}
			}
		}

		// ─────────────────────────────────────────────────────────────
		// 파싱 (§2.6)
		// ─────────────────────────────────────────────────────────────

		// 성공하면 out 에 **완성된** 프로필을 대입한다. 실패하면 out 을 한 글자도 건드리지
		// 않는다 — 반쯤 채워진 파라미터로 렌더러가 그리는 일이 없어야 하기 때문이다.
		//
		// 항상 "모든 값 = 기본값" 에서 시작해 코드가 명시한 것만 덮어쓴다. 같은 키가 두 번
		// 나오면 나중 것이 이긴다(토큰 스트림이므로 자연스러운 결과다).
		inline parse_report parse_code(const std::string &raw, profile &out)
		{
			parse_report rep;

			// 1) trim. 게임은 공백이 붙으면 거부하지만 우리는 관대하게 받는다(§2.6).
			std::size_t b = 0, e = raw.size();
			while (b < e && detail::is_space(raw[b]))
				++b;
			while (e > b && detail::is_space(raw[e - 1]))
				--e;

			if (b == e)
			{
				rep.error = parse_error::empty;
				return rep;
			}
			if (e - b > kMaxCodeLen)
			{
				rep.error = parse_error::too_long;
				return rep;
			}

			const std::string code = raw.substr(b, e - b);

			// 2) 문자 집합 밖의 글자가 하나라도 있으면 코드 전체 거부.
			for (char c : code)
				if (!detail::is_code_char(c))
				{
					rep.error = parse_error::illegal_char;
					return rep;
				}

			// 3) ';' 로 split.
			std::vector<std::string> tok;
			{
				std::string cur;
				for (char c : code)
				{
					if (c == ';')
					{
						tok.push_back(cur);
						cur.clear();
					}
					else
						cur += c;
				}
				tok.push_back(cur);
			}

			// 맨 앞 '0' 은 고정 접두 토큰이다. 프로필 인덱스가 아니고(§0.3 #7) 값으로도 안 쓴다.
			if (tok[0] != "0")
			{
				rep.error = parse_error::bad_prefix;
				return rep;
			}
			// 빈 토큰(";;" · 끝의 ';')은 키나 값 자리가 비었다는 뜻이다. 스펙의 명시적 거부
			// 사유는 아니지만 "부분 적용 없음" 원칙상 통째로 거부하는 쪽이 안전하다.
			// 정상 코드는 항상 숫자로 끝나므로 유통 코드가 여기 걸릴 일은 없다.
			for (const std::string &t : tok)
				if (t.empty())
				{
					rep.error = parse_error::empty_token;
					return rep;
				}

			profile p;
			section sec = section::root;
			std::string marker; // "" = 루트

			std::size_t i = 1;
			while (i < tok.size())
			{
				// 1글자 대문자 = 섹션 마커. **미지 마커 안전장치가 핵심이다**(§2.6) —
				// focusMode 용 마커나 미래 패치의 새 섹션이 나와도 그 뒤 전체가 밀리지 않는다.
				// HEX 값(대문자)은 키+값을 원자적으로 소비하므로 키 자리에서 검사되지 않는다.
				if (detail::is_marker_token(tok[i]))
				{
					marker = tok[i];
					sec = (marker == "P") ? section::primary
					    : (marker == "A") ? section::ads
					    : (marker == "S") ? section::sniper
					                      : section::unknown;
					++i;
					continue;
				}

				if (i + 1 >= tok.size())
				{
					rep.error = parse_error::dangling_key;
					return rep;
				}

				const std::string key = tok[i], val = tok[i + 1];
				i += 2;

				bool handled = false;
				switch (sec)
				{
				case section::root: handled = detail::apply_root(p, key, val, rep); break;
				case section::primary: handled = detail::apply_layer(p.primary, key, val, rep); break;
				case section::ads: handled = detail::apply_layer(p.ads, key, val, rep); break;
				case section::sniper: handled = detail::apply_sniper(p.snipe, key, val, rep); break;
				case section::unknown: handled = false; break;
				}

				if (!handled)
				{
					// §2.6 미지 키 → 값 소비 후 계속. 경고만 올리고 원문 그대로 보관한다.
					++rep.unknown_items;
					detail::unknown_bucket(p.unknowns, marker).push_back(unknown_entry { key, val });
				}
			}

			out = p;
			return rep;
		}

		// ─────────────────────────────────────────────────────────────
		// 인코딩 (§2.7)
		// ─────────────────────────────────────────────────────────────

		namespace detail
		{
			inline void emit(std::string &s, const std::string &k, const std::string &v)
			{
				s += ';';
				s += k;
				s += ';';
				s += v;
			}
			inline void emit_line_kv(std::string &s, char pfx, const char *k, const std::string &v)
			{
				std::string key;
				key += pfx;
				key += k;
				emit(s, key, v);
			}

			// 같은 마커의 버킷이 여러 개인 손수 만든 store 도 canonical() 의 병합과 같은
			// 결과가 나오도록 전부 이어 붙인다(파서가 만든 store 에는 중복이 없다).
			inline void emit_unknowns(std::string &s, const unknown_store &u, const std::string &marker)
			{
				for (const unknown_section &sec : u)
				{
					if (sec.marker != marker)
						continue;
					for (const unknown_entry &it : sec.items)
						if (valid_unknown_key(it.key) && valid_unknown_token(it.value))
							emit(s, it.key, it.value);
				}
			}

			// §2.7 라인 순서: b | t l v g o a m f s e
			inline void emit_line(std::string &s, const line &L, const line &D, char pfx)
			{
				if (L.show_lines != D.show_lines)
					emit_line_kv(s, pfx, "b", bool_string(L.show_lines));
				// §2.4 붕괴: bShowLines == false 면 <p>b;0 하나만. 나머지 라인 키는 일절 안 낸다.
				// 두 코퍼스 통틀어 위반 0건이다.
				if (!L.show_lines)
					return;

				if (int_differs(L.thickness, D.thickness))
					emit_line_kv(s, pfx, "t", int_string(L.thickness));
				if (int_differs(L.length, D.length))
					emit_line_kv(s, pfx, "l", int_string(L.length));
				// §2.4 ⚠️ v 는 **g 와 무관하게** 비-기본값이면 출력한다. genesy·ruwiss 가
				// g 켜졌을 때만 내보내서 값을 잃는 바로 그 자리다.
				if (int_differs(L.length_vertical, D.length_vertical))
					emit_line_kv(s, pfx, "v", int_string(L.length_vertical));
				if (L.allow_vert_scaling != D.allow_vert_scaling)
					emit_line_kv(s, pfx, "g", bool_string(L.allow_vert_scaling));
				if (int_differs(L.offset, D.offset))
					emit_line_kv(s, pfx, "o", int_string(L.offset));
				if (float_differs(L.opacity, D.opacity))
					emit_line_kv(s, pfx, "a", float_string(L.opacity));
				if (L.show_movement_error != D.show_movement_error)
					emit_line_kv(s, pfx, "m", bool_string(L.show_movement_error));
				if (L.show_shooting_error != D.show_shooting_error)
					emit_line_kv(s, pfx, "f", bool_string(L.show_shooting_error));
				if (float_differs(L.movement_error_scale, D.movement_error_scale))
					emit_line_kv(s, pfx, "s", float_string(L.movement_error_scale));
				if (float_differs(L.firing_error_scale, D.firing_error_scale))
					emit_line_kv(s, pfx, "e", float_string(L.firing_error_scale));
			}

			// §2.7 스칼라 순서: c u h t o d b z a f s m
			// (확증된 것은 c<t, t<o, t<d, t<f, u<b, h<b, d<b, b<z, f<s, f<m 뿐이고
			//  s vs m, t vs u/h 는 증거 0건이라 위상정렬이 임의로 배치한 것이다.
			//  순서가 틀려도 파싱에는 영향이 없다 — 게임 export 와 바이트 동일 재현에만 관계된다.)
			inline std::string layer_body(const layer &L, const layer &D, const unknown_store &u, const std::string &marker)
			{
				std::string s;

				if (int_differs(L.color_index, D.color_index))
					emit(s, "c", int_string(L.color_index));
				// §2.7 규칙 3: 커스텀 색을 쓰는 중이면 u 를 기본값이어도 낸다.
				// 다만 c 를 8 로, b 를 1 로 **덮어쓰지는 않는다** — 게임이 내는 코드에서는
				// c;8 ⇔ b;1 이지만(코퍼스 반례 0건), 외부 툴이 만든 비일관 프로필의 값을
				// export 가 조용히 바꿔 버리면 안 된다.
				if (uses_custom_color(L) || L.custom_color != D.custom_color)
					emit(s, "u", hex_string(L.custom_color));
				if (L.has_outline != D.has_outline)
					emit(s, "h", bool_string(L.has_outline));
				if (L.has_outline) // §2.4 붕괴: h;0 이면 t/o 를 안 낸다
				{
					if (int_differs(L.outline_thickness, D.outline_thickness))
						emit(s, "t", int_string(L.outline_thickness));
					if (float_differs(L.outline_opacity, D.outline_opacity))
						emit(s, "o", float_string(L.outline_opacity));
				}
				if (L.show_center_dot != D.show_center_dot)
					emit(s, "d", bool_string(L.show_center_dot));
				if (L.use_custom_color != D.use_custom_color)
					emit(s, "b", bool_string(L.use_custom_color));
				if (L.show_center_dot) // §2.4 붕괴: 중앙점이 꺼져 있으면 z/a 를 안 낸다
				{
					if (int_differs(L.center_dot_size, D.center_dot_size))
						emit(s, "z", int_string(L.center_dot_size));
					if (float_differs(L.center_dot_opacity, D.center_dot_opacity))
						emit(s, "a", float_string(L.center_dot_opacity));
				}
				if (L.fade_with_firing_error != D.fade_with_firing_error)
					emit(s, "f", bool_string(L.fade_with_firing_error));
				if (L.show_spectated != D.show_spectated)
					emit(s, "s", bool_string(L.show_spectated));
				if (L.fix_min_error != D.fix_min_error)
					emit(s, "m", bool_string(L.fix_min_error));

				emit_line(s, L.inner, D.inner, '0');
				emit_line(s, L.outer, D.outer, '1');
				emit_unknowns(s, u, marker);
				return s;
			}

			// §2.7 S 섹션 순서: d b c t s o
			// (b<c<t<s<o 는 확증. d 는 위치 추정이다.)
			// **붕괴 규칙은 적용하지 않는다** — §2.4 의 d 붕괴는 P/A 의 z/a 에 대한 것이고
			// S 섹션에서 d;0 이 나머지를 지운다는 증거는 없다. 값을 잃지 않는 쪽을 택한다.
			inline std::string sniper_body(const sniper &S, const sniper &D, const unknown_store &u)
			{
				std::string s;
				if (S.show_center_dot != D.show_center_dot)
					emit(s, "d", bool_string(S.show_center_dot));
				if (S.use_custom_color != D.use_custom_color)
					emit(s, "b", bool_string(S.use_custom_color));
				if (int_differs(S.color_index, D.color_index))
					emit(s, "c", int_string(S.color_index));
				if (uses_custom_color(S) || S.custom_color != D.custom_color)
					emit(s, "t", hex_string(S.custom_color));
				if (float_differs(S.center_dot_size, D.center_dot_size))
					emit(s, "s", float_string(S.center_dot_size));
				if (float_differs(S.center_dot_opacity, D.center_dot_opacity))
					emit(s, "o", float_string(S.center_dot_opacity));
				emit_unknowns(s, u, "S");
				return s;
			}
		}

		// 프로필 → 코드. §2.4 기본값 생략 규칙과 §2.7 정규 출력 순서를 따른다.
		// 아무것도 낼 게 없으면 결과는 "0" 한 글자다(= 발로란트 신규 계정 기본 조준점).
		//
		// 출력은 언제나 parse_code 가 받아들이는 문자열이다(음수·로케일 소수점·빈 토큰이
		// 나올 수 없게 막아 두었다).
		inline std::string generate_code(const profile &p)
		{
			const profile D; // 기본값 기준

			std::string out = "0";

			// 루트 p → c → s (확증: p<c 2건, p<s 20건, c<s 7건, 역방향 0건)
			if (p.ads_copies_primary != D.ads_copies_primary)
				detail::emit(out, "p", detail::bool_string(p.ads_copies_primary));
			if (p.override_all_primary != D.override_all_primary)
				detail::emit(out, "c", detail::bool_string(p.override_all_primary));
			if (p.advanced_options != D.advanced_options)
				detail::emit(out, "s", detail::bool_string(p.advanced_options));
			detail::emit_unknowns(out, p.unknowns, "");

			const std::string pbody = detail::layer_body(p.primary, D.primary, p.unknowns, "P");
			if (!pbody.empty())
			{
				out += ";P";
				out += pbody;
			}

			// §2.2 주의: 코퍼스에서 'A 섹션 존재 ⇒ 루트 p;0' 은 27/27 성립하지만, 우리는
			// 그 역함수로 p 를 덮어쓰지 않는다. ADS 값을 잃지 않는 것이 우선이다(§2.7 마지막).
			const std::string abody = detail::layer_body(p.ads, D.ads, p.unknowns, "A");
			if (!abody.empty())
			{
				out += ";A";
				out += abody;
			}

			const std::string sbody = detail::sniper_body(p.snipe, D.snipe, p.unknowns);
			if (!sbody.empty())
			{
				out += ";S";
				out += sbody;
			}

			// 미지 섹션은 알려진 섹션 뒤에, 처음 등장한 순서대로 원문 그대로 재출력한다.
			std::vector<std::string> emitted;
			for (const unknown_section &us : p.unknowns)
			{
				if (!detail::valid_unknown_marker(us.marker))
					continue;
				bool done = false;
				for (const std::string &m : emitted)
					if (m == us.marker)
						done = true;
				if (done)
					continue;
				emitted.push_back(us.marker);

				std::string body;
				detail::emit_unknowns(body, p.unknowns, us.marker);
				if (body.empty())
					continue;
				out += ';';
				out += us.marker;
				out += body;
			}

			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 정규형 — 왕복 동치의 기준
		// ─────────────────────────────────────────────────────────────

		// parse_code(generate_code(p)) 는 p 와 항상 같지는 **않다.** 코드가 담지 못하는
		// 정보가 세 가지 있기 때문이다:
		//   1) §2.4 붕괴 — b;0 / h;0 / 중앙점 OFF 면 그 아래 값들이 코드에 안 실린다.
		//   2) §2.5 출력 관례 — float 는 소수점 3자리까지만 실린다.
		//   3) §2.6 안전 상한 — 200px / 1000 을 넘는 값은 잘린다.
		// canonical() 은 그 세 가지를 미리 적용한 형태다. 그래서 다음이 **모든 p 에 대해**
		// 성립한다:  parse_code(generate_code(p)) == canonical(p)
		//
		// ⚠️ 라이브 상태에는 적용하지 마라. 유저가 선을 잠깐 껐다 켰다고 길이·두께가
		//    기본값으로 리셋되면 안 된다. 이건 순수한 "코드가 표현할 수 있는 형태" 계산이다.
		namespace detail
		{
			inline void canon_line(line &L, const line &D)
			{
				L.thickness = clamp_int(L.thickness);
				L.length = clamp_int(L.length);
				L.length_vertical = clamp_int(L.length_vertical);
				L.offset = clamp_int(L.offset);
				L.opacity = round_milli(L.opacity);
				L.movement_error_scale = round_milli(L.movement_error_scale);
				L.firing_error_scale = round_milli(L.firing_error_scale);
				if (!L.show_lines)
				{
					L = D;
					L.show_lines = false;
				}
			}
			inline void canon_layer(layer &L, const layer &D)
			{
				L.color_index = clamp_int(L.color_index);
				L.outline_thickness = clamp_int(L.outline_thickness);
				L.center_dot_size = clamp_int(L.center_dot_size);
				L.outline_opacity = round_milli(L.outline_opacity);
				L.center_dot_opacity = round_milli(L.center_dot_opacity);
				if (!L.has_outline)
				{
					L.outline_thickness = D.outline_thickness;
					L.outline_opacity = D.outline_opacity;
				}
				if (!L.show_center_dot)
				{
					L.center_dot_size = D.center_dot_size;
					L.center_dot_opacity = D.center_dot_opacity;
				}
				canon_line(L.inner, D.inner);
				canon_line(L.outer, D.outer);
			}
			inline void canon_sniper(sniper &S)
			{
				S.color_index = clamp_int(S.color_index);
				S.center_dot_size = round_milli(S.center_dot_size);
				S.center_dot_opacity = round_milli(S.center_dot_opacity);
			}
			// 무효한 항목을 버리고 중복 버킷을 합친 뒤 **출력 순서**("" → P → A → S → 미지)
			// 로 재배열한다. generate 가 그 순서로 내보내므로 이렇게 해야 왕복이 닫힌다.
			inline void canon_unknowns(unknown_store &u)
			{
				unknown_store merged;
				for (const unknown_section &s : u)
				{
					const bool known = s.marker.empty() || s.marker == "P" || s.marker == "A" || s.marker == "S";
					if (!known && !valid_unknown_marker(s.marker))
						continue;
					std::vector<unknown_entry> keep;
					for (const unknown_entry &it : s.items)
						if (valid_unknown_key(it.key) && valid_unknown_token(it.value))
							keep.push_back(it);
					if (keep.empty())
						continue;
					std::vector<unknown_entry> &dst = unknown_bucket(merged, s.marker);
					dst.insert(dst.end(), keep.begin(), keep.end());
				}

				unknown_store ordered;
				const char *known_order[4] = { "", "P", "A", "S" };
				for (const char *m : known_order)
					for (const unknown_section &s : merged)
						if (s.marker == m)
							ordered.push_back(s);
				for (const unknown_section &s : merged)
				{
					bool is_known = false;
					for (const char *m : known_order)
						if (s.marker == m)
							is_known = true;
					if (!is_known)
						ordered.push_back(s);
				}
				u.swap(ordered);
			}
		}

		inline profile canonical(profile p)
		{
			const profile D;
			detail::canon_layer(p.primary, D.primary);
			detail::canon_layer(p.ads, D.ads);
			detail::canon_sniper(p.snipe);
			detail::canon_unknowns(p.unknowns);
			return p;
		}

		// ─────────────────────────────────────────────────────────────
		// 기하 (§3) — "이 파라미터면 어떤 사각형이 어디에 있는가"
		//
		// 여기서 좌표를 **전부** 확정한다. ImGui 레이어는 아래 목록을 순서대로
		// AddRectFilled 로 옮기기만 한다. 그리기 쪽에 산술이 남아 있으면 경계가 잘못
		// 그어진 것이다 — 맥에서 테스트할 수 없는 코드에 판정이 숨는다.
		//
		// 모든 좌표는 **정수 픽셀**이다(§3.1). 서브픽셀 좌표를 쓰면 텍셀 블렌딩이 생겨
		// 발로란트의 하드 에지가 사라진다. 절대 픽셀이므로 DPI 스케일을 곱하면 안 된다(§0.4).
		// ─────────────────────────────────────────────────────────────

		// 좌상단 + 크기. 반열린 구간 [x, x+w) × [y, y+h) 다.
		struct rect
		{
			int x = 0, y = 0, w = 0, h = 0;
		};
		inline bool operator==(const rect &a, const rect &b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }
		inline bool operator!=(const rect &a, const rect &b) { return !(a == b); }

		// 그릴 사각형 하나. 색은 이미 해석돼 있다(프리셋/커스텀/검정 윤곽선).
		struct quad
		{
			rect r;
			rgb color;
			std::uint8_t alpha = 255;
		};
		inline bool operator==(const quad &a, const quad &b) { return a.r == b.r && a.color == b.color && a.alpha == b.alpha; }
		inline bool operator!=(const quad &a, const quad &b) { return !(a == b); }

		using quad_list = std::vector<quad>;

		// §3.2 · §3.3 — 팔 4개의 기하. 그리기 순서(우 → 좌 → 하 → 상)로 채운다.
		//
		//   1. 오프셋은 "중심에서 선의 **안쪽 끝**까지의 거리"다. 선 중앙까지가 아니고 배율도 없다.
		//   2. 길이는 항상 중심 **바깥** 방향으로 자란다. 우 팔은 cx+off → cx+off+lenH.
		//   3. 두께는 축에 수직으로 중앙 정렬되되 floor() 로 스냅된다.
		//   4. 바깥선도 안쪽선 끝 기준이 아니라 **똑같이 중심에서 재는 절대 오프셋**이다.
		//      inner/outer 는 이 함수를 다른 설정으로 두 번 부를 뿐이다.
		//
		// w<=0 이나 h<=0 인 팔도 그대로 채운다 — 그리지 않는 판정(§3.2 #5)은 push 단계에서 한다.
		inline void line_arms(const line &L, int cx, int cy, int off, rect out[4])
		{
			const int t = clamp_int(L.thickness);
			const int len_h = clamp_int(L.length);
			const int len_v = clamp_int(effective_vertical_length(L));
			const int o = clamp_int(off);

			// §3.3 홀수 두께 −0.5px 시프트. 짝수면 par==0 이라 두 분기가 완전히 같다.
			const int par = t & 1;
			const int snap  = kOddThicknessShiftsTopLeft ? (t + par) / 2 : (t - par) / 2;
			const int lead  = kOddThicknessShiftsTopLeft ? par : 0; // 좌·상 팔이 1px 더 자란다
			const int trail = kOddThicknessShiftsTopLeft ? 0 : par; // 우·하 팔의 시작이 1px 밀린다

			const int cross_y = cy - snap; // 좌/우 팔의 y = floor(cy - t/2)
			const int cross_x = cx - snap; // 상/하 팔의 x = floor(cx - t/2)

			out[0] = rect { cx + o + trail,           cross_y,                  len_h, t };     // 우
			out[1] = rect { cx - o - len_h - lead,    cross_y,                  len_h, t };     // 좌
			out[2] = rect { cross_x,                  cy + o + trail,           t, len_v };     // 하
			out[3] = rect { cross_x,                  cy - o - len_v - lead,    t, len_v };     // 상
		}

		// §3.5 중앙 점 — **원이 아니라 정사각형**이고, 값은 반지름이 아니라 **한 변의 길이**다.
		// 기존 Sherbet 의 AddCircleFilled 는 여기서 틀린다. 오차가 커져도 움직이지 않는다.
		// 홀짝 스냅은 팔과 동일하다(floor(c - n/2)).
		// (스나이퍼 중앙 점만 원이지만 Sherbet 은 스나이퍼를 그리지 않는다 — §3.5 마지막 · §5)
		inline rect center_dot_rect(const layer &L, int cx, int cy)
		{
			const int n = clamp_int(L.center_dot_size);
			const int par = n & 1;
			const int snap = kOddThicknessShiftsTopLeft ? (n + par) / 2 : (n - par) / 2;
			return rect { cx - snap, cy - snap, n, n };
		}

		namespace detail
		{
			// §3.6 #5 · §3.2 #5 — 길이 0 이면 그 팔을 아예 그리지 않는다(윤곽선도 없다).
			// 알파 0 도 그릴 게 없다 — 목록에서 빼면 그리기 쪽이 그만큼 가벼워진다.
			inline void push_body(quad_list &out, const rect &r, rgb c, std::uint8_t a)
			{
				if (r.w <= 0 || r.h <= 0 || a == 0)
					return;
				out.push_back(quad { r, c, a });
			}

			// §3.6 윤곽선 — 캔버스 원문 strokeRect(x−T/2, y−T/2, w+T, h+T) + lineWidth=T 는
			// 정확히 **본체를 사방 T픽셀로 감싸는 링**이다. ImGui 에는 stroke 가 없으므로 링을
			// 4조각으로 분해하되 **조각끼리 겹치지 않게 잘라** 모서리가 진해지지 않게 한다.
			//
			//   * 본체 아래는 채우지 않는다(링만). 그래야 본체 알파(기본 inner 0.8)가 배경과
			//     직접 블렌딩된다. 확장 사각형을 통째로 검게 채우면 선이 훨씬 탁해진다(§0.3 #4).
			//   * **선의 끝단도 감싼다** — 링이므로 4변 전부에 붙어 길이 방향으로 2T 만큼
			//     길어 보인다. 상·하 조각이 모서리를 포함하는 것이 그 끝단이다.
			//   * 색은 검정. 공유 코드에 윤곽선 색 키가 없다(§0.3 #9).
			inline void push_ring(quad_list &out, const rect &r, int T, std::uint8_t a)
			{
				if (r.w <= 0 || r.h <= 0 || T <= 0 || a == 0)
					return;
				out.push_back(quad { rect { r.x - T, r.y - T, r.w + 2 * T, T }, kOutlineColor, a }); // 상(모서리 포함)
				out.push_back(quad { rect { r.x - T, r.y + r.h, r.w + 2 * T, T }, kOutlineColor, a }); // 하(모서리 포함)
				out.push_back(quad { rect { r.x - T, r.y, T, r.h }, kOutlineColor, a });               // 좌
				out.push_back(quad { rect { r.x + r.w, r.y, T, r.h }, kOutlineColor, a });             // 우
			}

			// 본체 알파와 링 알파는 **곱해지지 않는다**. 각자 독립적으로 합성된다(§3.6 #3).
			inline void push_piece(quad_list &out, const rect &r, rgb c, std::uint8_t body_a, int T, std::uint8_t ring_a)
			{
				push_body(out, r, c, body_a);
				push_ring(out, r, T, ring_a);
			}

			inline void push_line_group(quad_list &out, const line &Ln, const layer &L, int cx, int cy, float err_px,
			                            rgb c, int T, std::uint8_t ring_a)
			{
				if (!Ln.show_lines)
					return;

				rect a[4];
				line_arms(Ln, cx, cy, effective_offset(Ln, L, err_px), a);
				const std::uint8_t body_a = alpha_from_opacity(Ln.opacity);

				if (kOutlineOnePass)
				{
					// §3.6 #6 팔 단위 1-pass — 팔마다 [본체 → 자기 링].
					for (int i = 0; i < 4; ++i)
						push_piece(out, a[i], c, body_a, T, ring_a);
				}
				else
				{
					// 대안(§6 #4): 그룹마다 [링 4개 → 본체 4개]. 본체가 항상 링 위에 온다.
					for (int i = 0; i < 4; ++i)
						push_ring(out, a[i], T, ring_a);
					for (int i = 0; i < 4; ++i)
						push_body(out, a[i], c, body_a);
				}
			}

			inline void push_dot(quad_list &out, const layer &L, int cx, int cy, rgb c, int T, std::uint8_t ring_a)
			{
				if (!L.show_center_dot)
					return;
				// 중앙 점도 윤곽선을 두른다 — §2.9 예제 D "검은 윤곽선이 1px 둘린 초록 2×2 정사각형",
				// 예제 B "검은 윤곽선 두른 흰 3px 사각점".
				push_piece(out, center_dot_rect(L, cx, cy), c, alpha_from_opacity(L.center_dot_opacity), T, ring_a);
			}
		}

		// 레이어 하나를 그리는 데 필요한 사각형을 **그리기 순서 그대로** out 에 채운다.
		// out 은 비워지고 다시 채워진다 — 호출 측이 하나를 계속 재사용해 매 프레임 할당을 피한다.
		//
		// §3.7 그리기 순서:
		//   1) Inner  : 우 → 좌 → 하 → 상   (가로 먼저, 세로 나중)
		//   2) Center dot                    (위치는 §6 #5 의 선택 사항 = kDotOrder)
		//   3) Outer  : 우 → 좌 → 하 → 상
		// 가로 먼저 세로 나중, outer 가 inner 위 — 4개 소스 전부 일치. **확정**이다.
		// 그래서 오프셋이 가까우면 outer 의 검은 윤곽선이 inner 선의 끝을 덮는다.
		// 실제 게임에서도 나는 아티팩트이므로 고치지 않는다.
		//
		// inner_err_px / outer_err_px 는 §4 의 동적 오차(태스크 4). 정적 조준점에서는 0 이다.
		// 오차는 **오프셋만** 늘린다 — 길이·두께·투명도는 절대 변하지 않는다(§4.1 #2).
		inline void build_crosshair(const layer &L, int cx, int cy, float inner_err_px, float outer_err_px, quad_list &out)
		{
			out.clear();

			const rgb c = resolve_color(L);
			// 윤곽선이 꺼져 있으면 두께·알파를 0 으로 만들어 링이 아예 안 나오게 한다(§2.4 붕괴와 동일한 뜻).
			const int T = L.has_outline ? clamp_int(L.outline_thickness) : 0;
			const std::uint8_t ring_a = L.has_outline ? alpha_from_opacity(L.outline_opacity) : 0;

			if (kDotOrder == dot_order::below_all)
				detail::push_dot(out, L, cx, cy, c, T, ring_a);

			detail::push_line_group(out, L.inner, L, cx, cy, inner_err_px, c, T, ring_a);

			if (kDotOrder == dot_order::above_inner)
				detail::push_dot(out, L, cx, cy, c, T, ring_a);

			detail::push_line_group(out, L.outer, L, cx, cy, outer_err_px, c, T, ring_a);
		}
	}
}
