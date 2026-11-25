// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "wifi_mgr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "settings.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_RECONNECT_DELAY_MS 2000

static const char *TAG = "wifi_mgr";
static EventGroupHandle_t wifi_events;
static TaskHandle_t reconnect_task;
static volatile bool connected;
static bool started;
static bool setup_mode;
static volatile bool provisioning;   /* new credentials are being tried: the background reconnect must stay out of the way */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        if (reconnect_task != NULL) {
            xTaskNotifyGive(reconnect_task);
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected; reconnecting");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        connected = true;
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void reconnect_task_fn(void *arg)
{
    (void)arg;

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (!connected) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
            if (connected) {
                break;
            }
            if (provisioning) {
                continue;
            }
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGW(TAG, "Wi-Fi reconnect failed: %s", esp_err_to_name(err));
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
        }
    }
}

static void fill_sta_config(wifi_config_t *config, const char *ssid, const char *password)
{
    memset(config, 0, sizeof(*config));
    strlcpy((char *)config->sta.ssid, ssid, sizeof(config->sta.ssid));
    strlcpy((char *)config->sta.password, password, sizeof(config->sta.password));
    config->sta.threshold.authmode = password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
}

static void start_setup_ap(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap = { 0 };
    int len = snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "PlutoPrint-%02X%02X", mac[4], mac[5]);
    ap.ap.ssid_len = len > 0 ? (uint8_t)len : 0;
    strlcpy((char *)ap.ap.password, "plutoprint", sizeof(ap.ap.password));   /* documented in the README */
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    setup_mode = true;
    ESP_LOGW(TAG, "No Wi-Fi saved: setup hotspot %s, open http://192.168.4.1/", (char *)ap.ap.ssid);
}

esp_err_t wifi_mgr_start(void)
{
    if (started) {
        return connected ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    wifi_events = xEventGroupCreate();
    if (wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    const settings_t *settings = settings_get();
    bool have_credentials = settings->ssid[0] != '\0';
    wifi_config_t config;
    fill_sta_config(&config, settings->ssid, settings->password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Mains-powered print server: keep the radio awake for low latency. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    if (xTaskCreate(reconnect_task_fn, "wifi_reconnect", 3072, NULL, 5,
                    &reconnect_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = true;
    if (!have_credentials) {
        start_setup_ap();
        return ESP_ERR_NOT_FOUND;
    }
    /* Do not wait for the connection: the serial WiFi setup must answer within a second or two of boot. */
    ESP_ERROR_CHECK(esp_wifi_connect());
    return ESP_OK;
}

bool wifi_mgr_connected(void)
{
    return connected;
}

bool wifi_mgr_setup_mode(void)
{
    return setup_mode && !connected;
}

esp_err_t wifi_mgr_provision(const char *ssid, const char *password, uint32_t timeout_ms)
{
    if (!started || ssid == NULL || password == NULL || ssid[0] == '\0' || strlen(ssid) > 32 ||
        strlen(password) > 63 || (password[0] != '\0' && strlen(password) < 8)) {
        return ESP_ERR_INVALID_ARG;
    }
    const settings_t *saved = settings_get();
    char old_ssid[sizeof(saved->ssid)], old_password[sizeof(saved->password)];
    strlcpy(old_ssid, saved->ssid, sizeof(old_ssid));
    strlcpy(old_password, saved->password, sizeof(old_password));

    wifi_config_t config;
    fill_sta_config(&config, ssid, password);
    provisioning = true;
    connected = false;
    xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_connect();
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        provisioning = false;
        esp_err_t err = settings_set_wifi(ssid, password);
        if (setup_mode) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            setup_mode = false;
        }
        return err;
    }
    /* Wrong password or out of range: go back to what worked before. */
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    if (old_ssid[0] != '\0') {
        fill_sta_config(&config, old_ssid, old_password);
        esp_wifi_set_config(WIFI_IF_STA, &config);
    }
    provisioning = false;
    esp_wifi_connect();
    return ESP_ERR_TIMEOUT;
}

/* Scan for networks. Returns the number written to out (strongest first, duplicates removed). */
size_t wifi_mgr_scan(wifi_mgr_network_t *out, size_t max)
{
    wifi_scan_config_t scan = { .show_hidden = false };
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        return 0;
    }
    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found > 24) found = 24;
    wifi_ap_record_t *records = calloc(found ? found : 1, sizeof(*records));
    if (records == NULL) {
        esp_wifi_clear_ap_list();
        return 0;
    }
    esp_wifi_scan_get_ap_records(&found, records);
    size_t count = 0;
    for (uint16_t i = 0; i < found && count < max; i++) {
        if (records[i].ssid[0] == '\0') continue;
        bool duplicate = false;
        for (size_t k = 0; k < count; k++) {
            if (strcmp(out[k].ssid, (char *)records[i].ssid) == 0) { duplicate = true; break; }
        }
        if (duplicate) continue;
        strlcpy(out[count].ssid, (char *)records[i].ssid, sizeof(out[count].ssid));
        out[count].rssi = records[i].rssi;
        out[count].secure = records[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    free(records);
    return count;
}
