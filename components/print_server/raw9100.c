// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "raw9100.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ipp_server.h"
#include "lwip/sockets.h"
#include "usb_printer.h"

#define RAW9100_PORT 9100
#define RAW9100_BACKLOG 1
#define RAW9100_BUFFER_SIZE 4096
#define RAW9100_RECEIVE_TIMEOUT_SECONDS 30
#define RAW9100_TASK_STACK_SIZE 4096
#define RAW9100_TASK_PRIORITY 5

static const char *TAG = "raw9100";
static TaskHandle_t s_task;

static void close_socket(int sock)
{
    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
}

static void handle_client(int client)
{
    if (!ipp_server_try_begin_job()) {
        close_socket(client);
        return;
    }
    int keepalive = 1;
    struct timeval receive_timeout = {
        .tv_sec = RAW9100_RECEIVE_TIMEOUT_SECONDS,
        .tv_usec = 0,
    };
    uint8_t *buffer = heap_caps_malloc(RAW9100_BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (buffer == NULL) {
        close_socket(client);
        ipp_server_end_job();
        return;
    }

    setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));

    if (!usb_printer_connected()) {
        ESP_LOGW(TAG, "closing client: printer is not connected");
        close_socket(client);
        free(buffer);
        ipp_server_end_job();
        return;
    }

    while (true) {
        int received = recv(client, buffer, RAW9100_BUFFER_SIZE, 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGW(TAG, "client receive failed: errno %d", errno);
            break;
        }

        if (usb_printer_write(buffer, (size_t)received, 5000) != ESP_OK) {
            ESP_LOGW(TAG, "closing client: printer write failed");
            break;
        }
    }

    close_socket(client);
    free(buffer);
    ipp_server_end_job();
}

static void raw9100_task(void *arg)
{
    (void)arg;

    while (true) {
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listener < 0) {
            ESP_LOGE(TAG, "socket failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons(RAW9100_PORT),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
            listen(listener, RAW9100_BACKLOG) < 0) {
            ESP_LOGE(TAG, "listener setup failed: errno %d", errno);
            close_socket(listener);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        ESP_LOGI(TAG, "listening on port %d", RAW9100_PORT);
        while (true) {
            int client = accept(listener, NULL, NULL);
            if (client < 0) {
                ESP_LOGW(TAG, "accept failed: errno %d", errno);
                break;
            }
            handle_client(client);
        }

        close_socket(listener);
    }
}

esp_err_t raw9100_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result = xTaskCreate(raw9100_task, "raw9100", RAW9100_TASK_STACK_SIZE,
                                    NULL, RAW9100_TASK_PRIORITY, &s_task);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
