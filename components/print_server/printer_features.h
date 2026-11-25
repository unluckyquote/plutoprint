// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "printer_caps.h"

/* What the printer is advertised as: the detected printer, adjusted by the Settings page. */
bool printer_features_color(void);
bool printer_features_duplex(void);
/* Snapshot of the detected printer; known is false until one has been read. */
void printer_features_caps(printer_caps_t *out);
/* The urf-supported keywords joined by commas. */
size_t printer_features_urf(char *out, size_t cap);
/* Media names to offer: the printer's own list, or A4 and Letter when it is not known yet. */
size_t printer_features_media(char names[][CAPS_MEDIA_NAME], size_t max);
