// Copyright 2026 svenski88
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// QMK's usual OLED timeout is 60 seconds. Use it to enter rain instead of power
// saving; only manual OFF powers down the displays.
#define OLED_TIMEOUT 0
#define OLED_UPDATE_INTERVAL 33
#ifndef LINK_OLED_IDLE_TIMEOUT
#    define LINK_OLED_IDLE_TIMEOUT 60000UL
#endif

#define SPLIT_TRANSACTION_IDS_USER LINK_OLED_SYNC
