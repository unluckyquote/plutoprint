// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "web_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ipp_server.h"
#include "job_log_sync.h"
#include "print_state.h"
#include "printer_state.h"
#include "settings.h"
#include "urf_testpage.h"
#include "usb_printer.h"
#include "lwip/inet.h"
#include "logbuf.h"
#include "print_mdns.h"
#include "printer_caps.h"
#include "mbedtls/base64.h"
#include "wifi_mgr.h"
#include "esp_netif.h"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char app_css_start[] asm("_binary_app_css_start");
extern const char app_css_end[] asm("_binary_app_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");
extern const char favicon_svg_start[] asm("_binary_favicon_svg_start");
extern const char favicon_svg_end[] asm("_binary_favicon_svg_end");

static const char *TAG = "web_ui";
static httpd_handle_t server;
static int64_t s_last_clear_us;   /* uptime of the last clear; the clock is not synchronised, so the interval counts from boot */

static void clear_jobs_now(void)
{
    job_log_sync_clear();
    s_last_clear_us = esp_timer_get_time();
}

static void auto_clear_timer(void *arg)
{
    uint32_t hours = settings_get()->jobs_clear_hours;
    if (hours != 0 && esp_timer_get_time() - s_last_clear_us >= (int64_t)hours * 3600 * 1000000) clear_jobs_now();
}

static esp_err_t ok_handler(httpd_req_t *req);

static esp_err_t clear_handler(httpd_req_t *req)
{
    clear_jobs_now();
    return ok_handler(req);
}
static void reboot_task(void *arg);

static esp_err_t send_json(httpd_req_t *req, cJSON *value, int status)
{
    char *text = cJSON_PrintUnformatted(value);
    if (text == NULL) return ESP_ERR_NO_MEM;
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, int status, const char *message)
{
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(object, "ok", 0);
    cJSON_AddStringToObject(object, "error", message);
    char status_text[32];
    snprintf(status_text, sizeof(status_text), "%d Error", status);
    httpd_resp_set_status(req, status_text);
    httpd_resp_set_type(req, "application/json");
    char *text = cJSON_PrintUnformatted(object);
    esp_err_t err = text == NULL ? ESP_ERR_NO_MEM : httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    if (text != NULL) cJSON_free(text);
    cJSON_Delete(object);
    return err;
}

static esp_err_t read_body(httpd_req_t *req, char body[513])
{
    if (req->content_len > 512) return ESP_ERR_INVALID_SIZE;
    size_t total = 0;
    while (total < req->content_len) {
        int received = httpd_req_recv(req, body + total, req->content_len - total);
        if (received <= 0) return ESP_FAIL;
        total += (size_t)received;
    }
    body[total] = '\0';
    return ESP_OK;
}

static esp_err_t read_json(httpd_req_t *req, cJSON **out)
{
    char body[513];
    esp_err_t err = read_body(req, body);
    if (err != ESP_OK) return err;
    *out = cJSON_Parse(body);
    return *out == NULL ? ESP_ERR_INVALID_ARG : ESP_OK;
}

static esp_err_t file_handler(httpd_req_t *req)
{
    const char *start = index_html_start;
    const char *end = index_html_end;
    const char *type = "text/html";
    if (strcmp(req->uri, "/app.css") == 0) { start = app_css_start; end = app_css_end; type = "text/css"; }
    if (strcmp(req->uri, "/favicon.svg") == 0) { start = favicon_svg_start; end = favicon_svg_end; type = "image/svg+xml"; }
    if (strcmp(req->uri, "/app.js") == 0) { start = app_js_start; end = app_js_end; type = "application/javascript"; }
    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    /* EMBED_TXTFILES appends a NUL terminator; a NUL in app.js is a syntax error in browsers. */
    ssize_t len = (ssize_t)(end - start);
    if (len > 0 && start[len - 1] == '\0') len--;
    return httpd_resp_send(req, start, len);
}

static const char *status_text(printer_status_t status)
{
    if (status.state == PRINTER_PROCESSING) return "Printing";
    if (strcmp(status.reason, "media-empty-error") == 0) return "Paper empty";
    if (strcmp(status.reason, "offline-report") == 0) return "Printer disconnected";
    if (strcmp(status.reason, "other-error") == 0) return "Printer error";
    return "Ready";
}

static esp_err_t status_handler(httpd_req_t *req)
{
    printer_status_t state = print_state_get();
    const settings_t *settings = settings_get();
    wifi_ap_record_t ap = { 0 };
    bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = { 0 };
    char ip_text[16] = "";
    if (netif != NULL && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&ip.ip));
    }
    const ipp_job_info_t *last = ipp_server_last_job();
    int64_t now = (int64_t)time(NULL);
    cJSON *object = cJSON_CreateObject();
    if (object == NULL) return ESP_ERR_NO_MEM;
    printer_caps_t caps;
    printer_caps_get(&caps);
    cJSON_AddStringToObject(object, "printer_model", caps.model);
    cJSON_AddStringToObject(object, "name", settings->name);
    cJSON_AddStringToObject(object, "state", state.state == PRINTER_PROCESSING ? "printing" : state.state == PRINTER_STOPPED ? "error" : "ready");
    cJSON_AddStringToObject(object, "status_text", status_text(state));
    int toner = print_state_toner_percent();
    if (toner >= 0) cJSON_AddNumberToObject(object, "toner_percent", toner);
    else cJSON_AddNullToObject(object, "toner_percent");
    if (last != NULL && now >= last->finished_unix_s) cJSON_AddNumberToObject(object, "last_job_age_s", now - last->finished_unix_s);
    else cJSON_AddNullToObject(object, "last_job_age_s");
    cJSON_AddStringToObject(object, "wifi_ssid", have_ap ? (const char *)ap.ssid : "");
    cJSON_AddNumberToObject(object, "wifi_rssi", have_ap ? ap.rssi : 0);
    cJSON_AddStringToObject(object, "ip", ip_text);
    cJSON_AddNumberToObject(object, "uptime_s", esp_timer_get_time() / 1000000);
    cJSON_AddStringToObject(object, "fw", esp_app_get_description()->version);
    esp_err_t err = send_json(req, object, 200);
    cJSON_Delete(object);
    return err;
}

static esp_err_t jobs_handler(httpd_req_t *req)
{
    job_record_t records[5];
    size_t count = job_log_sync_get(records, 5);
    time_t now = time(NULL);
    cJSON *array = cJSON_CreateArray();
    if (array == NULL) return ESP_ERR_NO_MEM;
    for (size_t i = 0; i < count; ++i) {
        cJSON *job = cJSON_CreateObject();
        cJSON_AddNumberToObject(job, "id", records[i].id);
        cJSON_AddStringToObject(job, "name", records[i].name);
        cJSON_AddNumberToObject(job, "bytes", (double)records[i].bytes);
        cJSON_AddStringToObject(job, "result", records[i].result == JOB_RESULT_CANCELLED ? "cancelled" : records[i].result == JOB_RESULT_FAILED ? "failed" : "ok");
        cJSON_AddNumberToObject(job, "age_s", now >= records[i].finished_unix_s ? now - records[i].finished_unix_s : 0);
        cJSON_AddItemToArray(array, job);
    }
    esp_err_t err = send_json(req, array, 200);
    cJSON_Delete(array);
    return err;
}

static esp_err_t settings_handler(httpd_req_t *req)
{
    const settings_t *settings = settings_get();
    if (req->method == HTTP_POST) {
        cJSON *body;
        if (read_json(req, &body) != ESP_OK) return send_error(req, 400, "Invalid JSON");
        cJSON *name = cJSON_GetObjectItem(body, "name");
        cJSON *location = cJSON_GetObjectItem(body, "location");
        cJSON *hostname = cJSON_GetObjectItem(body, "hostname");
        esp_err_t err = cJSON_IsString(name) && cJSON_IsString(location) && cJSON_IsString(hostname) ?
                        settings_set_device(name->valuestring, location->valuestring, hostname->valuestring) : ESP_ERR_INVALID_ARG;
        cJSON *clear_hours = cJSON_GetObjectItem(body, "jobs_clear_hours");
        if (err == ESP_OK && clear_hours != NULL) {
            /* The form posts it as a string, other clients may send a number. */
            uint32_t hours = cJSON_IsString(clear_hours) ? (uint32_t)strtoul(clear_hours->valuestring, NULL, 10) :
                             cJSON_IsNumber(clear_hours) ? (uint32_t)clear_hours->valuedouble : UINT32_MAX;
            err = settings_set_jobs_clear_hours(hours);
            s_last_clear_us = esp_timer_get_time();
        }
        cJSON *color_mode = cJSON_GetObjectItem(body, "color_mode");
        cJSON *duplex = cJSON_GetObjectItem(body, "duplex");
        if (err == ESP_OK && (color_mode != NULL || duplex != NULL)) {
            uint32_t mode = settings_get()->color_mode;
            uint32_t two_sided = settings_get()->duplex ? 1 : 0;
            if (color_mode != NULL) mode = cJSON_IsString(color_mode) ? (uint32_t)strtoul(color_mode->valuestring, NULL, 10) :
                                           cJSON_IsNumber(color_mode) ? (uint32_t)color_mode->valuedouble : UINT32_MAX;
            if (duplex != NULL) two_sided = cJSON_IsString(duplex) ? (uint32_t)strtoul(duplex->valuestring, NULL, 10) :
                                            cJSON_IsNumber(duplex) ? (uint32_t)duplex->valuedouble : cJSON_IsTrue(duplex) ? 1 : 0;
            err = mode <= 2 ? settings_set_features((uint8_t)mode, two_sided != 0) : ESP_ERR_INVALID_ARG;
            if (err == ESP_OK) print_mdns_refresh();
        }
        cJSON_Delete(body);
        if (err != ESP_OK) return send_error(req, 400, "Invalid settings");
        settings = settings_get();
    }
    cJSON *object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "name", settings->name);
    cJSON_AddStringToObject(object, "location", settings->location);
    cJSON_AddStringToObject(object, "hostname", settings->hostname);
    cJSON_AddStringToObject(object, "ssid", settings->ssid);
    cJSON_AddNumberToObject(object, "jobs_clear_hours", settings->jobs_clear_hours);
    cJSON_AddNumberToObject(object, "color_mode", settings->color_mode);
    cJSON_AddBoolToObject(object, "duplex", settings->duplex);
    cJSON_AddBoolToObject(object, "password_protected", settings->password_set);
    esp_err_t err = send_json(req, object, 200);
    cJSON_Delete(object);
    return err;
}

static esp_err_t ok_handler(httpd_req_t *req)
{
    cJSON *object = cJSON_CreateObject();
    cJSON_AddBoolToObject(object, "ok", 1);
    esp_err_t err = send_json(req, object, 200);
    cJSON_Delete(object);
    return err;
}

static size_t testpage_next(void *ctx, uint8_t *buf, size_t cap)
{
    return urf_testpage_next((urf_testpage_t *)ctx, buf, cap);
}

static void testpage_task(void *arg)
{
    urf_testpage_t *page = arg;
    esp_err_t err = ipp_server_run_stream("Test page", testpage_next, page);
    if (err != ESP_OK) ESP_LOGW(TAG, "Test page failed: %s", esp_err_to_name(err));
    free(page);
    vTaskDelete(NULL);
}

static esp_err_t testpage_handler(httpd_req_t *req)
{
    if (print_state_get().state == PRINTER_STOPPED || !ipp_server_try_begin_job()) {
        return send_error(req, 409, "Printer is busy");
    }
    urf_testpage_t *page = heap_caps_malloc(sizeof(*page), MALLOC_CAP_8BIT);
    if (page == NULL) {
        ipp_server_end_job();
        return send_error(req, 500, "Test page failed");
    }
    urf_testpage_init(page);
    if (xTaskCreate(testpage_task, "web_testpage", 8192, page, 5, NULL) != pdPASS) {
        free(page);
        ipp_server_end_job();
        return send_error(req, 500, "Test page failed");
    }
    return ok_handler(req);
}

static esp_err_t cancel_handler(httpd_req_t *req)
{
    ipp_server_cancel_current();
    return ok_handler(req);
}

static esp_err_t reboot_handler(httpd_req_t *req)
{
    if (xTaskCreate(reboot_task, "web_reboot", 2048, NULL, 5, NULL) != pdPASS) {
        return send_error(req, 500, "Could not reboot");
    }
    return ok_handler(req);
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    cJSON *body;
    if (read_json(req, &body) != ESP_OK) return send_error(req, 400, "Invalid JSON");
    cJSON *ssid = cJSON_GetObjectItem(body, "ssid");
    cJSON *password = cJSON_GetObjectItem(body, "password");
    esp_err_t err = cJSON_IsString(ssid) && cJSON_IsString(password) ? settings_set_wifi(ssid->valuestring, password->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(body);
    if (err != ESP_OK) return send_error(req, 400, "Invalid WiFi settings");
    if (xTaskCreate(reboot_task, "web_reboot", 2048, NULL, 5, NULL) != pdPASS) return send_error(req, 500, "Could not reboot");
    return ok_handler(req);
}

static esp_err_t ota_handler(httpd_req_t *req)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle;
    if (partition == NULL || esp_ota_begin(partition, req->content_len, &handle) != ESP_OK) return send_error(req, 500, "OTA start failed");
    /* On the heap: a 4 KB stack buffer plus the flash write path overflowed the httpd task stack. */
    const size_t buffer_size = 4096;
    uint8_t *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        esp_ota_abort(handle);
        return send_error(req, 500, "Out of memory");
    }
    int remaining = req->content_len;
    bool failed = false;
    while (remaining > 0) {
        int want = remaining > (int)buffer_size ? (int)buffer_size : remaining;
        int received = httpd_req_recv(req, (char *)buffer, want);
        if (received <= 0 || esp_ota_write(handle, buffer, received) != ESP_OK) { failed = true; break; }
        remaining -= received;
    }
    free(buffer);
    if (failed || remaining != 0) {
        esp_ota_abort(handle);
        return send_error(req, 500, "OTA failed");
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) {
        return send_error(req, 500, "OTA failed");
    }
    if (xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL) != pdPASS) return send_error(req, 500, "Could not reboot");
    return ok_handler(req);
}

/* HTTP Basic, user name "admin". Only the changing actions are checked; reading status and jobs stays open. */
static bool authorized(httpd_req_t *req)
{
    if (!settings_get()->password_set) return true;
    char header[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK ||
        strncmp(header, "Basic ", 6) != 0) return false;
    unsigned char decoded[100];
    size_t length = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &length, (const unsigned char *)header + 6, strlen(header + 6)) != 0) return false;
    decoded[length] = '\0';
    char *colon = strchr((char *)decoded, ':');
    if (colon == NULL || (size_t)(colon - (char *)decoded) != 5 || strncmp((char *)decoded, "admin", 5) != 0) return false;
    return settings_password_ok(colon + 1);
}

static esp_err_t unauthorized(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Pluto Print\"");
    return send_error(req, 401, "Password required");
}

static esp_err_t log_handler(httpd_req_t *req)
{
    if (!authorized(req)) return unauthorized(req);
    char *text = heap_caps_malloc(16385, MALLOC_CAP_8BIT);
    if (text == NULL) return send_error(req, 500, "Out of memory");
    size_t length = logbuf_read(text, 16385);
    httpd_resp_set_type(req, "text/plain");
    esp_err_t err = httpd_resp_send(req, text, (ssize_t)length);
    free(text);
    return err;
}

static esp_err_t password_handler(httpd_req_t *req)
{
    cJSON *body;
    if (read_json(req, &body) != ESP_OK) return send_error(req, 400, "Invalid JSON");
    cJSON *password = cJSON_GetObjectItem(body, "password");
    esp_err_t err = cJSON_IsString(password) ? settings_set_password(password->valuestring) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(body);
    if (err != ESP_OK) return send_error(req, 400, "Password must be 6 to 64 characters");
    return ok_handler(req);
}

static esp_err_t networks_handler(httpd_req_t *req)
{
    wifi_mgr_network_t *networks = calloc(16, sizeof(*networks));
    if (networks == NULL) return send_error(req, 500, "Out of memory");
    size_t count = wifi_mgr_scan(networks, 16);
    cJSON *array = cJSON_CreateArray();
    for (size_t i = 0; array != NULL && i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", networks[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", networks[i].rssi);
        cJSON_AddBoolToObject(item, "secure", networks[i].secure);
        cJSON_AddItemToArray(array, item);
    }
    free(networks);
    if (array == NULL) return send_error(req, 500, "Out of memory");
    esp_err_t err = send_json(req, array, 200);
    cJSON_Delete(array);
    return err;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    const char *route = req->user_ctx;
    /* The API intentionally has no login (home-network device); X-Requested-With guards POSTs. */
    char requested_with[sizeof("printer-ui")];
    if (httpd_req_get_hdr_value_len(req, "X-Requested-With") != strlen("printer-ui") ||
        httpd_req_get_hdr_value_str(req, "X-Requested-With", requested_with, sizeof(requested_with)) != ESP_OK ||
        strcmp(requested_with, "printer-ui") != 0) {
        return send_error(req, 403, "Forbidden");
    }
    if (!authorized(req)) return unauthorized(req);
    if (strcmp(route, "/api/ota") == 0) return ota_handler(req);
    if (req->content_len > 512) return send_error(req, 400, "Request body too large");
    if (strcmp(route, "/api/settings") == 0) return settings_handler(req);
    if (strcmp(route, "/api/wifi") == 0) return wifi_handler(req);
    if (strcmp(route, "/api/password") == 0) return password_handler(req);
    char body[513];
    if (read_body(req, body) != ESP_OK) return send_error(req, 400, "Invalid request body");
    if (strcmp(route, "/api/jobs/cancel") == 0) return cancel_handler(req);
    if (strcmp(route, "/api/jobs/clear") == 0) return clear_handler(req);
    if (strcmp(route, "/api/testpage") == 0) return testpage_handler(req);
    return reboot_handler(req);
}

esp_err_t web_ui_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 20;
    config.stack_size = 6144;
    if (httpd_start(&server, &config) != ESP_OK) return ESP_FAIL;
    const httpd_uri_t files[] = {
        { .uri = "/", .method = HTTP_GET, .handler = file_handler },
        { .uri = "/app.css", .method = HTTP_GET, .handler = file_handler },
        { .uri = "/app.js", .method = HTTP_GET, .handler = file_handler },
        { .uri = "/favicon.svg", .method = HTTP_GET, .handler = file_handler },
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) httpd_register_uri_handler(server, &files[i]);
    httpd_uri_t routes[] = {
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_handler },
        { .uri = "/api/jobs", .method = HTTP_GET, .handler = jobs_handler },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_handler },
        { .uri = "/api/settings", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/jobs/cancel", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/jobs/clear", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/testpage", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/reboot", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/password", .method = HTTP_POST, .handler = post_handler },
        { .uri = "/api/wifi/networks", .method = HTTP_GET, .handler = networks_handler },
        { .uri = "/api/log", .method = HTTP_GET, .handler = log_handler },
        { .uri = "/api/ota", .method = HTTP_POST, .handler = post_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        if (routes[i].method == HTTP_POST) routes[i].user_ctx = (void *)routes[i].uri;
        httpd_register_uri_handler(server, &routes[i]);
    }
    static esp_timer_handle_t clear_timer;
    const esp_timer_create_args_t timer_args = { .callback = auto_clear_timer, .name = "jobs_clear" };
    if (clear_timer == NULL && esp_timer_create(&timer_args, &clear_timer) == ESP_OK) esp_timer_start_periodic(clear_timer, 60 * 1000000LL);
    ESP_LOGI(TAG, "web UI listening on port 80");
    return ESP_OK;
}
