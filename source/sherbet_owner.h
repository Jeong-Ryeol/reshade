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

namespace sherbet
{
	inline bool has_owner()
	{
		return SHERBET_OWNER[0] != '\0';
	}
}
