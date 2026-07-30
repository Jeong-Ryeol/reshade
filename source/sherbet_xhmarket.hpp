/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 조준점 마켓 — 순수 로직(플랫폼 비의존, imgui/WinInet/파일IO 없음).
//
// 무엇인가: 오버레이 안에서 조준점을 **골라 쓰는** 진열대. 항목 하나는 공유 코드 한 줄과
// 표시용 메타데이터(이름·제작자·태그)가 전부다. 두 출처가 같은 카드 모양으로 섞인다.
//   1) 서버 배포분 — 기존 /content/me 페이로드에 "crosshairs" 배열로 실려 오고 역할로 잠긴다.
//      "프로 조준점 팩" 판매가 `/grant @user <key>` 한 줄로 끝난다(파일 배포 없음).
//   2) 로컬 — 사용자가 저장해 둔 자기 조준점("내 조준점"). 설정 문자열 하나로 왕복한다.
//
// **여기 있는 것이 이 기능에서 유일하게 맥에서 검증 가능한 층이다**(tools/sherbet_xhmarket_test.cpp).
// 서버가 보낸 코드는 신뢰할 수 없는 입력이므로 판정은 한 줄도 UI 로 새지 않게 전부 여기 둔다.
//
// ── 설계에서 못 박은 것 ────────────────────────────────────────────────
//  * 서버 코드도 사용자가 붙여넣은 코드와 **완전히 같은 파서**(crosshair::parse_code)를 지난다.
//    파서를 통과하지 못한 항목은 버리지 않고 broken 상태로 진열한다 — 카드가 조용히
//    사라지면 "내가 산 게 왜 없지?" 가 되고, 적용 버튼이 살아 있으면 절반만 적용된다.
//    둘 다 하지 않는다: **보이되 적용 불가**.
//  * 잠긴 항목은 코드를 아예 들고 있지 않는다. 서버가 잠긴 코드를 실어 보내도(구버전)
//    파싱 전에 지운다 — 코드 자체가 상품이라 캐시 파일에 남으면 판매가 무의미해진다.
//  * 되돌리기(§ session): 사용자가 튜닝하던 코드는 마켓에서 **처음** 적용할 때 한 번만
//    스냅샷된다. 카드를 20번 눌러도 undo 는 여전히 "내가 만들던 것" 을 가리킨다.
#pragma once

#include "sherbet_crosshair.hpp"
#include "sherbet_json.hpp"

#include <string>
#include <vector>
#include <cstddef>

namespace sherbet
{
	namespace xhmarket
	{
		// 한 페이로드에서 받아들이는 최대 항목 수. 서버가 실수로 거대한 배열을 내도
		// 파싱(=항목마다 parse_code)이 프레임을 잡아먹지 않게 하는 상한이다.
		constexpr std::size_t kMaxEntries = 200;
		// 로컬 슬롯 상한. 설정 파일 한 줄에 들어가야 하므로 무한히 늘리지 않는다.
		constexpr std::size_t kMaxLocals = 64;
		// 표시 문자열(이름/제작자/태그) 최대 바이트. UTF-8 경계에서 자른다.
		constexpr std::size_t kMaxNameBytes = 64;
		// id 최대 바이트. 서버가 정하는 불투명 키.
		constexpr std::size_t kMaxIdBytes = 64;

		// 카드가 취할 수 있는 상태. 셋뿐이고, 적용 가능한 것은 ready 하나다.
		enum class entry_state
		{
			ready,   // 코드가 파서를 통과했고 권한도 있다 → [적용] 활성
			locked,  // 서버가 잠갔다(역할 없음) → 이름만 진열, 코드는 애초에 안 들고 있다
			broken,  // 권한은 있는데 코드가 유효하지 않다 → '사용 불가' 표시, 적용 금지
		};

		struct entry
		{
			std::string id;      // 카드 키. 서버 항목은 매니페스트의 id, 로컬은 "L<n>"
			std::string name;    // 표시 이름(없으면 id)
			std::string author;  // 선택
			std::string tag;     // 선택(예: "프로", "점")
			std::string code;    // 공유 코드 원문. locked 면 비어 있다
			bool unlocked = true;
			bool local = false;  // 내 조준점(로컬 슬롯)인가
			bool code_ok = false;
			crosshair::parse_error error = crosshair::parse_error::empty;
			crosshair::profile parsed; // code_ok == true 일 때만 의미가 있다

			entry_state state() const
			{
				if (!unlocked) return entry_state::locked;
				return code_ok ? entry_state::ready : entry_state::broken;
			}
			// **적용 버튼의 유일한 판정.** UI 는 이 함수 밖에서 조건을 다시 쓰지 않는다.
			bool applicable() const { return unlocked && code_ok; }
		};

		// ─────────────────────────────────────────────────────────────
		// 표시 문자열 정리
		// ─────────────────────────────────────────────────────────────

		// 제어문자(0x00~0x1F, 0x7F)를 지우고 max_bytes 이하로 자른다.
		// 자를 때 UTF-8 연속 바이트 한가운데를 끊지 않는다(깨진 글자 방지).
		// 제어문자를 지우는 이유: 서버 문자열에 '\n' 이 들어오면 카드 레이아웃이 통째로
		// 밀리고, '\r' 은 설정 파일 한 줄 규약을 깬다. 서버는 신뢰할 수 없는 입력이다.
		inline std::string sanitize_text(const std::string &s, std::size_t max_bytes = kMaxNameBytes)
		{
			std::string out;
			out.reserve(s.size() < max_bytes ? s.size() : max_bytes);
			for (char c : s)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (u < 0x20 || u == 0x7F) continue;
				out += c;
			}
			if (out.size() > max_bytes)
			{
				std::size_t cut = max_bytes;
				// 연속 바이트(10xxxxxx) 위에 서 있으면 시작 바이트까지 물러난다.
				while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
					--cut;
				out.resize(cut);
			}
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 서버 매니페스트 파싱
		// ─────────────────────────────────────────────────────────────

		// /content/me 응답의 "crosshairs" 배열 → 카드 목록.
		//
		// 규칙:
		//  * id 가 없으면 **버린다**(카드를 식별할 키가 없다 = 선택 상태를 기록할 수 없다).
		//  * id 가 중복이면 먼저 나온 것만 남긴다(뒤엣것은 조용히 무시 — 같은 키의 카드가
		//    둘이면 적용 상태 표시가 두 곳에 켜진다).
		//  * unlocked=false 면 코드를 지우고 파싱하지 않는다.
		//  * 그 외에는 crosshair::parse_code 결과를 그대로 기록한다(실패해도 카드는 남는다).
		//  * display_name 이 없으면 id 를 이름으로 쓴다(테마/프리셋 매니페스트와 같은 관례).
		inline std::vector<entry> parse_manifest(const std::string &body, const char *key = "crosshairs")
		{
			std::vector<entry> out;
			for (const std::string &obj : detail::split_top_level_objects(body, key))
			{
				if (out.size() >= kMaxEntries) break;

				entry e;
				std::string raw;
				if (!detail::json_str(obj, "id", raw)) continue;
				e.id = sanitize_text(raw, kMaxIdBytes);
				if (e.id.empty()) continue;

				bool dup = false;
				for (const entry &prev : out)
					if (prev.id == e.id) { dup = true; break; }
				if (dup) continue;

				if (detail::json_str(obj, "display_name", raw))
					e.name = sanitize_text(raw);
				if (e.name.empty())
					e.name = e.id;
				if (detail::json_str(obj, "author", raw))
					e.author = sanitize_text(raw);
				if (detail::json_str(obj, "tag", raw))
					e.tag = sanitize_text(raw);

				e.unlocked = detail::json_bool(obj, "unlocked", true); // 미표기(구버전 서버)면 열림
				if (e.unlocked && detail::json_str(obj, "code", raw))
				{
					e.code = raw; // 코드는 정리하지 않는다 — parse_code 의 문자 검사가 곧 검증이다
					const crosshair::parse_report rep = crosshair::parse_code(e.code, e.parsed);
					e.code_ok = rep.ok();
					e.error = rep.error;
					if (!e.code_ok)
						e.parsed = crosshair::profile(); // 실패분을 절반 적용된 상태로 남기지 않는다
				}
				else if (!e.unlocked)
				{
					e.error = crosshair::parse_error::empty; // 잠김: 코드를 들고 있지 않다
				}
				out.push_back(std::move(e));
			}
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 로컬 슬롯("내 조준점")
		// ─────────────────────────────────────────────────────────────

		struct local_slot
		{
			std::string name, code;
		};
		inline bool operator==(const local_slot &a, const local_slot &b) { return a.name == b.name && a.code == b.code; }
		inline bool operator!=(const local_slot &a, const local_slot &b) { return !(a == b); }

		// 설정 파일 한 줄로 왕복시키기 위한 인코딩.
		//   레코드 구분 '~' / 필드 구분 '|' / 이스케이프 '\'
		//   이름에 들어갈 수 있는 '\' '|' '~' ',' 를 전부 이스케이프한다.
		//
		// ',' 까지 이스케이프하는 이유: ini_file 은 값 안의 콤마를 ",," 로 이스케이프해
		// 왕복시키지만(ini_file.cpp:89), 빈 원소를 건너뛰는 등 가장자리 규칙이 따로 있다.
		// **내 인코딩 결과에 raw 콤마가 아예 없으면** 그 규칙을 밟을 일 자체가 없다.
		// 공유 코드는 문자 집합이 [0-9A-Za-z;.] 라 원래 이스케이프가 필요 없지만,
		// 같은 함수를 통과시켜 두면 나중에 규칙이 바뀌어도 한 곳만 고치면 된다.
		inline std::string escape_field(const std::string &s)
		{
			std::string out;
			out.reserve(s.size() + 4);
			for (char c : s)
			{
				if (c == '\\' || c == '|' || c == '~' || c == ',')
					out += '\\';
				out += c;
			}
			return out;
		}

		inline std::string encode_locals(const std::vector<local_slot> &slots)
		{
			std::string out;
			std::size_t n = 0;
			for (const local_slot &s : slots)
			{
				if (n >= kMaxLocals) break;
				if (s.code.empty()) continue; // 적용할 것이 없는 슬롯은 저장하지 않는다
				if (n != 0) out += '~';
				out += escape_field(sanitize_text(s.name));
				out += '|';
				out += escape_field(s.code);
				++n;
			}
			return out;
		}

		inline std::vector<local_slot> decode_locals(const std::string &text)
		{
			std::vector<local_slot> out;
			local_slot cur;
			bool in_code = false; // 지금 읽는 필드가 code 인가('|' 를 한 번 지났나)
			bool esc = false;
			auto flush = [&]() {
				cur.name = sanitize_text(cur.name);
				// 코드 상한을 넘는 것은 어차피 parse_code 가 too_long 으로 거부한다. 여기서 버린다.
				if (!cur.code.empty() && cur.code.size() <= crosshair::kMaxCodeLen && out.size() < kMaxLocals)
					out.push_back(cur);
				cur = local_slot();
				in_code = false;
			};
			for (char c : text)
			{
				if (esc)
				{
					// 알 수 없는 이스케이프는 그 문자 그대로. (인코더가 만든 것은 넷뿐이다.)
					(in_code ? cur.code : cur.name) += c;
					esc = false;
					continue;
				}
				if (c == '\\') { esc = true; continue; }
				if (c == '~') { flush(); continue; }
				if (c == '|' && !in_code) { in_code = true; continue; }
				(in_code ? cur.code : cur.name) += c;
			}
			// 끝에 남은 '\' 는 버린다(잘린 설정값). 마지막 레코드는 항상 flush.
			flush();
			return out;
		}

		// 로컬 슬롯 하나 → 카드. id 는 목록 안 위치로 만들되 "local:" 접두사를 붙인다 —
		// 서버 매니페스트의 id 와 겹치면 '사용 중' 표시가 엉뚱한 카드에 켜지기 때문이다.
		// (접두사는 우리 매니페스트에서 쓰지 않는 형태로 고른다.)
		inline entry make_local_entry(const local_slot &s, std::size_t index)
		{
			entry e;
			e.local = true;
			e.unlocked = true;
			e.id = "local:" + std::to_string(index);
			e.name = sanitize_text(s.name);
			if (e.name.empty())
				e.name = e.id;
			e.code = s.code;
			const crosshair::parse_report rep = crosshair::parse_code(e.code, e.parsed);
			e.code_ok = rep.ok();
			e.error = rep.error;
			if (!e.code_ok)
				e.parsed = crosshair::profile();
			return e;
		}

		inline std::vector<entry> local_entries(const std::vector<local_slot> &slots)
		{
			std::vector<entry> out;
			for (std::size_t i = 0; i < slots.size() && i < kMaxLocals; ++i)
				out.push_back(make_local_entry(slots[i], i));
			return out;
		}

		// 지금 코드를 새 슬롯으로 추가. 코드가 유효하지 않거나 자리가 없으면 false(무변경).
		// 이름 중복은 허용한다 — 사용자가 "새 조준점" 을 여러 개 만드는 것은 정상이고,
		// 카드 키는 이름이 아니라 위치라서 충돌하지 않는다.
		inline bool add_local(std::vector<local_slot> &slots, const std::string &name, const std::string &code)
		{
			if (slots.size() >= kMaxLocals) return false;
			crosshair::profile tmp;
			if (!crosshair::parse_code(code, tmp).ok()) return false;
			slots.push_back(local_slot { sanitize_text(name), code });
			return true;
		}

		inline bool remove_local(std::vector<local_slot> &slots, std::size_t index)
		{
			if (index >= slots.size()) return false;
			slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(index));
			return true;
		}

		// ─────────────────────────────────────────────────────────────
		// 선택 · 되돌리기 상태
		//
		// 이 기능에서 가장 나쁜 결과는 **카드를 눌렀더니 내가 튜닝하던 조준점이 사라지는 것**이다.
		// 그래서 "지금 화면의 코드가 마켓에서 온 것인가" 를 항상 알고 있는다:
		//   applied_id 가 비어 있으면 = 사용자가 만든/편집한 코드 → 다음 적용 직전에 스냅샷한다.
		//   applied_id 가 차 있으면 = 마켓 항목이 적용된 상태 → 스냅샷하지 않는다(덮어쓰기 금지).
		// 결과: 카드를 몇 번을 눌러도 되돌리기는 언제나 "내 것" 으로 돌아간다.
		// ─────────────────────────────────────────────────────────────

		struct session
		{
			std::string applied_id; // "" = 사용자가 직접 만든 코드가 적용 중
			std::string undo_code;  // 마켓 적용 직전의 사용자 코드. "" 면 되돌릴 것이 없다
		};

		inline bool can_revert(const session &s) { return !s.undo_code.empty(); }

		// 항목 적용. 적용 가능하면 out_code 에 코드를 넣고 true.
		// **적용 불가면 s 도 out_code 도 한 글자 건드리지 않는다**(잠김/코드 불량).
		inline bool apply(session &s, const entry &e, const std::string &current_code, std::string &out_code)
		{
			if (!e.applicable()) return false;
			if (s.applied_id.empty() && !current_code.empty())
				s.undo_code = current_code; // 사용자가 만지던 코드 — 여기서만 스냅샷한다
			s.applied_id = e.id;
			out_code = e.code;
			return true;
		}

		// 슬라이더/코드 입력 등 사용자가 직접 프로필을 바꿨다 → 더 이상 마켓 항목이 아니다.
		inline void note_user_edit(session &s) { s.applied_id.clear(); }

		// 되돌리기. 스냅샷이 있으면 out_code 에 넣고 true(그리고 스냅샷을 소비한다).
		inline bool revert(session &s, std::string &out_code)
		{
			if (!can_revert(s)) return false;
			out_code = s.undo_code;
			s.undo_code.clear();
			s.applied_id.clear(); // 되돌린 코드는 다시 '내 것' 이다
			return true;
		}

		// ─────────────────────────────────────────────────────────────
		// 미리보기 보조
		// ─────────────────────────────────────────────────────────────

		// quad 목록의 바운딩 박스. 빈 목록이면 {0,0,0,0}.
		// 카드 안에 실제 크기로 그릴 때 "이 조준점이 카드보다 큰가" 를 UI 가 판단하는 근거.
		inline crosshair::rect preview_bounds(const crosshair::quad_list &quads)
		{
			bool any = false;
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			for (const crosshair::quad &q : quads)
			{
				if (q.r.w <= 0 || q.r.h <= 0) continue;
				const int qx1 = q.r.x + q.r.w, qy1 = q.r.y + q.r.h;
				if (!any) { x0 = q.r.x; y0 = q.r.y; x1 = qx1; y1 = qy1; any = true; continue; }
				if (q.r.x < x0) x0 = q.r.x;
				if (q.r.y < y0) y0 = q.r.y;
				if (qx1 > x1) x1 = qx1;
				if (qy1 > y1) y1 = qy1;
			}
			if (!any) return crosshair::rect {};
			return crosshair::rect { x0, y0, x1 - x0, y1 - y0 };
		}
	}
}
