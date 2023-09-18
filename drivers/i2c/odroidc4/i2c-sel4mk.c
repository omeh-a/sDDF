/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// i2c-sel4cp.c
// Client-server interaction for sDDF i2c driver.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 09/2023

#include "i2c-sel4mk.h"

typedef struct i2c_server {
    i2c_ctx_t context;
    uint64_t server_id;
    uint8_t ready;
} i2c_server_t;

static i2c_server_t servers[I2C_MAX_SERVERS] = {0};


/**
 * Initialise client-side shared memory for the i2c driver. Future i2c
 * operations will silently fail until this occurs!
 * 
 * @param shared_mem Shared memory map for the server-client transport layer.
 *                   Must map to the same physical address as the server-side map.
 * @param server_channel Channel ID of the server.
 * @param bus i2c interface this connection models. Only used for indexing.
 * 
*/
void i2c_init(uintptr_t shared_mem, uint64_t server_channel, int bus) {
    if (bus >= I2C_MAX_SERVERS || bus < 0) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: Bus number invalid in setup!\n");
        return;
    }

    // Do nothing if already initialised
    if (servers[bus].ready) {
        return;
    }

    // Make sure shared mem is page aligned
    if ((uint64_t)shared_mem % 0x1000) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: Shared memory address not page aligned! Cannot init\n");
        return;
    }
    servers[bus].server_id = server_channel;

    // Populate context struct
    context.req_free = (drv_transport);
    context.req_used = (drv_transport + I2C_RINGBUF_ENTRIES);
    context.ret_free = (drv_transport + I2C_RINGBUF_ENTRIES*2);
    context.ret_used = (drv_transport + I2C_RINGBUF_ENTRIES*3);
    context.driver_bufs = (drv_transport + I2C_RINGBUF_ENTRIES*4);

    // Initialise shared memory
    i2cTransportInit(&(servers[bus].context), 0);
    servers[bus].ready = 1;
    return;
}

// Underlying write code. Hides continuation from user.
static inline int __i2c_write(int bus, uint8_t addr, uint8_t *data, uint32_t len, int cont) {
    if (bus >= I2C_MAX_SERVERS || bus < 0) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: Bus number invalid in write!\n");
        return;
    }
    if (!servers[bus].ready]) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c not initialised but write attempted!\n");
        return -1;
    }
    if (len > I2C_MAXLEN) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c write too long!\n");
        return -1;
    }

    // Put data in ring buffer. We offload the work of composing the transaction to the ring
    // buffer library to avoid an additional round of copying here.
    req_buf_ptr_t ret = clientAllocReqBuf(&(servers[bus].context), len, data, addr->addr, 
                                          (cont) ? I2C_MODE_WRITE_CONT : I2C_MODE_WRITE);
    if (ret == 0) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c write failed to allocate buffer!\n");
        return -1;
    }

    // Ping server
    sel4cp_nofify(servers[bus].server_id);
    return 0;
}

static inline int __i2c_read(int bus, uint8_t addr, uint8_t *data, uint32_t len, int cont) {
    if (bus >= I2C_MAX_SERVERS || bus < 0) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: Bus number invalid in read!\n");
        return;
    }
    if (!servers[bus].ready]) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c not initialised but read attempted!\n");
        return -1;
    }
    if (len > I2C_MAXLEN) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c read too long!\n");
        return -1;
    }

    // Put data in ring buffer. We offload the work of composing the transaction to the ring
    // buffer library to avoid an additional round of copying here.
    req_buf_ptr_t ret = clientAllocReqBuf(&(servers[bus].context), len, data, addr->addr, 
                                          (cont) ? I2C_MODE_READ_CONT : I2C_MODE_READ);
    if (ret == 0) {
        sel4cp_dbg_puts("I2C-CLIENT|ERROR: i2c read failed to allocate buffer!\n");
        return -1;
    }

    // Ping server
    sel4cp_nofify(servers[bus].server_id);
    return 0;
}

/**
 * Write the contents of a supplied buffer to the i2c device
 * specified by addr.
 * @param addr Address struct for target device, specifying bus and addr.
 * @param data Pointer to buffer containing data to write.
 * @param len Length of data to write. Max = 508 bytes.
 *            if longer is needed, use i2c_writecont multiple times.
 * @return 0 on success, -1 on failure.
*/
int i2c_write(int bus, uint8_t addr, uint8_t *data, uint32_t len) {
    return __i2c_write(bus, addr, data, len, 0);
}

/**
 * Read the contents of a supplied buffer from the i2c device.
 * @param addr Address struct for target device, specifying bus and addr.
 * @param data Pointer to buffer to store read data.
 * @param len Length of data to read.
 * @return 0 on success, -1 on failure.
*/
int i2c_read(int bus, uint8_t addr, uint8_t *data, uint32_t len) {
    return __i2c_read(bus, addr, data, len, 0);
}

/**
 * Write a single byte before reading from the i2c device. Used for subaddressing.
 * @param addr Address struct for target device, specifying bus and addr.
 * @param wdata Data to write. Usually the address of a device register.
 * @param rdata Pointer to buffer to store read data.
 * @param len Length of data to read.
 * @return 0 on success, -1 on failure.
*/
int i2c_writeread(int bus, uint8_t addr, uint8_t wdata, uint8_t *rdata, uint32_t len) {
    int ret = __i2c_write(bus, addr, &wdata, 1, 1);
    if (ret != 0) return ret;
    return i2c_read(bus, addr, rdata, len);
}
