// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Enter dim rain at 60 seconds idle. The display model powers down after
// ten minutes of continuous rain, or immediately when Off is selected.
#define OLED_TIMEOUT 0
#define OLED_UPDATE_INTERVAL 33
#ifndef LINK_OLED_IDLE_TIMEOUT
#    define LINK_OLED_IDLE_TIMEOUT 60000UL
#endif

#define SPLIT_TRANSACTION_IDS_USER LINK_OLED_SYNC
