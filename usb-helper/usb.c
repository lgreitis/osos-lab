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

static struct event_queue events;
static struct usb_class_driver_ep_allocation endpoints[] = {
    {.type = USB_ENDPOINT_XFER_BULK, .dir = DIR_OUT, .mps = -1},
};
static int interface_number;
static bool finished, receiving, stopping, start_pending, closed;
static int pending_length = -1;
static uint32_t activity, return_at;
static uint8_t status_data[320] USB_DEVBSS_ATTR;

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
    if (req->wIndex != interface_number || req->wValue != 0)
        return false;
    if (req->bRequestType == 0xa1 && req->bRequest == 0x52 &&
        req->wLength == sizeof(status_data)) {
        memcpy(status_data, UPLOAD_RESULT, sizeof(status_data));
        usb_core_control_response(USB_CONTROL_ACK, status_data, sizeof(status_data));
        return true;
    }
    if (req->bRequestType != 0x21 || req->wLength != 16 ||
        memcmp(data, UPLOAD_RESULT->nonce, 16))
        return false;
    if (req->bRequest == 0x53 && UPLOAD_RESULT->state == 1 && !finished) {
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
        if (TIME_AFTER(USEC_TIMER, activity + 30000000)) {
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
        if (UPLOAD_RESULT->state == 4 && !closed) {
            upload_close();
            closed = true;
        }
    }
    stopping = true;
    usb_core_exit();
    queue_delete(&events);
    upload_close();
    return UPLOAD_RESULT->rc;
}
