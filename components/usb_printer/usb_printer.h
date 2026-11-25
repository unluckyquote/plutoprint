// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t usb_printer_init(void);
bool usb_printer_connected(void);
esp_err_t usb_printer_write(const uint8_t *data, size_t len, uint32_t timeout_ms);
// Reads use an internal packet-aligned buffer; received bytes beyond cap are dropped.
esp_err_t usb_printer_read(uint8_t *buf, size_t cap, size_t *got, uint32_t timeout_ms);
uint8_t usb_printer_port_status(void);
// Samsung vendor status (8 bytes: status and toner codes); ESP_OK on success.
esp_err_t usb_printer_samsung_status(uint8_t out[8]);
// IEEE 1284 device ID text (manufacturer, model, command sets). ESP_OK on success.
esp_err_t usb_printer_device_id(char *out, size_t cap);
// Samsung status monitor XML (paper sizes, trays, toner list). cap must exceed the file size, about 13 KB.
esp_err_t usb_printer_status_xml(char *out, size_t cap);
