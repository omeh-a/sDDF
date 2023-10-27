/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */


#include <sel4cp.h>
#include "x550t.h"
#include "stdint.h"

#define ECAM_BASE 0xE0000000    // Put your ECAM base address here. seL4 cannot read the
                                // ECAM in its current state, so we have to empirically find it
                                // in Linux and hardcode it here. This can be found using
                                // `cat /proc/iomem | grep MMCONFIG` in Linux.

#define ECAM_SIZE 0xFFFFFFF     // Size of the ECAM address space for your machine. I am not sure
                                // if this actually changes.