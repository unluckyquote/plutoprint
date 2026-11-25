// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAPS_MEDIA_MAX 24
#define CAPS_MEDIA_NAME 40
#define CAPS_TONER_MAX 4

typedef struct {
    bool known;                 /* a printer has been read since it connected */
    char model[40];             /* MDL from the IEEE 1284 device ID */
    bool urf;                   /* CMD lists URF */
    bool pwg;                   /* CMD lists PWG raster */
    bool toner_colors[CAPS_TONER_MAX];   /* black, cyan, magenta, yellow listed in the status file */
    uint8_t media_count;
    char media[CAPS_MEDIA_MAX][CAPS_MEDIA_NAME];   /* PWG media names, for example iso_a4_210x297mm */
    uint8_t tray_count;
} printer_caps_t;

/* Parse the printer's IEEE 1284 device ID text. */
void printer_caps_parse_device_id(const char *id, printer_caps_t *caps);
/* Parse the Samsung status monitor XML (paper sizes, trays, toner colours). */
void printer_caps_parse_status_xml(const char *xml, printer_caps_t *caps);
/* True when cyan, magenta or yellow toner is listed. */
bool printer_caps_has_color_toner(const printer_caps_t *caps);

/* Shared with the firmware: the currently detected printer. */
void printer_caps_set(const printer_caps_t *caps);
void printer_caps_get(printer_caps_t *out);
void printer_caps_clear(void);
