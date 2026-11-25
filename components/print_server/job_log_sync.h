// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include "job_log.h"

void job_log_sync_add(const job_record_t *record);
size_t job_log_sync_get(job_record_t *out, size_t max);
void job_log_sync_clear(void);
