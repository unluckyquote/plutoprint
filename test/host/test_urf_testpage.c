// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "urf_testpage.h"

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    unsigned char *data;

    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    *size = (size_t)ftell(file);
    assert(fseek(file, 0, SEEK_SET) == 0);
    data = malloc(*size);
    assert(data != NULL);
    assert(fread(data, 1, *size, file) == *size);
    fclose(file);
    return data;
}

static void check(size_t cap, const unsigned char *expected, size_t expected_size)
{
    urf_testpage_t state;
    unsigned char *actual = malloc(expected_size);
    size_t used = 0;

    assert(actual != NULL);
    urf_testpage_init(&state);
    for (;;) {
        size_t n = urf_testpage_next(&state, actual + used, cap);
        if (n == 0) {
            break;
        }
        assert(used + n <= expected_size);
        used += n;
    }
    assert(used == expected_size);
    assert(memcmp(actual, expected, expected_size) == 0);
    free(actual);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../fixtures/hello0.urf";
    size_t size;
    unsigned char *expected = read_file(path, &size);

    check(64, expected, size);
    check(4096, expected, size);
    free(expected);
    return 0;
}
