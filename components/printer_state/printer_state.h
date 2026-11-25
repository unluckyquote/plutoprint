// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PRINTER_IDLE,
    PRINTER_PROCESSING,
    PRINTER_STOPPED,
} printer_state_t;

typedef struct {
    printer_state_t state;
    const char *reason;
} printer_status_t;

printer_status_t printer_state_map(bool usb_connected, bool job_active,
                                   uint8_t port_status);
