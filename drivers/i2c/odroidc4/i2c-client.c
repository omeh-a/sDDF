// Simple test client PD for I2C

#include <sel4cp.h>

#include "i2c.h"
#include "printf.h"
#include "i2c-sel4mk.h"

#define SERVER_CHANNEL 1
#define BUS 3

#define SHARED_MEM 0x3000000

i2c_ctx_t context;

void init(void) {
    sel4cp_dbg_puts("I2C client init\n");
    i2c_init(&context, (uintptr_t) SHARED_MEM, SERVER_CHANNEL);
    
}


void notified(sel4cp_channel c) {

}