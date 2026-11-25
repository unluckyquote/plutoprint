// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "http_min.h"

static void test_content_length(void)
{
    static const char request[] =
        "POST /print HTTP/1.1\r\n"
        "Host: printer\r\n"
        "Content-Length: 42\r\n"
        "\r\nbody";
    http_head_t head;

    assert(http_parse_head(request, sizeof(request) - 1, &head) == 59);
    assert(strcmp(head.method, "POST") == 0);
    assert(strcmp(head.path, "/print") == 0);
    assert(head.content_length == 42);
    assert(!head.chunked);
    assert(!head.expect_continue);
}

static void test_chunked_head(void)
{
    static const char request[] =
        "pOsT /x HTTP/1.1\r\n"
        "tRaNsFeR-EnCoDiNg: ChUnKeD\r\n"
        "eXpEcT: 100-CoNtInUe\r\n"
        "\r\n";
    http_head_t head;

    assert(http_parse_head(request, sizeof(request) - 1, &head) > 0);
    assert(strcmp(head.method, "pOsT") == 0);
    assert(strcmp(head.path, "/x") == 0);
    assert(head.chunked);
    assert(head.expect_continue);
}

static void test_partial_head(void)
{
    static const char request[] = "POST /print HTTP/1.1\r\nHost: printer\r\n";
    http_head_t head;

    assert(http_parse_head(request, sizeof(request) - 1, &head) == 0);
}

static void feed_all(const uint8_t *body, size_t body_len, size_t step,
                     char *decoded, size_t *decoded_len)
{
    chunk_dec_t decoder;
    size_t offset = 0;

    chunk_dec_init(&decoder);
    *decoded_len = 0;
    while (offset < body_len) {
        size_t offered = body_len - offset;
        size_t consumed = 0;
        int produced;

        if (offered > step) {
            offered = step;
        }
        produced = chunk_dec_feed(&decoder, body + offset, offered,
                                  (uint8_t *)decoded + *decoded_len,
                                  &consumed);
        assert(produced >= 0);
        *decoded_len += (size_t)produced;
        offset += consumed;
        assert(consumed == offered);
    }
    assert(chunk_dec_done(&decoder));
}

static void test_chunked_body(void)
{
    static const uint8_t body[] =
        "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    char decoded[sizeof(body)];
    size_t decoded_len;

    feed_all(body, sizeof(body) - 1, sizeof(body), decoded, &decoded_len);
    assert(decoded_len == 11);
    assert(memcmp(decoded, "hello world", decoded_len) == 0);

    feed_all(body, sizeof(body) - 1, 1, decoded, &decoded_len);
    assert(decoded_len == 11);
    assert(memcmp(decoded, "hello world", decoded_len) == 0);
}

static void test_chunk_extensions_and_bad_size(void)
{
    static const uint8_t body[] = "A;ext=1\r\n0123456789\r\n0\r\n\r\n";
    static const uint8_t bad[] = "Z\r\n";
    chunk_dec_t decoder;
    uint8_t decoded[sizeof(body)];
    size_t consumed;

    chunk_dec_init(&decoder);
    assert(chunk_dec_feed(&decoder, body, sizeof(body) - 1, decoded,
                          &consumed) == 10);
    assert(chunk_dec_done(&decoder));

    chunk_dec_init(&decoder);
    assert(chunk_dec_feed(&decoder, bad, sizeof(bad) - 1, decoded,
                          &consumed) == -1);
}

int main(void)
{
    test_content_length();
    test_chunked_head();
    test_partial_head();
    test_chunked_body();
    test_chunk_extensions_and_bad_size();
    return 0;
}
