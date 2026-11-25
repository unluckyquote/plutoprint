// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "job_log_sync.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static StaticSemaphore_t s_mutex_storage;
static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t job_log_mutex(void)
{
    portENTER_CRITICAL(&s_init_lock);
    if (s_mutex == NULL) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_storage);
    SemaphoreHandle_t mutex = s_mutex;
    portEXIT_CRITICAL(&s_init_lock);
    return mutex;
}

void job_log_sync_add(const job_record_t *record)
{
    SemaphoreHandle_t mutex = job_log_mutex();
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        job_log_add(record);
        xSemaphoreGive(mutex);
    }
}

size_t job_log_sync_get(job_record_t *out, size_t max)
{
    SemaphoreHandle_t mutex = job_log_mutex();
    size_t count = 0;
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        count = job_log_get(out, max);
        xSemaphoreGive(mutex);
    }
    return count;
}

void job_log_sync_clear(void)
{
    SemaphoreHandle_t mutex = job_log_mutex();
    if (xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
        job_log_clear();
        xSemaphoreGive(mutex);
    }
}
