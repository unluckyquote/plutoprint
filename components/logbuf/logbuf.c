// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "logbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LOG_SIZE 16384

static char ring[LOG_SIZE];
static size_t head;
static size_t used;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t previous;

static int hook(const char *format, va_list args)
{
    char line[256];
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (length > 0) {
        size_t n = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        portENTER_CRITICAL(&lock);
        for (size_t i = 0; i < n; i++) {
            ring[head] = line[i];
            head = (head + 1) % LOG_SIZE;
        }
        used = used + n > LOG_SIZE ? LOG_SIZE : used + n;
        portEXIT_CRITICAL(&lock);
    }
    return previous != NULL ? previous(format, args) : length;
}

void logbuf_start(void)
{
    if (previous == NULL) {
        previous = esp_log_set_vprintf(hook);
    }
}

size_t logbuf_read(char *out, size_t cap)
{
    if (cap == 0) return 0;
    portENTER_CRITICAL(&lock);
    size_t n = used < cap - 1 ? used : cap - 1;
    size_t start = (head + LOG_SIZE - n) % LOG_SIZE;
    for (size_t i = 0; i < n; i++) out[i] = ring[(start + i) % LOG_SIZE];
    portEXIT_CRITICAL(&lock);
    out[n] = '\0';
    return n;
}
