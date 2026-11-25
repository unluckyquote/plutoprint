// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "printer_caps.h"

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    char *buffer = calloc(1, 65536);
    size_t got = fread(buffer, 1, 65535, f);
    assert(got > 0);
    fclose(f);
    return buffer;
}

int main(void)
{
    printer_caps_t caps;
    memset(&caps, 0, sizeof(caps));

    printer_caps_parse_device_id("MFG:Samsung;CMD:SPL,URF,FWV,PIC,EXT,DCU;MDL:M2020 Series;CLS:PRINTER;CID:SA_SPLV3_BW;", &caps);
    assert(strcmp(caps.model, "M2020 Series") == 0);
    assert(caps.urf && !caps.pwg);

    char *xml = slurp("../fixtures/status-monitor.xml");
    printer_caps_parse_status_xml(xml, &caps);
    assert(caps.media_count == 4);
    assert(strcmp(caps.media[0], "na_letter_8.5x11in") == 0);
    assert(strcmp(caps.media[2], "iso_a4_210x297mm") == 0);
    assert(caps.tray_count == 1);
    assert(caps.toner_colors[0] && !printer_caps_has_color_toner(&caps));

    const char *color = "<TonerID><Black>A</Black><Cyan>B</Cyan><Magenta>C</Magenta><Yellow>D</Yellow></TonerID>";
    printer_caps_parse_status_xml(color, &caps);
    assert(printer_caps_has_color_toner(&caps));

    printer_caps_t copy;
    printer_caps_set(&caps);
    printer_caps_get(&copy);
    assert(copy.known && printer_caps_has_color_toner(&copy));
    printer_caps_clear();
    printer_caps_get(&copy);
    assert(!copy.known);

    printer_caps_parse_device_id("CMD:PCL,PWGRaster;MDL:Other;", &caps);
    assert(caps.pwg && !caps.urf);
    printer_caps_parse_status_xml("not xml", &caps);
    assert(caps.media_count == 0);
    return 0;
}
