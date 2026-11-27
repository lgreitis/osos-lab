/* SPDX-License-Identifier: MIT */

/* Requests to the host's file worker, which runs on its own task. A Queue
 * entry hands the request over; the worker writes the outputs and links the
 * request after the previous completion through that one's next word, and the
 * host passes the list to the image with a later frame. Nothing marks a
 * request done, so the outputs start as a value the worker never writes and a
 * wait watches them change, yielding in between so the worker runs. The
 * worker writes the outputs in no fixed order and only result on failure, so
 * a wait takes result first and the operation's other outputs on success.
 * An open or a listing that failed still registered its handle, which the
 * caller gives back; a listing's handle arrives after the result, unless the
 * host had no slot for it and overwrote the result with FileStatus_NoSlot.
 *
 * The requests are a ring that is never freed: the worker writes a completed
 * request's next word when the following request completes, which can be
 * after the frame handed the list over, so memory a request lived in must
 * stay a request. The runtime's calls are synchronous, one request in flight
 * at a time, and nothing reads the list the frames carry. */

#include "osos-2.0.5.h"
#include "platform.h"

#define PENDING EAPP_REQUEST_PENDING

static struct GameFileRequest ring[8];
static unsigned next_slot;
uint32_t eapp_file_error_status;

uint32_t eapp_file_error(void)
{
    return eapp_file_error_status;
}

struct GameFileRequest *eapp_request_new(uint32_t type)
{
    struct GameFileRequest *request = &ring[next_slot];
    next_slot = (next_slot + 1) % (sizeof ring / sizeof ring[0]);
    memset(request, 0, sizeof *request);
    request->type = type;
    request->result = PENDING;
    request->transferred = PENDING;
    request->size = PENDING;
    request->handle = PENDING;
    return request;
}

static void wait_for(const volatile uint32_t *word)
{
    while (*word == PENDING)
        GameSystem_Sleep(0);
}

int eapp_request_finish(struct GameFileRequest *request, int queued, unsigned outputs)
{
    if (!queued) {
        eapp_file_error_status = FileStatus_NoSlot;
        return FileStatus_NoSlot;
    }
    wait_for(&request->result);
    if (request->result == FileStatus_Ok) {
        if (outputs & EAPP_REQUEST_SIZE)
            wait_for(&request->size);
        if (outputs & EAPP_REQUEST_HANDLE)
            wait_for(&request->handle);
        if (outputs & EAPP_REQUEST_TRANSFERRED)
            wait_for(&request->transferred);
        return FileStatus_Ok;
    }
    if (outputs & EAPP_REQUEST_HANDLE)
        while (request->handle == PENDING && request->result != FileStatus_NoSlot)
            GameSystem_Sleep(0);
    eapp_file_error_status = request->result;
    return (int)request->result;
}
