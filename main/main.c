// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "improv.h"
#include "logbuf.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "raw9100.h"
#include "ipp_server.h"
#include "print_mdns.h"
#include "printer_detect.h"
#include "settings.h"
#include "usb_printer.h"
#include "wifi_mgr.h"
#include "web_ui.h"

static const char *TAG = "main";

static void status_led_off(void)
{
    const int led_pins[] = {48, 38};
    for (size_t i = 0; i < sizeof(led_pins) / sizeof(led_pins[0]); i++) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = led_pins[i],
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
            .mem_block_symbols = 64,
            .flags.with_dma = false,
        };
        led_strip_handle_t led_strip = NULL;
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize status LED on GPIO %d: %s", led_pins[i],
                     esp_err_to_name(err));
            continue;
        }

        err = led_strip_clear(led_strip);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to clear status LED on GPIO %d: %s", led_pins[i],
                     esp_err_to_name(err));
        }
        err = led_strip_del(led_strip);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to release status LED on GPIO %d: %s", led_pins[i],
                     esp_err_to_name(err));
        }
    }
}

/* A freshly updated image that cannot start its services goes back to the previous one. */
static void startup_failed(void)
{
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

/* Holding the BOOT button for 10 seconds removes the web UI password. */
static void password_reset_task(void *arg)
{
    (void)arg;
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    int held = 0;
    for (;;) {
        held = gpio_get_level(GPIO_NUM_0) == 0 ? held + 1 : 0;
        if (held == 10 && settings_get()->password_set) {
            settings_set_password("");
            ESP_LOGW(TAG, "BOOT held for 10 s: web UI password removed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    logbuf_start();
    status_led_off();

    esp_err_t err = settings_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Settings startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    const settings_t *settings = settings_get();
    err = wifi_mgr_start();
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "No Wi-Fi saved: waiting for setup over the hotspot or the web flasher");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
    }
    err = improv_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Improv serial setup unavailable: %s", esp_err_to_name(err));
    }
    xTaskCreate(password_reset_task, "pw_reset", 3072, NULL, 3, NULL);
    err = print_mdns_start(settings->hostname, settings->name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    err = usb_printer_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB printer startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    err = raw9100_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "raw 9100 startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    err = ipp_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "IPP server startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    err = web_ui_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web UI startup failed: %s", esp_err_to_name(err));
        startup_failed();
        return;
    }

    /* Every service is up: keep this image. Until now a reset or a failed start returns to the previous image. */
    esp_ota_mark_app_valid_cancel_rollback();

    uint32_t last_ip = 0;
    bool previous_printer_state = false;
    while (true) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif != NULL && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != last_ip) {
            last_ip = ip_info.ip.addr;
            if (last_ip != 0) ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&ip_info.ip));
        }
        bool printer_state = usb_printer_connected();
        if (printer_state != previous_printer_state) {
            ESP_LOGI(TAG, "USB printer %s", printer_state ? "connected" : "disconnected");
            if (printer_state) printer_detect_run();
            else printer_detect_forget();
            previous_printer_state = printer_state;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
