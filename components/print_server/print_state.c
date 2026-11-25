// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "print_state.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "ipp_server.h"
#include "usb_printer.h"

static const char *TAG = "print_state";
static uint8_t s_cached_port_status = 0xff;
static int64_t s_last_port_poll_us;

printer_status_t print_state_get(void)
{
    bool job_active = ipp_server_job_active();
    int64_t now = esp_timer_get_time();

    if (!job_active &&
        (s_last_port_poll_us == 0 || now - s_last_port_poll_us >= 1000000)) {
        uint8_t status = usb_printer_port_status();
        if (status != s_cached_port_status) {
            ESP_LOGI(TAG, "port status 0x%02x", status);
        }
        s_cached_port_status = status;
        s_last_port_poll_us = now;
    }
    return printer_state_map(usb_printer_connected(), job_active,
                             job_active ? 0xff : s_cached_port_status);
}

int print_state_toner_percent(void)
{
    static int s_toner = -1;
    static int64_t s_last_poll_us;
    int64_t now = esp_timer_get_time();

    if (!usb_printer_connected()) {
        s_toner = -1;
        s_last_poll_us = 0;
        return -1;
    }
    if (!ipp_server_job_active() && (s_last_poll_us == 0 || now - s_last_poll_us >= 10000000)) {
        /* Samsung vendor status: byte 0 is 0x01 when online, byte 4 is the toner percentage. */
        uint8_t raw[8];
        s_last_poll_us = now;
        if (usb_printer_samsung_status(raw) == ESP_OK && raw[0] == 0x01 && raw[4] <= 100) {
            if (raw[4] != s_toner) ESP_LOGI(TAG, "toner %u%%", raw[4]);
            s_toner = raw[4];
        }
    }
    return s_toner;
}
