// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "job_log.h"

#include <string.h>

#define JOB_LOG_CAPACITY 5

static job_record_t records[JOB_LOG_CAPACITY];
static size_t count;
static size_t next_index;

void job_log_clear(void)
{
    memset(records, 0, sizeof(records));
    count = 0;
    next_index = 0;
}

void job_log_add(const job_record_t *r)
{
    if (r == NULL) {
        return;
    }

    records[next_index] = *r;
    records[next_index].name[sizeof(records[next_index].name) - 1] = '\0';
    next_index = (next_index + 1) % JOB_LOG_CAPACITY;
    if (count < JOB_LOG_CAPACITY) {
        count++;
    }
}

size_t job_log_get(job_record_t *out, size_t max)
{
    size_t result_count = count < max ? count : max;

    if (out == NULL) {
        return 0;
    }

    for (size_t i = 0; i < result_count; i++) {
        size_t index = (next_index + JOB_LOG_CAPACITY - 1 - i) % JOB_LOG_CAPACITY;
        out[i] = records[index];
    }
    return result_count;
}
