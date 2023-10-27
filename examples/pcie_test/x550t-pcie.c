/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// x550t-pcie.c
// Extremely minimal PCIe example which sets up and polls some registers on the
// Intel X550t 10G NIC.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 10/2023

#include "x550t-pcie.h"
#include <printf.h>

uintptr_t nic;
uintptr_t pcie;

void _putchar(char character) {
    sel4cp_dbg_putc(character);
}

void init(void) {
    // Start: just read all registers
    volatile uint32_t ctrl = *((volatile uint32_t*)(nic + BAR0_CTRL_REG));
    volatile uint32_t status = *((volatile uint32_t*)(nic + BAR0_STATUS_REG));

    printf("ctrl: %x\n", ctrl);
    printf("status: %x\n", status);

    // Next: Try make a change to both registers and observe change
    
}


void notified(sel4cp_channel c) {
    
    return;
}

