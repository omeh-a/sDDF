/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// i2c-sel4cp.c
// Client library for i2c driver.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 09/2023


#ifndef __I2C_SEL4CP_H__
#define __I2C_SEL4CP_H__

#include "i2c.h"
#include <stdint.h>
#include <sel4cp.h>

#define I2C_MAXLEN 508
#define I2C_MAX_SERVERS 8


int i2c_init(i2c_ctx_t *context, uintptr_t shared_mem, uint64_t server_channel);


int i2c_write(int bus, uint8_t addr, uint8_t *data, uint32_t len);

int i2c_read(int bus, uint8_t addr, uint8_t *data, uint32_t len);

int i2c_writeread(int bus, uint8_t addr, uint8_t wdata, uint8_t *rdata, uint32_t len);

uint8_t *i2c_notify(int bus);

#endif