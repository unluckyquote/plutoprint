// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#ifndef JOB_LOG_H
#define JOB_LOG_H

#include <stddef.h>
#include <stdint.h>

#define JOB_RESULT_OK 0
#define JOB_RESULT_CANCELLED 1
#define JOB_RESULT_FAILED 2

typedef struct {
    uint32_t id;
    char name[64];
    size_t bytes;
    int result;
    int64_t finished_unix_s;
} job_record_t;

void job_log_clear(void);
void job_log_add(const job_record_t *r);
size_t job_log_get(job_record_t *out, size_t max);

#endif
