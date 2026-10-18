// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Enter dim rain at 60 seconds idle. Dedicated rain uses the selected normal
// brightness while active. All continuous rain powers down after ten minutes.
#define OLED_TIMEOUT 0
#define OLED_UPDATE_INTERVAL 33
#ifndef LINK_OLED_IDLE_TIMEOUT
#    define LINK_OLED_IDLE_TIMEOUT 60000UL
#endif

#define SPLIT_TRANSACTION_IDS_USER LINK_OLED_SYNC
// Protocol 3 includes the effective brightness and all active reactive drops.
#define RPC_M2S_BUFFER_SIZE 64
