// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <string.h>

#include "job_log.h"

static job_record_t record(uint32_t id)
{
    job_record_t r = {
        .id = id,
        .bytes = id * 100,
        .result = JOB_RESULT_OK,
        .finished_unix_s = id,
    };
    strcpy(r.name, "job");
    return r;
}

int main(void)
{
    job_record_t out[5];

    job_log_clear();
    assert(job_log_get(out, 5) == 0);

    for (uint32_t id = 1; id <= 12; id++) {
        job_record_t r = record(id);
        job_log_add(&r);
    }
    assert(job_log_get(out, 5) == 5);
    for (size_t i = 0; i < 5; i++) {
        assert(out[i].id == 12 - i);
    }
    assert(job_log_get(out, 3) == 3);

    job_record_t long_name = record(13);
    memset(long_name.name, 'x', sizeof(long_name.name));
    job_log_add(&long_name);
    assert(job_log_get(out, 1) == 1);
    assert(out[0].name[sizeof(out[0].name) - 1] == '\0');

    job_log_clear();
    assert(job_log_get(out, 5) == 0);

    return 0;
}
