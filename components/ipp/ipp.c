// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "ipp.h"

#include <string.h>

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

int ipp_parse_request(const uint8_t *buf, size_t len, ipp_request_t *out)
{
    size_t at;
    uint8_t group = 0;

    if (buf == NULL || out == NULL || len < 8) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->major = buf[0];
    out->minor = buf[1];
    out->op = get_u16(buf + 2);
    out->request_id = get_u32(buf + 4);
    out->copies = 1;
    at = 8;

    while (at < len) {
        uint8_t tag = buf[at++];

        if (tag == 0x03) {
            out->body_offset = at;
            return 0;
        }
        if (tag <= 0x0f) {
            group = tag;
            continue;
        }
        if (len - at < 4) {
            return -1;
        }

        {
            uint16_t name_len = get_u16(buf + at);
            uint16_t value_len;
            const uint8_t *name;
            const uint8_t *value;

            at += 2;
            if (len - at < (size_t)name_len + 2) {
                return -1;
            }
            name = buf + at;
            at += name_len;
            value_len = get_u16(buf + at);
            at += 2;
            if (len - at < value_len) {
                return -1;
            }
            value = buf + at;

            if (name_len != 0) {
                if (group == 0x01 && tag == 0x49 && name_len == 15 &&
                    memcmp(name, "document-format", 15) == 0) {
                    if (value_len >= sizeof(out->document_format)) {
                        return -1;
                    }
                    memcpy(out->document_format, value, value_len);
                    out->document_format[value_len] = '\0';
                } else if ((group == 0x01 || group == 0x02) &&
                           tag == 0x21 && name_len == 6 &&
                           memcmp(name, "copies", 6) == 0 && value_len == 4) {
                    out->copies = (int32_t)get_u32(value);
                } else if (group == 0x01 && tag == 0x42 && name_len == 8 &&
                           memcmp(name, "job-name", 8) == 0) {
                    size_t copy_len = value_len;

                    if (copy_len >= sizeof(out->job_name)) {
                        copy_len = sizeof(out->job_name) - 1;
                    }
                    memcpy(out->job_name, value, copy_len);
                    out->job_name[copy_len] = '\0';
                }
            }
            at += value_len;
        }
    }

    return -1;
}

static void put_u16(ipp_writer_t *w, uint16_t value)
{
    uint8_t bytes[2] = { (uint8_t)(value >> 8), (uint8_t)value };

    if (w->overflow || w->len > w->cap || w->cap - w->len < sizeof(bytes)) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, bytes, sizeof(bytes));
    w->len += sizeof(bytes);
}

static void put_bytes(ipp_writer_t *w, const void *data, size_t len)
{
    if (w->overflow || w->len > w->cap || w->cap - w->len < len) {
        w->overflow = true;
        return;
    }
    memcpy(w->buf + w->len, data, len);
    w->len += len;
}

static void put_attr(ipp_writer_t *w, uint8_t value_tag, const char *name,
                     const void *value, size_t value_len)
{
    size_t name_len = strlen(name);

    if (name_len > UINT16_MAX || value_len > UINT16_MAX) {
        w->overflow = true;
        return;
    }
    put_bytes(w, &value_tag, 1);
    put_u16(w, (uint16_t)name_len);
    put_bytes(w, name, name_len);
    put_u16(w, (uint16_t)value_len);
    put_bytes(w, value, value_len);
}

void ipp_w_begin(ipp_writer_t *w, uint8_t *buf, size_t cap,
                 const ipp_request_t *req, uint16_t status)
{
    uint8_t header[8] = {
        req->major, req->minor, (uint8_t)(status >> 8), (uint8_t)status,
        (uint8_t)(req->request_id >> 24), (uint8_t)(req->request_id >> 16),
        (uint8_t)(req->request_id >> 8), (uint8_t)req->request_id
    };

    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = false;
    put_bytes(w, header, sizeof(header));
    ipp_w_group(w, 0x01);
    ipp_w_str(w, 0x47, "attributes-charset", "utf-8");
    ipp_w_str(w, 0x48, "attributes-natural-language", "en");
}

void ipp_w_group(ipp_writer_t *w, uint8_t group_tag)
{
    put_bytes(w, &group_tag, 1);
}

void ipp_w_str(ipp_writer_t *w, uint8_t value_tag, const char *name,
               const char *value)
{
    put_attr(w, value_tag, name, value, strlen(value));
}

void ipp_w_more(ipp_writer_t *w, uint8_t value_tag, const char *value)
{
    put_attr(w, value_tag, "", value, strlen(value));
}

void ipp_w_col_begin(ipp_writer_t *w, const char *name)
{
    put_attr(w, 0x34, name, "", 0);
}

void ipp_w_member(ipp_writer_t *w, const char *member)
{
    put_attr(w, 0x4a, "", member, strlen(member));
}

void ipp_w_col_end(ipp_writer_t *w)
{
    put_attr(w, 0x37, "", "", 0);
}

void ipp_w_int(ipp_writer_t *w, uint8_t value_tag, const char *name, int32_t v)
{
    uint32_t value = (uint32_t)v;

    put_attr(w, value_tag, name, (uint8_t[4]) {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value
    }, 4);
}

void ipp_w_bool(ipp_writer_t *w, const char *name, bool v)
{
    uint8_t value = v ? 1 : 0;

    put_attr(w, 0x22, name, &value, 1);
}

void ipp_w_res(ipp_writer_t *w, const char *name, int32_t x, int32_t y)
{
    uint32_t ux = (uint32_t)x;
    uint32_t uy = (uint32_t)y;
    uint8_t value[9] = {
        (uint8_t)(ux >> 24), (uint8_t)(ux >> 16), (uint8_t)(ux >> 8), (uint8_t)ux,
        (uint8_t)(uy >> 24), (uint8_t)(uy >> 16), (uint8_t)(uy >> 8), (uint8_t)uy,
        3
    };

    put_attr(w, 0x32, name, value, sizeof(value));
}

size_t ipp_w_end(ipp_writer_t *w)
{
    uint8_t end = 0x03;

    put_bytes(w, &end, 1);
    return w->overflow ? 0 : w->len;
}
