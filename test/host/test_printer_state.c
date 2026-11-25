// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "printer_state.h"

static void assert_status(printer_status_t status, printer_state_t state,
                          const char *reason)
{
    assert(status.state == state);
    assert(strcmp(status.reason, reason) == 0);
}

int main(void)
{
    assert_status(printer_state_map(false, false, 0x08), PRINTER_STOPPED,
                  "offline-report");
    assert_status(printer_state_map(false, true, 0x20), PRINTER_STOPPED,
                  "offline-report");
    assert_status(printer_state_map(true, false, 0x28), PRINTER_STOPPED,
                  "media-empty-error");
    assert_status(printer_state_map(true, true, 0x28), PRINTER_STOPPED,
                  "media-empty-error");
    assert_status(printer_state_map(true, false, 0x00), PRINTER_STOPPED,
                  "other-error");
    assert_status(printer_state_map(true, true, 0x00), PRINTER_STOPPED,
                  "other-error");
    assert_status(printer_state_map(true, true, 0x08), PRINTER_PROCESSING,
                  "none");
    assert_status(printer_state_map(true, false, 0x08), PRINTER_IDLE, "none");
    assert_status(printer_state_map(true, true, 0xff), PRINTER_PROCESSING,
                  "none");
    assert_status(printer_state_map(true, false, 0xff), PRINTER_IDLE, "none");
    assert_status(printer_state_map(true, true, 0x18), PRINTER_PROCESSING,
                  "none");
    return 0;
}
