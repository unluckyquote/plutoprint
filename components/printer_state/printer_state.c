// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "printer_state.h"

printer_status_t printer_state_map(bool usb_connected, bool job_active,
                                   uint8_t port_status)
{
    if (!usb_connected) {
        return (printer_status_t){ PRINTER_STOPPED, "offline-report" };
    }
    if (port_status != 0xff && (port_status & 0x20) != 0) {
        return (printer_status_t){ PRINTER_STOPPED, "media-empty-error" };
    }
    if (port_status != 0xff && (port_status & 0x08) == 0) {
        return (printer_status_t){ PRINTER_STOPPED, "other-error" };
    }
    if (job_active) {
        return (printer_status_t){ PRINTER_PROCESSING, "none" };
    }
    return (printer_status_t){ PRINTER_IDLE, "none" };
}
