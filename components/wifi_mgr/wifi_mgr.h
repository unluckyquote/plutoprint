// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t wifi_mgr_start(void);
bool wifi_mgr_connected(void);
bool wifi_mgr_setup_mode(void);   /* true while no Wi-Fi is saved and the setup hotspot is up */

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_mgr_network_t;

/* Try new credentials. Saves them only if the connection works; otherwise restores the old ones. */
esp_err_t wifi_mgr_provision(const char *ssid, const char *password, uint32_t timeout_ms);
size_t wifi_mgr_scan(wifi_mgr_network_t *out, size_t max);
