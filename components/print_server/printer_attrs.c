// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "printer_attrs.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_timer.h"
#include "settings.h"
#include "print_mdns.h"
#include "print_state.h"
#include "printer_features.h"

static void write_range(ipp_writer_t *w, const char *name, int32_t low, int32_t high)
{
    uint32_t a = (uint32_t)low;
    uint32_t b = (uint32_t)high;
    uint8_t value[8] = {
        (uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a,
        (uint8_t)(b >> 24), (uint8_t)(b >> 16), (uint8_t)(b >> 8), (uint8_t)b,
    };
    size_t name_len = strlen(name);

    if (name_len > UINT16_MAX || w->overflow || w->len > w->cap ||
        w->cap - w->len < 1 + 2 + name_len + 2 + sizeof(value)) {
        w->overflow = true;
        return;
    }
    w->buf[w->len++] = 0x33;
    w->buf[w->len++] = (uint8_t)(name_len >> 8);
    w->buf[w->len++] = (uint8_t)name_len;
    memcpy(w->buf + w->len, name, name_len);
    w->len += name_len;
    w->buf[w->len++] = 0;
    w->buf[w->len++] = sizeof(value);
    memcpy(w->buf + w->len, value, sizeof(value));
    w->len += sizeof(value);
}

static void write_enum(ipp_writer_t *w, const char *name, int32_t value)
{
    ipp_w_int(w, 0x23, name, value);
}

static void write_enum_more(ipp_writer_t *w, int32_t value)
{
    uint32_t encoded = (uint32_t)value;

    if (w->overflow || w->len > w->cap || w->cap - w->len < 9) {
        w->overflow = true;
        return;
    }
    w->buf[w->len++] = 0x23;
    w->buf[w->len++] = 0;
    w->buf[w->len++] = 0;
    w->buf[w->len++] = 0;
    w->buf[w->len++] = 4;
    w->buf[w->len++] = (uint8_t)(encoded >> 24);
    w->buf[w->len++] = (uint8_t)(encoded >> 16);
    w->buf[w->len++] = (uint8_t)(encoded >> 8);
    w->buf[w->len++] = (uint8_t)encoded;
}

/* Page size in hundredths of a millimetre from a PWG media name such as iso_a4_210x297mm or na_letter_8.5x11in. */
static bool media_size(const char *name, int32_t *x, int32_t *y)
{
    const char *tail = strrchr(name, '_');
    float width, height;
    char unit[3] = "";
    if (tail == NULL || sscanf(tail + 1, "%fx%f%2s", &width, &height, unit) != 3) return false;
    float scale = strcmp(unit, "in") == 0 ? 2540.0f : strcmp(unit, "mm") == 0 ? 100.0f : 0.0f;
    if (scale == 0.0f) return false;
    *x = (int32_t)(width * scale + 0.5f);
    *y = (int32_t)(height * scale + 0.5f);
    return true;
}

static void write_media_size(ipp_writer_t *w, const char *attribute_name, int32_t x, int32_t y)
{
    /* attribute_name is the member name for media-col, or NULL for a plain size collection */
    if (attribute_name != NULL) {
        ipp_w_member(w, attribute_name);
        ipp_w_col_begin(w, "");
    }
    ipp_w_member(w, "x-dimension");
    ipp_w_int(w, 0x21, "", x);
    ipp_w_member(w, "y-dimension");
    ipp_w_int(w, 0x21, "", y);
    if (attribute_name != NULL) ipp_w_col_end(w);
}

static void write_media_collections(ipp_writer_t *w, char media[][CAPS_MEDIA_NAME], size_t count)
{
    bool first = true;
    for (size_t i = 0; i < count; i++) {
        int32_t x, y;
        if (!media_size(media[i], &x, &y)) continue;
        ipp_w_col_begin(w, first ? "media-size-supported" : "");
        write_media_size(w, NULL, x, y);
        ipp_w_col_end(w);
        first = false;
    }
    first = true;
    for (size_t i = 0; i < count; i++) {
        int32_t x, y;
        if (!media_size(media[i], &x, &y)) continue;
        ipp_w_col_begin(w, first ? "media-col-database" : "");
        write_media_size(w, "media-size", x, y);
        const char *margins[] = { "media-bottom-margin", "media-left-margin", "media-right-margin", "media-top-margin" };
        for (size_t m = 0; m < 4; m++) {
            ipp_w_member(w, margins[m]);
            ipp_w_int(w, 0x21, "", 423);
        }
        ipp_w_col_end(w);
        first = false;
    }
}

size_t printer_attrs_write(ipp_writer_t *w, const char *printer_uri)
{
    printer_status_t status = print_state_get();
    bool stopped = status.state == PRINTER_STOPPED;
    bool active = status.state == PRINTER_PROCESSING;
    char uuid_uri[46] = "urn:uuid:";
    if (print_mdns_uuid(uuid_uri + 9, sizeof(uuid_uri) - 9) != ESP_OK) {
        w->overflow = true;
        return 0;
    }
    int64_t uptime = esp_timer_get_time() / 1000000;

    ipp_w_group(w, 0x04);
    char media[CAPS_MEDIA_MAX][CAPS_MEDIA_NAME];
    size_t media_count = printer_features_media(media, CAPS_MEDIA_MAX);
    const char *media_default = media[0];
    for (size_t i = 0; i < media_count; i++) {
        if (strcmp(media[i], "iso_a4_210x297mm") == 0) media_default = media[i];
    }

    ipp_w_str(w, 0x45, "printer-uri-supported", printer_uri);
    ipp_w_str(w, 0x45, "printer-uuid", uuid_uri);
    ipp_w_int(w, 0x21, "printer-up-time", uptime > INT32_MAX ? INT32_MAX : (int32_t)uptime);
    const settings_t *cfg = settings_get();
    char more_info[64];
    snprintf(more_info, sizeof(more_info), "http://%s.local/", cfg->hostname);
    ipp_w_str(w, 0x42, "printer-name", cfg->name);
    ipp_w_str(w, 0x41, "printer-info", cfg->name);
    ipp_w_str(w, 0x41, "printer-location", cfg->location);
    ipp_w_str(w, 0x45, "printer-more-info", more_info);
    ipp_w_str(w, 0x44, "media-ready", media_default);
    ipp_w_str(w, 0x41, "printer-make-and-model", "Pluto Print");
    write_enum(w, "printer-state", stopped ? 5 : active ? 4 : 3);
    ipp_w_str(w, 0x44, "printer-state-reasons", status.reason);
    ipp_w_bool(w, "printer-is-accepting-jobs", !stopped && !active);
    ipp_w_str(w, 0x49, "document-format-supported", "image/urf");
    ipp_w_more(w, 0x49, "image/pwg-raster");
    ipp_w_str(w, 0x49, "document-format-default", "image/urf");
    ipp_w_str(w, 0x44, "compression-supported", "none");
    char urf[96];
    bool color = printer_features_color();
    bool duplex = printer_features_duplex();
    if (printer_features_urf(urf, sizeof(urf)) != 0) {
        char *token = urf;
        for (bool first = true; token != NULL && *token != '\0'; first = false) {
            char *comma = strchr(token, ',');
            if (comma != NULL) *comma = '\0';
            if (first) ipp_w_str(w, 0x44, "urf-supported", token); else ipp_w_more(w, 0x44, token);
            token = comma != NULL ? comma + 1 : NULL;
        }
    }
    ipp_w_res(w, "pwg-raster-document-resolution-supported", 600, 600);
    ipp_w_str(w, 0x44, "pwg-raster-document-sheet-back", "normal");
    ipp_w_str(w, 0x44, "pwg-raster-document-type-supported", "sgray_8");
    if (color) ipp_w_more(w, 0x44, "srgb_8");
    ipp_w_res(w, "printer-resolution-supported", 600, 600);
    ipp_w_res(w, "printer-resolution-default", 600, 600);
    ipp_w_int(w, 0x23, "print-quality-supported", 3);
    ipp_w_int(w, 0x23, "", 4);
    ipp_w_int(w, 0x23, "", 5);
    ipp_w_int(w, 0x23, "print-quality-default", 4);
    for (size_t i = 0; i < media_count; i++) {
        if (i == 0) ipp_w_str(w, 0x44, "media-supported", media[i]); else ipp_w_more(w, 0x44, media[i]);
    }
    ipp_w_str(w, 0x44, "media-default", media_default);
    ipp_w_str(w, 0x44, "media-col-supported", "media-size");
    ipp_w_more(w, 0x44, "media-bottom-margin");
    ipp_w_more(w, 0x44, "media-left-margin");
    ipp_w_more(w, 0x44, "media-right-margin");
    ipp_w_more(w, 0x44, "media-top-margin");
    write_media_collections(w, media, media_count);
    {
        int32_t dx, dy;
        if (media_size(media_default, &dx, &dy)) {
            ipp_w_col_begin(w, "media-col-default");
            write_media_size(w, "media-size", dx, dy);
            ipp_w_col_end(w);
        }
    }
    ipp_w_str(w, 0x44, "sides-supported", "one-sided");
    if (duplex) {
        ipp_w_more(w, 0x44, "two-sided-long-edge");
        ipp_w_more(w, 0x44, "two-sided-short-edge");
    }
    ipp_w_str(w, 0x44, "sides-default", "one-sided");
    write_range(w, "copies-supported", 1, 99);
    ipp_w_bool(w, "color-supported", color);
    ipp_w_str(w, 0x44, "print-color-mode-supported", color ? "auto" : "monochrome");
    if (color) {
        ipp_w_more(w, 0x44, "color");
        ipp_w_more(w, 0x44, "monochrome");
    }
    ipp_w_str(w, 0x44, "print-color-mode-default", color ? "auto" : "monochrome");
    ipp_w_str(w, 0x44, "ipp-versions-supported", "1.1");
    ipp_w_more(w, 0x44, "2.0");
    write_enum(w, "operations-supported", 0x0002);
    write_enum_more(w, 0x0004);
    write_enum_more(w, 0x0008);
    write_enum_more(w, 0x0009);
    write_enum_more(w, 0x000a);
    write_enum_more(w, 0x000b);
    ipp_w_str(w, 0x44, "uri-security-supported", "none");
    ipp_w_str(w, 0x44, "uri-authentication-supported", "none");
    ipp_w_int(w, 0x21, "queued-job-count", active ? 1 : 0);
    int toner = print_state_toner_percent();
    if (toner >= 0) {
        /* macOS shows these as "supply levels". */
        ipp_w_str(w, 0x42, "marker-names", "Black Toner");
        ipp_w_str(w, 0x44, "marker-types", "toner");
        ipp_w_str(w, 0x42, "marker-colors", "#000000");
        ipp_w_int(w, 0x21, "marker-levels", toner);
        ipp_w_int(w, 0x21, "marker-low-levels", 10);
        ipp_w_int(w, 0x21, "marker-high-levels", 100);
    }
    ipp_w_str(w, 0x47, "charset-configured", "utf-8");
    ipp_w_str(w, 0x47, "charset-supported", "utf-8");
    ipp_w_str(w, 0x48, "natural-language-configured", "en");
    ipp_w_str(w, 0x48, "generated-natural-language-supported", "en");
    ipp_w_str(w, 0x44, "pdl-override-supported", "not-attempted");
    return w->overflow ? 0 : w->len;
}
