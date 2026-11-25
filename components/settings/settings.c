// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "settings.h"

#include <stdbool.h>
#include <string.h>

#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"

#define SETTINGS_NAMESPACE "settings"

static const settings_t defaults = {
    .name = "Pluto Print",
    .location = "",
    .hostname = "printer",
    .ssid = "",
    .password = "",
};
static settings_t current;
static uint8_t pass_salt[16];
static uint8_t pass_hash[32];
static bool valid_hostname(const char *hostname);

static bool jobs_clear_hours_valid(uint32_t hours)
{
    return hours == 0 || hours == 6 || hours == 12 || hours == 24 || hours == 48 || hours == 72;
}

static esp_err_t load_string(nvs_handle_t handle, const char *key,
                             char *value, size_t size)
{
    size_t length = size;
    esp_err_t err = nvs_get_str(handle, key, value, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err == ESP_OK) {
        value[size - 1] = '\0';
    }
    return err;
}

esp_err_t settings_init(void)
{
    current = defaults;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = load_string(handle, "name", current.name, sizeof(current.name));
    if (err == ESP_OK) {
        err = load_string(handle, "location", current.location,
                          sizeof(current.location));
    }
    if (err == ESP_OK) {
        err = load_string(handle, "hostname", current.hostname,
                          sizeof(current.hostname));
        if (err == ESP_OK && !valid_hostname(current.hostname)) {
            strlcpy(current.hostname, defaults.hostname, sizeof(current.hostname));
        }
    }
    if (err == ESP_OK) {
        err = load_string(handle, "ssid", current.ssid, sizeof(current.ssid));
    }
    if (err == ESP_OK) {
        err = load_string(handle, "password", current.password,
                          sizeof(current.password));
    }
    if (err == ESP_OK) {
        uint16_t hours = 0;
        if (nvs_get_u16(handle, "clr_h", &hours) == ESP_OK && jobs_clear_hours_valid(hours)) {
            current.jobs_clear_hours = hours;
        }
    }
    if (err == ESP_OK) {
        uint8_t mode = 0, duplex = 0;
        if (nvs_get_u8(handle, "colormode", &mode) == ESP_OK && mode <= 2) current.color_mode = mode;
        if (nvs_get_u8(handle, "duplex", &duplex) == ESP_OK) current.duplex = duplex != 0;
    }
    if (err == ESP_OK) {
        size_t salt_len = sizeof(pass_salt), hash_len = sizeof(pass_hash);
        if (nvs_get_blob(handle, "pw_salt", pass_salt, &salt_len) == ESP_OK && salt_len == sizeof(pass_salt) &&
            nvs_get_blob(handle, "pw_hash", pass_hash, &hash_len) == ESP_OK && hash_len == sizeof(pass_hash)) {
            current.password_set = true;
        }
    }
    nvs_close(handle);
    return err;
}

const settings_t *settings_get(void)
{
    return &current;
}

static bool valid_hostname(const char *hostname)
{
    if (hostname == NULL) {
        return false;
    }
    size_t length = strlen(hostname);
    if (length == 0 || length > 31 || hostname[0] == '-' ||
        hostname[length - 1] == '-') {
        return false;
    }
    for (const char *p = hostname; *p != '\0'; ++p) {
        if (!(('a' <= *p && *p <= 'z') || ('0' <= *p && *p <= '9') ||
              *p == '-')) {
            return false;
        }
    }
    return true;
}

static esp_err_t save_device(const settings_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, "name", value->name);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "location", value->location);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "hostname", value->hostname);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t settings_set_device(const char *name, const char *location,
                              const char *hostname)
{
    if (name == NULL || location == NULL || hostname == NULL ||
        strlen(name) >= sizeof(current.name) ||
        strlen(location) >= sizeof(current.location) || !valid_hostname(hostname)) {
        return ESP_ERR_INVALID_ARG;
    }

    settings_t next = current;
    strlcpy(next.name, name, sizeof(next.name));
    strlcpy(next.location, location, sizeof(next.location));
    strlcpy(next.hostname, hostname, sizeof(next.hostname));

    esp_err_t err = save_device(&next);
    if (err == ESP_OK) {
        current = next;
    }
    return err;
}

esp_err_t settings_set_wifi(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ssid_length = strlen(ssid);
    size_t password_length = strlen(password);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 63 ||
        (password_length != 0 && password_length < 8)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "password", password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        strlcpy(current.ssid, ssid, sizeof(current.ssid));
        strlcpy(current.password, password, sizeof(current.password));
    }
    return err;
}

esp_err_t settings_set_jobs_clear_hours(uint32_t hours)
{
    if (!jobs_clear_hours_valid(hours)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u16(handle, "clr_h", (uint16_t)hours);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        current.jobs_clear_hours = hours;
    }
    return err;
}

static void hash_password(const char *password, const uint8_t salt[16], uint8_t out[32])
{
    uint8_t block[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, salt, 16);
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, block);
    for (int i = 0; i < 5000; i++) {   /* simple stretching; the CPU has a SHA accelerator */
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, block, 32);
        mbedtls_sha256_update(&ctx, salt, 16);
        mbedtls_sha256_finish(&ctx, block);
    }
    mbedtls_sha256_free(&ctx);
    memcpy(out, block, 32);
}

esp_err_t settings_set_password(const char *password)
{
    if (password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t length = strlen(password);
    if (length != 0 && (length < 6 || length > 64)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t salt[16], hash[32];
    if (length == 0) {
        nvs_erase_key(handle, "pw_salt");
        nvs_erase_key(handle, "pw_hash");
        err = nvs_commit(handle);
    } else {
        esp_fill_random(salt, sizeof(salt));
        hash_password(password, salt, hash);
        err = nvs_set_blob(handle, "pw_salt", salt, sizeof(salt));
        if (err == ESP_OK) err = nvs_set_blob(handle, "pw_hash", hash, sizeof(hash));
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK) {
        if (length == 0) {
            current.password_set = false;
        } else {
            memcpy(pass_salt, salt, sizeof(salt));
            memcpy(pass_hash, hash, sizeof(hash));
            current.password_set = true;
        }
    }
    return err;
}

bool settings_password_ok(const char *password)
{
    if (!current.password_set) {
        return true;
    }
    if (password == NULL || strlen(password) > 64) {
        return false;
    }
    uint8_t hash[32];
    hash_password(password, pass_salt, hash);
    uint8_t diff = 0;
    for (size_t i = 0; i < sizeof(hash); i++) diff |= hash[i] ^ pass_hash[i];
    return diff == 0;
}

esp_err_t settings_set_features(uint8_t color_mode, bool duplex)
{
    if (color_mode > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, "colormode", color_mode);
    if (err == ESP_OK) err = nvs_set_u8(handle, "duplex", duplex ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) {
        current.color_mode = color_mode;
        current.duplex = duplex;
    }
    return err;
}
