/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <atomic>
#include <string>

namespace sherbet
{
	namespace http
	{
		// ── 절단(truncation) 규약 — 아래 세 함수가 같은 코드 경로로 공유한다 ────────────────
		// * InternetReadFile 이 루프 중간에 false 를 반환하면 EOF 가 아니라 **절단**이다.
		//   (네트워크 오류/타임아웃) EOF 로 눙치면 잘린 셰이더가 디스크에 남아 컴파일 실패와
		//   드라이버(dxgi) 크래시로 이어진다 — 여러 파일을 연속 다운로드할 때 특히 잘 터진다.
		// * Content-Length 헤더가 **있는데** 수신량과 다르면 절단이다.
		//   (헤더가 없는 것은 절단이 아니다 — chunked 응답이 정상적으로 존재한다.)
		// * 절단이면 out 을 비우고(get_to_file 은 dest 를 지우고) **0** 을 반환한다.
		//   → 호출자는 `status != 200` 하나만 보면 잘린 바디를 절대 저장하지 않는다.
		// ────────────────────────────────────────────────────────────────────────────────

		// HTTPS(443). 반환=HTTP 상태코드. 0=전송 실패 또는 상태코드 판독 불가(둘 다 '서버 사용 불가'로 취급).
		// 호출자는 0과 5xx를 재시도/오프라인 신호로 다뤄야 한다. out=응답 바디.
		// 타임아웃 5초(연결/송신/수신) — 수십 KB fx 기준.
		int post_json(const wchar_t *host, const wchar_t *path, const std::string &json_body, std::string &out, const char *bearer = nullptr);
		int get(const wchar_t *host, const wchar_t *path_query, std::string &out, const char *bearer = nullptr);

		// 응답 바디를 메모리가 아니라 dest 파일로 흘려 쓴다(4MB DLL 다운로드용). 진행률 콜백과 취소 지원.
		// 반환 = HTTP 상태코드(위 규약 그대로: 0 = 전송 실패·절단·취소·상한 초과·크기 불일치).
		//
		// 계약:
		// * 바디를 통째로 메모리에 들지 않는다. 기존 get() 은 out.append() 로 무한정 커져서,
		//   잘못된 URL 이 큰 HTML 에러 페이지를 주면 게임 프로세스가 bad_alloc 으로 죽는다.
		// * **32MiB 하드 상한을 읽기 루프 안에서** 검사한다. 넘으면 즉시 중단하고 dest 를 지운 뒤 0.
		//   (상한은 매니페스트 size 상한과 같다 — 그보다 큰 응답은 우리 DLL 일 수 없다.)
		// * expect_size != 0 이면 그보다 많이 받는 즉시 중단하고, 다 받은 뒤에도 정확히 일치하지 않으면
		//   dest 를 지우고 0 을 반환한다. expect_size == 0 이면 크기 검사를 하지 않는다.
		// * 200 이 아닌 모든 반환(0 포함)에서 dest 는 **남지 않는다.** 부분 파일을 절대 남기지 않는다.
		// * 타임아웃은 이 함수만 다르다: 20초(연결/송신/수신) + 전체 5분 캡. 기존 5초는 fx 기준이라
		//   4MB 에 부족하다. post_json/get 의 5초는 건드리지 않는다.
		// * Content-Length 부재를 실패로 보지 않는다 — GitHub 리다이렉트 종착지가 chunked 일 수 있다.
		//   **최종 게이트는 호출자의 size + sha256 이다.**
		// * on_chunk(ctx, received, total) 는 UI 진행률용. total 은 Content-Length, 없으면 expect_size,
		//   둘 다 없으면 0. nullptr 가능. 워커 스레드에서 청크마다 불리므로 ImGui 를 만지면 안 된다.
		// * cancel 은 청크마다 확인한다. true 가 되면 즉시 중단, dest 삭제, 0 반환. nullptr 가능.
		int get_to_file(const wchar_t *host, const wchar_t *path, const wchar_t *dest,
			unsigned long long expect_size,
			void *ctx, void (*on_chunk)(void *, unsigned long long, unsigned long long),
			const std::atomic<bool> *cancel);
	}
}
