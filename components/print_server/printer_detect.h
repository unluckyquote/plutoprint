// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

/* Reads the connected printer's device ID and status file once and stores what it can do. */
void printer_detect_run(void);
void printer_detect_forget(void);
