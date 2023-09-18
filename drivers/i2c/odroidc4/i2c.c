/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// i2c.c
// Server for the i2c driver. Responsible for managing device security and multiplexing.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 08/2023

#include <sel4cp.h>
#include <sel4/sel4.h>
#include "i2c-driver.h"
#include "i2c-token.h"
#include "sw_shared_ringbuffer.h"
#include "printf.h"
#include "i2c-transport.h"
#include "i2c.h"

#define NUM_CLIENTS 1       // REMOVE

#ifndef BUS_NUM
#error "BUS_NUM must be defined!"
#endif
#ifndef NUM_CLIENTS
#error "NUM_CLIENTS must be defined!"
#endif
#if NUM_CLIENTS > I2C_SECURITY_LIST_SZ
#error "NUM_CLIENTS must be less than or equal to I2C_SECURITY_LIST_SZ!"
#endif
#ifndef CLIENT_TRANSPORT_VADDRS
#error "CLIENT_TRANSPORT_VADDRS must be defined!"
#endif



// Client table: maps client IDs to their corresponding transport context
uint64_t clients[I2C_SECURITY_LIST_SZ];


// Security list: owner of each i2c address on the bus
i2c_security_list_t security_list[I2C_SECURITY_LIST_SZ];

// ## Transport layer context ##
// This rat's nest of horrifying macros is used to guarantee that users do not need
// to touch the driver code to use i2c. Sorry makefile. Memory addresses of all shared
// regions need to be passed in by the makefile matching the system definition.
#define EXPAND_AS_INIT_LIST(base_vaddr) \
    { \
        base_vaddr, \
        base_vaddr + I2C_RINGBUF_ENTRIES, \
        base_vaddr + I2C_RINGBUF_ENTRIES * 2, \
        base_vaddr + I2C_RINGBUF_ENTRIES * 3, \
        base_vaddr + I2C_RINGBUF_ENTRIES * 4, \
        0, 0, 0 \
    },

#define MAKE_ENTRY(base_vaddr) EXPAND_AS_INIT_LIST(base_vaddr)

#define PROCESS_LISTS(CLIENT_TRANSPORT_VADDRS) MAKE_ENTRY(CLIENT_TRANSPORT_VADDRS)

#define INIT_STRUCT_ARRAY \
{ \
    PROCESS_LISTS(CLIENT_TRANSPORT_VADDRS) \
}

i2c_ctx_t i2c_contexts[] = INIT_STRUCT_ARRAY;

// Driver transport layer. This is entirely static and these values are just supplied
// from the system definition.
i2c_ctx_t driver_context;
uintptr_t drv_transport;

/**
 * Main entrypoint for server.
*/
void init(void) {
    sel4cp_dbg_puts("I2C server init\n");

    // Set up server<=>driver transport layer
    // NOTE: this cannot be done statically because the shared memory regions need
    //       to be ELF patched in after compile time.

    // Ring buffers: 512 64-bit pointers + 2 uint32_ts = 0x1008
    //               round up to 0x100A -> increment pointer by 515
    // Backing buffers: 512*512 = 0x100000, but we just give it the entire
    //                  rest of the transport page.
    
    driver_context.req_free = (drv_transport);
    driver_context.req_used = (drv_transport + I2C_RINGBUF_ENTRIES);
    driver_context.ret_free = (drv_transport + I2C_RINGBUF_ENTRIES*2);
    driver_context.ret_used = (drv_transport + I2C_RINGBUF_ENTRIES*3);
    driver_context.driver_bufs = (drv_transport + I2C_RINGBUF_ENTRIES*4);
    i2cTransportInit(&driver_context, 1);

    // Set up client<=>server transport layers
    INIT_STRUCT_ARRAY();
    for (int i = 0; i < NUM_CLIENTS; i++) {
        i2cTransportInit(&client_contexts[i], 1);
    }

    i2cTransportInit(&driver_context, 1);

    // Clear security list
    for (int j = 0; j < I2C_SECURITY_LIST_SZ; j++) {
        security_list[j] = -1;
    }

    // Initialisation procedure: 
}

/**
 * Handler for notification from the driver. Driver notifies when
 * there is data to retrieve from the return path.
*/
static inline void driverNotify(void) {
    printf("server: Notified by driver!\n");
    // Read the return buffer
        if (retBufEmpty()) {
        return;
    }
    size_t sz;
    ret_buf_ptr_t ret = popRetBuf(&sz);
    if (ret == 0) {
        return;
    }
    printf("ret buf first 4 bytes: %x %x %x %x\n", ret[0], ret[1], ret[2], ret[3]);
    // printf("server: Got return buffer %p\n", ret);
    printf("bus = %i client = %i addr = %i sz=%u\n", *(uint8_t *) ret,
    *(uint8_t *) (ret + sizeof(uint8_t)), *(uint8_t *) (ret + 2*sizeof(uint8_t)),
    sz);

    uint8_t err = ret[RET_BUF_ERR];
    uint8_t err_tk = ret[RET_BUF_ERR_TK];
    uint8_t client = ret[RET_BUF_CLIENT];
    uint8_t addr = ret[RET_BUF_ADDR];

    if (err) {
        printf("server: Error %i on bus %i for client %i at token of type %i\n", err, BUS_NUM, client, addr);
    } else {
        printf("server: Success on bus %i for client %i at address %i\n", BUS_NUM, client, addr);

        // TODO: logic here to return
    }

    // releaseRetBuf(ret);
    
}


static inline void clientNotify(int channel) {
    // 
}


void notified(sel4cp_channel c) {
    switch (c) {
        case DRIVER_NOTIFY_ID:
            driverNotify();
            break;
        case 2:
            break;
    }
}

static inline seL4_MessageInfo_t ppcError(void) {
    sel4cp_mr_set(0, -1);
    return sel4cp_msginfo_new(0, 1);
}

/**
 * Claim an address on a bus.
*/
static inline seL4_MessageInfo_t securityClaim(uint8_t addr, uint64_t client) {
    // Check that the address is not already claimed

    if (security_list[addr] != -1) {
        sel4cp_dbg_puts("I2C|ERROR: Address already claimed!\n");
        return ppcError();
    }

    // Claim
    security_list[addr] = client;
    sel4cp_mr_set(0, 0);
    return sel4cp_msginfo_new(0, 1);
}

/**
 * Release an address on a bus.
*/
static inline seL4_MessageInfo_t securityRelease(uint8_t addr, uint64_t client) {
    // Check that the address is claimed by the client
    if (security_list[addr] != client) {
        sel4cp_dbg_puts("I2C|ERROR: Address not claimed by client!\n");
        return ppcError();
    }

    // Release
    security_list[addr] = -1;
    sel4cp_mr_set(0, 0);
    return sel4cp_msginfo_new(0, 1);
}



/**
 * Protected procedure calls into this server are used managing the security lists. 
*/
seL4_MessageInfo_t protected(sel4cp_channel c, seL4_MessageInfo_t m) {
    // Determine the type of request
    uint64_t req = sel4cp_mr_get(I2C_PPC_MR_REQTYPE);
    uint64_t ppc_addr = sel4cp_mr_get(I2C_PPC_MR_ADDR);
    uint64_t client_pd = sel4cp_mr_get(I2C_PPC_MR_CID);

    // Check arguments are valid
    if (req != I2C_PPC_CLAIM || req != I2C_PPC_RELEASE) {
        sel4cp_dbg_puts("I2C|ERROR: Invalid PPC request type!\n");
        return ppcError();
    }
    if (ppc_addr < 0 || ppc_addr > 127) {
        sel4cp_dbg_puts("I2C|ERROR: Invalid i2c address in PPC!\n");
        return ppcError();
    }


    switch (req) {
        case I2C_PPC_CLAIM:
            // Claim an address
            return securityClaim(ppc_addr, client_pd);
        case I2C_PPC_RELEASE:
            // Release an address
            return securityRelease(ppc_addr, client_pd);
    }

    return sel4cp_msginfo_new(0, 7);
}