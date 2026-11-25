// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    char name[32];
    char location[32];
    char hostname[32];
    char ssid[33];
    char password[65];
    bool password_set;           /* web UI password for changing actions */
    uint8_t color_mode;          /* 0 follow the printer, 1 black and white, 2 colour */
    bool duplex;                 /* offer two-sided printing */
    uint32_t jobs_clear_hours;   /* 0 = never; otherwise 6, 12, 24, 48 or 72 */
} settings_t;

esp_err_t settings_init(void);
const settings_t *settings_get(void);
esp_err_t settings_set_device(const char *name, const char *location,
                              const char *hostname);
esp_err_t settings_set_jobs_clear_hours(uint32_t hours);
esp_err_t settings_set_features(uint8_t color_mode, bool duplex);
esp_err_t settings_set_wifi(const char *ssid, const char *password);
/* Empty password removes protection. 6 to 64 characters otherwise. Only a salted hash is stored. */
esp_err_t settings_set_password(const char *password);
bool settings_password_ok(const char *password);
