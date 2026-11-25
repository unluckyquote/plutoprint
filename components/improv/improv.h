// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include "esp_err.h"

/* Improv Wi-Fi over the serial port (https://www.improv-wifi.com/serial/): lets the web flasher
 * scan for networks and send Wi-Fi credentials right after flashing. */
esp_err_t improv_start(void);
