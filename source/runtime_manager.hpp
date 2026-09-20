/*
 * Copyright (C) 2024 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "reshade_api_device.hpp"

namespace reshade
{
	void create_effect_runtime(api::swapchain *swapchain, api::command_queue *graphics_queue, bool vr = false);
	void destroy_effect_runtime(api::swapchain *swapchain);

	void init_effect_runtime(api::swapchain *swapchain);
	void reset_effect_runtime(api::swapchain *swapchain);
	void present_effect_runtime(api::swapchain *swapchain);

	// SHERBET: 게임이 Present 에 넘긴 SyncInterval 을 런타임에 알린다(「최적화」 탭 상한 진단).
	// dxgi 프록시의 on_present 에서 DXGI_PRESENT_TEST 를 거른 뒤에 부른다.
	void sherbet_note_present_sync_interval(api::swapchain *swapchain, unsigned int sync_interval);
}
