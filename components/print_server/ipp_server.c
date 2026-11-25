// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "ipp_server.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_min.h"
#include "ipp.h"
#include "job_log_sync.h"
#include "lwip/sockets.h"
#include "pjl_frame.h"
#include "raster_convert.h"
#include "printer_attrs.h"
#include "print_state.h"
#include "usb_printer.h"

#define IPP_PORT 631
#define IPP_OP_GET_JOBS 0x000a
#define IPP_HEAD_CAP 2048
#define IPP_BODY_CAP 8192
#define IPP_RESPONSE_CAP 8192
#define IPP_RECEIVE_CAP 4096
#define IPP_TASK_STACK 6144
#define SPOOL_MAX (6u * 1024 * 1024)   /* largest document accepted; PSRAM is 8 MB */
#define SPOOL_SEND_CHUNK (32u * 1024)
#define IPP_STATUS_NOT_FOUND 0x0406
#define IPP_STATUS_INTERNAL_ERROR 0x0500
#define IPP_URI_CAP 320

static const char *TAG = "ipp";
static TaskHandle_t s_listener_task;
static atomic_bool s_job_active;
static atomic_bool s_cancel_requested;
static ipp_job_info_t s_last_job;
static bool s_have_last_job;
static uint32_t s_next_job_id;
static ipp_job_info_t s_active_job;
static bool s_have_active_job;
static portMUX_TYPE s_job_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    int socket;
    uint8_t ipp_body[IPP_BODY_CAP];
    size_t ipp_len;
    ipp_request_t request;
    bool parsed;
    bool admitted;
    uint16_t admission_status;
    bool usb_failed;
    bool job_present;
    bool job_pending;
    bool response_sent;
    uint8_t *response;
    ipp_job_info_t job;
    char printer_uri[IPP_URI_CAP];
    size_t document_bytes;
    size_t body_bytes;
    uint8_t *spool;        /* whole document, held in PSRAM until it has fully arrived */
    size_t spool_cap;
} request_state_t;

typedef struct {
    request_state_t state;
    char head[IPP_HEAD_CAP];
    uint8_t receive[IPP_RECEIVE_CAP];
    uint8_t decoded[IPP_RECEIVE_CAP];
    uint8_t response[IPP_RESPONSE_CAP];
} connection_buffers_t;

static void send_ipp_response(request_state_t *state, uint16_t status);

bool ipp_server_try_begin_job(void)
{
    portENTER_CRITICAL(&s_job_lock);
    bool admitted = !atomic_exchange(&s_job_active, true);
    if (admitted) {
        atomic_store(&s_cancel_requested, false);
        s_have_active_job = false;
    }
    portEXIT_CRITICAL(&s_job_lock);
    return admitted;
}

void ipp_server_end_job(void)
{
    portENTER_CRITICAL(&s_job_lock);
    s_have_active_job = false;
    atomic_store(&s_job_active, false);
    portEXIT_CRITICAL(&s_job_lock);
}

static void begin_job_record(ipp_job_info_t *job, const char *name)
{
    memset(job, 0, sizeof(*job));
    strlcpy(job->name, name[0] != '\0' ? name : "Print job", sizeof(job->name));
    portENTER_CRITICAL(&s_job_lock);
    job->id = ++s_next_job_id;
    s_active_job = *job;
    s_have_active_job = true;
    portEXIT_CRITICAL(&s_job_lock);
}

static void finish_job_record(ipp_job_info_t *job)
{
    job->finished_unix_s = (int64_t)time(NULL);
    portENTER_CRITICAL(&s_job_lock);
    s_last_job = *job;
    s_have_last_job = true;
    portEXIT_CRITICAL(&s_job_lock);
    job_log_sync_add((const job_record_t *)job);
    ipp_server_end_job();
}

static void close_socket(int socket)
{
    if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
        close(socket);
    }
}

static bool send_all(int socket, const void *data, size_t len)
{
    const uint8_t *p = data;

    while (len != 0) {
        int sent = send(socket, p, len, 0);
        if (sent <= 0) {
            return false;
        }
        p += sent;
        len -= (size_t)sent;
    }
    return true;
}

static bool format_supported(const ipp_request_t *request)
{
    return request->document_format[0] == '\0' ||
           strcmp(request->document_format, "image/urf") == 0 ||
           strcmp(request->document_format, "image/pwg-raster") == 0 ||
           strcmp(request->document_format, "application/octet-stream") == 0;
}

static void start_forwarding(request_state_t *state)
{
    if (state->request.op != IPP_OP_PRINT_JOB) {
        return;
    }
    if (!format_supported(&state->request) || state->request.copies > 99) {
        state->admission_status = IPP_STATUS_DOCUMENT_FORMAT_NOT_SUPPORTED;
        return;
    }
    if (print_state_get().state == PRINTER_STOPPED) {
        state->admission_status = IPP_STATUS_SERVICE_UNAVAILABLE;
        return;
    }
    const bool admitted = ipp_server_try_begin_job();
    state->admitted = admitted;
    state->admission_status = admitted ? IPP_STATUS_OK : IPP_STATUS_BUSY;
    if (!admitted) return;
    begin_job_record(&state->job, state->request.job_name);
    state->job_present = true;
    /* Nothing goes to the printer until the whole document has arrived: a half page makes it print an error sheet. */
}

static void forward_document(request_state_t *state, const uint8_t *data, size_t len)
{
    if (!state->admitted) {
        return;
    }
    state->document_bytes += len;
    if (state->usb_failed || atomic_load(&s_cancel_requested)) {
        return;
    }
    size_t used = state->document_bytes - len;
    if (state->document_bytes > SPOOL_MAX) {
        ESP_LOGW(TAG, "document larger than %u bytes, job dropped", (unsigned)SPOOL_MAX);
        state->usb_failed = true;
        return;
    }
    if (state->document_bytes > state->spool_cap) {
        size_t cap = state->spool_cap == 0 ? 256 * 1024 : state->spool_cap;
        while (cap < state->document_bytes) cap *= 2;
        if (cap > SPOOL_MAX) cap = SPOOL_MAX;
        uint8_t *grown = heap_caps_realloc(state->spool, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (grown == NULL) {
            ESP_LOGW(TAG, "no memory to hold the document, job dropped");
            state->usb_failed = true;
            return;
        }
        state->spool = grown;
        state->spool_cap = cap;
    }
    memcpy(state->spool + used, data, len);
}

static int process_decoded(request_state_t *state, const uint8_t *data, size_t len)
{
    size_t document_at = 0;

    state->body_bytes += len;
    while (len != 0 && !state->parsed) {
        if (state->ipp_len == IPP_BODY_CAP) {
            return -1;
        }
        size_t copy = IPP_BODY_CAP - state->ipp_len;
        if (copy > len) {
            copy = len;
        }
        memcpy(state->ipp_body + state->ipp_len, data, copy);
        state->ipp_len += copy;
        data += copy;
        len -= copy;
        if (ipp_parse_request(state->ipp_body, state->ipp_len, &state->request) == 0) {
            state->parsed = true;
            document_at = state->request.body_offset;
            start_forwarding(state);
            if (state->request.op == IPP_OP_PRINT_JOB && !state->admitted) {
                send_ipp_response(state, state->admission_status);
            }
            if (document_at < state->ipp_len) {
                forward_document(state, state->ipp_body + document_at,
                                 state->ipp_len - document_at);
            }
        }
    }
    if (state->parsed && len != 0) {
        forward_document(state, data, len);
    }
    return 0;
}

static uint16_t request_status(const request_state_t *state)
{
    const ipp_request_t *request = &state->request;

    if (request->op == IPP_OP_GET_PRINTER_ATTRIBUTES || request->op == IPP_OP_GET_JOBS) {
        return IPP_STATUS_OK;
    }
    if (request->op == IPP_OP_GET_JOB_ATTRIBUTES || request->op == IPP_OP_CANCEL_JOB) {
        return state->job_present ? IPP_STATUS_OK : IPP_STATUS_NOT_FOUND;
    }
    if (request->op == IPP_OP_VALIDATE_JOB) {
        return (request->document_format[0] == '\0' ||
                strcmp(request->document_format, "image/urf") == 0) &&
               request->copies <= 99
                   ? IPP_STATUS_OK : IPP_STATUS_DOCUMENT_FORMAT_NOT_SUPPORTED;
    }
    if (request->op == IPP_OP_PRINT_JOB) {
        if (!state->admitted) return state->admission_status;
        return state->job.result == IPP_JOB_FAILED ? IPP_STATUS_INTERNAL_ERROR : IPP_STATUS_OK;
    }
    return 0x0501;
}

static size_t build_response(const request_state_t *state, uint16_t status,
                             uint8_t *buffer, size_t cap)
{
    ipp_writer_t writer;

    ipp_w_begin(&writer, buffer, cap, &state->request, status);
    if (state->request.op == IPP_OP_GET_PRINTER_ATTRIBUTES && status == IPP_STATUS_OK) {
        printer_attrs_write(&writer, state->printer_uri);
    } else if (state->job_present &&
               (state->request.op == IPP_OP_PRINT_JOB ||
                 ((state->request.op == IPP_OP_GET_JOB_ATTRIBUTES ||
                   state->request.op == IPP_OP_GET_JOBS) && status == IPP_STATUS_OK))) {
        char job_uri[IPP_URI_CAP + 12];
        snprintf(job_uri, sizeof(job_uri), "%s/%lu", state->printer_uri,
                 (unsigned long)state->job.id);
        ipp_w_group(&writer, 0x02);
        ipp_w_int(&writer, 0x21, "job-id", (int32_t)state->job.id);
        ipp_w_str(&writer, 0x45, "job-uri", job_uri);
        ipp_w_int(&writer, 0x23, "job-state",
                  state->job_pending ? 5 : state->job.result == IPP_JOB_OK ? 9 :
                  state->job.result == IPP_JOB_CANCELLED ? 7 : 8);
        ipp_w_str(&writer, 0x44, "job-state-reasons",
                  state->job_pending ? "job-printing" :
                  state->job.result == IPP_JOB_OK ? "job-completed-successfully" :
                  state->job.result == IPP_JOB_CANCELLED ? "job-canceled-by-user" :
                  "aborted-by-system");
    }
    return ipp_w_end(&writer);
}

static void send_ipp_response(request_state_t *state, uint16_t status)
{
    state->response_sent = true;
    size_t len = build_response(state, status, state->response, IPP_RESPONSE_CAP);
    if (len == 0) return;
    char head[128];
    int head_len = snprintf(head, sizeof(head),
                            "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\n"
                            "Content-Length: %u\r\nConnection: close\r\n\r\n", (unsigned)len);
    if (head_len > 0 && (size_t)head_len < sizeof(head) &&
        send_all(state->socket, head, (size_t)head_len)) {
        send_all(state->socket, state->response, len);
    }
}

static void finish_forwarding(request_state_t *state)
{
    if (!state->admitted) {
        return;
    }
    /* Only a complete, uncancelled document reaches the printer; anything else is dropped without printing a page. */
    size_t send_len = state->document_bytes;
    if (!state->usb_failed && state->document_bytes >= 4 && memcmp(state->spool, "RaS2", 4) == 0) {
        size_t converted = raster_pwg_to_urf(state->spool, state->document_bytes);
        if (converted == 0) {
            ESP_LOGW(TAG, "PWG raster not usable (needs 8-bit gray pages), job dropped");
            state->usb_failed = true;
        } else {
            send_len = converted;
        }
    }
    int copies = state->request.copies > 0 && state->request.copies <= 99 ? (int)state->request.copies : 1;
    for (int copy = 0; copy < copies && !state->usb_failed && !atomic_load(&s_cancel_requested) &&
                       state->document_bytes != 0; copy++) {
        /* Every copy is a complete framed document, so the printer sees an ordinary job each time. */
        uint8_t frame[64];
        size_t len = pjl_frame_header(frame, sizeof(frame));
        if (len == 0 || usb_printer_write(frame, len, 5000) != ESP_OK) state->usb_failed = true;
        for (size_t at = 0; !state->usb_failed && at < send_len; ) {
            if (atomic_load(&s_cancel_requested)) break;
            size_t chunk = send_len - at < SPOOL_SEND_CHUNK ? send_len - at : SPOOL_SEND_CHUNK;
            esp_err_t err = usb_printer_write(state->spool + at, chunk, 5000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "usb write failed: %s", esp_err_to_name(err));
                state->usb_failed = true;
            }
            at += chunk;
        }
        len = pjl_frame_trailer(frame, sizeof(frame));
        if (len == 0 || usb_printer_write(frame, len, 5000) != ESP_OK) {
            ESP_LOGW(TAG, "trailer write failed");
            state->usb_failed = true;
        }
    }
    state->job.bytes = state->document_bytes;
    state->job.result = state->usb_failed ? IPP_JOB_FAILED :
                        atomic_load(&s_cancel_requested) ? IPP_JOB_CANCELLED : IPP_JOB_OK;
    finish_job_record(&state->job);
}

esp_err_t ipp_server_run_stream(const char *name,
                                size_t (*next)(void *ctx, uint8_t *buf, size_t cap),
                                void *ctx)
{
    uint8_t *buffer;
    uint8_t frame[64];
    ipp_job_info_t job = { 0 };
    esp_err_t result = ESP_OK;
    size_t len;
    size_t total = 0;

    if (name == NULL || next == NULL) {
        ipp_server_end_job();
        return ESP_ERR_INVALID_ARG;
    }
    buffer = heap_caps_malloc(IPP_RECEIVE_CAP, MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        ipp_server_end_job();
        return ESP_ERR_NO_MEM;
    }
    if (print_state_get().state == PRINTER_STOPPED) {
        free(buffer);
        ipp_server_end_job();
        return ESP_ERR_INVALID_STATE;
    }
    begin_job_record(&job, name);
    len = pjl_frame_header(frame, sizeof(frame));
    if (len == 0 || usb_printer_write(frame, len, 5000) != ESP_OK) {
        result = ESP_FAIL;
    }
    while (result == ESP_OK && !atomic_load(&s_cancel_requested) &&
           (len = next(ctx, buffer, IPP_RECEIVE_CAP)) != 0) {
        total += len;
        if (usb_printer_write(buffer, len, 5000) != ESP_OK) result = ESP_FAIL;
    }
    len = pjl_frame_trailer(frame, sizeof(frame));
    if (len == 0 || usb_printer_write(frame, len, 5000) != ESP_OK) {
        result = ESP_FAIL;
    }
    job.bytes = total;
    job.result = result != ESP_OK ? IPP_JOB_FAILED :
                 atomic_load(&s_cancel_requested) ? IPP_JOB_CANCELLED : IPP_JOB_OK;
    finish_job_record(&job);
    free(buffer);
    return result;
}

static void send_http_error(int socket, const char *status)
{
    char head[128];
    int len = snprintf(head, sizeof(head),
                       "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    if (len > 0 && (size_t)len < sizeof(head)) send_all(socket, head, (size_t)len);
}

static void make_printer_uri(int socket, const char *head, size_t len,
                             char *uri, size_t cap)
{
    char host[256] = { 0 };
    size_t at = 0;

    while (at < len) {
        const char *lf = memchr(head + at, '\n', len - at);
        if (lf == NULL) break;
        const char *end = lf;
        if (end > head + at && end[-1] == '\r') --end;
        const char *colon = memchr(head + at, ':', (size_t)(end - (head + at)));
        if (colon != NULL && colon - (head + at) == 4 &&
            strncasecmp(head + at, "Host", 4) == 0) {
            const char *value = colon + 1;
            while (value < end && (*value == ' ' || *value == '\t')) ++value;
            while (end > value && (end[-1] == ' ' || end[-1] == '\t')) --end;
            size_t host_len = (size_t)(end - value);
            bool valid = host_len > 0 && host_len < sizeof(host);
            for (size_t i = 0; i < host_len && valid; ++i) {
                unsigned char c = (unsigned char)value[i];
                valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                        c == ':' || c == '[' || c == ']';
            }
            if (valid) memcpy(host, value, host_len);
            break;
        }
        at = (size_t)(lf - head) + 1;
    }
    if (host[0] == '\0') {
        struct sockaddr_in local;
        socklen_t local_len = sizeof(local);
        if (getsockname(socket, (struct sockaddr *)&local, &local_len) == 0) {
            char ip[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip)) != NULL) {
                snprintf(host, sizeof(host), "%s:%u", ip, (unsigned)ntohs(local.sin_port));
            }
        }
    }
    snprintf(uri, cap, "ipp://%s/ipp/print", host);
}

/* Extract only operation job-id from the validated attribute block. */
static uint32_t requested_job_id(const request_state_t *state)
{
    size_t at = 8;
    size_t end = state->request.body_offset;
    uint8_t group = 0;
    if (end > state->ipp_len) return 0;
    while (at < end) {
        uint8_t tag = state->ipp_body[at++];
        if (tag == 0x03) break;
        if (tag <= 0x0f) { group = tag; continue; }
        if (end - at < 2) return 0;
        size_t name_len = ((size_t)state->ipp_body[at] << 8) | state->ipp_body[at + 1];
        at += 2;
        if (end - at < name_len + 2) return 0;
        const uint8_t *name = state->ipp_body + at;
        at += name_len;
        size_t value_len = ((size_t)state->ipp_body[at] << 8) | state->ipp_body[at + 1];
        at += 2;
        if (end - at < value_len) return 0;
        if (group == 0x01 && tag == 0x21 && name_len == 6 && value_len == 4 &&
            memcmp(name, "job-id", 6) == 0) {
            const uint8_t *v = state->ipp_body + at;
            return ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                   ((uint32_t)v[2] << 8) | v[3];
        }
        at += value_len;
    }
    return 0;
}

static void handle_job_operation(request_state_t *state)
{
    if (state->request.op == IPP_OP_GET_JOBS) {
        /* Only the default which-jobs is supported: active job, otherwise last job. */
        portENTER_CRITICAL(&s_job_lock);
        if (s_have_active_job) {
            state->job = s_active_job;
            state->job_present = true;
            state->job_pending = true;
        } else if (s_have_last_job) {
            state->job = s_last_job;
            state->job_present = true;
        }
        portEXIT_CRITICAL(&s_job_lock);
        return;
    }
    if (state->request.op != IPP_OP_CANCEL_JOB &&
        state->request.op != IPP_OP_GET_JOB_ATTRIBUTES) return;
    uint32_t id = requested_job_id(state);
    portENTER_CRITICAL(&s_job_lock);
    if (id != 0 && s_have_active_job && s_active_job.id == id) {
        state->job = s_active_job;
        state->job_present = true;
        state->job_pending = true;
        if (state->request.op == IPP_OP_CANCEL_JOB) ipp_server_cancel_current();
    } else if (id != 0 && state->request.op == IPP_OP_GET_JOB_ATTRIBUTES &&
               s_have_last_job && s_last_job.id == id) {
        state->job = s_last_job;
        state->job_present = true;
    }
    portEXIT_CRITICAL(&s_job_lock);
}

static void handle_client(int socket)
{
    connection_buffers_t *buffers = heap_caps_malloc(sizeof(*buffers), MALLOC_CAP_8BIT);
    if (buffers == NULL) {
        send_http_error(socket, "503 Service Unavailable");
        close_socket(socket);
        return;
    }
    memset(&buffers->state, 0, sizeof(buffers->state));
    request_state_t *state = &buffers->state;
    state->socket = socket;
    state->response = buffers->response;
    char *head = buffers->head;
    uint8_t *receive = buffers->receive;
    uint8_t *decoded = buffers->decoded;
    size_t head_len = 0;
    int head_size = 0;
    http_head_t http;
    bool body_error = false;
    struct timeval receive_timeout = { .tv_sec = 10, .tv_usec = 0 };
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout,
                   sizeof(receive_timeout)) != 0) goto cleanup;

    while (head_len < IPP_HEAD_CAP) {
        int received = recv(socket, head + head_len, IPP_HEAD_CAP - head_len, 0);
        if (received <= 0) {
            send_http_error(socket, "400 Bad Request");
            goto cleanup;
        }
        head_len += (size_t)received;
        head_size = http_parse_head(head, head_len, &http);
        if (head_size != 0) break;
    }
    if (head_size <= 0) {
        send_http_error(socket, head_size == 0 ? "431 Request Header Fields Too Large" : "400 Bad Request");
        goto cleanup;
    }
    if (strcmp(http.method, "POST") != 0 || strcmp(http.path, "/ipp/print") != 0) {
        send_http_error(socket, "404 Not Found");
        goto cleanup;
    }
    make_printer_uri(socket, head, (size_t)head_size, state->printer_uri, sizeof(state->printer_uri));
    if (http.expect_continue) {
        static const char continue_response[] = "HTTP/1.1 100 Continue\r\n\r\n";
        if (!send_all(socket, continue_response, sizeof(continue_response) - 1)) goto cleanup;
    }

    if (http.chunked) {
        chunk_dec_t decoder;
        chunk_dec_init(&decoder);
        size_t initial = head_len - (size_t)head_size;
        size_t used;
        int produced = chunk_dec_feed(&decoder, (uint8_t *)head + head_size, initial,
                                      decoded, &used);
        if (produced < 0 || process_decoded(state, decoded, (size_t)produced) < 0) {
            body_error = true;
        }
        while (!body_error && !chunk_dec_done(&decoder)) {
            int received = recv(socket, receive, IPP_RECEIVE_CAP, 0);
            if (received <= 0) { body_error = true; break; }
            size_t at = 0;
            while (at < (size_t)received && !body_error) {
                produced = chunk_dec_feed(&decoder, receive + at, (size_t)received - at,
                                          decoded, &used);
                if (produced < 0 || used == 0 || process_decoded(state, decoded, (size_t)produced) < 0) {
                    body_error = true;
                }
                at += used;
            }
        }
        if (!chunk_dec_done(&decoder)) body_error = true;
    } else {
        size_t remaining = http.content_length;
        size_t initial = head_len - (size_t)head_size;
        if (initial > remaining) initial = remaining;
        if (process_decoded(state, (uint8_t *)head + head_size, initial) < 0) body_error = true;
        remaining -= initial;
        while (!body_error && remaining != 0) {
            size_t want = remaining < IPP_RECEIVE_CAP ? remaining : IPP_RECEIVE_CAP;
            int received = recv(socket, receive, want, 0);
            if (received <= 0) {
                ESP_LOGW(TAG, "body receive ended (recv=%d errno=%d, %u bytes left)", received, errno, (unsigned)remaining);
                body_error = true;
                break;
            }
            if (process_decoded(state, receive, (size_t)received) < 0) body_error = true;
            remaining -= (size_t)received;
        }
        if (remaining != 0) body_error = true;
    }
    if (!state->parsed) body_error = true;
    if (state->admitted && body_error) state->usb_failed = true;
    finish_forwarding(state);
    if (!body_error) handle_job_operation(state);

    uint16_t status = body_error && (!state->parsed || state->request.op != IPP_OP_PRINT_JOB)
                          ? IPP_STATUS_BAD_REQUEST : request_status(state);
    if (!state->response_sent) send_ipp_response(state, status);
cleanup:
    heap_caps_free(state->spool);
    free(buffers);
    close_socket(socket);
}

static void connection_task(void *arg)
{
    int socket = (int)(intptr_t)arg;
    handle_client(socket);
    vTaskDelete(NULL);
}

static void listener_task(void *arg)
{
    (void)arg;
    while (true) {
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listener < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in address = {
            .sin_family = AF_INET, .sin_port = htons(IPP_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
            listen(listener, 4) < 0) {
            close_socket(listener);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "listening on port %d", IPP_PORT);
        while (true) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) break;
            if (xTaskCreate(connection_task, "ipp_conn", IPP_TASK_STACK,
                            (void *)(intptr_t)client, 5, NULL) != pdPASS) {
                send_http_error(client, "503 Service Unavailable");
                close_socket(client);
            }
        }
        close_socket(listener);
    }
}

esp_err_t ipp_server_start(void)
{
    if (s_listener_task != NULL) return ESP_ERR_INVALID_STATE;
    if (xTaskCreate(listener_task, "ipp_listener", 4096, NULL, 5, &s_listener_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool ipp_server_job_active(void) { return atomic_load(&s_job_active); }

void ipp_server_cancel_current(void)
{
    if (atomic_load(&s_job_active)) atomic_store(&s_cancel_requested, true);
}

const ipp_job_info_t *ipp_server_last_job(void)
{
    return s_have_last_job ? &s_last_job : NULL;
}
