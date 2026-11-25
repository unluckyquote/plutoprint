// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stddef.h>

/* Keeps the most recent log output in memory so it can be read over Wi-Fi. */
void logbuf_start(void);
size_t logbuf_read(char *out, size_t cap);
