/* SPDX-License-Identifier: GPL-3.0-only */

#include "config.h"
#include "system.h"
#include "kernel.h"
#include "usb_core.h"
#include "usb-designware.h"
#include "usb_drv.h"
#include "usb_class_driver.h"
#include "string.h"
#include "upload.h"
#include "winusb.h"
#include "layout.h"

static struct event_queue events;
static struct usb_class_driver_ep_allocation endpoints[] = {
    {.type = USB_ENDPOINT_XFER_BULK, .dir = DIR_OUT, .mps = -1},
};
static int interface_number;
static bool finished, receiving, stopping, start_pending, next_pending;
static struct upload_config next_config;
static int pending_length = -1;
static uint32_t activity, return_at;
static uint8_t status_data[320] USB_DEVBSS_ATTR;
static uint8_t probe_data[sizeof(struct layout_attempt)] USB_DEVBSS_ATTR;
_Static_assert(sizeof(struct storage_diagnostics) <= sizeof(status_data), "diagnostics buffer");

bool upload_usb_control_request(struct usb_ctrlrequest *req, uint8_t *data, size_t capacity)
{
    const void *descriptor;
    unsigned size = upload_os_descriptor(req->bRequestType, req->bRequest,
                                        req->wValue, req->wIndex, &descriptor);
    if (!size)
        return false;
    size = MIN(size, req->wLength);
    if (size > capacity)
        return false;
    memcpy(data, descriptor, size);
    usb_core_control_response(USB_CONTROL_ACK, data, size);
    return true;
}

void usb_signal_transfer_completion(struct usb_transfer_completion_event_data *event)
{
    queue_post(&events, USB_TRANSFER_COMPLETION, (intptr_t)event);
}

void usb_signal_notify(long id, intptr_t data)
{
    queue_post(&events, id, data);
}

void usb_clear_pending_transfer_completion_events(void)
{
    queue_clear(&events);
}

void usb_charger_update(void)
{
    usb_signal_notify(USB_CHARGER_UPDATE, 0);
}

bool usb_exclusive_storage(void)
{
    return false;
}

void usb_release_exclusive_storage(void)
{
}

void usb_request_exclusive_storage(void)
{
    upload_fail(-410);
}

static int first_interface(int interface)
{
    interface_number = interface;
    return interface + 1;
}

static int descriptor(unsigned char *dest, int packet)
{
    const uint8_t data[] = {
        9,
        USB_DT_INTERFACE,
        interface_number,
        0,
        1,
        0xff,
        0x52,
        2,
        0,
        7,
        USB_DT_ENDPOINT,
        endpoints[0].ep,
        USB_ENDPOINT_XFER_BULK,
        packet & 255,
        packet >> 8,
        0,
    };
    memcpy(dest, data, sizeof(data));
    return sizeof(data);
}

static int connected(void)
{
    UPLOAD_RESULT->high_speed = usb_drv_port_speed();
    UPLOAD_RESULT->endpoint = endpoints[0].ep;
    return 0;
}

static void disconnected(void)
{
    if (!stopping) {
        upload_fail(-411);
        finished = true;
    }
}

static void receive_next(void)
{
    unsigned count = MIN(UPLOAD_RESULT->size - UPLOAD_RESULT->received, UPLOAD_CHUNK);
    receiving = true;
    /* DMA receives whole packets; only count bytes enter the file and hash. */
    if (usb_drv_recv_nonblocking(endpoints[0].ep, UPLOAD_DATA, (count + 511) & ~511u))
        upload_fail(-412);
}

static void completed(int ep, int dir, int status, int length)
{
    if (stopping || !receiving || ep != endpoints[0].ep || dir != USB_DIR_OUT)
        return;
    receiving = false;
    unsigned count = MIN(UPLOAD_RESULT->size - UPLOAD_RESULT->received, UPLOAD_CHUNK);
    if (status || length != (int)((count + 511) & ~511u)) {
        upload_fail(-412);
        return;
    }
    pending_length = count;
    activity = USEC_TIMER;
}

static bool control(struct usb_ctrlrequest *req, uint8_t *data, size_t capacity)
{
    (void)capacity;
    if (req->wIndex != interface_number)
        return false;
    if (req->bRequestType == 0xa1 && req->bRequest == 0x58 &&
        req->wValue < upload_probe.count && req->wLength == sizeof(struct layout_attempt)) {
        memcpy(probe_data, &upload_attempts[req->wValue], sizeof(probe_data));
        usb_core_control_response(USB_CONTROL_ACK, probe_data, sizeof(probe_data));
        activity = USEC_TIMER;
        return true;
    }
    if (req->wValue != 0)
        return false;
    if (req->bRequestType == 0xa1 && req->bRequest == 0x57 &&
        req->wLength == sizeof(upload_probe)) {
        memcpy(probe_data, &upload_probe, sizeof(upload_probe));
        usb_core_control_response(USB_CONTROL_ACK, probe_data, sizeof(upload_probe));
        activity = USEC_TIMER;
        return true;
    }
    if (req->bRequestType == 0xa1 && req->bRequest == 0x56 &&
        req->wLength == sizeof(upload_diagnostics)) {
        memcpy(status_data, &upload_diagnostics, sizeof(upload_diagnostics));
        usb_core_control_response(USB_CONTROL_ACK, status_data, sizeof(upload_diagnostics));
        return true;
    }
    if (req->bRequestType == 0xa1 && req->bRequest == 0x52 &&
        req->wLength == sizeof(status_data)) {
        memcpy(status_data, UPLOAD_RESULT, sizeof(status_data));
        usb_core_control_response(USB_CONTROL_ACK, status_data, sizeof(status_data));
        return true;
    }
    if (req->bRequestType != 0x21 || req->wLength < 16 ||
        memcmp(data, UPLOAD_RESULT->nonce, 16))
        return false;
    if (req->bRequest == 0x59 && req->wLength == sizeof(next_config) &&
        UPLOAD_RESULT->state == 4 && !UPLOAD_RESULT->rc && !finished) {
        memcpy(&next_config, data, sizeof(next_config));
        activity = USEC_TIMER;
        next_pending = true;
    } else if (req->wLength != 16) {
        return false;
    } else if (req->bRequest == 0x53 && UPLOAD_RESULT->state == 1 && !finished) {
        activity = USEC_TIMER;
        UPLOAD_RESULT->state = 2;
        start_pending = true;
    } else if (req->bRequest == 0x54 && UPLOAD_RESULT->state == 4) {
        return_at = USEC_TIMER + 250000;
        finished = true;
    } else if (req->bRequest == 0x55 && !finished) {
        upload_fail(-416);
        return_at = USEC_TIMER + 250000;
        finished = true;
    } else
        return false;
    usb_core_control_response(USB_CONTROL_ACK, NULL, 0);
    return true;
}

/* Replaces Rockbox mass storage with the upload interface. */
struct usb_class_driver usb_cdrv_storage = {
    .needs_exclusive_storage = false,
    .needs_cpu_boost = false,
    .config = 1,
    .ep_allocs_size = ARRAYLEN(endpoints),
    .ep_allocs = endpoints,
    .set_first_interface = first_interface,
    .get_config_descriptor = descriptor,
    .init_connection = connected,
    .disconnect = disconnected,
    .transfer_complete = completed,
    .control_request = control,
};

int upload_usb(void)
{
    queue_init(&events, false);
    usb_core_init();
    usb_core_enable_driver(USB_DRIVER_MASS_STORAGE, true);
    activity = USEC_TIMER;
    while (!finished || (return_at && TIME_BEFORE(USEC_TIMER, return_at))) {
        /* Allow Windows to bind WinUSB on the first connection. */
        unsigned timeout = (UPLOAD_RESULT->state == 1 || UPLOAD_RESULT->state == 4)
                           ? 120000000 : 30000000;
        if (TIME_AFTER(USEC_TIMER, activity + timeout)) {
            upload_fail(-415);
            break;
        }
        struct queue_event event;
        queue_wait_w_tmo(&events, &event, UPLOAD_RESULT->state == 3 ? 0 : 1);
        if (event.id == USB_TRANSFER_COMPLETION)
            usb_core_handle_transfer_completion((void *)event.data);
        else if (event.id == USB_CHARGER_UPDATE)
            usb_charging_maxcurrent_change(usb_charging_maxcurrent());
        else if (event.id != SYS_TIMEOUT)
            usb_core_handle_notify(event.id, event.data);
        if (next_pending) {
            next_pending = false;
            upload_next(&next_config);
            activity = USEC_TIMER;
        }
        if (start_pending && UPLOAD_RESULT->state == 2) {
            start_pending = false;
            upload_start();
            if (UPLOAD_RESULT->state == 2)
                receive_next();
        }
        if (pending_length >= 0 && UPLOAD_RESULT->state == 2) {
            unsigned n = pending_length;
            pending_length = -1;
            UPLOAD_RESULT->received += n;
            upload_write(n);
            activity = USEC_TIMER;
            if (UPLOAD_RESULT->state == 2)
                receive_next();
        }
        if (UPLOAD_RESULT->state == 3) {
            upload_verify_step();
            activity = USEC_TIMER;
        }
    }
    stopping = true;
    usb_core_exit();
    queue_delete(&events);
    upload_close();
    return UPLOAD_RESULT->rc;
}
