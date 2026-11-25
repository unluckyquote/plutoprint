// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "printer_caps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *snmp;
    const char *pwg;
} media_map_t;

static const media_map_t media_map[] = {
    { "na_letter", "na_letter_8.5x11in" },
    { "na_legal", "na_legal_8.5x14in" },
    { "na_foolscap", "na_foolscap_8.5x13in" },
    { "iso_a4", "iso_a4_210x297mm" },
    { "iso_b5", "iso_b5_176x250mm" },
    { "jis_b5", "jis_b5_182x257mm" },
    { "na_executive", "na_executive_7.25x10.5in" },
    { "iso_a5", "iso_a5_148x210mm" },
    { "iso_a6", "iso_a6_105x148mm" },
    { "na_monarch", "na_monarch_3.875x7.5in" },
    { "na_number-10", "na_number-10_4.125x9.5in" },
    { "iso_dl", "iso_dl_110x220mm" },
    { "iso_c5", "iso_c5_162x229mm" },
    { "iso_c6", "iso_c6_114x162mm" },
    { "na_index-4x6", "na_index-4x6_4x6in" },
};

static const char *find_field(const char *text, const char *key, size_t *length)
{
    /* Returns the value after key (for example "MDL:") up to the next semicolon. */
    const char *at = strstr(text, key);
    if (at == NULL) return NULL;
    at += strlen(key);
    const char *end = strchr(at, ';');
    *length = end != NULL ? (size_t)(end - at) : strlen(at);
    return at;
}

void printer_caps_parse_device_id(const char *id, printer_caps_t *caps)
{
    size_t length = 0;
    const char *value;
    if (id == NULL || caps == NULL) return;
    value = find_field(id, "MDL:", &length);
    if (value == NULL) value = find_field(id, "MODEL:", &length);
    if (value != NULL) {
        if (length >= sizeof(caps->model)) length = sizeof(caps->model) - 1;
        memcpy(caps->model, value, length);
        caps->model[length] = '\0';
    }
    value = find_field(id, "CMD:", &length);
    if (value == NULL) value = find_field(id, "COMMAND SET:", &length);
    if (value != NULL) {
        char list[160];
        if (length >= sizeof(list)) length = sizeof(list) - 1;
        memcpy(list, value, length);
        list[length] = '\0';
        caps->urf = strstr(list, "URF") != NULL;
        caps->pwg = strstr(list, "PWG") != NULL;
    }
}

static const char *attribute(const char *tag, const char *name, char *out, size_t cap)
{
    char key[24];
    snprintf(key, sizeof(key), "%s=\"", name);
    const char *tag_end = strchr(tag, '>');
    const char *at = strstr(tag, key);
    if (at == NULL || (tag_end != NULL && at > tag_end)) return NULL;
    at += strlen(key);
    const char *end = strchr(at, '"');
    if (end == NULL) return NULL;
    size_t length = (size_t)(end - at);
    if (length >= cap) length = cap - 1;
    memcpy(out, at, length);
    out[length] = '\0';
    return out;
}

void printer_caps_parse_status_xml(const char *xml, printer_caps_t *caps)
{
    if (xml == NULL || caps == NULL) return;

    caps->media_count = 0;
    const char *table = strstr(xml, "<PaperSizeTable");
    const char *table_end = table != NULL ? strstr(table, "</PaperSizeTable>") : NULL;
    for (const char *entry = table; entry != NULL && table_end != NULL; ) {
        entry = strstr(entry + 1, "<PaperSize ");
        if (entry == NULL || entry > table_end) break;
        char snmp[32];
        if (attribute(entry, "snmpcode", snmp, sizeof(snmp)) == NULL) continue;
        for (size_t i = 0; i < sizeof(media_map) / sizeof(media_map[0]); i++) {
            if (strcmp(snmp, media_map[i].snmp) == 0 && caps->media_count < CAPS_MEDIA_MAX) {
                snprintf(caps->media[caps->media_count++], CAPS_MEDIA_NAME, "%s", media_map[i].pwg);
                break;
            }
        }
    }

    caps->tray_count = 0;
    const char *trays = strstr(xml, "<TrayName");
    const char *trays_end = trays != NULL ? strstr(trays, "</TrayName>") : NULL;
    for (const char *entry = trays; entry != NULL && trays_end != NULL; ) {
        entry = strstr(entry + 1, "<Tray ");
        if (entry == NULL || entry > trays_end) break;
        caps->tray_count++;
    }

    memset(caps->toner_colors, 0, sizeof(caps->toner_colors));
    const char *toner = strstr(xml, "<TonerID>");
    const char *toner_end = toner != NULL ? strstr(toner, "</TonerID>") : NULL;
    if (toner != NULL && toner_end != NULL) {
        static const char *tags[CAPS_TONER_MAX] = { "<Black", "<Cyan", "<Magenta", "<Yellow" };
        for (int i = 0; i < CAPS_TONER_MAX; i++) {
            const char *found = strstr(toner, tags[i]);
            caps->toner_colors[i] = found != NULL && found < toner_end;
        }
    }
}

bool printer_caps_has_color_toner(const printer_caps_t *caps)
{
    return caps != NULL && (caps->toner_colors[1] || caps->toner_colors[2] || caps->toner_colors[3]);
}

static printer_caps_t current;
static volatile uint32_t sequence;

void printer_caps_set(const printer_caps_t *caps)
{
    sequence++;
    current = *caps;
    current.known = true;
    sequence++;
}

void printer_caps_get(printer_caps_t *out)
{
    uint32_t before;
    do {
        before = sequence;
        *out = current;
    } while ((before & 1) != 0 || before != sequence);
}

void printer_caps_clear(void)
{
    printer_caps_t empty;
    memset(&empty, 0, sizeof(empty));
    sequence++;
    current = empty;
    sequence++;
}
