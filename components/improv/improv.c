// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "improv.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"
#include "wifi_mgr.h"

#define IMPROV_UART UART_NUM_0
#define VERSION 1
#define TYPE_STATE 0x01
#define TYPE_ERROR 0x02
#define TYPE_RPC 0x03
#define TYPE_RESULT 0x04
#define STATE_READY 0x02
#define STATE_PROVISIONING 0x03
#define STATE_PROVISIONED 0x04
#define ERR_NONE 0x00
#define ERR_INVALID_RPC 0x01
#define ERR_UNKNOWN_RPC 0x02
#define ERR_UNABLE_TO_CONNECT 0x03
#define CMD_WIFI 0x01
#define CMD_STATE 0x02
#define CMD_INFO 0x03
#define CMD_SCAN 0x04
#define MAX_DATA 255

static const char *TAG = "improv";
static const uint8_t header[] = { 'I', 'M', 'P', 'R', 'O', 'V' };

static void send_packet(uint8_t type, const uint8_t *data, size_t len)
{
    uint8_t packet[6 + 3 + MAX_DATA + 1];
    size_t at = 0;
    memcpy(packet, header, sizeof(header));
    at = sizeof(header);
    packet[at++] = VERSION;
    packet[at++] = type;
    packet[at++] = (uint8_t)len;
    memcpy(packet + at, data, len);
    at += len;
    uint8_t sum = 0;
    for (size_t i = 0; i < at; i++) sum = (uint8_t)(sum + packet[i]);
    packet[at++] = sum;
    packet[at++] = '\n';
    uart_write_bytes(IMPROV_UART, packet, at);
}

static void send_state(uint8_t state) { send_packet(TYPE_STATE, &state, 1); }
static void send_error(uint8_t error) { send_packet(TYPE_ERROR, &error, 1); }

/* RPC result: [command][length][len,string]... */
static void send_result(uint8_t command, const char *const *strings, size_t count)
{
    uint8_t data[MAX_DATA];
    size_t at = 2;
    data[0] = command;
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(strings[i]);
        if (at + 1 + n > MAX_DATA) break;
        data[at++] = (uint8_t)n;
        memcpy(data + at, strings[i], n);
        at += n;
    }
    data[1] = (uint8_t)(at - 2);
    send_packet(TYPE_RESULT, data, at);
}

static void device_url(char *out, size_t cap)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
        snprintf(out, cap, "http://" IPSTR "/", IP2STR(&ip.ip));
    } else {
        snprintf(out, cap, "http://%s.local/", settings_get()->hostname);
    }
}

static void send_provisioned(uint8_t command)
{
    char url[64];
    device_url(url, sizeof(url));
    const char *strings[] = { url };
    send_result(command, strings, 1);
}

static void handle_wifi(const uint8_t *data, size_t len)
{
    char ssid[33] = "", password[65] = "";
    if (len < 2) { send_error(ERR_INVALID_RPC); return; }
    size_t ssid_len = data[0];
    if (1 + ssid_len + 1 > len || ssid_len > 32) { send_error(ERR_INVALID_RPC); return; }
    memcpy(ssid, data + 1, ssid_len);
    size_t password_len = data[1 + ssid_len];
    if (1 + ssid_len + 1 + password_len > len || password_len > 63) { send_error(ERR_INVALID_RPC); return; }
    memcpy(password, data + 2 + ssid_len, password_len);

    send_state(STATE_PROVISIONING);
    if (wifi_mgr_provision(ssid, password, 20000) == ESP_OK) {
        send_state(STATE_PROVISIONED);
        send_provisioned(CMD_WIFI);
    } else {
        send_error(ERR_UNABLE_TO_CONNECT);
        send_state(STATE_READY);
    }
}

static void handle_info(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const char *strings[] = { "Pluto Print", app->version, "ESP32-S3", settings_get()->name };
    send_result(CMD_INFO, strings, 4);
}

static void handle_scan(void)
{
    static wifi_mgr_network_t networks[16];
    size_t count = wifi_mgr_scan(networks, 16);
    for (size_t i = 0; i < count; i++) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", networks[i].rssi);
        const char *strings[] = { networks[i].ssid, rssi, networks[i].secure ? "YES" : "NO" };
        send_result(CMD_SCAN, strings, 3);
    }
    send_result(CMD_SCAN, NULL, 0);   /* empty result ends the list */
}

static void handle_rpc(const uint8_t *data, size_t len)
{
    if (len < 2 || (size_t)data[1] + 2 > len) { send_error(ERR_INVALID_RPC); return; }
    send_error(ERR_NONE);
    switch (data[0]) {
    case CMD_WIFI: handle_wifi(data + 2, data[1]); break;
    case CMD_STATE:
        send_state(wifi_mgr_connected() ? STATE_PROVISIONED : STATE_READY);
        if (wifi_mgr_connected()) send_provisioned(CMD_STATE);
        break;
    case CMD_INFO: handle_info(); break;
    case CMD_SCAN: handle_scan(); break;
    default: send_error(ERR_UNKNOWN_RPC); break;
    }
}

static void improv_task(void *arg)
{
    (void)arg;
    uint8_t packet[6 + 3 + MAX_DATA + 1];
    size_t at = 0;
    for (;;) {
        uint8_t byte;
        if (uart_read_bytes(IMPROV_UART, &byte, 1, portMAX_DELAY) != 1) continue;
        if (at < sizeof(header)) {
            /* Log lines share this port: only a run of the header bytes starts a packet. */
            if (byte == header[at]) packet[at++] = byte; else at = (byte == header[0]) ? (packet[0] = byte, 1) : 0;
            continue;
        }
        packet[at++] = byte;
        if (at < 9) continue;
        size_t total = 9 + packet[8] + 1;
        if (at < total) continue;
        uint8_t sum = 0;
        for (size_t i = 0; i < total - 1; i++) sum = (uint8_t)(sum + packet[i]);
        if (sum == packet[total - 1] && packet[6] == VERSION && packet[7] == TYPE_RPC) {
            handle_rpc(packet + 9, packet[8]);
        }
        at = 0;
    }
}

esp_err_t improv_start(void)
{
    esp_err_t err = uart_driver_install(IMPROV_UART, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (xTaskCreate(improv_task, "improv", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Improv Wi-Fi serial ready");
    return ESP_OK;
}
