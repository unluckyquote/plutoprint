// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ipp.h"

static size_t add_attr(uint8_t *buf, size_t at, uint8_t tag,
                       const char *name, const uint8_t *value, size_t value_len)
{
    size_t name_len = strlen(name);

    buf[at++] = tag;
    buf[at++] = (uint8_t)(name_len >> 8);
    buf[at++] = (uint8_t)name_len;
    memcpy(buf + at, name, name_len);
    at += name_len;
    buf[at++] = (uint8_t)(value_len >> 8);
    buf[at++] = (uint8_t)value_len;
    memcpy(buf + at, value, value_len);
    return at + value_len;
}

static size_t make_request(uint8_t *buf, int32_t copies)
{
    static const uint8_t charset[] = "utf-8";
    static const uint8_t language[] = "en";
    static const uint8_t format[] = "image/urf";
    uint8_t integer[4] = {
        (uint8_t)(copies >> 24), (uint8_t)(copies >> 16),
        (uint8_t)(copies >> 8), (uint8_t)copies
    };
    size_t at = 0;

    buf[at++] = 1;
    buf[at++] = 1;
    buf[at++] = 0;
    buf[at++] = 2;
    buf[at++] = 0;
    buf[at++] = 0;
    buf[at++] = 0;
    buf[at++] = 7;
    buf[at++] = 0x01;
    at = add_attr(buf, at, 0x47, "attributes-charset", charset, 5);
    at = add_attr(buf, at, 0x48, "attributes-natural-language", language, 2);
    at = add_attr(buf, at, 0x49, "document-format", format, 9);
    at = add_attr(buf, at, 0x21, "copies", integer, sizeof(integer));
    buf[at++] = 0x03;
    memcpy(buf + at, "UNIRAST\0", 8);
    return at + 8;
}

static void test_parser(void)
{
    uint8_t request[256];
    ipp_request_t parsed;
    size_t len = make_request(request, 1);

    assert(ipp_parse_request(request, len, &parsed) == 0);
    assert(parsed.major == 1 && parsed.minor == 1);
    assert(parsed.op == 0x0002);
    assert(parsed.request_id == 7);
    assert(strcmp(parsed.document_format, "image/urf") == 0);
    assert(parsed.copies == 1);
    assert(parsed.job_name[0] == '\0');
    assert(parsed.body_offset < len);
    assert(request[parsed.body_offset] == 'U');

    len = make_request(request, 3);
    assert(ipp_parse_request(request, len, &parsed) == 0);
    assert(parsed.copies == 3);

    for (size_t cut = 0; cut < parsed.body_offset; ++cut) {
        assert(ipp_parse_request(request, cut, &parsed) == -1);
    }
}

static void test_job_group_copies(void)
{
    uint8_t request[256];
    ipp_request_t header = { .major = 1, .minor = 1, .request_id = 7 };
    ipp_request_t parsed;
    ipp_writer_t writer;
    size_t len;

    ipp_w_begin(&writer, request, sizeof(request), &header, IPP_OP_PRINT_JOB);
    ipp_w_str(&writer, 0x49, "document-format", "image/urf");
    ipp_w_group(&writer, 0x02);
    ipp_w_int(&writer, 0x21, "copies", 2);
    /* A job-group document-format must not override the operation attribute. */
    ipp_w_str(&writer, 0x49, "document-format", "application/pdf");
    len = ipp_w_end(&writer);
    assert(len != 0);
    assert(ipp_parse_request(request, len, &parsed) == 0);
    assert(parsed.op == IPP_OP_PRINT_JOB);
    assert(parsed.copies == 2);
    assert(strcmp(parsed.document_format, "image/urf") == 0);
}

static void test_large_attribute_block(void)
{
    uint8_t request[6017];
    char padding[101];
    ipp_request_t header = { .major = 1, .minor = 1, .request_id = 7 };
    ipp_request_t parsed;
    ipp_writer_t writer;
    const size_t attributes_end = 8 + 6000;
    size_t len;

    memset(padding, 'x', sizeof(padding) - 1);
    padding[88] = '\0';
    ipp_w_begin(&writer, request, sizeof(request), &header, IPP_OP_PRINT_JOB);
    ipp_w_str(&writer, 0x49, "document-format", "image/urf");
    while (attributes_end - writer.len > 100 + 12) {
        ipp_w_str(&writer, 0x41, "padding", padding);
    }
    memset(padding, 'x', sizeof(padding) - 1);
    padding[attributes_end - writer.len - 12] = '\0';
    ipp_w_str(&writer, 0x41, "padding", padding);
    assert(writer.len == attributes_end);
    len = ipp_w_end(&writer);
    assert(len == attributes_end + 1);
    memcpy(request + len, "UNIRAST\0", 8);
    assert(ipp_parse_request(request, len + 8, &parsed) == 0);
    assert(parsed.body_offset == len);
    assert(strcmp(parsed.document_format, "image/urf") == 0);
    assert(parsed.copies == 1);
    assert(memcmp(request + parsed.body_offset, "UNIRAST\0", 8) == 0);
}

static void test_job_name(void)
{
    uint8_t request[256];
    char long_name[81];
    ipp_request_t header = { .major = 1, .minor = 1, .request_id = 7 };
    ipp_request_t parsed;
    ipp_writer_t writer;
    size_t len;

    ipp_w_begin(&writer, request, sizeof(request), &header, IPP_OP_PRINT_JOB);
    ipp_w_str(&writer, 0x42, "job-name", "Test document");
    ipp_w_str(&writer, 0x41, "job-name", "Wrong value tag");
    ipp_w_group(&writer, 0x02);
    ipp_w_str(&writer, 0x42, "job-name", "Wrong group");
    len = ipp_w_end(&writer);
    assert(len != 0);
    assert(ipp_parse_request(request, len, &parsed) == 0);
    assert(strcmp(parsed.job_name, "Test document") == 0);

    memset(long_name, 'n', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    ipp_w_begin(&writer, request, sizeof(request), &header, IPP_OP_PRINT_JOB);
    ipp_w_str(&writer, 0x42, "job-name", long_name);
    len = ipp_w_end(&writer);
    assert(len != 0);
    assert(ipp_parse_request(request, len, &parsed) == 0);
    assert(strlen(parsed.job_name) == sizeof(parsed.job_name) - 1);
    assert(memcmp(parsed.job_name, long_name, sizeof(parsed.job_name) - 1) == 0);
}

static void test_writer(void)
{
    static const uint8_t expected[] = {
        0x01, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x01,
        0x47, 0x00, 0x12, 'a', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's',
        '-', 'c', 'h', 'a', 'r', 's', 'e', 't', 0x00, 0x05, 'u', 't', 'f', '-', '8',
        0x48, 0x00, 0x1b, 'a', 't', 't', 'r', 'i', 'b', 'u', 't', 'e', 's', '-',
        'n', 'a', 't', 'u', 'r', 'a', 'l', '-', 'l', 'a', 'n', 'g', 'u', 'a', 'g', 'e',
        0x00, 0x02, 'e', 'n',
        0x04,
        0x41, 0x00, 0x0c, 'p', 'r', 'i', 'n', 't', 'e', 'r', '-', 'n', 'a', 'm', 'e',
        0x00, 0x06, 'M', '2', '0', '2', '0', 'W',
        0x44, 0x00, 0x0f, 'm', 'e', 'd', 'i', 'a', '-', 's', 'u', 'p', 'p', 'o', 'r', 't', 'e', 'd',
        0x00, 0x02, 'A', '4',
        0x44, 0x00, 0x00, 0x00, 0x06, 'L', 'e', 't', 't', 'e', 'r',
        0x23, 0x00, 0x0d, 'p', 'r', 'i', 'n', 't', 'e', 'r', '-', 's', 't', 'a', 't', 'e',
        0x00, 0x04, 0x00, 0x00, 0x00, 0x03,
        0x22, 0x00, 0x0f, 'c', 'o', 'l', 'o', 'r', '-', 's', 'u', 'p', 'p', 'o', 'r', 't', 'e', 'd',
        0x00, 0x01, 0x00,
        0x32, 0x00, 0x12, 'p', 'r', 'i', 'n', 't', 'e', 'r', '-', 'r', 'e', 's', 'o', 'l', 'u', 't', 'i', 'o', 'n',
        0x00, 0x09, 0x00, 0x00, 0x01, 0x2c, 0x00, 0x00, 0x01, 0x2c, 0x03,
        0x03
    };
    uint8_t output[sizeof(expected)];
    ipp_request_t request = { .major = 1, .minor = 1, .request_id = 0x01020304 };
    ipp_writer_t writer;

    ipp_w_begin(&writer, output, sizeof(output), &request, 0);
    ipp_w_group(&writer, 0x04);
    ipp_w_str(&writer, 0x41, "printer-name", "M2020W");
    ipp_w_str(&writer, 0x44, "media-supported", "A4");
    ipp_w_more(&writer, 0x44, "Letter");
    ipp_w_int(&writer, 0x23, "printer-state", 3);
    ipp_w_bool(&writer, "color-supported", false);
    ipp_w_res(&writer, "printer-resolution", 300, 300);
    assert(ipp_w_end(&writer) == sizeof(expected));
    assert(memcmp(output, expected, sizeof(expected)) == 0);

    ipp_w_begin(&writer, output, 1, &request, 0);
    assert(ipp_w_end(&writer) == 0);
}

int main(void)
{
    test_parser();
    test_large_attribute_block();
    test_job_group_copies();
    test_job_name();
    test_writer();
    return 0;
}
