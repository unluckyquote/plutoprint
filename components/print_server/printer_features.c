// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "printer_features.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"

void printer_features_caps(printer_caps_t *out)
{
    printer_caps_get(out);
}

bool printer_features_color(void)
{
    uint8_t mode = settings_get()->color_mode;
    if (mode == 1) return false;
    if (mode == 2) return true;
    printer_caps_t caps;
    printer_caps_get(&caps);
    return printer_caps_has_color_toner(&caps);
}

bool printer_features_duplex(void)
{
    return settings_get()->duplex;
}

size_t printer_features_urf(char *out, size_t cap)
{
    int n = snprintf(out, cap, "V1.4,CP1,W8%s,RS600,PQ3-4-5%s", printer_features_color() ? ",SRGB24" : "",
                     printer_features_duplex() ? ",DM1" : "");
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t printer_features_media(char names[][CAPS_MEDIA_NAME], size_t max)
{
    printer_caps_t caps;
    printer_caps_get(&caps);
    size_t count = 0;
    if (caps.known && caps.media_count > 0) {
        for (; count < caps.media_count && count < max; count++) strlcpy(names[count], caps.media[count], CAPS_MEDIA_NAME);
        return count;
    }
    if (max >= 2) {
        strlcpy(names[0], "iso_a4_210x297mm", CAPS_MEDIA_NAME);
        strlcpy(names[1], "na_letter_8.5x11in", CAPS_MEDIA_NAME);
        return 2;
    }
    return 0;
}
