// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "print_mdns.h"

#include <stdio.h>
#include <string.h>

#include "esp_wifi.h"
#include "mdns.h"
#include "printer_features.h"

esp_err_t print_mdns_uuid(char *uuid, size_t cap)
{
    if (uuid == NULL || cap < 37) return ESP_ERR_INVALID_ARG;
    uuid[0] = '\0';
    uint8_t mac[6];
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK) return err;
    snprintf(uuid, cap, "00000000-0000-1000-8000-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t print_mdns_start(const char *hostname, const char *instance_name)
{
    if (hostname == NULL || instance_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        return err;
    }

    err = mdns_hostname_set(hostname);
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    err = mdns_instance_name_set(instance_name);
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    char uuid[37];
    err = print_mdns_uuid(uuid, sizeof(uuid));
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    char adminurl[64];
    snprintf(adminurl, sizeof(adminurl), "http://%s.local/", hostname);

    char urf[96];
    if (printer_features_urf(urf, sizeof(urf)) == 0) strlcpy(urf, "V1.4,CP1,W8,RS600", sizeof(urf));

    mdns_txt_item_t txt[] = {
        {"txtvers", "1"},
        {"qtotal", "1"},
        {"rp", "ipp/print"},
        {"ty", "Pluto Print"},
        {"product", "(Pluto Print)"},
        {"pdl", "image/urf,image/pwg-raster"},
        {"URF", urf},
        {"Color", printer_features_color() ? "T" : "F"},
        {"Duplex", printer_features_duplex() ? "T" : "F"},
        {"Copies", "T"},
        {"TLS", "none"},
        {"kind", "document"},
        {"UUID", uuid},
        {"adminurl", adminurl},
        {"note", ""},
    };
    const size_t txt_count = sizeof(txt) / sizeof(txt[0]);

    err = mdns_service_add(NULL, "_ipp", "_tcp", 631, txt, txt_count);
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    err = mdns_service_subtype_add_for_host(NULL, "_ipp", "_tcp", hostname, "_universal");
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    err = mdns_service_add(instance_name, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        mdns_free();
        return err;
    }

    return ESP_OK;
}

/* Re-announce what the printer can do after it was detected or the settings changed. */
void print_mdns_refresh(void)
{
    char urf[96];
    if (printer_features_urf(urf, sizeof(urf)) == 0) return;
    mdns_service_txt_item_set("_ipp", "_tcp", "URF", urf);
    mdns_service_txt_item_set("_ipp", "_tcp", "Color", printer_features_color() ? "T" : "F");
    mdns_service_txt_item_set("_ipp", "_tcp", "Duplex", printer_features_duplex() ? "T" : "F");
}
