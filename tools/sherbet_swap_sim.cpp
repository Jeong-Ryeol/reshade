/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
// 가짜 파일시스템 위에서 교체 알고리즘(스펙 §4.4 S7~S13)과 복구·롤백 경로(§5.4 R3~R10)를
// 돌리며 **모든 단계마다 '전원차단' 과 'rename 거부(AV 가 파일을 물고 있음)' 를 주입**하고,
// 어느 지점에서 끊겨도 startup_repair 가 정상 상태로 수렴하는지 단언한다. 맥에서는 진짜
// rename 을 검증할 수 없으므로 판정을 순수 함수(classify/decide_repair)로 뽑은 이유가 이것이다.
//
// 여기서 증명하는 성질:
//   P1 브릭 없음   — 어느 순간에도 DLL 소스(self/.new/.bak)가 하나는 남고, 수렴 후엔 self 가 있다.
//                    self 가 없는 동안에는 어떤 DLL 사본도 지우지 않는다(.sherbet-bak.old 포함)
//   P2 수렴·멱등   — 복구를 반복하면 고정점에 도달하고, 그 뒤로는 아무것도 바뀌지 않는다
//   P3 잔재 없음   — 수렴 후 .sherbet-new / .sherbet-bak.old / 복구안내가 남지 않는다.
//                    반대로 .sherbet-bak(롤백 재료)과 .sherbet-failed(§5.4 R9 증거)는
//                    복구가 **절대 지우지 않는다**
//   P4 마커 무결성 — 최종 마커는 다시 파싱되고, 모르는 키는 원문 그대로 살아남으며,
//                    파싱에 실패한 마커는 단 한 바이트도 덮어쓰지 않는다
//   P5 진실성      — rolledback 이면 self 는 옛 바이너리다. **귀납적으로** 단언한다:
//                    시작 상태가 P5 를 만족하면 복구 후에도 만족한다(복구가 깨뜨리지 않는다)
//   P6 전순성      — 파일 8조합 × 모든 마커 상태가 빠짐없이, 없는 파일을 옮기지 않는 동작으로 간다
//   P7 임계값 2    — R6 순서를 모델링해도 롤백은 '2회' 부팅 실패에서 일어난다(1회가 아니다).
//                    **직전 롤백 마커(tries=2)를 물려받은 교체도 첫 부팅에 롤백되지 않는다**
//   P8 블랙리스트  — 롤백이 어느 지점에서 끊기거나 실패해도 bad_ver/bad_sha 가 살아남는다
//
// clang++ -std=c++17 -Wall -Isource tools/sherbet_swap_sim.cpp -o /tmp/sim && /tmp/sim
#include "sherbet_update_core.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace sherbet::update;

// 가짜 파일시스템: 경로 → 내용("OLD"/"NEW"/마커 텍스트)
using fs_t = std::map<std::string, std::string>;

static const char *SELF   = "dxgi.dll";
static const char *NEWF   = "dxgi.dll.sherbet-new";
static const char *BAKF   = "dxgi.dll.sherbet-bak";
static const char *BAKOLD = "dxgi.dll.sherbet-bak.old";
static const char *FAILED = "dxgi.dll.sherbet-failed";
static const char *NOTE   = "Sherbet-복구안내.txt";
static const char *MARK   = "sherbet.update";

static const char *OLDVER = "1.3.0";
static const char *NEWVER = "1.4.0";
static const char *NEWSHA = "1111111111111111111111111111111111111111111111111111111111111111";

// ── 실패 보고 ──────────────────────────────────────────────────────────────
static std::string g_scene;
static long g_checks = 0;

static void fail(const std::string &msg)
{
	std::printf("FAIL [%s]: %s\n", g_scene.c_str(), msg.c_str());
	std::exit(1);
}

static void check(bool cond, const std::string &msg)
{
	++g_checks;
	if (!cond) fail(msg);
}

// ── 가짜 파일시스템 원시연산 ───────────────────────────────────────────────
static bool has(const fs_t &fs, const char *p) { return fs.count(p) != 0; }

// ⚠️ operator[] 로 읽지 말 것 — 없는 키를 빈 값으로 '만들어' 버려서 has() 가 뒤집힌다.
static std::string get(const fs_t &fs, const char *p)
{
	const fs_t::const_iterator it = fs.find(p);
	return it == fs.end() ? std::string() : it->second;
}

static void rm(fs_t &fs, const char *p) { fs.erase(p); }

// MoveFileExW(dwFlags=0) 모델. **대상이 이미 있으면 실패하고 아무것도 하지 않는다.**
// REPLACE_EXISTING 을 쓰지 않는 것이 S10 의 핵심 안전장치이고(묵은 .bak 이 남아 있으면
// 교체가 시작조차 안 된다), classify 의 추론이 전부 여기에 걸려 있다.
static bool mv(fs_t &fs, const char *from, const char *to)
{
	const fs_t::iterator it = fs.find(from);
	if (it == fs.end()) return false;
	if (fs.count(to) != 0) return false;
	fs[to] = it->second;
	fs.erase(it);
	return true;
}

// ── 불변식 ─────────────────────────────────────────────────────────────────
// DLL 소스가 하나라도 남아 있는가. 이게 깨지는 순간이 곧 '고객 게임이 안 켜짐' 이다.
static bool recoverable(const fs_t &fs)
{
	return has(fs, SELF) || has(fs, NEWF) || has(fs, BAKF);
}

// DLL 슬롯에는 온전한 바이너리만 들어 있어야 한다(부분 기록·엉뚱한 값 금지).
static void check_slots(const fs_t &fs)
{
	const char *const slots[] = { SELF, NEWF, BAKF, BAKOLD, FAILED };
	for (const char *const s : slots)
		if (has(fs, s))
		{
			const std::string v = get(fs, s);
			check(v == "OLD" || v == "NEW", std::string("슬롯 ") + s + " 의 내용이 쓰레기: '" + v + "'");
		}
}

// 단계 실행기. cut == k 이면 k 번째 단계 직후 전원이 나간다(그 이후 단계는 실행 안 됨).
struct machine
{
	fs_t fs;
	fs_t prev;          // 직전 단계 끝의 스냅숏 — 단계 단위 불변식 검사용
	int cut;            // 0 이면 중단 없음
	int step = 0;
	bool cut_hit = false;
	bool aborted = false;
	bool started_recoverable;

	machine(const fs_t &f, int c) : fs(f), prev(f), cut(c), started_recoverable(recoverable(f)) {}

	bool done(const char *what)
	{
		++step;
		check_slots(fs);
		// 재료를 하나라도 들고 시작했으면 어느 단계 뒤에도 들고 있어야 한다.
		// (처음부터 하나도 없이 시작한 시나리오에서만 이 검사를 끈다)
		if (started_recoverable)
			check(recoverable(fs), std::string("단계 '") + what + "' 직후 DLL 소스가 전부 사라짐");

		// ★ self 가 없는 동안에는 **어떤 DLL 사본도** 지우지 않는다. 옮기는 것(= self 가 그
		// 내용을 받는 것)만 허용된다. .sherbet-bak.old 는 잔재처럼 보이지만 한때 동작하던
		// 진짜 바이너리라, self/.new/.bak 이 전부 사라진 디렉터리에 남은 마지막 사본일 수 있다.
		// 단계 단위로 본다 — '복구 시작 시점' 이 아니라 '그 삭제를 하는 순간' 이 기준이다.
		if (!has(prev, SELF))
		{
			const char *const copies[] = { NEWF, BAKF, BAKOLD, FAILED };
			for (const char *const c : copies)
				if (has(prev, c) && !has(fs, c))
					check(has(fs, SELF) && get(fs, SELF) == get(prev, c),
						std::string("단계 '") + what + "' 에서 self 가 없는데 DLL 사본 " + c + " 을 지웠다");
		}
		prev = fs;

		if (cut != 0 && step == cut) { cut_hit = true; return true; }
		return false;
	}
};

// ── 마커 IO ────────────────────────────────────────────────────────────────
// unknown 은 오직 parse_marker 만 채운다 — 손으로 채우면 조작된 줄이 아는 필드로 승격된다.
static bool read_marker(const fs_t &fs, boot_marker &m)
{
	if (!has(fs, MARK)) return false;
	return parse_marker(get(fs, MARK), m);
}

static void write_marker(fs_t &fs, const boot_marker &m)
{
	const std::string text = serialize_marker(m);
	boot_marker rt;
	check(parse_marker(text, rt), "우리가 쓴 마커가 다시 파싱되지 않는다(라운드트립 파괴)");
	fs[MARK] = text;
}

// ── 교체 절차(스펙 §4.4) ───────────────────────────────────────────────────
// rename 거부(ERROR_ACCESS_DENIED)는 **그 실행 안에서만** 유효한 일시적 상황으로 모델링한다
// — AV 가 그 순간 파일을 물고 있는 경우다. 다음 실행의 startup_repair 는 정상적으로 옮길 수
// 있다고 본다. 락이 영구적이면 self 부재가 지속되는데, 그 상태는 애초에 우리 DLL 이 로드되지
// 않아 어떤 판정도 돌지 않는다(파일 맨 위 한계 참고).
struct swap_opts
{
	bool stale_bak = false;      // 묵은 .sherbet-bak 이 이미 있다
	bool bak_locked = false;     // S8 의 삭제가 그 순간 실패한다
	bool bakold_blocked = false; // .sherbet-bak.old 로 밀어내기도 실패한다
	bool s10_fails = false;      // S10 self → .bak 이 거부된다
	bool s11_fails = false;      // S11 .new → self 가 거부된다(→ 즉시 .bak → self 원복)
	bool s11_undo_fails = false; //   그 원복마저 거부된다
	bool s12_mismatch = false;   // S12 사후 sha 재검증 불일치 → S13 없이 즉시 롤백
};

static void run_swap(machine &M, const swap_opts &o)
{
	// 파싱 안 되는 마커가 이미 있으면 교체를 시작하지 않는다. 여기서 state=swapping 을
	// 덮어쓰면 다른 writer 의 모르는 키를 통째로 잃고, 안 쓰면 중단 복구가 불가능하다.
	// 둘 중 안전한 쪽은 '시작하지 않는다'(디스크 무변화)다.
	boot_marker m;
	if (has(M.fs, MARK) && !read_marker(M.fs, m)) { M.aborted = true; return; }

	// S7 스테이징 — 다운로드·검증(sha256 + pe_check)이 끝나 .part → .sherbet-new 로 옮겨진 상태
	M.fs[NEWF] = "NEW";
	if (M.done("S7 .sherbet-new 배치")) return;

	// S8 묵은 .sherbet-bak 제거. 못 지우면 .sherbet-bak.old 로 밀어낸다(다음 시작의 정리 대상)
	if (has(M.fs, BAKF))
	{
		if (!o.bak_locked) rm(M.fs, BAKF);
		else if (!o.bakold_blocked) { rm(M.fs, BAKOLD); mv(M.fs, BAKF, BAKOLD); }
	}
	if (M.done("S8 묵은 .sherbet-bak 정리")) return;

	// S8 이 끝나고도 .bak 이 남아 있으면 S10 의 rename(dwFlags=0)이 어차피 실패한다.
	// self 를 밀어내기 전에 멈춘다 — 디스크는 원본 그대로다.
	if (has(M.fs, BAKF)) { M.aborted = true; return; }

	// S9 복구안내(사람이 손으로 되살릴 유일한 수단) → 마커
	M.fs[NOTE] = "dxgi.dll 이 없으면 dxgi.dll.sherbet-bak 을 dxgi.dll 로 이름을 바꾸세요";
	if (M.done("S9 복구안내 기록")) return;

	// ⚠️ tries 는 물려받는다(스펙 S9 는 tries 를 안 건드린다). 직전 롤백 마커면 여기서
	// tries=2 가 그대로 따라온다 — S13/promote_pending 의 tries=0 리셋이 그래서 필수다.
	m.state = "swapping"; m.version = NEWVER; m.prev = OLDVER;
	m.bak = BAKF; m.exe = "GAME.exe"; m.sha = NEWSHA;
	write_marker(M.fs, m);
	if (M.done("S9 마커 swapping")) return;

	// S10 ★ self 가 사라지는 유일한 구간의 시작(1ms 미만)
	if (o.s10_fails)
	{
		// 아직 아무것도 안 옮겼으므로 되돌릴 것도 없다. 마커는 swapping 인 채로 남고
		// 다음 시작의 startup_repair 가 staged → promote_pending 으로 닫는다(낡지만 무해).
		M.aborted = true;
		M.done("S10 거부 → 교체 중단(디스크 무변화)");
		return;
	}
	check(mv(M.fs, SELF, BAKF), "S10 self → .sherbet-bak rename 실패");
	if (M.done("S10 self → .sherbet-bak")) return;

	// S11 ★ 설치
	if (o.s11_fails)
	{
		// 스펙 S11: "실패면 즉시 .bak → self 원복 5회". 같은 세션에서 도는 보상 트랜잭션이다
		// (이미지는 메모리에 매핑돼 있어 self 파일이 없어도 이 코드는 계속 돈다).
		M.aborted = true;
		if (o.s11_undo_fails)
		{
			// 원복까지 거부됨 = self 부재. .bak 과 .new 가 남아 있으므로 다음 실행(또는
			// 사람이 복구안내대로)의 startup_repair 가 되살린다.
			M.done("S11 거부 + 원복 거부(self 부재)");
			return;
		}
		check(mv(M.fs, BAKF, SELF), "S11 실패 원복(.sherbet-bak → self)이 실패");
		M.done("S11 거부 → .sherbet-bak → self 원복");
		return;
	}
	check(mv(M.fs, NEWF, SELF), "S11 .sherbet-new → self rename 실패");
	if (M.done("S11 .sherbet-new → self")) return;

	// S12 사후검증 — 새 self 를 다시 읽어 sha 재확인. 불일치면 S13 없이 곧바로 롤백한다.
	// (마커가 아직 swapping 인 상태에서 롤백이 시작되는 유일한 경로다)
	if (o.s12_mismatch) { M.aborted = true; return; }

	// S13 확정
	m.state = "pending"; m.tries = 0;
	write_marker(M.fs, m);
	if (M.done("S13 마커 pending")) return;

	rm(M.fs, NOTE);
	if (M.done("S13 복구안내 삭제")) return;
}

// ── 다음 실행의 복구 경로(스펙 §5.4 R3) ────────────────────────────────────
// 어느 disk_state 를 몇 번 밟았는지. '단언이 통과했다' 가 '거기까지 가 봤다' 를 뜻하도록
// 마지막에 전부 1회 이상인지 확인한다 — 못 밟은 갈래는 검사된 적이 없는 갈래다.
static const int kStates = 6;
static long g_seen[kStates];

static const char *state_name(disk_state s)
{
	switch (s)
	{
	case disk_state::normal:             return "normal";
	case disk_state::staged:             return "staged";
	case disk_state::swapping_lost_self: return "swapping_lost_self";
	case disk_state::resume_from_new:    return "resume_from_new";
	case disk_state::rollback_ready:     return "rollback_ready";
	case disk_state::broken_no_dll:      return "broken_no_dll";
	}
	return "?";
}

static void startup_repair(machine &M)
{
	boot_marker m;
	const bool marker_ok = read_marker(M.fs, m);
	// ⚠️ 파싱에 실패한 마커는 절대 다시 쓰지 않는다 — 다른 writer 의 모르는 키가 통째로
	// 날아간다. 그래도 '파일 복구' 는 한다: 마커가 낡은 것보다 DLL 이 없는 쪽이 훨씬 나쁘다.
	const bool can_write = marker_ok;

	const disk_state s = classify(has(M.fs, SELF), has(M.fs, NEWF), has(M.fs, BAKF), m);
	g_seen[static_cast<int>(s)]++;
	switch (decide_repair(s))
	{
	case repair_action::promote_pending:
		// self 는 유효한 DLL 이다. 어느 버전인지는 모르지만, 틀렸을 때 §5.2 게이트1 이
		// 잡아 주는 쪽이 안전모드를 켜는 쪽보다 훨씬 덜 해롭다(헤더 주석 참고).
		// ⚠️ tries=0 은 장식이 아니다 — 직전 롤백 마커의 tries=2 를 물려받은 채로 pending 을
		// 쓰면 새로 깐 멀쩡한 빌드가 **첫 부팅에** 롤백된다.
		if (can_write) { m.state = "pending"; m.tries = 0; write_marker(M.fs, m); }
		if (M.done("R3 promote_pending")) return;
		break;
	case repair_action::restore_from_bak:
		// ★ 마커 먼저, 파일 나중(self 가 없는 동안의 규칙). 순서를 뒤집으면 이동 직후
		// 끊겼을 때 (self 있음 + .bak 없음 + pending) 이 되는데, 이건 '새 버전이 정상적으로
		// 깔려 확정 대기 중' 과 구별이 안 되어 블랙리스트가 통째로 증발한다.
		if (can_write)
		{
			m.state = "rolledback"; m.bad_ver = m.version; m.bad_sha = m.sha;
			write_marker(M.fs, m);
			if (M.done("R10 마커 rolledback")) return;
		}
		check(mv(M.fs, BAKF, SELF), "R3 .sherbet-bak → self rename 실패");
		if (M.done("R3 .sherbet-bak → self")) return;
		break;
	case repair_action::restore_from_new:
		// ★ 마커 먼저, 파일 나중. 같은 규칙이다.
		// 마커는 pending/tries=0 으로 확정하고 bad_ver/bad_sha 는 그대로 둔다(헤더 규칙).
		// rolledback 을 남겨 두면 방금 되살린 바이너리로 안전모드가 켜져 효과가 통째로
		// 꺼진다 — 낡은 pending 은 게이트1 이 걸러 주지만 거짓 안전모드는 아무도 안 막는다.
		if (can_write)
		{
			m.state = "pending"; m.tries = 0;
			write_marker(M.fs, m);
			if (M.done("R3 마커 pending(재개)")) return;
		}
		check(mv(M.fs, NEWF, SELF), "R3 .sherbet-new → self rename 실패");
		if (M.done("R3 .sherbet-new → self")) return;
		break;
	case repair_action::give_up:
		// 재료가 없다. 아무 파일도 건드리지 않고 기록만 남긴다(§5.4 R8).
		if (can_write) { m.state = "rollback_failed"; write_marker(M.fs, m); }
		if (M.done("R8 rollback_failed")) return;
		break;
	case repair_action::none:
		break;
	}

	// 잔재 정리 — **self 가 있을 때만.** self 가 없으면 남은 파일이 유일한 DLL 사본일 수
	// 있다(.sherbet-new 는 물론이고, 여기 안 지워지는 .sherbet-bak.old 도 진짜 바이너리다).
	// '마지막 사본을 지우지 않는다' 가 이 청소의 유일한 규칙이다.
	// (.sherbet-bak 은 지우지 않는다 — 롤백 재료이고 S8 이 다음 교체 때 정리한다.
	//  .sherbet-failed 도 남긴다 — 증거 1개 보관이 §5.4 R9 의 요구다.)
	if (has(M.fs, SELF))
	{
		if (has(M.fs, NEWF))   { rm(M.fs, NEWF);   if (M.done("잔재 .sherbet-new 삭제")) return; }
		if (has(M.fs, BAKOLD)) { rm(M.fs, BAKOLD); if (M.done("잔재 .sherbet-bak.old 삭제")) return; }
		if (has(M.fs, NOTE))   { rm(M.fs, NOTE);   if (M.done("복구안내 삭제")) return; }
	}
}

// 중단 없이 한 번 돌린다.
static void repair_once(fs_t &fs)
{
	machine M(fs, 0);
	startup_repair(M);
	fs = M.fs;
}

// 고정점까지 반복. 수렴하지 않으면 실패다.
static int repair_to_fixpoint(fs_t &fs)
{
	for (int round = 1; round <= 8; ++round)
	{
		fs_t next = fs;
		repair_once(next);
		if (next == fs) return round;
		fs = next;
	}
	fail("복구가 8회 안에 수렴하지 않음(진동하거나 계속 파일을 바꾼다)");
	return -1;
}

// ── 롤백 절차(스펙 §5.4 R8~R10) ────────────────────────────────────────────
struct rollback_opts
{
	bool r9b_fails = false;      // .bak → self 가 거부된다(→ 첫 이동을 즉시 되돌린다)
	bool r9b_undo_fails = false; //   그 되돌리기마저 거부된다
};

static void run_rollback(machine &M, const rollback_opts &o = rollback_opts())
{
	boot_marker m;
	if (!read_marker(M.fs, m)) { M.aborted = true; return; }

	// R8 재료 검증이 먼저다. .bak 이 없으면 **아무 파일도 건드리지 않는다** —
	// self 를 먼저 밀어내고 뒤늦게 알면 디렉터리에 DLL 이 아예 없게 된다.
	if (!has(M.fs, BAKF))
	{
		m.state = "rollback_failed";
		write_marker(M.fs, m);
		M.done("R8 재료 없음 → rollback_failed");
		return;
	}

	// R9a 증거 보관은 1개만. 이미 있으면 밀어낸다.
	rm(M.fs, FAILED);
	check(mv(M.fs, SELF, FAILED), "R9 self → .sherbet-failed rename 실패");
	if (M.done("R9 self → .sherbet-failed")) return;

	// R10 을 여기서 쓴다(스펙 표의 순서와 다르다). self 가 이미 없으므로 마커를 먼저
	// 써도 잃을 게 없고, 뒤로 미루면 아래 rename 직후 끊겼을 때 '되돌렸는데 안 되돌렸다고
	// 적힌' 마커(= 블랙리스트 소실)가 남는다.
	m.state = "rolledback"; m.bad_ver = m.version; m.bad_sha = m.sha;
	write_marker(M.fs, m);
	if (M.done("R10 마커 rolledback")) return;

	if (!o.r9b_fails)
	{
		check(mv(M.fs, BAKF, SELF), "R9 .sherbet-bak → self rename 실패");
		if (M.done("R9 .sherbet-bak → self")) return;
		return;
	}

	// R9b 거부됨 → 스펙 R9 "두 번째 실패 시 첫 번째 즉시 되돌림".
	// ⚠️ 되돌리기 **전에** rollback_failed 를 기록한다. self 가 없는 지금 쓰는 것이 규칙이고,
	// 되돌린 뒤에 쓰면 그 사이에 끊겼을 때 (self=새 바이너리 + 마커 rolledback) 이 되어
	// 거짓 안전모드가 켜진 채로 **고정점**이 된다(복구가 손댈 이유를 못 찾는다).
	m.state = "rollback_failed";
	write_marker(M.fs, m);
	if (M.done("R9b 거부 → rollback_failed 기록")) return;

	if (o.r9b_undo_fails)
	{
		// 둘 다 실패. self 부재지만 .bak/.failed 가 남아 다음 실행이 되살릴 수 있다.
		M.done("R9b 거부 + 되돌리기 거부(self 부재)");
		return;
	}
	check(mv(M.fs, FAILED, SELF), "R9 되돌리기(.sherbet-failed → self)가 실패");
	if (M.done("R9 되돌리기 .sherbet-failed → self")) return;
}

// ── 시나리오 픽스처 ────────────────────────────────────────────────────────
// 0: 마커 없음(첫 설치)
// 1: 예전 롤백 마커 + 모르는 키
// 2: 파싱 불가 마커
// 3: **직전 롤백 마커(tries=2) + 모르는 키** — 롤백을 겪은 고객이 다시 업데이트하는 경우.
//    S9 가 tries 를 물려받으므로 pending 을 쓰는 모든 지점의 tries=0 리셋이 여기서 검증된다.
static const int kFixtures = 4;
static const char *const kUnparseable = "이건 마커가 아니라 그냥 쓰레기\n";
static const char *const kUnknownKey  = "future_key=42";

static bool fixture_has_unknown(int fixture) { return fixture == 1 || fixture == 3; }

static fs_t make_fixture(int fixture, const swap_opts &o)
{
	fs_t fs;
	fs[SELF] = "OLD";
	if (o.stale_bak) fs[BAKF] = "OLD";
	if (o.stale_bak && o.bakold_blocked) fs[BAKOLD] = "OLD";
	if (fixture == 1 || fixture == 3)
	{
		boot_marker m;
		m.state = "rolledback"; m.version = "1.2.0"; m.bad_ver = "1.2.0";
		m.bad_sha = "2222222222222222222222222222222222222222222222222222222222222222";
		if (fixture == 3) m.tries = 2;   // 그 롤백을 유발한 부팅 실패 2회가 그대로 남아 있다
		std::string text = serialize_marker(m);
		text += std::string(kUnknownKey) + "\n";   // 다른 writer 가 남긴 줄
		boot_marker rt;
		check(parse_marker(text, rt), "픽스처 마커가 파싱되지 않는다");
		check(rt.tries == m.tries, "픽스처의 tries 가 라운드트립에서 사라졌다");
		fs[MARK] = text;
	}
	else if (fixture == 2)
	{
		fs[MARK] = kUnparseable;
	}
	return fs;
}

// ── 부팅 한 번(§5.2 3중 게이트 + §5.4 R6 + decide_boot) ────────────────────
// running = 지금 매핑된 바이너리의 버전. 게이트2(exe)·게이트3(뮤텍스)는 이 시뮬레이터의
// 관심사가 아니라 통과했다고 본다.
static boot_action simulate_boot(fs_t &fs, const std::string &running)
{
	boot_marker on_disk;
	if (!read_marker(fs, on_disk)) return boot_action::none;
	if (on_disk.state != "pending") return boot_action::none;
	if (on_disk.version != running) return boot_action::none;   // ★ 게이트1
	// R6: tries+1 을 **먼저 디스크에 쓴다**(쓰기 전에 죽으면 카운트가 안 늘어 미탐).
	boot_marker persisted = on_disk;
	persisted.tries = on_disk.tries + 1;
	write_marker(fs, persisted);
	// ⚠️ 판정에는 **디스크에서 읽은 그대로**의 마커를 넘긴다(Task 7 계약).
	return decide_boot(on_disk);
}

static std::string version_of(const std::string &content) { return content == "NEW" ? NEWVER : OLDVER; }

// ★ 절차가 **스스로** 끝났다면(전원차단도, OS 거부도 없었다면) 반드시 self 를 남겨야 한다.
// self 가 없는 채로 세션이 끝나면 다음 실행에는 우리 DLL 이 **로드조차 되지 않아**
// startup_repair 가 영영 안 돈다(게임은 System32 폴백으로 켜지고 Sherbet 만 사라진다).
// §4.4 S11 의 '실패면 즉시 .bak → self 원복' 과 §5.4 R9 의 '두 번째 실패 시 첫 번째 즉시
// 되돌림' 이 존재하는 이유가 정확히 이것이다 — 이 두 보상 트랜잭션을 빼면 시뮬레이터의
// 다른 단언은 전부 통과한다(복구가 대신 고쳐 주니까). 여기서만 잡힌다.
static void check_session_left_self(const machine &M, bool os_refused)
{
	if (M.cut_hit) return;      // 전원차단은 아무것도 마무리할 기회를 주지 않는다
	if (os_refused) return;     // OS 가 되돌리기까지 거부한 경우(주입된 최악)
	check(has(M.fs, SELF), "절차가 스스로 끝났는데 self 가 없다 — 다음 실행에 복구 코드가 아예 안 돈다");
}

// P5: rolledback 은 '옛 바이너리로 되돌아가 있다' 는 뜻이고, 이 상태가 §5.4 R11 안전모드
// (효과 전면 중단)와 블랙리스트를 켠다. 그래서 거짓이면 고객이 산 기능이 사라진다.
// self 가 없으면 서술할 대상이 없으므로 공허하게 참이다.
static bool p5_holds(const fs_t &fs)
{
	boot_marker m;
	if (!read_marker(fs, m)) return true;
	if (m.state != "rolledback") return true;
	if (!has(fs, SELF)) return true;
	return get(fs, SELF) == "OLD";
}

// 복구가 절대 건드리면 안 되는 것들.
static void check_repair_preserved(const fs_t &before, const fs_t &after)
{
	// 롤백 재료: self 가 멀쩡히 있는데 .bak 을 지우면 그 설치는 되돌릴 수 없게 되고,
	// 그 사실은 두 번째 부팅 실패가 나서야 드러난다.
	if (has(before, BAKF) && has(before, SELF))
		check(has(after, BAKF), "복구가 롤백 재료(.sherbet-bak)를 지웠다");

	// 증거: §5.4 R9 가 '1개만' 남기기로 한 불량 바이너리. 고객 신고를 진단할 유일한 물증이고,
	// 복구는 이 파일에 아무 볼일이 없다. (롤백 자신이 되돌리기로 소비하는 것은 별개다)
	if (has(before, FAILED) && has(before, SELF))
	{
		check(has(after, FAILED), "복구가 §5.4 R9 증거(.sherbet-failed)를 지웠다");
		check(get(after, FAILED) == get(before, FAILED), "복구가 증거 파일의 내용을 바꿨다");
	}
}

// 수렴 상태에 대한 공통 단언.
static void check_converged(const fs_t &before, const fs_t &after, int fixture)
{
	// P1 게임이 켜진다
	check(has(after, SELF), "수렴 후 self 가 없다 — 고객 게임이 안 켜진다");
	const std::string self = get(after, SELF);
	check(self == "OLD" || self == "NEW", "수렴 후 self 내용이 옛것도 새것도 아니다: '" + self + "'");

	// P3 잔재 없음 / 보존해야 할 것 보존
	check(!has(after, NEWF), "수렴 후 .sherbet-new 잔재");
	check(!has(after, BAKOLD), "수렴 후 .sherbet-bak.old 잔재");
	check(!has(after, NOTE), "수렴 후 복구안내 잔재");
	check_repair_preserved(before, after);

	// P5 는 귀납적으로 — 복구가 성질을 **깨뜨리지 않는다**
	if (p5_holds(before))
		check(p5_holds(after), "복구가 P5 를 깨뜨렸다(rolledback 인데 self 가 새 바이너리)");

	// P4 마커 무결성
	if (fixture == 2)
	{
		check(get(after, MARK) == kUnparseable, "파싱 실패한 마커를 덮어썼다(다른 writer 의 키가 날아간다)");
		return;
	}
	if (!has(after, MARK))
	{
		// 마커가 없는 것은 정당한 최종 상태다: 아직 한 번도 안 썼거나(§4.4 S9 전 중단),
		// §5.3 부팅 성공 래치가 지웠거나, §5.4 R13 [그래도 다시 시도] 가 지웠다.
		// 그때 성립해야 하는 것은 '아무것도 세지 않는다' 뿐이다 — 롤백 장치가 무장 해제된
		// 상태이므로 부팅 판정이 조용히 none 이어야 한다(마커가 없으니 tries 도 없다).
		fs_t probe = after;
		check(simulate_boot(probe, version_of(self)) == boot_action::none, "마커가 없는데 부팅 판정이 돈다");
		check(probe == after, "마커가 없는데 부팅 경로가 디스크를 건드렸다");
		return;
	}
	boot_marker m;
	check(parse_marker(get(after, MARK), m), "수렴 후 마커가 파싱되지 않는다");
	if (fixture_has_unknown(fixture))
		check(get(after, MARK).find(kUnknownKey) != std::string::npos, "모르는 키가 사라졌다");

	// pending 은 '확정(S13) 전에 끊겨 어느 버전이 깔렸는지 디스크로 알 수 없는' 경우에
	// 한해 낡은 값일 수 있다. 그때 반드시 **무해**해야 한다:
	//   (1) §5.2 게이트1 이 버전 불일치를 잡아 몇 번을 부팅해도 롤백이 안 일어난다
	//   (2) 그 마커로 복구를 다시 돌려도 파일을 건드리는 판정이 나오지 않는다
	if (m.state == "pending")
	{
		check(m.tries == 0, "새로 쓴 pending 마커의 tries 가 0 이 아니다(첫 부팅에 롤백된다)");
		if (m.version != version_of(self))
		{
			fs_t probe = after;
			for (int i = 0; i < 5; ++i)
				check(simulate_boot(probe, version_of(self)) == boot_action::none,
					"낡은 마커가 멀쩡한 설치를 되돌리려 든다(게이트1 이 안 막는다)");
			check(decide_repair(classify(true, has(after, NEWF), has(after, BAKF), m)) == repair_action::none,
				"낡은 마커가 파일을 건드리는 판정으로 이어진다");
		}
		else
		{
			// 마커가 지금 깔린 바이너리를 정확히 서술하는 경우 — 롤백 장치가 제대로 무장돼
			// 있어야 한다. 첫 부팅 실패는 세기만 하고 되돌리지 않는다(임계값 2).
			fs_t probe = after;
			check(simulate_boot(probe, version_of(self)) == boot_action::count,
				"진실한 pending 마커인데 첫 부팅 실패가 count 가 아니다");
		}
	}
}

static void scene(const char *what, int a, int b, int c, int d)
{
	char buf[256];
	std::snprintf(buf, sizeof(buf), "%s cut=%d opt=%d fixture=%d repair_cut=%d", what, a, b, c, d);
	g_scene = buf;
}

// ── 1. 교체 중단 × 복구 중 2차 중단 전수 ───────────────────────────────────
static void test_swap_interruptions()
{
	static const swap_opts kOpts[7] = {
		swap_opts{ false, false, false },  // 0 묵은 .bak 없음
		swap_opts{ true,  false, false },  // 1 묵은 .bak 있고 지울 수 있다
		swap_opts{ true,  true,  false },  // 2 못 지워서 .sherbet-bak.old 로 밀어냄
		swap_opts{ true,  true,  true  },  // 3 밀어내기도 실패 → 교체 자체를 시작하지 않는다
		swap_opts{ false, false, false, /*s10_fails*/true },
		swap_opts{ false, false, false, false, /*s11_fails*/true },
		swap_opts{ false, false, false, false, true, /*s11_undo_fails*/true }
	};

	for (int swap_cut = 0; swap_cut <= 9; ++swap_cut)
		for (int opt = 0; opt < 7; ++opt)
			for (int fixture = 0; fixture < kFixtures; ++fixture)
				for (int repair_cut = 0; repair_cut <= 6; ++repair_cut)
				{
					scene("swap", swap_cut, opt, fixture, repair_cut);
					const fs_t start = make_fixture(fixture, kOpts[opt]);

					machine S(start, swap_cut);
					run_swap(S, kOpts[opt]);
					check_session_left_self(S, kOpts[opt].s11_undo_fails);

					// 시작조차 못 한 경우(파싱 불가 마커 / S8 총체적 실패)는 디스크가 그대로여야 한다.
					if (S.aborted && S.step == 0)
						check(S.fs == start, "교체를 시작하지 않았는데 디스크가 변했다");

					// 1차 중단 상태에서 복구를 돌리다 2차 중단.
					machine R(S.fs, repair_cut);
					startup_repair(R);
					check(recoverable(R.fs), "복구 중 2차 중단으로 DLL 소스가 전부 사라짐");

					// P2 수렴
					fs_t conv = R.fs;
					repair_to_fixpoint(conv);
					check_converged(S.fs, conv, fixture);

					// P2 멱등 — 두 번 더 돌려도 아무것도 안 바뀐다
					fs_t again = conv;
					repair_once(again);
					check(again == conv, "복구가 멱등이 아니다(1회차)");
					repair_once(again);
					check(again == conv, "복구가 멱등이 아니다(2회차)");

					// 중단 없이 완주했으면 새 버전이 깔려 있어야 한다
					if (swap_cut == 0 && !S.aborted)
					{
						check(get(conv, SELF) == "NEW", "완주했는데 새 바이너리가 안 깔렸다");
						// ★ 직전 롤백 마커(tries=2)를 물려받았더라도 첫 부팅에 롤백되면 안 된다
						fs_t probe = conv;
						check(simulate_boot(probe, NEWVER) == boot_action::count,
							"멀쩡한 새 빌드가 첫 부팅에 롤백된다(tries 리셋 누락)");
					}
					// rename 이 거부돼 원복까지 돈 경우에도 고객은 켤 수 있어야 한다
					if (kOpts[opt].s11_fails && !kOpts[opt].s11_undo_fails && swap_cut == 0)
						check(get(conv, SELF) == "OLD", "S11 원복 후 옛 바이너리가 안 남았다");
				}
}

// ── 2. 롤백 중단 × rename 거부 전수 ────────────────────────────────────────
// 롤백이 어떻게 끝났든 지켜야 하는 것.
static void check_rollback_outcome(const fs_t &fs)
{
	check(has(fs, SELF), "롤백 후 self 가 없다");
	boot_marker m;
	check(parse_marker(get(fs, MARK), m), "롤백 후 마커가 파싱되지 않는다");
	// P8 — 끊기든 거부되든 블랙리스트는 살아남는다
	check(m.bad_ver == NEWVER && m.bad_sha == NEWSHA, "롤백 후 블랙리스트가 비었다");
	info u;
	u.ok = true; u.version = NEWVER; u.sha256 = NEWSHA;
	check(!should_offer(version_of(get(fs, SELF)), u, m.bad_ver, m.bad_sha), "방금 되돌린 버전을 다시 제안한다");
	if (m.state == "rolledback")
		check(get(fs, SELF) == "OLD", "rolledback 인데 self 가 옛 바이너리가 아니다");
	else
		check(m.state == "rollback_failed",
			std::string("롤백 후 상태가 rolledback 도 rollback_failed 도 아니다: ") + m.state);
}

static void test_rollback_interruptions()
{
	static const rollback_opts kR[3] = {
		rollback_opts{ false, false },
		rollback_opts{ true,  false },   // .bak → self 거부 → 첫 이동 되돌림
		rollback_opts{ true,  true  }    // 되돌리기까지 거부
	};

	for (int rb_cut = 0; rb_cut <= 6; ++rb_cut)
		for (int ropt = 0; ropt < 3; ++ropt)
			for (int fixture = 0; fixture < 2; ++fixture)
				for (int repair_cut = 0; repair_cut <= 6; ++repair_cut)
				{
					scene("rollback", rb_cut, ropt, fixture, repair_cut);
					fs_t fs = make_fixture(fixture, swap_opts());
					{
						machine S(fs, 0);
						run_swap(S, swap_opts());
						fs = S.fs;
					}
					check(get(fs, SELF) == "NEW" && get(fs, BAKF) == "OLD", "롤백 전제 상태가 아니다");

					machine B(fs, rb_cut);
					run_rollback(B, kR[ropt]);
					check_session_left_self(B, kR[ropt].r9b_undo_fails);
					check(recoverable(B.fs), "롤백 중 중단으로 DLL 소스가 전부 사라짐");

					machine R(B.fs, repair_cut);
					startup_repair(R);
					check(recoverable(R.fs), "롤백 복구 중 2차 중단으로 DLL 소스가 전부 사라짐");

					fs_t conv = R.fs;
					repair_to_fixpoint(conv);
					check_converged(B.fs, conv, fixture);
					check_rollback_outcome(conv);

					fs_t again = conv;
					repair_once(again);
					check(again == conv, "롤백 후 복구가 멱등이 아니다");
				}
}

// ── 2b. S12 사후검증 불일치 → state==swapping 상태에서의 롤백 ──────────────
// S12 는 S11 과 S13 사이에서 돈다. 즉 **마커가 아직 swapping 인 채로 롤백이 시작되는**
// 유일한 경로이고, 다른 스위트의 롤백(pending 에서 시작)과 진입 상태가 다르다.
static void test_s12_mismatch_rollback()
{
	static const rollback_opts kR[3] = {
		rollback_opts{ false, false }, rollback_opts{ true, false }, rollback_opts{ true, true }
	};

	for (int rb_cut = 0; rb_cut <= 6; ++rb_cut)
		for (int ropt = 0; ropt < 3; ++ropt)
			for (int repair_cut = 0; repair_cut <= 6; ++repair_cut)
			{
				scene("S12", rb_cut, ropt, 0, repair_cut);
				fs_t fs = make_fixture(0, swap_opts());
				{
					swap_opts o;
					o.s12_mismatch = true;
					machine S(fs, 0);
					run_swap(S, o);
					fs = S.fs;
				}
				check(get(fs, SELF) == "NEW" && get(fs, BAKF) == "OLD", "S12 전제 상태가 아니다");
				boot_marker m0;
				check(read_marker(fs, m0) && m0.state == "swapping", "S12 는 확정(S13) 전에 돈다");

				machine B(fs, rb_cut);
				run_rollback(B, kR[ropt]);
				check_session_left_self(B, kR[ropt].r9b_undo_fails);
				check(recoverable(B.fs), "S12 롤백 중 중단으로 DLL 소스가 전부 사라짐");

				machine R(B.fs, repair_cut);
				startup_repair(R);
				check(recoverable(R.fs), "S12 롤백 복구 중 2차 중단으로 DLL 소스가 전부 사라짐");

				fs_t conv = R.fs;
				repair_to_fixpoint(conv);
				check_converged(B.fs, conv, 0);
				check_rollback_outcome(conv);
				check(!has(conv, NOTE), "S12 롤백 후 복구안내가 남았다");
			}
}

// ── 2c. 외부 간섭(AV 격리·사용자 삭제) ─────────────────────────────────────
// 전원차단만이 위험이 아니다. 교체 중 어느 지점에서 파일 하나가 통째로 사라져도
// 같은 성질이 성립해야 한다 — 여기서 resume_from_new 갈래가 실제로 밟힌다.
static void test_external_interference()
{
	static const char *const kVictims[] = { SELF, NEWF, BAKF, MARK };
	for (int swap_cut = 0; swap_cut <= 9; ++swap_cut)
		for (int v = 0; v < 4; ++v)
			for (int fixture = 0; fixture < kFixtures; ++fixture)
				for (int repair_cut = 0; repair_cut <= 6; ++repair_cut)
				{
					scene("간섭", swap_cut, v, fixture, repair_cut);
					fs_t fs = make_fixture(fixture, swap_opts());
					{
						machine S(fs, swap_cut);
						run_swap(S, swap_opts());
						fs = S.fs;
					}
					rm(fs, kVictims[v]);
					if (!recoverable(fs)) continue;   // 마지막 사본까지 지운 경우는 아래 probe 스위트 담당

					machine R(fs, repair_cut);
					startup_repair(R);
					check(recoverable(R.fs), "간섭 + 복구 중 중단으로 DLL 소스가 전부 사라짐");

					fs_t conv = R.fs;
					repair_to_fixpoint(conv);
					// 마커를 지운 경우는 픽스처의 마커 성질(모르는 키·파싱 불가)이 더 이상
					// 성립하지 않으므로 픽스처 0 으로 본다.
					check_converged(fs, conv, (kVictims[v] == MARK) ? 0 : fixture);

					fs_t again = conv;
					repair_once(again);
					check(again == conv, "간섭 후 복구가 멱등이 아니다");
				}
}

// ── 3. 전순성: 파일 8조합 × 모든 마커 상태 ─────────────────────────────────
static const char *const kMarkerStates[] = {
	"", "pending", "swapping", "rolledback", "rollback_failed", "알 수 없는 상태"
};
static const int kMarkerStateCount = 6;

static void test_totality()
{
	int visited = 0;
	for (int bits = 0; bits < 8; ++bits)
		for (int st = 0; st < kMarkerStateCount; ++st)
		{
			const bool hs = (bits & 1) != 0, hn = (bits & 2) != 0, hb = (bits & 4) != 0;
			char buf[160];
			std::snprintf(buf, sizeof(buf), "전순성 self=%d new=%d bak=%d state='%s'", hs, hn, hb, kMarkerStates[st]);
			g_scene = buf;

			boot_marker m;         // ⚠️ unknown 은 손으로 채우지 않는다(parse_marker 전용)
			m.state = kMarkerStates[st];
			m.version = NEWVER; m.sha = NEWSHA;

			const disk_state s = classify(hs, hn, hb, m);
			const repair_action a = decide_repair(s);
			++visited;

			// 각 동작이 요구하는 소스 파일이 실제로 존재해야 한다.
			switch (a)
			{
			case repair_action::restore_from_bak:
				check(hb, "restore_from_bak 인데 .sherbet-bak 이 없다");
				check(!hs, "restore_from_bak 인데 self 가 이미 있다(rename 이 실패한다)");
				break;
			case repair_action::restore_from_new:
				check(hn, "restore_from_new 인데 .sherbet-new 가 없다");
				check(!hs, "restore_from_new 인데 self 가 이미 있다(rename 이 실패한다)");
				break;
			case repair_action::promote_pending:
				check(hs, "마커만 고치는 동작인데 self 가 없다");
				break;
			case repair_action::give_up:
				check(!hs && !hn && !hb, "재료가 남아 있는데 포기한다");
				break;
			case repair_action::none:
				check(hs, "self 가 없는데 아무것도 하지 않는다");
				break;
			}

			// self 가 없고 재료가 있으면 반드시 되살리는 동작이어야 한다.
			if (!hs && (hn || hb))
				check(a == repair_action::restore_from_bak || a == repair_action::restore_from_new,
					"self 가 없고 재료가 있는데 복구 동작이 아니다");

			// 옛 것과 새 것이 모두 있으면 스펙 §5.4 R3 대로 옛 것을 고른다.
			if (!hs && hn && hb)
				check(a == repair_action::restore_from_bak, "재료가 둘 다 있는데 옛 것을 고르지 않았다");

			// 판정은 결정적이어야 한다.
			check(decide_repair(classify(hs, hn, hb, m)) == a, "같은 입력에 다른 판정");
		}
	check(visited == 8 * kMarkerStateCount, "전순성 순회가 빠졌다");
}

// ── 3b. 손으로 만든 상태 전수(생성기가 못 만드는 조합까지) ─────────────────
// 파일 4종(self/.new/.bak/.bak.old) × 마커 6상태 × self 내용 2가지를 직접 만들어,
// 복구가 (a) 마지막 사본을 지우지 않고 (b) 재료가 있으면 self 를 되살리고
// (c) P5 를 깨뜨리지 않고 (d) 고정점에 도달하는지 본다. 중단도 함께 주입한다.
static void test_probe_states()
{
	for (int bits = 0; bits < 16; ++bits)
		for (int st = 0; st < kMarkerStateCount; ++st)
			for (int selfnew = 0; selfnew < 2; ++selfnew)
			{
				fs_t start;
				if (bits & 1) start[SELF]   = selfnew ? "NEW" : "OLD";
				if (bits & 2) start[NEWF]   = "NEW";
				if (bits & 4) start[BAKF]   = "OLD";
				if (bits & 8) start[BAKOLD] = "OLD";
				if (*kMarkerStates[st])
				{
					boot_marker m;
					m.state = kMarkerStates[st]; m.version = NEWVER; m.sha = NEWSHA; m.tries = 1;
					start[MARK] = serialize_marker(m);
				}
				char buf[192];
				std::snprintf(buf, sizeof(buf), "probe self=%d(%s) new=%d bak=%d bakold=%d state='%s'",
					(bits & 1) != 0, selfnew ? "NEW" : "OLD", (bits & 2) != 0, (bits & 4) != 0,
					(bits & 8) != 0, kMarkerStates[st]);
				g_scene = buf;

				// 복구 도중 어디서 끊겨도 마지막 사본을 잃지 않는다
				for (int cut = 0; cut <= 6; ++cut)
				{
					machine R(start, cut);
					startup_repair(R);
					check_repair_preserved(start, R.fs);
					if (recoverable(start))
						check(recoverable(R.fs), "복구 중 중단으로 DLL 소스가 전부 사라짐");
				}

				fs_t conv = start;
				repair_to_fixpoint(conv);
				check_repair_preserved(start, conv);
				if (recoverable(start))
					check(has(conv, SELF), "재료가 있는데 복구가 self 를 되살리지 못했다");
				if (p5_holds(start))
					check(p5_holds(conv), "복구가 P5 를 깨뜨렸다(rolledback 인데 self 가 새 바이너리)");

				fs_t again = conv;
				repair_once(again);
				check(again == conv, "probe 복구가 멱등이 아니다");
			}
}

// ── 4. R6 쓰기 순서와 롤백 임계값(Task 7 계약) ─────────────────────────────
static void test_boot_threshold()
{
	g_scene = "부팅 실패 임계값";
	fs_t fs = make_fixture(0, swap_opts());
	{
		machine S(fs, 0);
		run_swap(S, swap_opts());
		fs = S.fs;
	}
	int rollback_at = -1;
	for (int boot = 1; boot <= 6 && rollback_at < 0; ++boot)
	{
		boot_marker on_disk;
		check(read_marker(fs, on_disk), "부팅 시 마커가 없다");
		const boot_action a = simulate_boot(fs, NEWVER);
		if (boot == 1)
		{
			check(a == boot_action::count, "첫 부팅 실패에서 벌써 롤백한다");
			// ⚠️ 이 한 줄이 계약의 실체다: 증가시킨 구조체를 decide_boot 에 넘기면
			// 임계값이 2 에서 1 로 반토막 나 알트탭 한 번에 멀쩡한 설치가 되돌아간다.
			boot_marker incremented = on_disk;
			incremented.tries = on_disk.tries + 1;
			check(decide_boot(incremented) == boot_action::rollback,
				"증가시킨 구조체를 넘기면 임계값이 반토막 난다는 사실 자체가 사라졌다");
		}
		if (a == boot_action::rollback) rollback_at = boot;
	}
	check(rollback_at == 2, "롤백은 2회째 부팅 실패에서 일어나야 한다");

	// 그 상태에서 실제로 롤백을 돌리면 옛 바이너리로 돌아온다.
	machine B(fs, 0);
	run_rollback(B);
	check(get(B.fs, SELF) == "OLD", "롤백 후 self 가 옛 바이너리가 아니다");
	check(get(B.fs, FAILED) == "NEW", "불량 바이너리를 증거로 남기지 않았다");

	// ★ 그 롤백 마커(tries=2)를 그대로 물려받아 다시 업데이트한다 — 새 빌드는 다시
	// 두 번의 기회를 받아야 한다. 여기서 tries 를 리셋하지 않으면 첫 부팅에 롤백된다.
	g_scene = "롤백 직후 재업데이트";
	fs_t after = B.fs;
	{
		boot_marker m;
		check(read_marker(after, m) && m.tries == 2, "롤백 마커가 tries=2 를 들고 있어야 한다");
		machine S2(after, 0);
		run_swap(S2, swap_opts());
		after = S2.fs;
	}
	check(get(after, SELF) == "NEW", "재업데이트가 완주하지 못했다");
	check(simulate_boot(after, NEWVER) == boot_action::count, "재업데이트한 빌드가 첫 부팅에 롤백된다");
	check(simulate_boot(after, NEWVER) == boot_action::rollback, "두 번째 부팅 실패에서 롤백해야 한다");

	// 교체가 S11 직후 끊겨 복구가 확정한 경우에도 같아야 한다(promote_pending 경로).
	g_scene = "롤백 직후 재업데이트 + S11 직후 중단";
	fs_t cut = B.fs;
	{
		machine S3(cut, 6);   // S11 직후 전원차단
		run_swap(S3, swap_opts());
		cut = S3.fs;
	}
	repair_to_fixpoint(cut);
	check(get(cut, SELF) == "NEW", "복구 후 새 바이너리가 안 깔렸다");
	check(simulate_boot(cut, NEWVER) == boot_action::count,
		"promote_pending 이 tries 를 리셋하지 않아 첫 부팅에 롤백된다");
}

// ── 5. 단위 시나리오 ───────────────────────────────────────────────────────
static void test_unit_scenarios()
{
	// 중단 없이 완주한 경우 정상 상태여야 한다
	{
		g_scene = "완주";
		fs_t fs = make_fixture(0, swap_opts());
		machine S(fs, 0);
		run_swap(S, swap_opts());
		check(get(S.fs, SELF) == "NEW", "완주 후 self 가 새 바이너리가 아니다");
		check(has(S.fs, BAKF) && get(S.fs, BAKF) == "OLD", "완주 후 .bak 에 옛 바이너리가 없다");
		check(!has(S.fs, NEWF), "완주 후 .sherbet-new 가 남았다");
		check(!has(S.fs, NOTE), "완주 후 복구안내가 남았다");
		boot_marker m;
		check(parse_marker(get(S.fs, MARK), m) && m.state == "pending" && m.tries == 0, "완주 후 마커가 pending/0 이 아니다");
	}

	// .bak 이 사라진 상태에서 롤백을 시도하면 아무 파일도 건드리지 않아야 한다
	{
		g_scene = ".bak 없는 롤백";
		boot_marker m;
		m.state = "pending"; m.version = NEWVER; m.sha = NEWSHA; m.tries = 1;
		fs_t fs;
		fs[MARK] = serialize_marker(m);
		fs[SELF] = "NEW";
		const disk_state s = classify(true, false, false, m);
		check(decide_repair(s) != repair_action::restore_from_bak, "없는 .bak 에서 복원하려 든다");

		machine B(fs, 0);
		run_rollback(B);
		check(get(B.fs, SELF) == "NEW", "재료가 없는데 self 를 건드렸다");
		check(!has(B.fs, FAILED), "재료가 없는데 self 를 밀어냈다");
		boot_marker after;
		check(parse_marker(get(B.fs, MARK), after) && after.state == "rollback_failed", "rollback_failed 를 남기지 않았다");

		// 그 뒤 복구를 돌려도 계속 아무것도 안 건드린다.
		fs_t conv = B.fs;
		repair_to_fixpoint(conv);
		check(get(conv, SELF) == "NEW", "복구가 self 를 건드렸다");
	}

	// 확정 전에 끊겨 낡아진 pending 마커는 무해하고, 다음 교체가 진실로 되돌려 놓는다
	{
		g_scene = "낡은 마커 치유";
		fs_t fs = make_fixture(0, swap_opts());
		{
			machine S(fs, 4);   // S9 마커(swapping) 직후 전원차단 — self 는 아직 옛 바이너리
			run_swap(S, swap_opts());
			fs = S.fs;
		}
		repair_to_fixpoint(fs);
		boot_marker stale;
		check(parse_marker(get(fs, MARK), stale), "복구 후 마커가 파싱되지 않는다");
		check(stale.state == "pending" && stale.version == NEWVER, "복구가 pending 으로 확정하지 않았다");
		check(get(fs, SELF) == "OLD", "S10 전에 끊겼는데 self 가 바뀌었다");
		// 몇 번을 부팅해도 게이트1 이 막아 롤백이 안 일어난다
		fs_t probe = fs;
		for (int i = 0; i < 5; ++i)
			check(simulate_boot(probe, OLDVER) == boot_action::none, "낡은 마커가 멀쩡한 설치를 되돌린다");
		// 다시 교체하면 진실한 마커로 돌아온다
		machine S2(fs, 0);
		run_swap(S2, swap_opts());
		boot_marker healed;
		check(parse_marker(get(S2.fs, MARK), healed), "재교체 후 마커가 파싱되지 않는다");
		check(healed.state == "pending" && healed.version == NEWVER && healed.tries == 0, "재교체가 마커를 치유하지 못했다");
		check(get(S2.fs, SELF) == "NEW", "재교체가 완주하지 못했다");
		check(simulate_boot(S2.fs, NEWVER) == boot_action::count, "치유된 마커에서 부팅 카운트가 안 돈다");
	}

	// DLL 소스가 하나도 없으면 아무 파일도 만들지 않고 기록만 남긴다(§5.4 R8)
	{
		g_scene = "재료 없음";
		fs_t fs;
		boot_marker m;
		m.state = "pending"; m.version = NEWVER; m.sha = NEWSHA; m.tries = 1;
		fs[MARK] = serialize_marker(m);
		machine M(fs, 0);
		startup_repair(M);
		check(M.fs.size() == 1, "DLL 이 하나도 없는데 파일을 만들어 냈다");
		boot_marker after;
		check(parse_marker(get(M.fs, MARK), after) && after.state == "rollback_failed", "rollback_failed 를 남기지 않았다");
		check(decide_boot(after) == boot_action::none, "포기한 뒤에도 부팅 실패를 세고 있다");
		fs_t conv = M.fs;
		repair_to_fixpoint(conv);
		check(conv == M.fs, "포기 상태가 고정점이 아니다");
	}

	// ★ 포기하더라도 남아 있는 진짜 바이너리(.sherbet-bak.old)를 지우면 안 된다.
	// give_up 은 'self/.new/.bak 이 없다' 는 뜻일 뿐, 디렉터리가 비었다는 뜻이 아니다.
	{
		g_scene = "포기 + .sherbet-bak.old 잔존";
		fs_t fs;
		boot_marker m;
		m.state = "pending"; m.version = NEWVER; m.sha = NEWSHA; m.tries = 1;
		fs[MARK] = serialize_marker(m);
		fs[BAKOLD] = "OLD";
		fs[NOTE] = "복구안내";
		const fs_t before = fs;
		repair_to_fixpoint(fs);
		check(get(fs, BAKOLD) == "OLD", "give_up 이 마지막 DLL 사본(.sherbet-bak.old)을 지웠다");
		check(has(fs, NOTE), "self 가 없는데 사람이 읽어야 할 복구안내를 지웠다");
	}

	// 파싱 불가 마커는 복구가 절대 덮어쓰지 않는다(모르는 키 보존 계약)
	{
		g_scene = "파싱 불가 마커";
		fs_t fs;
		fs[SELF] = "NEW";
		fs[NEWF] = "NEW";
		fs[MARK] = kUnparseable;
		repair_to_fixpoint(fs);
		check(get(fs, MARK) == kUnparseable, "파싱 실패한 마커를 덮어썼다");
		check(!has(fs, NEWF), "잔재를 치우지 않았다");
		check(get(fs, SELF) == "NEW", "self 를 건드렸다");
	}

	// 복구가 §5.4 R9 증거를 보존한다(고객 신고를 진단할 유일한 물증)
	{
		g_scene = "증거 보존";
		fs_t fs;
		fs[SELF] = "OLD";
		fs[FAILED] = "NEW";
		fs[NEWF] = "NEW";
		boot_marker m;
		m.state = "rolledback"; m.version = NEWVER; m.sha = NEWSHA;
		m.bad_ver = NEWVER; m.bad_sha = NEWSHA;
		fs[MARK] = serialize_marker(m);
		const fs_t before = fs;
		repair_to_fixpoint(fs);
		check_repair_preserved(before, fs);
		check(get(fs, FAILED) == "NEW", "복구가 증거(.sherbet-failed)를 지웠다");
		check(!has(fs, NEWF), "잔재는 치웠어야 한다");
	}
}

int main()
{
	test_totality();
	test_probe_states();
	test_swap_interruptions();
	test_external_interference();
	test_rollback_interruptions();
	test_s12_mismatch_rollback();
	test_boot_threshold();
	test_unit_scenarios();

	// 못 밟은 갈래는 검사된 적이 없는 갈래다 — 커버리지를 단언으로 못 박는다.
	g_scene = "커버리지";
	for (int i = 0; i < kStates; ++i)
	{
		const disk_state s = static_cast<disk_state>(i);
		check(g_seen[i] > 0, std::string("복구가 ") + state_name(s) + " 상태를 한 번도 안 밟았다");
	}
	std::printf("sherbet_swap_sim: 단언 %ld 개 통과, 복구 진입 상태 분포:\n", g_checks);
	for (int i = 0; i < kStates; ++i)
		std::printf("  %-20s %ld\n", state_name(static_cast<disk_state>(i)), g_seen[i]);
	std::printf("sherbet_swap_sim: ALL PASS\n");
	return 0;
}
