// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    IPP_JOB_OK,
    IPP_JOB_CANCELLED,
    IPP_JOB_FAILED,
} ipp_job_result_t;

typedef struct {
    uint32_t id;
    char name[64];
    size_t bytes;
    ipp_job_result_t result;
    int64_t finished_unix_s;
} ipp_job_info_t;

esp_err_t ipp_server_start(void);
bool ipp_server_try_begin_job(void);
void ipp_server_end_job(void);
bool ipp_server_job_active(void);
void ipp_server_cancel_current(void);
const ipp_job_info_t *ipp_server_last_job(void);
/* Consumes a job gate reserved by the caller; releases it on every return. */
esp_err_t ipp_server_run_stream(const char *name,
                                size_t (*next)(void *ctx, uint8_t *buf, size_t cap),
                                void *ctx);
