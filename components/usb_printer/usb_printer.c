// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

#include "usb_printer.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define PRINTER_VID 0x04e8
#define TRANSFER_SIZE 4096

static const char *TAG = "usb_printer";
#define CONTROL_DATA_MAX 256

typedef struct {
    SemaphoreHandle_t done;
    usb_transfer_status_t status;
    bool pending;
} transfer_context_t;

static struct {
    SemaphoreHandle_t io_lock;
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *out_transfer;
    usb_transfer_t *in_transfer;
    usb_transfer_t *control_transfer;
    transfer_context_t out_context;
    transfer_context_t in_context;
    transfer_context_t control_context;
    uint8_t out_endpoint;
    uint8_t in_endpoint;
    uint16_t out_mps;
    uint16_t in_mps;
    uint8_t interface_number;
    bool interface_claimed;
    volatile bool connected;
    volatile bool faulted;
    bool disconnecting;
    bool drain_timed_out;
    TickType_t disconnect_started;
    bool started;
} s_printer;

static void transfer_callback(usb_transfer_t *transfer)
{
    transfer_context_t *context = transfer->context;
    context->status = transfer->status;
    xSemaphoreGive(context->done);
}

static void clear_connection(void)
{
    s_printer.connected = false;
    s_printer.device = NULL;
    s_printer.out_endpoint = 0;
    s_printer.in_endpoint = 0;
    s_printer.out_mps = 0;
    s_printer.in_mps = 0;
    s_printer.interface_claimed = false;
}

static void close_device(usb_device_handle_t device)
{
    if (device == NULL) {
        return;
    }
    if (s_printer.interface_claimed) {
        usb_host_interface_release(s_printer.client, device, s_printer.interface_number);
    }
    usb_host_device_close(s_printer.client, device);
    clear_connection();
    s_printer.disconnecting = false;
}

static bool transfer_drained(transfer_context_t *context)
{
    if (context->pending && xSemaphoreTake(context->done, 0) == pdTRUE) {
        context->pending = false;
    }
    return !context->pending;
}

static void drain_disconnected_device(void)
{
    /* Keep dispatching callbacks on the client task; never wait inside its event callback. */
    if (xSemaphoreTake(s_printer.io_lock, 0) == pdTRUE) {
        bool out_drained = transfer_drained(&s_printer.out_context);
        bool in_drained = transfer_drained(&s_printer.in_context);
        bool control_drained = transfer_drained(&s_printer.control_context);
        if (out_drained && in_drained && control_drained) {
            close_device(s_printer.device);
        }
        xSemaphoreGive(s_printer.io_lock);
    }
    if (s_printer.disconnecting && !s_printer.drain_timed_out &&
        xTaskGetTickCount() - s_printer.disconnect_started >= pdMS_TO_TICKS(2000)) {
        s_printer.faulted = true;
        s_printer.drain_timed_out = true;
        ESP_LOGE(TAG, "disconnect callbacks did not drain; retaining device until they arrive");
    }
}

static void connect_device(uint8_t address)
{
    usb_device_handle_t device = NULL;
    const usb_device_desc_t *device_desc = NULL;
    const usb_config_desc_t *config_desc = NULL;
    const usb_intf_desc_t *interface_desc = NULL;
    const usb_ep_desc_t *out_desc = NULL;
    const usb_ep_desc_t *in_desc = NULL;
    esp_err_t err = usb_host_device_open(s_printer.client, address, &device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb device open failed for address %u: %s", address, esp_err_to_name(err));
        return;
    }

    err = usb_host_get_device_descriptor(device, &device_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb device descriptor failed: %s", esp_err_to_name(err));
        usb_host_device_close(s_printer.client, device);
        return;
    }
    ESP_LOGI(TAG, "usb device %04x:%04x", device_desc->idVendor, device_desc->idProduct);
    if (device_desc->idVendor != PRINTER_VID) {
        ESP_LOGW(TAG, "unsupported USB device %04x:%04x", device_desc->idVendor, device_desc->idProduct);
        usb_host_device_close(s_printer.client, device);
        return;
    }

    err = usb_host_get_active_config_descriptor(device, &config_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "active config descriptor failed: %s", esp_err_to_name(err));
        usb_host_device_close(s_printer.client, device);
        return;
    }

    int offset = 0;
    const usb_standard_desc_t *descriptor = (const usb_standard_desc_t *)config_desc;
    while (descriptor != NULL) {
        if (descriptor->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *candidate = (const usb_intf_desc_t *)descriptor;
            ESP_LOGI(TAG, "usb interface %u alt %u class %u subclass %u protocol %u endpoints %u",
                     candidate->bInterfaceNumber, candidate->bAlternateSetting, candidate->bInterfaceClass,
                     candidate->bInterfaceSubClass, candidate->bInterfaceProtocol, candidate->bNumEndpoints);
            if (candidate->bAlternateSetting == 0 && candidate->bInterfaceClass == USB_CLASS_PRINTER &&
                candidate->bInterfaceProtocol == 2) {
                interface_desc = candidate;
                break;
            }
        }
        descriptor = usb_parse_next_descriptor(descriptor, config_desc->wTotalLength, &offset);
    }

    if (interface_desc == NULL) {
        ESP_LOGW(TAG, "no printer interface class 7 protocol 2 found");
        usb_host_device_close(s_printer.client, device);
        return;
    }

    for (int index = 0; index < interface_desc->bNumEndpoints; ++index) {
        /* offset is a running cursor inside the helper: restart from the interface for every endpoint */
        offset = (int)((const uint8_t *)interface_desc - (const uint8_t *)config_desc);
        const usb_ep_desc_t *endpoint = usb_parse_endpoint_descriptor_by_index(interface_desc, index,
                                                                                 config_desc->wTotalLength,
                                                                                 &offset);
        if (endpoint == NULL || USB_EP_DESC_GET_XFERTYPE(endpoint) != USB_TRANSFER_TYPE_BULK) {
            continue;
        }
        if (USB_EP_DESC_GET_EP_DIR(endpoint)) {
            in_desc = endpoint;
        } else {
            out_desc = endpoint;
        }
    }

    if (out_desc == NULL || in_desc == NULL || USB_EP_DESC_GET_MPS(out_desc) == 0 || USB_EP_DESC_GET_MPS(in_desc) == 0) {
        ESP_LOGW(TAG, "missing or zero-MPS bulk endpoints: out_desc=%s in_desc=%s out_mps=%u in_mps=%u",
                 out_desc ? "non-null" : "null", in_desc ? "non-null" : "null",
                 out_desc ? USB_EP_DESC_GET_MPS(out_desc) : 0,
                 in_desc ? USB_EP_DESC_GET_MPS(in_desc) : 0);
        usb_host_device_close(s_printer.client, device);
        return;
    }

    err = usb_host_interface_claim(s_printer.client, device, interface_desc->bInterfaceNumber, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "interface claim failed: %s", esp_err_to_name(err));
        usb_host_device_close(s_printer.client, device);
        return;
    }

    s_printer.device = device;
    s_printer.interface_number = interface_desc->bInterfaceNumber;
    s_printer.out_endpoint = out_desc->bEndpointAddress;
    s_printer.in_endpoint = in_desc->bEndpointAddress;
    s_printer.out_mps = USB_EP_DESC_GET_MPS(out_desc);
    s_printer.in_mps = USB_EP_DESC_GET_MPS(in_desc);
    s_printer.interface_claimed = true;
    s_printer.faulted = false;
    s_printer.connected = true;
    ESP_LOGI(TAG, "printer connected %04x:%04x out=0x%02x in=0x%02x", device_desc->idVendor, device_desc->idProduct, s_printer.out_endpoint, s_printer.in_endpoint);
}

static void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    (void)arg;
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV && s_printer.device == NULL) {
        connect_device(event_msg->new_dev.address);
    } else if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE && event_msg->dev_gone.dev_hdl == s_printer.device) {
        s_printer.connected = false;
        s_printer.disconnecting = true;
        s_printer.drain_timed_out = false;
        s_printer.disconnect_started = xTaskGetTickCount();
    }
}

static void usb_library_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

static void usb_client_task(void *arg)
{
    (void)arg;
    const usb_host_client_config_t config = {
        .is_synchronous = false,
        .max_num_event_msg = 4,
        .async = {
            .client_event_callback = client_event_callback,
            .callback_arg = NULL,
        },
    };
    esp_err_t err = usb_host_client_register(&config, &s_printer.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "client registration failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    while (true) {
        usb_host_client_handle_events(s_printer.client,
                                      s_printer.disconnecting ? pdMS_TO_TICKS(20) : portMAX_DELAY);
        if (s_printer.disconnecting) {
            drain_disconnected_device();
        }
    }
}

static esp_err_t submit_transfer(usb_transfer_t *transfer, transfer_context_t *context,
                                 usb_device_handle_t device, uint8_t endpoint,
                                 size_t length, uint32_t timeout_ms)
{
    if (!s_printer.connected || s_printer.faulted || context->pending) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(context->done, 0);
    transfer->device_handle = device;
    transfer->bEndpointAddress = endpoint;
    transfer->num_bytes = (int)length;
    transfer->actual_num_bytes = 0;
    transfer->status = USB_TRANSFER_STATUS_ERROR;
    transfer->callback = transfer_callback;
    transfer->context = context;
    context->pending = true;
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        context->pending = false;
        return err;
    }
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    if (xSemaphoreTake(context->done, ticks) != pdTRUE) {
        esp_err_t halt_err = usb_host_endpoint_halt(device, endpoint);
        esp_err_t flush_err = usb_host_endpoint_flush(device, endpoint);
        if (xSemaphoreTake(context->done, pdMS_TO_TICKS(2000)) != pdTRUE) {
            s_printer.faulted = true;
            return halt_err == ESP_OK && flush_err == ESP_OK ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        context->pending = false;
        esp_err_t clear_err = usb_host_endpoint_clear(device, endpoint);
        if (halt_err != ESP_OK || flush_err != ESP_OK || clear_err != ESP_OK) {
            s_printer.faulted = true;
            return ESP_FAIL;
        }
        return ESP_ERR_TIMEOUT;
    }
    context->pending = false;
    return context->status == USB_TRANSFER_STATUS_COMPLETED ? ESP_OK : ESP_FAIL;
}

esp_err_t usb_printer_init(void)
{
    if (s_printer.started) {
        return ESP_OK;
    }
    s_printer.io_lock = xSemaphoreCreateMutex();
    s_printer.out_context.done = xSemaphoreCreateBinary();
    s_printer.in_context.done = xSemaphoreCreateBinary();
    s_printer.control_context.done = xSemaphoreCreateBinary();
    if (s_printer.io_lock == NULL || s_printer.out_context.done == NULL || s_printer.in_context.done == NULL ||
        s_printer.control_context.done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = NULL,
        .fifo_settings_custom = {0},
        .peripheral_map = 0,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        return err;
    }
    err = usb_host_transfer_alloc(TRANSFER_SIZE, 0, &s_printer.out_transfer);
    if (err == ESP_OK) {
        err = usb_host_transfer_alloc(TRANSFER_SIZE, 0, &s_printer.in_transfer);
    }
    if (err == ESP_OK) {
        err = usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + CONTROL_DATA_MAX, 0, &s_printer.control_transfer);
    }
    if (err != ESP_OK) {
        usb_host_transfer_free(s_printer.out_transfer);
        usb_host_transfer_free(s_printer.in_transfer);
        usb_host_transfer_free(s_printer.control_transfer);
        usb_host_uninstall();
        return err;
    }
    if (xTaskCreate(usb_library_task, "usb_lib", 4096, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(usb_client_task, "usb_client", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_printer.started = true;
    return ESP_OK;
}

bool usb_printer_connected(void)
{
    return s_printer.connected;
}

esp_err_t usb_printer_write(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    if (!s_printer.started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL && len != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_printer.connected || s_printer.faulted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_printer.io_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_OK;
    if (!s_printer.connected || s_printer.faulted || s_printer.out_mps == 0 ||
        s_printer.out_mps > TRANSFER_SIZE) {
        xSemaphoreGive(s_printer.io_lock);
        return ESP_ERR_INVALID_STATE;
    }
    size_t chunk_limit = (TRANSFER_SIZE / s_printer.out_mps) * s_printer.out_mps;
    while (err == ESP_OK && len != 0) {
        size_t chunk = len < chunk_limit ? len : chunk_limit;
        memcpy(s_printer.out_transfer->data_buffer, data, chunk);
        err = submit_transfer(s_printer.out_transfer, &s_printer.out_context, s_printer.device,
                              s_printer.out_endpoint, chunk, timeout_ms);
        data += chunk;
        len -= chunk;
    }
    xSemaphoreGive(s_printer.io_lock);
    return err;
}

esp_err_t usb_printer_read(uint8_t *buf, size_t cap, size_t *got, uint32_t timeout_ms)
{
    if (buf == NULL || got == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *got = 0;
    if (!s_printer.started || !s_printer.connected || s_printer.faulted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_printer.io_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = s_printer.connected && !s_printer.faulted && s_printer.in_mps != 0 &&
                    s_printer.in_mps <= TRANSFER_SIZE ? ESP_OK : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK && cap != 0) {
        size_t limit = (TRANSFER_SIZE / s_printer.in_mps) * s_printer.in_mps;
        size_t request = cap >= limit ? limit :
                         ((cap + s_printer.in_mps - 1) / s_printer.in_mps) * s_printer.in_mps;
        err = submit_transfer(s_printer.in_transfer, &s_printer.in_context, s_printer.device,
                              s_printer.in_endpoint, request, timeout_ms);
        if (err == ESP_OK) {
            size_t received = (size_t)s_printer.in_transfer->actual_num_bytes;
            *got = received < cap ? received : cap;
            memcpy(buf, s_printer.in_transfer->data_buffer, *got);
        }
    }
    xSemaphoreGive(s_printer.io_lock);
    return err;
}

/* Device-to-host control read on endpoint 0. Returns ESP_OK and fills out[0..len). */
static esp_err_t control_in(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint8_t *out, size_t len, size_t *got)
{
    if (!s_printer.started || len == 0 || len > CONTROL_DATA_MAX ||
        xSemaphoreTake(s_printer.io_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_printer.connected || s_printer.faulted || !transfer_drained(&s_printer.control_context)) {
        xSemaphoreGive(s_printer.io_lock);
        return ESP_ERR_INVALID_STATE;
    }
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_printer.control_transfer->data_buffer;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = (uint16_t)len;
    s_printer.control_transfer->device_handle = s_printer.device;
    s_printer.control_transfer->bEndpointAddress = 0;
    s_printer.control_transfer->num_bytes = (int)(USB_SETUP_PACKET_SIZE + len);
    s_printer.control_transfer->actual_num_bytes = 0;
    s_printer.control_transfer->status = USB_TRANSFER_STATUS_ERROR;
    s_printer.control_transfer->callback = transfer_callback;
    s_printer.control_transfer->context = &s_printer.control_context;
    xSemaphoreTake(s_printer.control_context.done, 0);
    s_printer.control_context.pending = true;
    esp_err_t err = usb_host_transfer_submit_control(s_printer.client, s_printer.control_transfer);
    if (err != ESP_OK) {
        s_printer.control_context.pending = false;
        xSemaphoreGive(s_printer.io_lock);
        return err;
    }
    if (xSemaphoreTake(s_printer.control_context.done, pdMS_TO_TICKS(1000)) != pdTRUE) {
        xSemaphoreGive(s_printer.io_lock);
        return ESP_ERR_TIMEOUT;
    }
    s_printer.control_context.pending = false;
    int received = s_printer.control_transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
    if (s_printer.control_context.status != USB_TRANSFER_STATUS_COMPLETED || received < 0 ||
        (got == NULL && received < (int)len)) {
        xSemaphoreGive(s_printer.io_lock);
        return ESP_FAIL;
    }
    size_t copied = (size_t)received < len ? (size_t)received : len;
    memcpy(out, s_printer.control_transfer->data_buffer + USB_SETUP_PACKET_SIZE, copied);
    if (got != NULL) *got = copied;
    xSemaphoreGive(s_printer.io_lock);
    return ESP_OK;
}

static esp_err_t control_read(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint8_t *out, size_t len)
{
    return control_in(request_type, request, value, index, out, len, NULL);
}

/* IEEE 1284 device ID (printer class GET_DEVICE_ID): two length bytes, then "MFG:...;MDL:...;CMD:...;". */
esp_err_t usb_printer_device_id(char *out, size_t cap)
{
    uint8_t raw[CONTROL_DATA_MAX];
    size_t got = 0;
    if (out == NULL || cap < 2) return ESP_ERR_INVALID_ARG;
    uint16_t indexes[] = { (uint16_t)(s_printer.interface_number << 8), s_printer.interface_number };
    esp_err_t err = ESP_FAIL;
    for (size_t i = 0; i < 2 && err != ESP_OK; i++) {
        err = control_in(0xa1, 0x00, 0, indexes[i], raw, sizeof(raw), &got);
    }
    if (err != ESP_OK || got < 2) return err != ESP_OK ? err : ESP_FAIL;
    size_t length = ((size_t)raw[0] << 8) | raw[1];
    size_t text = got - 2 < length - 2 ? got - 2 : (length >= 2 ? length - 2 : 0);
    if (text >= cap) text = cap - 1;
    memcpy(out, raw + 2, text);
    out[text] = '\0';
    return ESP_OK;
}

uint8_t usb_printer_port_status(void)
{
    uint8_t status;
    return control_read(0xa1, 0x01, 0, s_printer.interface_number, &status, 1) == ESP_OK ? status : 0xff;
}

esp_err_t usb_printer_samsung_status(uint8_t out[8])
{
    /* Same request Samsung's own macOS status code sends: class, interface, bRequest 2, 8 bytes. */
    return control_read(0xc1, 0x02, 0, 0, out, 8);
}

/* Samsung status monitor definition (XML): control IN 0xC1/0x0D. Value 0 returns an 8-byte header whose bytes 2-5
 * (big endian) hold the total size, then each 255-byte chunk is read with its 1-based number as the value. */
esp_err_t usb_printer_status_xml(char *out, size_t cap)
{
    uint8_t chunk[CONTROL_DATA_MAX];
    size_t got = 0;
    if (out == NULL || cap < 2) return ESP_ERR_INVALID_ARG;
    esp_err_t err = control_in(0xc1, 0x0d, 0, 0, chunk, 8, &got);
    if (err != ESP_OK || got < 8) return err != ESP_OK ? err : ESP_FAIL;
    size_t total = ((size_t)chunk[2] << 24) | ((size_t)chunk[3] << 16) | ((size_t)chunk[4] << 8) | chunk[5];
    if (total == 0 || total >= cap) return ESP_ERR_NO_MEM;
    size_t used = 0;
    for (uint16_t number = 1; used < total; number++) {
        err = control_in(0xc1, 0x0d, number, 0, chunk, 255, &got);
        if (err != ESP_OK) return err;
        if (got == 0) break;
        size_t take = got < total - used ? got : total - used;
        memcpy(out + used, chunk, take);
        used += take;
    }
    out[used] = '\0';
    return used > 0 ? ESP_OK : ESP_FAIL;
}
