// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stddef.h>

#include "ipp.h"

size_t printer_attrs_write(ipp_writer_t *w, const char *printer_uri);
