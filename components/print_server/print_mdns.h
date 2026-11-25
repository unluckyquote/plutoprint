// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stddef.h>

#include "esp_err.h"

/* Wi-Fi MAC-derived UUID, without the urn:uuid: prefix; needs 37 bytes. */
esp_err_t print_mdns_uuid(char *uuid, size_t cap);
esp_err_t print_mdns_start(const char *hostname, const char *instance_name);
void print_mdns_refresh(void);
