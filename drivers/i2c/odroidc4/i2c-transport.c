/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// i2c-transport.c
// Transport layer for sDDF i2c drivers. Manages all shared ring buffers.
// This module is imported by both the driver and server. Note that we expect
// the server to be responsible for initialising the buffers.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 08/2023

#include "i2c-driver.h"
#include "i2c-transport.h"
#include "printf.h"

void i2cTransportInit(i2c_ctx_t *context, int buffer_init) {
    ring_init(&context->reqRing, (ring_buffer_t *)context->req_free, (ring_buffer_t *)context->req_used, buffer_init);
    ring_init(&context->retRing, (ring_buffer_t *)context->ret_free, (ring_buffer_t *)context->ret_used, buffer_init);

    if (buffer_init) {
        for (int i = 0; i < I2C_BUF_COUNT; i++) {
            enqueue_free(&context->reqRing, context->driver_bufs + (i * I2C_BUF_SZ), I2C_BUF_SZ);
            enqueue_free(&context->retRing, context->driver_bufs + (I2C_BUF_SZ * (i + I2C_BUF_COUNT)), I2C_BUF_SZ);
        }
    }
}

req_buf_ptr_t allocReqBuf(i2c_ctx_t *context, size_t size, uint8_t *data, uint8_t client, uint8_t addr) {
    if (size > I2C_BUF_SZ - REQ_BUF_DAT_OFFSET*sizeof(i2c_token_t)) {
        printf("transport: Requested buffer size %zu too large\n", size);
        return 0;
    }
    
    uintptr_t buf;
    unsigned int sz;
    int ret = dequeue_free(&context->reqRing, &buf, &sz);
    if (ret != 0) {
        return 0;
    }

    ((uint8_t *) buf)[REQ_BUF_CLIENT] = client;
    ((uint8_t *) buf)[REQ_BUF_ADDR] = addr;
    const uint8_t sz_offset = REQ_BUF_DAT_OFFSET*sizeof(uint8_t);

    memcpy((void *) buf + sz_offset, data, size);
    
    ret = enqueue_used(&context->reqRing, buf, size + sz_offset);
    if (ret != 0) {
        enqueue_free(&context->reqRing, buf, I2C_BUF_SZ);
        return 0;
    }
    
    return (req_buf_ptr_t)buf;
}

ret_buf_ptr_t getRetBuf(i2c_ctx_t *context) {
    uintptr_t buf;
    unsigned int sz;
    int ret = dequeue_free(&context->retRing, &buf, &sz);
    if (ret != 0) {
        return 0;
    }
    return (ret_buf_ptr_t)buf;
}

int pushRetBuf(i2c_ctx_t *context, ret_buf_ptr_t buf, size_t size) {
    if (size > I2C_BUF_SZ || !buf) {
        return 0;
    }
    return enqueue_used(&context->retRing, (uintptr_t)buf, size);
}

uintptr_t popBuf(i2c_ctx_t *context, ring_handle_t *ring, size_t *sz) {
    uintptr_t buf;
    int ret = dequeue_used(ring, &buf, (unsigned *)sz);
    if (ret != 0) return 0;
    return buf;
}

req_buf_ptr_t popReqBuf(i2c_ctx_t *context, size_t *size) {
    return (req_buf_ptr_t)popBuf(context, &context->reqRing, size);
}

ret_buf_ptr_t popRetBuf(i2c_ctx_t *context, size_t *size) {
    return (ret_buf_ptr_t)popBuf(context, &context->retRing, size);
}

int retBufEmpty(i2c_ctx_t *context) { 
    return ring_empty(context->retRing.used_ring);
}

int reqBufEmpty(i2c_ctx_t *context) {
    return ring_empty(context->reqRing.used_ring);
}

int releaseReqBuf(i2c_ctx_t *context, req_buf_ptr_t buf) {
    return enqueue_free(&context->reqRing, (uintptr_t)buf, I2C_BUF_SZ);
}

int releaseRetBuf(i2c_ctx_t *context, ret_buf_ptr_t buf) {
    return enqueue_free(&context->retRing, (uintptr_t)buf, I2C_BUF_SZ);
}
