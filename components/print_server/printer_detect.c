// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "printer_detect.h"

#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "print_mdns.h"
#include "printer_caps.h"
#include "usb_printer.h"

#define XML_CAP (24 * 1024)

static const char *TAG = "printer_detect";

void printer_detect_run(void)
{
    printer_caps_t caps;
    char id[256];
    memset(&caps, 0, sizeof(caps));

    if (usb_printer_device_id(id, sizeof(id)) == ESP_OK) {
        ESP_LOGI(TAG, "device id: %s", id);
        printer_caps_parse_device_id(id, &caps);
    } else {
        ESP_LOGW(TAG, "device id unavailable");
    }

    char *xml = heap_caps_malloc(XML_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (xml != NULL) {
        if (usb_printer_status_xml(xml, XML_CAP) == ESP_OK) {
            const char *start = strstr(xml, "<?xml");
            printer_caps_parse_status_xml(start != NULL ? start : xml, &caps);
        } else {
            ESP_LOGW(TAG, "status file unavailable");
        }
        free(xml);
    }

    printer_caps_set(&caps);
    ESP_LOGI(TAG, "model '%s' urf=%d pwg=%d sizes=%u trays=%u color toner=%d", caps.model, caps.urf, caps.pwg,
             caps.media_count, caps.tray_count, printer_caps_has_color_toner(&caps));
    print_mdns_refresh();
}

void printer_detect_forget(void)
{
    printer_caps_clear();
    print_mdns_refresh();
}
