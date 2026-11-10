/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// Sherbet 사이드공지 — 순수 로직(플랫폼 비의존). tools/sherbet_sidenotice_test.cpp 가 그대로 컴파일해 검증한다.
//
// 무엇인가: RP 서버에서 경찰청·보안국이 채팅으로 올리는 '사이드공지' 문장을 단축키 창에서 장소 몇 글자로
// 찾아 바로 올리는 기능. 문장은 서버 /content/me 의 "sidenotices" 배열로 오고 **역할 있는 것만** 온다
// (조준점 코드처럼 문장 자체가 상품이라 잠긴 문장은 아예 안 내려온다).
//
// ── 검색에서 못 박은 것 ────────────────────────────────────────────────
//  * 게임 중엔 한글 키밀림 방지 때문에 입력기가 **영문**이다. 단축키 창에서 '빈1' 을 치려고 손이 움직이면
//    'qls1' 이 들어온다. 그래서 입력칸은 두벌식 자판대로 직접 한글로 조합한다(compose) — 입력기 상태와
//    상관없이 늘 한글로 보이고 찾아진다. 한글 상태로 쳐서 완성 글자가 들어와도 결과는 같다.
//  * 글자 단위 매칭은 '앞부분 일치'다: 'ㅅ'·'시'·'신' 은 전부 '신' 에 맞는다(초성 검색과 치는 도중 둘 다).
//  * 조합 경계가 어긋나는 순간('빈ㅈ' 대신 '빉' 이 되는 찰나)은 자판 문자열 부분일치로 받아 준다.
#pragma once

#include "sherbet_json.hpp"

#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace sherbet
{
	namespace sidenotice
	{
		constexpr std::size_t kMaxEntries = 300;
		constexpr std::size_t kMaxIdBytes = 64;
		constexpr std::size_t kMaxNameBytes = 96;   // 그룹·장소 이름
		constexpr std::size_t kMaxTextBytes = 1024; // 채팅 한 줄(파엠 채팅칸 상한보다 넉넉히)

		struct entry
		{
			std::string id;
			std::string group; // "경찰청" / "보안국"
			std::string zone;  // "빈집1" — 화면에 보이는 이름
			std::string text;  // 채팅에 그대로 칠 한 줄("/경찰청 ...")
			std::vector<std::string> aliases; // 사람들이 부르는 줄임말("북편1", "보습") — 검색에만 쓴다
		};

		// 제어문자(줄바꿈 포함)를 지우고 UTF-8 경계에서 자른다. 채팅은 한 줄이라 '\n' 이 들어가면
		// 파엠 채팅칸이 Enter 로 받아 문장이 반쯤 나간다 — 서버 문자열은 신뢰할 수 없는 입력이다.
		inline std::string clean(const std::string &s, std::size_t max_bytes)
		{
			std::string out;
			for (char c : s)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (u < 0x20 || u == 0x7F) continue;
				out += c;
			}
			if (out.size() > max_bytes)
			{
				std::size_t cut = max_bytes;
				while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
					--cut;
				out.resize(cut);
			}
			return out;
		}

		// /content/me 의 "sidenotices" 배열 → 목록. id·zone·text 가 없으면 버리고, id 중복은 앞엣것만.
		inline std::vector<entry> parse_manifest(const std::string &body, const char *key = "sidenotices")
		{
			std::vector<entry> out;
			for (const std::string &obj : detail::split_top_level_objects(body, key))
			{
				if (out.size() >= kMaxEntries) break;
				entry e;
				std::string raw;
				if (detail::json_str(obj, "id", raw)) e.id = clean(raw, kMaxIdBytes);
				if (detail::json_str(obj, "group", raw)) e.group = clean(raw, kMaxNameBytes);
				if (detail::json_str(obj, "zone", raw)) e.zone = clean(raw, kMaxNameBytes);
				if (detail::json_str(obj, "text", raw)) e.text = clean(raw, kMaxTextBytes);
				// 별칭은 '|' 로 이어 붙인 문자열 하나(평면 파서라 배열을 못 읽는다). 예: "북편1|ㅂㅍ1"
				if (detail::json_str(obj, "alias", raw))
				{
					std::string cur;
					for (std::size_t i = 0; i <= raw.size(); ++i)
					{
						if (i == raw.size() || raw[i] == '|')
						{
							cur = clean(cur, kMaxNameBytes);
							if (!cur.empty() && e.aliases.size() < 8) e.aliases.push_back(cur);
							cur.clear();
						}
						else cur += raw[i];
					}
				}
				if (e.id.empty() || e.zone.empty() || e.text.empty()) continue;
				bool dup = false;
				for (const entry &p : out)
					if (p.id == e.id) { dup = true; break; }
				if (!dup) out.push_back(std::move(e));
			}
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// UTF-8 ↔ 코드포인트
		// ─────────────────────────────────────────────────────────────
		inline std::u32string to_u32(const std::string &s)
		{
			std::u32string out;
			for (std::size_t i = 0; i < s.size();)
			{
				const unsigned char c = static_cast<unsigned char>(s[i]);
				char32_t cp = 0;
				std::size_t n = 0;
				if (c < 0x80) { cp = c; n = 1; }
				else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
				else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
				else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
				else { ++i; continue; } // 깨진 바이트는 건너뛴다
				if (i + n > s.size()) break;
				for (std::size_t k = 1; k < n; ++k)
					cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
				out += cp;
				i += n;
			}
			return out;
		}

		inline std::string to_utf8(const std::u32string &s)
		{
			std::string out;
			for (char32_t cp : s)
			{
				if (cp < 0x80) out += static_cast<char>(cp);
				else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
				else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
			}
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 한글 자모 · 두벌식
		// ─────────────────────────────────────────────────────────────
		constexpr char32_t kSyllableFirst = 0xAC00, kSyllableLast = 0xD7A3;
		constexpr char32_t kJamoFirst = 0x3131, kJamoLast = 0x3163; // 호환 자모 ㄱ..ㅣ
		constexpr char32_t kVowelFirst = 0x314F;                     // ㅏ

		inline bool is_syllable(char32_t c) { return c >= kSyllableFirst && c <= kSyllableLast; }
		inline bool is_jamo(char32_t c) { return c >= kJamoFirst && c <= kJamoLast; }
		inline bool is_vowel(char32_t c) { return c >= kVowelFirst && c <= kJamoLast; }
		inline bool is_hangul(char32_t c) { return is_syllable(c) || is_jamo(c); }

		// 호환 자모 0x3131..0x3163 각각의 두벌식 키(겹자음·겹모음은 두 키, 쌍자음·ㅒㅖ 는 Shift = 대문자).
		inline const char *jamo_keys(char32_t c)
		{
			static const char *const k[51] = {
				"r", "R", "rt", "s", "sw", "sg", "e", "E", "f", "fr", "fa", "fq", "ft", "fx", "fv", "fg", // ㄱ..ㅀ
				"a", "q", "Q", "qt", "t", "T", "d", "w", "W", "c", "z", "x", "v", "g",                     // ㅁ..ㅎ
				"k", "o", "i", "O", "j", "p", "u", "P", "h", "hk", "ho", "hl", "y", "n", "nj", "np", "nl", "b", "m", "ml", "l", // ㅏ..ㅣ
			};
			return is_jamo(c) ? k[c - kJamoFirst] : "";
		}

		// 영문 키 하나 → 호환 자모(없으면 0). 대문자는 쌍자음·ㅒ·ㅖ 만 따로 있고 나머지는 소문자와 같다.
		inline char32_t key_to_jamo(char32_t key)
		{
			if (key >= 'A' && key <= 'Z')
			{
				for (char32_t c = kJamoFirst; c <= kJamoLast; ++c)
				{
					const char *const ks = jamo_keys(c);
					if (ks[0] == static_cast<char>(key) && ks[1] == '\0')
						return c;
				}
				key = key - 'A' + 'a';
			}
			if (key < 'a' || key > 'z')
				return 0;
			for (char32_t c = kJamoFirst; c <= kJamoLast; ++c)
			{
				const char *const ks = jamo_keys(c);
				if (ks[0] == static_cast<char>(key) && ks[1] == '\0')
					return c;
			}
			return 0;
		}

		// 초성 19 · 종성 27(+없음) 의 호환 자모 순서.
		inline int cho_index(char32_t c)
		{
			static const char32_t t[19] = { 0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145, 0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			for (int i = 0; i < 19; ++i) if (t[i] == c) return i;
			return -1;
		}
		inline char32_t cho_jamo(int i)
		{
			static const char32_t t[19] = { 0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145, 0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			return (i >= 0 && i < 19) ? t[i] : 0;
		}
		inline int jong_index(char32_t c) // 0 = 받침 없음, -1 = 받침이 될 수 없음(ㄸ ㅃ ㅉ 와 모음)
		{
			static const char32_t t[28] = { 0, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A, 0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			if (c == 0) return 0;
			for (int i = 1; i < 28; ++i) if (t[i] == c) return i;
			return -1;
		}
		inline char32_t jong_jamo(int i)
		{
			static const char32_t t[28] = { 0, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A, 0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			return (i >= 0 && i < 28) ? t[i] : 0;
		}

		// 겹모음 · 겹받침 조합표. 없으면 0.
		inline char32_t combine_vowel(char32_t a, char32_t b)
		{
			if (a == 0x3157 && b == 0x314F) return 0x3158; // ㅗ+ㅏ=ㅘ
			if (a == 0x3157 && b == 0x3150) return 0x3159; // ㅗ+ㅐ=ㅙ
			if (a == 0x3157 && b == 0x3163) return 0x315A; // ㅗ+ㅣ=ㅚ
			if (a == 0x315C && b == 0x3153) return 0x315D; // ㅜ+ㅓ=ㅝ
			if (a == 0x315C && b == 0x3154) return 0x315E; // ㅜ+ㅔ=ㅞ
			if (a == 0x315C && b == 0x3163) return 0x315F; // ㅜ+ㅣ=ㅟ
			if (a == 0x3161 && b == 0x3163) return 0x3162; // ㅡ+ㅣ=ㅢ
			return 0;
		}
		struct pair32 { char32_t a, b, ab; };
		inline const pair32 *final_pairs(std::size_t &n)
		{
			static const pair32 t[] = {
				{ 0x3131, 0x3145, 0x3133 }, { 0x3134, 0x3148, 0x3135 }, { 0x3134, 0x314E, 0x3136 },
				{ 0x3139, 0x3131, 0x313A }, { 0x3139, 0x3141, 0x313B }, { 0x3139, 0x3142, 0x313C }, { 0x3139, 0x3145, 0x313D },
				{ 0x3139, 0x314C, 0x313E }, { 0x3139, 0x314D, 0x313F }, { 0x3139, 0x314E, 0x3140 }, { 0x3142, 0x3145, 0x3144 },
			};
			n = sizeof(t) / sizeof(t[0]);
			return t;
		}
		inline char32_t combine_final(char32_t a, char32_t b)
		{
			std::size_t n = 0;
			const pair32 *t = final_pairs(n);
			for (std::size_t i = 0; i < n; ++i) if (t[i].a == a && t[i].b == b) return t[i].ab;
			return 0;
		}
		// 겹받침 → (앞, 뒤). 겹받침이 아니면 false.
		inline bool split_final(char32_t ab, char32_t &a, char32_t &b)
		{
			std::size_t n = 0;
			const pair32 *t = final_pairs(n);
			for (std::size_t i = 0; i < n; ++i) if (t[i].ab == ab) { a = t[i].a; b = t[i].b; return true; }
			return false;
		}
		// 겹모음 → (앞, 뒤). 겹모음이 아니면 false.
		inline bool split_vowel(char32_t ab, char32_t &a, char32_t &b)
		{
			static const char32_t base[] = { 0x3157, 0x315C, 0x3161 };
			for (char32_t x : base)
				for (char32_t y = kVowelFirst; y <= kJamoLast; ++y)
					if (combine_vowel(x, y) == ab) { a = x; b = y; return true; }
			return false;
		}

		// 두벌식 조합기. 자모를 하나씩 먹여 완성 글자를 만든다.
		class composer
		{
		public:
			void consonant(char32_t c)
			{
				if (_cho == 0 && _jung == 0) { _cho = c; return; }
				if (_jung == 0 || _cho == 0) { flush(); _cho = c; return; } // 초성만 / 모음만 있던 자리
				if (_jong == 0)
				{
					if (jong_index(c) > 0) _jong = c; else { flush(); _cho = c; }
					return;
				}
				if (const char32_t ab = combine_final(_jong, c)) _jong = ab;
				else { flush(); _cho = c; }
			}
			void vowel(char32_t v)
			{
				if (_jung == 0) { _jung = v; return; } // 초성 뒤 첫 모음, 또는 모음 단독 시작
				if (_jong == 0)
				{
					if (const char32_t ab = combine_vowel(_jung, v)) _jung = ab;
					else { flush(); _jung = v; }
					return;
				}
				// 받침이 다음 글자 초성으로 넘어간다(겹받침이면 뒤 자음만).
				char32_t keep = 0, move = _jong, a = 0, b = 0;
				if (split_final(_jong, a, b)) { keep = a; move = b; }
				_jong = keep;
				flush();
				_cho = move;
				_jung = v;
			}
			void other(char32_t c) { flush(); _out += c; }
			void flush()
			{
				if (_cho != 0 && _jung != 0)
				{
					const int l = cho_index(_cho), v = static_cast<int>(_jung - kVowelFirst), t = jong_index(_jong);
					if (l >= 0 && t >= 0)
						_out += static_cast<char32_t>(kSyllableFirst + (l * 21 + v) * 28 + t);
				}
				else if (_cho != 0) _out += _cho;
				else if (_jung != 0) _out += _jung;
				_cho = _jung = _jong = 0;
			}
			std::u32string take() { flush(); return std::move(_out); }

		private:
			char32_t _cho = 0, _jung = 0, _jong = 0;
			std::u32string _out;
		};

		inline void feed_jamo(composer &cm, char32_t j)
		{
			if (is_vowel(j))
			{
				char32_t a = 0, b = 0;
				if (split_vowel(j, a, b)) { cm.vowel(a); cm.vowel(b); } // 쪼개서 먹여야 뒤에 오는 키와 다시 묶인다
				else cm.vowel(j);
			}
			else
			{
				char32_t a = 0, b = 0;
				if (split_final(j, a, b)) { cm.consonant(a); cm.consonant(b); }
				else cm.consonant(j);
			}
		}

		// 영문 키·자모·완성 글자가 섞인 문자열을 두벌식대로 다시 조합한다.
		// 'qls1' → '빈1', '나'+'a' → '남', 이미 완성된 한글은 그대로(멱등). 공백·숫자·기호는 그대로 끊고 지나간다.
		inline std::u32string compose(const std::u32string &in)
		{
			composer cm;
			for (char32_t c : in)
			{
				if (const char32_t j = key_to_jamo(c)) { feed_jamo(cm, j); continue; }
				if (is_jamo(c)) { feed_jamo(cm, c); continue; }
				if (is_syllable(c))
				{
					const int idx = static_cast<int>(c - kSyllableFirst);
					feed_jamo(cm, cho_jamo(idx / 588));
					feed_jamo(cm, static_cast<char32_t>(kVowelFirst + (idx % 588) / 28));
					if (const int t = idx % 28) feed_jamo(cm, jong_jamo(t));
					continue;
				}
				cm.other(c);
			}
			return cm.take();
		}
		inline std::string compose_utf8(const std::string &s) { return to_utf8(compose(to_u32(s))); }

		// 글자 하나의 두벌식 키 문자열. 한글이 아니면 그 글자 자체(영문은 소문자로).
		inline std::u32string keys_of(char32_t c)
		{
			std::u32string out;
			auto add = [&out](const char *k) { for (; *k; ++k) out += static_cast<char32_t>(*k); };
			if (is_syllable(c))
			{
				const int idx = static_cast<int>(c - kSyllableFirst);
				add(jamo_keys(cho_jamo(idx / 588)));
				add(jamo_keys(static_cast<char32_t>(kVowelFirst + (idx % 588) / 28)));
				if (const int t = idx % 28) add(jamo_keys(jong_jamo(t)));
			}
			else if (is_jamo(c)) add(jamo_keys(c));
			else if (c >= 'A' && c <= 'Z') out += static_cast<char32_t>(c - 'A' + 'a');
			else out += c;
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 검색
		// ─────────────────────────────────────────────────────────────

		// 입력 글자 q 가 장소 글자 z 에 맞는가. 같으면(영문은 대소문자 무시) 맞고, 아니면
		// q 의 키가 z 의 키 앞부분이면 맞는다('ㅅ'·'시'·'신' → '신', 한글 상태로 친 'ㅁ' → 'A').
		inline bool unit_match(char32_t q, char32_t z, bool &exact)
		{
			auto fold = [](char32_t c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; };
			exact = fold(q) == fold(z);
			if (exact) return true;
			const std::u32string qk = keys_of(q), zk = keys_of(z);
			return !qk.empty() && zk.size() >= qk.size() && zk.compare(0, qk.size(), qk) == 0;
		}

		// 받침 떼기 후보들. 두벌식은 다음 글자 첫 자음을 일단 앞 글자 받침으로 붙인다('신위'+ㅁ → '신윔',
		// '빈'+ㅈ → '빉'). 사람은 그걸 다음 글자로 친 것이므로, 받침이 있는 글자마다 '떼어 다음 글자로'
		// 본 경우도 같이 만든다(겹받침이면 뒤 자음만 뗀다). 글자 수가 적어 조합이 많지 않다(상한 64).
		inline std::vector<std::u32string> split_variants(const std::u32string &q)
		{
			std::vector<std::u32string> out(1);
			for (char32_t c : q)
			{
				char32_t base = 0, moved = 0;
				if (is_syllable(c))
				{
					const int idx = static_cast<int>(c - kSyllableFirst), t = idx % 28;
					if (t != 0)
					{
						char32_t a = 0, b = 0, keep = 0;
						moved = jong_jamo(t);
						if (split_final(moved, a, b)) { keep = a; moved = b; }
						base = static_cast<char32_t>(c - t + (keep != 0 ? jong_index(keep) : 0));
					}
				}
				const std::size_t n = out.size();
				for (std::size_t k = 0; k < n; ++k)
				{
					if (base != 0 && out.size() < 64)
						out.push_back(out[k] + base + moved);
					out[k] += c;
				}
			}
			return out;
		}

		struct zone_unit { char32_t c; bool word_start; };

		// 검색어 q 를 장소 글자들 z 에 맞춘 점수(-1 = 안 맞음). exact_only 면 글자가 똑같을 때만 맞는다
		// — 영어로 친 그대로('atm')를 볼 때 쓴다. 'a' 하나가 'ㅁ' 으로 시작하는 모든 글자에 걸리면 안 된다.
		inline int score_units(const std::u32string &q, const std::vector<zone_unit> &z, bool exact_only)
		{
			// (A) 글자 단위 순서 일치(사이를 건너뛰어도 된다 — '신위편' → '신경 위 편의점').
			//     받침을 뗀 후보는 사람이 친 그대로가 아니므로 1점 깎는다(그대로 맞는 쪽이 이긴다).
			int best = -1;
			const std::vector<std::u32string> variants = exact_only ? std::vector<std::u32string>{ q } : split_variants(q);
			for (std::size_t vi = 0; vi < variants.size(); ++vi)
			{
				const std::u32string &qv = variants[vi];
				int s = vi == 0 ? 0 : -1;
				std::size_t j = 0;
				std::ptrdiff_t prev = -2;
				for (std::size_t i = 0; i < z.size() && j < qv.size(); ++i)
				{
					bool exact = false;
					if (!unit_match(qv[j], z[i].c, exact) || (exact_only && !exact)) continue;
					s += exact ? 10 : 6;
					if (z[i].word_start) s += 4;
					if (static_cast<std::ptrdiff_t>(i) == prev + 1) s += 3;
					if (i == 0 && j == 0) s += 5;
					prev = static_cast<std::ptrdiff_t>(i);
					++j;
				}
				if (j == qv.size())
				{
					if (qv.size() == z.size()) s += 20; // 전부 쳤다
					best = std::max(best, s);
				}
			}
			// (B) 키 문자열 부분일치 — 조합 경계가 잠깐 어긋난 순간('빉' = 'qlsw')을 받아 준다.
			if (best < 0 && !exact_only)
			{
				std::u32string qk, zk;
				for (char32_t c : q) qk += keys_of(c);
				for (const zone_unit &u : z) zk += keys_of(u.c);
				const std::size_t at = zk.find(qk);
				if (!qk.empty() && at != std::u32string::npos)
					best = 3 + (at == 0 ? 4 : 0);
			}
			return best;
		}

		// 점수. -1 = 안 맞음. 빈 검색어는 0(전부 맞음).
		// 검색어를 **두 가지로** 본다: 두벌식으로 한글 조합한 것('qls1' → '빈1')과 친 그대로('atm').
		// 영문 상태로 한글을 치는 사람도, 영어 이름(ATM)을 치는 사람도 둘 다 찾게 — 더 잘 맞는 쪽 점수.
		inline int score(const std::string &query_utf8, const std::string &zone_utf8)
		{
			std::u32string raw, composed;
			const std::u32string q32 = to_u32(query_utf8);
			for (char32_t c : q32)
				if (c != ' ') raw += c;
			for (char32_t c : compose(q32))
				if (c != ' ') composed += c;
			if (raw.empty() && composed.empty()) return 0;

			std::vector<zone_unit> z;
			bool next_start = true;
			for (char32_t c : to_u32(zone_utf8))
			{
				if (c == ' ') { next_start = true; continue; }
				z.push_back({ c, next_start });
				next_start = false;
			}
			if (z.empty()) return -1;

			int best = score_units(composed, z, false);
			if (raw != composed)
				best = std::max(best, score_units(raw, z, true));
			return best;
		}

		// 검색 결과: 맞는 항목의 인덱스를 점수 높은 순으로(같으면 원래 순서). 빈 검색어면 전부 원래 순서.
		inline std::vector<std::size_t> search(const std::vector<entry> &list, const std::string &query_utf8)
		{
			std::vector<std::pair<int, std::size_t>> hits;
			for (std::size_t i = 0; i < list.size(); ++i)
			{
				int s = score(query_utf8, list[i].zone);
				for (const std::string &a : list[i].aliases) // 줄임말로 쳐도 같은 항목(점수는 더 잘 맞는 쪽)
					s = std::max(s, score(query_utf8, a));
				if (s >= 0) hits.push_back({ s, i });
			}
			std::stable_sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
			std::vector<std::size_t> out;
			for (const auto &h : hits) out.push_back(h.second);
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 시간 칸 — 문장에 '[00:00]' 이 있으면 올릴 때 시간을 받아 채운다(보안국 습격 '준비 시간').
		// ─────────────────────────────────────────────────────────────
		constexpr const char *kTimeSlot = "[00:00]";
		constexpr int kDefaultTimeAheadMin = 10; // 기본값 = 지금부터 10분 뒤(정렬 결정)

		inline bool has_time_slot(const std::string &text) { return text.find(kTimeSlot) != std::string::npos; }

		inline std::string format_hhmm(int h, int m)
		{
			char b[8];
			b[0] = static_cast<char>('0' + h / 10); b[1] = static_cast<char>('0' + h % 10); b[2] = ':';
			b[3] = static_cast<char>('0' + m / 10); b[4] = static_cast<char>('0' + m % 10); b[5] = '\0';
			return b;
		}

		// 지금 시각 + ahead 분(자정을 넘기면 다음 날 시각으로 돈다).
		inline std::string default_time(int now_h, int now_m, int ahead_min = kDefaultTimeAheadMin)
		{
			const int t = ((now_h * 60 + now_m + ahead_min) % 1440 + 1440) % 1440;
			return format_hhmm(t / 60, t % 60);
		}

		// 사람이 친 시간 → "HH:MM". "2135", "21:35", "935", "9:35", "21.35", "21 35" 를 받는다. 틀리면 false.
		inline bool parse_time(const std::string &in, std::string &out)
		{
			std::string digits;
			int seps = 0;
			for (char c : in)
			{
				if (c >= '0' && c <= '9') digits += c;
				else if (c == ':' || c == '.' || c == ' ') { if (!digits.empty()) ++seps; }
				else return false;
			}
			int h = -1, m = -1;
			if (seps > 0)
			{
				// 구분자가 있으면 앞 1~2자리 = 시, 뒤 2자리 = 분
				const std::size_t colon = in.find_first_of(":. ", in.find_first_not_of(' '));
				std::string hs, ms;
				for (std::size_t i = 0; i < in.size(); ++i)
					if (in[i] >= '0' && in[i] <= '9') (i < colon ? hs : ms) += in[i];
				if (hs.empty() || hs.size() > 2 || ms.size() != 2) return false;
				h = std::stoi(hs); m = std::stoi(ms);
			}
			else if (digits.size() == 3 || digits.size() == 4)
			{
				h = std::stoi(digits.substr(0, digits.size() - 2));
				m = std::stoi(digits.substr(digits.size() - 2));
			}
			else return false;
			if (h < 0 || h > 23 || m < 0 || m > 59) return false;
			out = format_hhmm(h, m);
			return true;
		}

		// '[00:00]' 을 '[HH:MM]' 으로(대괄호는 그대로). 시간 칸이 없으면 그대로 돌려준다.
		inline std::string fill_time(const std::string &text, const std::string &hhmm)
		{
			std::string out = text;
			const std::size_t at = out.find(kTimeSlot);
			if (at != std::string::npos)
				out.replace(at, std::string(kTimeSlot).size(), "[" + hhmm + "]");
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 채울 칸 — 문장 안의 {…} (수배·알디컨·영장처럼 올리기 전에 값을 넣는 문장)
		//   {대상}      고번 닉네임. '516우공비' '516 우공비' '516/우공비' 를 전부 '516/우공비' 로
		//   {시간}      지금 시각이 미리 들어간 HH:MM 칸 / {시간+30} 은 지금+30분
		//   {그 밖}     아무 글자(예: {팩션})
		//   [00:00]     예전 보습 문장의 시간 칸 = {시간+10} 과 같다(대괄호 포함 자리)
		// ─────────────────────────────────────────────────────────────
		enum class slot_kind { text, target, time };

		struct slot
		{
			slot_kind kind = slot_kind::text;
			std::string label;   // 칸 이름(화면에 보이는 것): "대상" / "시간" / "팩션"
			int ahead_min = 0;   // time 의 기본값 = 지금 + ahead_min 분
			std::size_t pos = 0; // 문장 안 자리(바이트)
			std::size_t len = 0;
			bool bracketed = false; // [00:00] 처럼 대괄호까지 바꿔 끼우는 자리
		};

		inline std::vector<slot> find_slots(const std::string &text)
		{
			std::vector<slot> out;
			for (std::size_t i = 0; i < text.size();)
			{
				if (text.compare(i, std::string(kTimeSlot).size(), kTimeSlot) == 0)
				{
					slot s;
					s.kind = slot_kind::time;
					s.label = "\xEC\x8B\x9C\xEA\xB0\x84"; // "시간"
					s.ahead_min = kDefaultTimeAheadMin;
					s.pos = i;
					s.len = std::string(kTimeSlot).size();
					s.bracketed = true;
					out.push_back(s);
					i += s.len;
					continue;
				}
				if (text[i] == '{')
				{
					const std::size_t close = text.find('}', i + 1);
					if (close != std::string::npos && close - i - 1 <= 40)
					{
						slot s;
						s.pos = i;
						s.len = close - i + 1;
						std::string name = text.substr(i + 1, close - i - 1);
						const std::string kTime = "\xEC\x8B\x9C\xEA\xB0\x84";   // "시간"
						const std::string kTarget = "\xEB\x8C\x80\xEC\x83\x81"; // "대상"
						if (name.compare(0, kTime.size(), kTime) == 0)
						{
							s.kind = slot_kind::time;
							s.label = kTime;
							const std::string rest = name.substr(kTime.size());
							if (rest.size() > 1 && (rest[0] == '+' || rest[0] == '-'))
							{
								int v = 0;
								for (std::size_t k = 1; k < rest.size() && rest[k] >= '0' && rest[k] <= '9'; ++k)
									v = v * 10 + (rest[k] - '0');
								s.ahead_min = rest[0] == '-' ? -v : v;
							}
						}
						else if (name == kTarget)
						{
							s.kind = slot_kind::target;
							s.label = kTarget;
						}
						else
						{
							s.label = name;
						}
						out.push_back(s);
						i = close + 1;
						continue;
					}
				}
				++i;
			}
			return out;
		}

		inline bool has_slots(const std::string &text) { return !find_slots(text).empty(); }

		// 파엠 채팅칸 글자 수 상한(서버 채팅 UI 의 '9/166' 카운터). 닉네임이 아주 길면 수배 문장이 넘을 수 있다.
		constexpr std::size_t kChatMaxChars = 166;
		inline std::size_t char_count(const std::string &utf8) { return to_u32(utf8).size(); }

		// 고번 닉네임 → "516/우공비". 앞의 숫자 = 고번, 나머지(앞쪽 공백·'/' 등 구분자 떼고) = 닉네임.
		// 둘 중 하나라도 없으면 false.
		inline bool normalize_target(const std::string &in, std::string &out)
		{
			std::size_t i = 0;
			while (i < in.size() && (in[i] == ' ' || in[i] == '[')) ++i;
			std::string id;
			while (i < in.size() && in[i] >= '0' && in[i] <= '9') id += in[i++];
			while (i < in.size() && (in[i] == ' ' || in[i] == '/' || in[i] == ':' || in[i] == '-' || in[i] == '_' || in[i] == '.' || in[i] == ',')) ++i;
			std::string nick = in.substr(i);
			while (!nick.empty() && (nick.back() == ' ' || nick.back() == ']')) nick.pop_back();
			if (id.empty() || nick.empty())
				return false;
			out = id + "/" + nick;
			return true;
		}

		// 칸 하나의 입력값을 문장에 들어갈 모양으로. 틀리면 false(그 칸에서 Enter 가 안 넘어간다).
		inline bool slot_value(const slot &s, const std::string &input, std::string &out)
		{
			switch (s.kind)
			{
			case slot_kind::target: return normalize_target(input, out);
			case slot_kind::time:   return parse_time(input, out);
			default:
			{
				std::string t = input;
				while (!t.empty() && t.back() == ' ') t.pop_back();
				std::size_t a = 0;
				while (a < t.size() && t[a] == ' ') ++a;
				out = t.substr(a);
				return !out.empty();
			}
			}
		}

		// 칸마다 미리 넣어 둘 값. 시간은 지금 + ahead, 나머지는 빈칸.
		inline std::string slot_default(const slot &s, int now_h, int now_m)
		{
			return s.kind == slot_kind::time ? default_time(now_h, now_m, s.ahead_min) : std::string();
		}

		// 값들을 끼워 넣은 문장. values[i] 가 비어 있으면(아직 안 쳤거나 틀림) placeholder 를 넣는다.
		inline std::string fill_slots(const std::string &text, const std::vector<slot> &slots, const std::vector<std::string> &values, const std::string &placeholder = "...")
		{
			std::string out = text;
			for (std::size_t n = slots.size(); n-- > 0;) // 뒤에서부터 바꿔야 앞 자리가 안 밀린다
			{
				const slot &s = slots[n];
				std::string v = n < values.size() && !values[n].empty() ? values[n] : placeholder;
				if (s.bracketed)
					v = "[" + v + "]";
				out.replace(s.pos, s.len, v);
			}
			return out;
		}

		// ─────────────────────────────────────────────────────────────
		// 클립보드 칸 — 문장 안 '{클립보드}' 는 올리는 순간 클립보드에 있는 이름으로 바꾼다(신세계렉카).
		//   렉카 기사는 압류할 차 주인 이름('🚧하은(210)')을 미리 복사해 두고 목록에서 고르기만 한다.
		//   채팅에 붙여넣느라 클립보드가 문장으로 바뀌므로 올린 뒤 이름으로 되돌린다(runtime 쪽) — 다음 공지도 바로.
		// ─────────────────────────────────────────────────────────────
		constexpr const char *kClipSlot = "{\xED\x81\xB4\xEB\xA6\xBD\xEB\xB3\xB4\xEB\x93\x9C}"; // "{클립보드}"
		constexpr std::size_t kMaxClipNameChars = 32;

		inline bool has_clip_slot(const std::string &text) { return text.find(kClipSlot) != std::string::npos; }

		// 클립보드 글자 → 문장에 넣을 이름. 앞뒤 공백·줄바꿈은 뗀다. 이름으로 안 보이면 false:
		// 비었음 · 여러 줄 · 너무 김 · '/' 로 시작(채팅 명령 = 문장이 남아 있는 것) · 칸 기호({ } [00:00]).
		inline bool clip_name(const std::string &raw, std::string &out)
		{
			const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
			std::size_t a = 0, b = raw.size();
			while (a < b && blank(raw[a])) ++a;
			while (b > a && blank(raw[b - 1])) --b;
			const std::string s = raw.substr(a, b - a);
			if (s.empty() || s[0] == '/' || char_count(s) > kMaxClipNameChars)
				return false;
			for (char c : s)
			{
				const unsigned char u = static_cast<unsigned char>(c);
				if (u < 0x20 || u == 0x7F || c == '{' || c == '}')
					return false;
			}
			if (s.find(kTimeSlot) != std::string::npos)
				return false;
			out = s;
			return true;
		}

		inline std::string fill_clip(const std::string &text, const std::string &name)
		{
			std::string out = text;
			const std::string slot = kClipSlot;
			for (std::size_t at = out.find(slot); at != std::string::npos; at = out.find(slot, at + name.size()))
				out.replace(at, slot.size(), name);
			return out;
		}

		// 작은 창에 그릴 모양 — 폰트에 없는 그림 문자(🚧 같은 BMP 밖 글자, 이모지 변형 선택자, ZWJ)를 뺀다.
		// 채팅에 올라가는 문장은 그대로다(채팅은 이모지를 그린다). 우리 창에만 '?' 가 안 찍히게.
		inline std::string display_text(const std::string &utf8)
		{
			std::u32string out;
			for (char32_t c : to_u32(utf8))
				if (c < 0x10000 && c != 0xFE0F && c != 0xFE0E && c != 0x200D)
					out += c;
			while (!out.empty() && out.front() == ' ') out.erase(out.begin());
			return to_utf8(out);
		}

		// 이름 끝의 '(…)' 를 떼어 따로 — '압류 2분전 (2분)' → '압류 2분전' + '(2분)'(덧붙임은 강조색으로 그린다).
		inline void split_note(const std::string &zone, std::string &main, std::string &note)
		{
			main = zone;
			note.clear();
			const std::size_t open = zone.rfind(" (");
			if (open != std::string::npos && open > 0 && zone.size() > open + 3 && zone.back() == ')')
			{
				main = zone.substr(0, open);
				note = zone.substr(open + 1);
			}
		}

		// ─────────────────────────────────────────────────────────────
		// 쿨타임 — 이름 끝이 '(N분)' 인 공지(렉카 '압류 2분전 (2분)')를 올리면 그 대상 이름에 N분 타이머가 돈다.
		//   게임 중엔 작은 창 자리에, 창을 열면 창 안에 '하은(210) 1:42' 로 보인다. 다 되면 'N분 지났어요' 를
		//   3초 더 보여 주고 사라진다. 같은 대상에 다른 공지(압류 2분후 등)를 올리면 그 타이머는 끝난다.
		//   창 안 타이머 줄의 ✕ 로 바로 지울 수도 있다.
		// ─────────────────────────────────────────────────────────────
		constexpr long long kCooldownLingerMs = 3000; // 'N분 지났어요' 뜨고 3초 뒤 사라짐
		constexpr std::size_t kMaxCooldowns = 4; // 차 여러 대를 동시에 걸어 둘 수 있게. 넘치면 가장 오래된 것부터

		// "(2분)" → 2. 모양이 다르면 0.
		inline int note_minutes(const std::string &note)
		{
			const std::string kMin = "\xEB\xB6\x84"; // "분"
			if (note.size() < 2 + kMin.size() + 1 || note.front() != '(' || note.back() != ')')
				return 0;
			const std::string body = note.substr(1, note.size() - 2);
			if (body.compare(body.size() - kMin.size(), kMin.size(), kMin) != 0)
				return 0;
			const std::string digits = body.substr(0, body.size() - kMin.size());
			if (digits.empty() || digits.size() > 3)
				return 0;
			int v = 0;
			for (char c : digits)
			{
				if (c < '0' || c > '9') return 0;
				v = v * 10 + (c - '0');
			}
			return v;
		}

		struct cooldown
		{
			std::string target; // 클립보드 이름 원문('🚧하은(210)')
			int minutes = 0;
			long long end_ms = 0;
		};

		class cooldowns
		{
		public:
			// 공지를 올렸다. minutes > 0 이면 그 대상 타이머를 (다시) 시작, 0 이면 그 대상 타이머를 끝낸다.
			void posted(const std::string &target, int minutes, long long now_ms)
			{
				_v.erase(std::remove_if(_v.begin(), _v.end(), [&target](const cooldown &c) { return c.target == target; }), _v.end());
				if (minutes <= 0)
					return;
				if (_v.size() >= kMaxCooldowns)
					_v.erase(_v.begin());
				_v.push_back({ target, minutes, now_ms + minutes * 60000LL });
			}
			// ✕ 로 그 대상 타이머를 지운다.
			void dismiss(const std::string &target)
			{
				_v.erase(std::remove_if(_v.begin(), _v.end(), [&target](const cooldown &c) { return c.target == target; }), _v.end());
			}
			// 다 되고 3초 지난 것을 지운다.
			void prune(long long now_ms)
			{
				_v.erase(std::remove_if(_v.begin(), _v.end(), [now_ms](const cooldown &c) { return now_ms >= c.end_ms + kCooldownLingerMs; }), _v.end());
			}
			const std::vector<cooldown> &items() const { return _v; }
			bool empty() const { return _v.empty(); }

		private:
			std::vector<cooldown> _v;
		};

		// 남은 시간 "1:42"(초는 올림 — 막 시작하면 '2:00', 마지막 순간은 '0:01'). 다 됐으면 빈칸.
		inline std::string format_remaining(long long remain_ms)
		{
			if (remain_ms <= 0)
				return std::string();
			const long long s = (remain_ms + 999) / 1000;
			std::string out = std::to_string(s / 60) + ":";
			if (s % 60 < 10) out += '0';
			return out + std::to_string(s % 60);
		}

		// ─────────────────────────────────────────────────────────────
		// 채팅으로 올리기 — 시간표
		// ─────────────────────────────────────────────────────────────
		// 메뉴를 닫고(입력 막힘이 풀릴 때까지 기다림) → 채팅 키 → (채팅창 열릴 때까지) → Ctrl+V → Enter.
		// 키를 한꺼번에 보내면 채팅창이 열리기 전에 붙여넣기가 도착해 허공에 사라진다. 그래서 단계마다 기다린다.
		enum class post_action
		{
			none,
			open_chat, // 채팅 키 누르고 떼기
			paste,     // Ctrl+V
			send,      // Enter
			done,
		};

		constexpr int kCloseWaitMs = 120;  // 메뉴가 닫히고 게임 입력 막힘이 풀리는 시간
		constexpr int kPasteToSendMs = 150; // 붙여넣은 글자가 채팅칸에 들어가는 시간
		constexpr int kClipRestoreMs = 600; // Enter 뒤 클립보드를 이름으로 되돌리기까지(렉카 — 게임이 렉이어도 붙여넣기가 먼저 끝나게)
		constexpr int kMinChatDelayMs = 80, kMaxChatDelayMs = 1500, kDefaultChatDelayMs = 300;

		class poster
		{
		public:
			void start(long long now_ms, int chat_delay_ms)
			{
				_t0 = now_ms;
				_chat_delay = std::clamp(chat_delay_ms, kMinChatDelayMs, kMaxChatDelayMs);
				_step = 1;
			}
			void cancel() { _step = 0; }
			bool active() const { return _step != 0; }

			// 매 프레임 부른다. 지금 해야 할 동작을 하나 돌려준다(한 프레임에 둘 이상 내지 않는다).
			post_action tick(long long now_ms)
			{
				const long long t_open = _t0 + kCloseWaitMs;
				const long long t_paste = t_open + _chat_delay;
				const long long t_send = t_paste + kPasteToSendMs;
				switch (_step)
				{
				case 1: if (now_ms >= t_open) { _step = 2; return post_action::open_chat; } break;
				case 2: if (now_ms >= t_paste) { _step = 3; return post_action::paste; } break;
				case 3: if (now_ms >= t_send) { _step = 0; return post_action::send; } break;
				default: break;
				}
				return post_action::none;
			}

		private:
			long long _t0 = 0;
			int _chat_delay = kDefaultChatDelayMs;
			int _step = 0;
		};
	}
}
