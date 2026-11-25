// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#ifndef IPP_H
#define IPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IPP_OP_PRINT_JOB 0x0002
#define IPP_OP_VALIDATE_JOB 0x0004
#define IPP_OP_CANCEL_JOB 0x0008
#define IPP_OP_GET_JOB_ATTRIBUTES 0x0009
#define IPP_OP_GET_PRINTER_ATTRIBUTES 0x000b

#define IPP_STATUS_OK 0x0000
#define IPP_STATUS_BAD_REQUEST 0x0400
#define IPP_STATUS_DOCUMENT_FORMAT_NOT_SUPPORTED 0x040a
#define IPP_STATUS_SERVICE_UNAVAILABLE 0x0503
#define IPP_STATUS_BUSY 0x0507

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t op;
    uint32_t request_id;
    char document_format[64];
    char job_name[64]; /* Optional; empty when absent, truncated to fit. */
    int32_t copies;
    size_t body_offset;
} ipp_request_t;

int ipp_parse_request(const uint8_t *buf, size_t len, ipp_request_t *out);

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    bool overflow;
} ipp_writer_t;

void ipp_w_begin(ipp_writer_t *w, uint8_t *buf, size_t cap,
                 const ipp_request_t *req, uint16_t status);
void ipp_w_group(ipp_writer_t *w, uint8_t group_tag);
void ipp_w_str(ipp_writer_t *w, uint8_t value_tag, const char *name,
               const char *value);
void ipp_w_more(ipp_writer_t *w, uint8_t value_tag, const char *value);
void ipp_w_int(ipp_writer_t *w, uint8_t value_tag, const char *name, int32_t v);
void ipp_w_bool(ipp_writer_t *w, const char *name, bool v);
void ipp_w_res(ipp_writer_t *w, const char *name, int32_t x, int32_t y);
/* Collections: begin (name empty for further values of a set), then member name followed by its value, then end. */
void ipp_w_col_begin(ipp_writer_t *w, const char *name);
void ipp_w_member(ipp_writer_t *w, const char *member);
void ipp_w_col_end(ipp_writer_t *w);
size_t ipp_w_end(ipp_writer_t *w);

#endif
