// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include "printer_state.h"

printer_status_t print_state_get(void);
// Black toner level 0-100, or -1 when unknown (printer not connected or no reading yet).
int print_state_toner_percent(void);
