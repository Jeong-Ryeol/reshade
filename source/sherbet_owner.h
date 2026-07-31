/*
 * Copyright (C) 2026 정렬 (Jeong-Ryeol)
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * SHERBET — 구매자 개인화 상수.
 * 빌드 시 CI(workflow_dispatch)가 이 파일의 #define 값을 덮어써 주문별 빌드를 만든다.
 * 값이 비어 있으면(기본) 데모 빌드로 취급되어 개인화 문구가 숨겨진다.
 */
#pragma once

#ifndef SHERBET_OWNER
#define SHERBET_OWNER "" // 구매자 닉네임 (UTF-8)
#endif
#ifndef SHERBET_ORDER_NO
#define SHERBET_ORDER_NO "" // 주문번호
#endif
#ifndef SHERBET_DEFAULT_THEME
#define SHERBET_DEFAULT_THEME "mint" // 기본으로 열려 있는 테마 id
#endif

// 디스코드 초대 링크 (스플래시/정보/마켓/노드락 안내 공용)
#ifndef SHERBET_DISCORD_URL
#define SHERBET_DISCORD_URL "https://discord.gg/5NGR7XVFta"
#endif

// 노드락(첫 실행 PC 고정). 기본 0(꺼짐) — 실제 PC에서 검증 후 주문 빌드에서만 1로 켠다.
// 켜져 있으면 첫 실행 시 DLL 옆 sherbet.lic 에 HWID 서명을 기록하고,
// 이후 다른 PC에서는 오버레이 대신 안내문만 표시한다(게임/DLL 자체는 정상 동작).
#ifndef SHERBET_NODELOCK
#define SHERBET_NODELOCK 0
#endif

// 온라인 인증 게이트. 기본 0(꺼짐/개발) — 배포 빌드에서만 1로 켠다.
// 켜져 있으면 네트워크를 통해 주문 유효성을 검증한다.
#ifndef SHERBET_ONLINE_AUTH
#define SHERBET_ONLINE_AUTH 0
#endif

// Sherbet 자체 버전. 업스트림 리쉐이드 버전(res/version.h)과 완전히 별개다.
// res/version.h 는 git describe --tags 기반인데 sherbet-base 에는 조상 태그가 없어
// 항상 0.0.0.N 으로 떨어지고 32/64비트가 서로 달라 식별자로 쓸 수 없다.
//
// ⚠️ 릴리스 태그는 반드시 `sherbet-1.0.0` 형식(v 금지). `sherbet-v1.0.0` 은
//    tools/update_version.ps1 의 앵커 없는 -match 에 걸려 res/version.h 를 탈취한다.
// ⚠️ 이 값과 태그 접미사가 다르면 release.yml 이 빌드를 실패시킨다.
#ifndef SHERBET_VERSION
#define SHERBET_VERSION "1.6.4"
#endif

namespace sherbet
{
	inline bool has_owner()
	{
		return SHERBET_OWNER[0] != '\0';
	}
}
