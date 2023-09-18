/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// i2c-odroid-c4.c
// Implementation of the i2c driver targeting the ODROID C4.
// Each instance of this driver corresponds to one of the four
// available i2c master interfaces on the device.
// Matt Rossouw (matthew.rossouw@unsw.edu.au)
// 08/2023


#include <sel4cp.h>
#include "i2c-driver.h"
#include "odroidc4-i2c-mem.h"

#ifndef BUS_NUM
#error "BUS_NUM must be defined!"
#endif

typedef volatile struct {
    uint32_t ctl;
    uint32_t addr;
    uint32_t tk_list0;
    uint32_t tk_list1;
    uint32_t wdata0;
    uint32_t wdata1;
    uint32_t rdata0;
    uint32_t rdata1;
} i2c_if_t;

// Hardware memory
uintptr_t i2c;
const int bus = BUS_NUM;
uintptr_t gpio;
uintptr_t clk;

// Transport layer
uintptr_t transport;
uintptr_t driver_bufs;

ring_handle_t reqRing;
ring_handle_t retRing;
i2c_ctx_t i2c_ctx = { 0 };

// Actual interfaces. Note that address here must match the one in i2c.system defined for `i2c` memory region.
// Hardcoded because the C compiler cannot handle the non-constant symbol `uinptr_t i2c`, which is added by the
// elf patcher.
volatile i2c_if_t *interface = (void *)(uintptr_t)(0x3000000);


static inline int i2cDump(void) {
    printf("i2c: dumping interface state...\n");
    
    // Print control register fields
    // uint8_t ctl_man = (ctl & REG_CTRL_MANUAL) ? 1 : 0;
    uint8_t ctl_rd_cnt = ((interface->ctl & REG_CTRL_RD_CNT) >> 8);
    uint8_t ctl_curr_tk = ((interface->ctl & REG_CTRL_CURR_TK) >> 4);
    uint8_t ctl_err = (interface->ctl & REG_CTRL_ERROR) ? 1 : 0;
    uint8_t ctl_status = (interface->ctl & REG_CTRL_STATUS) ? 1 : 0;
    uint8_t ctl_start = (interface->ctl & REG_CTRL_START) ? 1 : 0;
    printf("\t Control register:\n");
    printf("\t\t Start: %u\n", ctl_start);
    printf("\t\t Status: %u\n", ctl_status);
    printf("\t\t Error: %u\n", ctl_err);
    printf("\t\t Current token: %u\n", ctl_curr_tk);
    printf("\t\t Read count: %u\n", ctl_rd_cnt);

    // Print address
    printf("\t Address register: 0x%x\n", (interface->addr >> 1) & 0x7F);
    
    // Print token register 0 tokens
    printf("\t Token register 0:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t tk = (interface->tk_list0 >> (i * 4)) & 0xF;
        printf("\t\t Token %d: %x\n", i, tk);
    }

    // Print token register 1 tokens
    printf("\t Token register 1:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t tk = (interface->tk_list1 >> (i * 4)) & 0xF;
        printf("\t\t Token %d: %x\n", i, tk);
    }

    // Print wdata register 0 tokens
    printf("\t Write data register 0:\n");
    for (int i = 0; i < 4; i++) {
        uint8_t tk = (interface->wdata0 >> (i * 8)) & 0xFF;
        printf("\t\t Data %d: %x\n", i, tk);
    }

    // Print wdata register 1 tokens
    printf("\t Write data register 1:\n");
    for (int i = 0; i < 4; i++) {
        uint8_t tk = (interface->wdata1 >> (i * 8)) & 0xFF;
        printf("\t\t Data %d: %x\n", i, tk);
    }

    // Print rdata register 0
    printf("\t Read data register 0:\n");
    for (int i = 0; i < 4; i++) {
        uint8_t tk = (interface->rdata0 >> (i * 8)) & 0xFF;
        printf("\t\t Data %d: %x\n", i, tk);
    }

    // Print rdata register 1
    printf("\t Read data register 1:\n");
    for (int i = 0; i < 4; i++) {
        uint8_t tk = (interface->rdata1 >> (i * 8)) & 0xFF;
        printf("\t\t Data %d: %x\n", i, tk);
    }
    return 0;
}

// Driver state for each interface
volatile i2c_ifState_t i2c_ifState;


/**
 * Initialise the i2c master interfaces.
*/
static inline void setupi2c() {
    printf("driver: initialising i2c master interfaces...\n");

    // Initialise transport
    // NOTE: this cannot be done statically because the shared memory regions need
    //       to be ELF patched in after compile time.

    // Ring buffers: 512 64-bit pointers + 2 uint32_ts = 0x1008
    //               round up to 0x100A -> increment pointer by 515
    // Backing buffers: 512*512 = 0x100000, but we just give it the entire
    //                  rest of the transport page.
    
    i2c_ctx.req_free = transport;
    i2c_ctx.req_used = (transport + I2C_RINGBUF_ENTRIES);
    i2c_ctx.ret_free = (transport + I2C_RINGBUF_ENTRIES*2);
    i2c_ctx.ret_used = (transport + I2C_RINGBUF_ENTRIES*3);
    i2c_ctx.driver_bufs = (transport + I2C_RINGBUF_ENTRIES*4);
    i2c_ctx.reqRing = reqRing;
    i2c_ctx.retRing = retRing;
    i2cTransportInit(&i2c_ctx, 0);


    // Note: this is hacky - should do this using a GPIO driver.    
    // Set up pinmux
    volatile uint32_t *gpio_mem = (void*)(gpio + GPIO_OFFSET);

    volatile uint32_t *pinmux5_ptr      = ((void*)gpio_mem + GPIO_PINMUX_5*4);
    volatile uint32_t *pinmuxE_ptr      = ((void*)gpio_mem + GPIO_PINMUX_E*4);
    volatile uint32_t *pad_ds2b_ptr     = ((void*)gpio_mem + GPIO_DS_2B*4);
    volatile uint32_t *pad_ds5a_ptr     = ((void*)gpio_mem + GPIO_DS_5A*4);
    volatile uint32_t *pad_bias2_ptr    = ((void*)gpio_mem + GPIO_BIAS_2_EN*4);
    volatile uint32_t *pad_bias5_ptr    = ((void*)gpio_mem + GPIO_BIAS_5_EN*4);
    volatile uint32_t *clk81_ptr        = ((void*)clk + I2C_CLK_OFFSET);
   
    // Read existing register values
    uint32_t pinmux5 = *pinmux5_ptr;
    uint32_t pinmuxE = *pinmuxE_ptr;
    uint32_t clk81 = *clk81_ptr;

    // Common values
    const uint8_t ds = 3;    // 3 mA
    uint8_t pinfunc;

    // Enable i2cm2 -> pinmux 5
    if (bus == 2) {
        pinfunc = GPIO_PM5_X_I2C;
        pinmux5 |= (pinfunc << 4) | (pinfunc << 8);
        *pinmux5_ptr = pinmux5;

        // Check that registers actually changed
        if (!(*pinmux5_ptr & (GPIO_PM5_X18 | GPIO_PM5_X17))) {
            printf("driver: failed to set pinmux5!\n");
        }

        // Set GPIO drive strength
        *pad_ds2b_ptr &= ~(GPIO_DS_2B_X17 | GPIO_DS_2B_X18);
        *pad_ds2b_ptr |= ((ds << GPIO_DS_2B_X17_SHIFT) | 
                        (ds << GPIO_DS_2B_X18_SHIFT));
        
        // Check register updated
        if ((*pad_ds2b_ptr & (GPIO_DS_2B_X17 | GPIO_DS_2B_X18)) != ((ds << GPIO_DS_2B_X17_SHIFT) | 
                        (ds << GPIO_DS_2B_X18_SHIFT))) {
            printf("driver: failed to set drive strength for m2!\n");
        }

        // Disable bias, because the odroid i2c hardware has undocumented internal ones
        *pad_bias2_ptr &= ~((1 << 18) | (1 << 17)); // Disable m2 bias - x17 and x18
        
        // Check registers updated
        if ((*pad_bias2_ptr & ((1 << 18) | (1 << 17))) != 0) {
            printf("driver: failed to disable bias for m2!\n");
        }
    }

    // Enable i2cm3 -> pinmux E
    if (bus == 3) {
        pinfunc = GPIO_PE_A_I2C;
        pinmuxE |= (pinfunc << 24) | (pinfunc << 28);
        *pinmuxE_ptr = pinmuxE;
        
        // Check registers actually changed
        if (!(*pinmuxE_ptr & (GPIO_PE_A15 | GPIO_PE_A14))) {
            printf("driver: failed to set pinmuxE!\n");
        }

        // Set GPIO drive strength
        *pad_ds5a_ptr &= ~(GPIO_DS_5A_A14 | GPIO_DS_5A_A15);
        *pad_ds5a_ptr |= ((ds << GPIO_DS_5A_A14_SHIFT) | 
                        (ds << GPIO_DS_5A_A15_SHIFT));
        
        // Check register updated
        if ((*pad_ds5a_ptr & (GPIO_DS_5A_A14 | GPIO_DS_5A_A15)) != ((ds << GPIO_DS_5A_A14_SHIFT) | 
                        (ds << GPIO_DS_5A_A15_SHIFT))) {
            printf("driver: failed to set drive strength for m3!\n");
        }

        // Disable bias, because the odroid i2c hardware has undocumented internal ones
        *pad_bias5_ptr &= ~((1 << 14) | (1 << 15)); // Disable m3 bias - a14 and a15
        
        // Check registers updated
        if ((*pad_bias5_ptr & ((1 << 14) | (1 << 15))) != 0) {
            printf("driver: failed to disable bias for m3!\n");
        }
    }

    // Enable i2c by removing clock gate
    clk81 |= (I2C_CLK81_BIT);
    *clk81_ptr = clk81;

    // Check that registers actually changed
    if (!(*clk81_ptr & I2C_CLK81_BIT)) {
        printf("driver: failed to toggle clock!\n");
    }


    // Initialise fields
    interface->ctl &= ~(REG_CTRL_MANUAL);       // Disable manual mode
    interface->ctl &= ~(REG_CTRL_ACK_IGNORE);   // Disable ACK IGNORE
    interface->ctl |= (REG_CTRL_CNTL_JIC);      // Bypass dynamic clock gating

    // Handle clocking
    // (comment) Stolen from Linux Kernel's amlogic driver
    /*
    * According to I2C-BUS Spec 2.1, in FAST-MODE, LOW period should be at
    * least 1.3uS, and HIGH period should be at lease 0.6. HIGH to LOW
    * ratio as 2 to 5 is more safe.
    * Duty  = H/(H + L) = 2/5	-- duty 40%%   H/L = 2/3
    * Fast Mode : 400k
    * High Mode : 3400k
    */
    // const uint32_t clk_rate = 166666666; // 166.666MHz -> clk81
    // const uint32_t freq = 400000; // 400kHz
    // const uint32_t delay_adjust = 0;
    // uint32_t div_temp = (clk_rate * 2)/(freq * 5);
	// uint32_t div_h = div_temp - delay_adjust;
	// uint32_t div_l = (clk_rate * 3)/(freq * 10);

    // Duty cycle slightly high with this - should adjust (47% instead of 40%)
    // TODO: add clock driver logic here to dynamically set speed. This is a hack.
    //       The below values allow us to be a spec compliant 400KHz controller w/
    //       duty cycle, and high/low time appropriate
    uint32_t div_h = 154;
    uint32_t div_l = 116;

    // Set clock divider
    interface->ctl &= ~(REG_CTRL_CLKDIV_MASK);
    interface->ctl |= (div_h << REG_CTRL_CLKDIV_SHIFT);

    // Set SCL filtering
    interface->addr &= ~(REG_ADDR_SCLFILTER);
    interface->addr |= (0x0 << 11);


    // Set SDA filtering
    interface->addr &= ~(REG_ADDR_SDAFILTER);
    interface->addr |= (0x0 << 8);

    // Set clock delay levels
    // Field has 9 bits: clear then shift in div_l
    interface->addr &= ~(0x1FF << REG_ADDR_SCLDELAY_SHFT);
    interface->addr |= (div_l << REG_ADDR_SCLDELAY_SHFT);

    // Enable low delay time adjustment
    interface->addr |= REG_ADDR_SCLDELAY_ENABLE;
}

/**
 * Given a bus number, retrieve the error code stored in the control register
 * associated.
 * @param bus i2c EE-domain master interface to check
 * @return int error number - non-negative numbers are a success with n. bytes read / 0 if writing,
 *         while a negative value corresponds to a bus NACK at a token of value -(ret).
 *         e.g. if NACK on a ADDRW (0x3) the return value will be -3.
 */
static inline int i2cGetError() {
    // Index into ctl register - i2c base + address of appropriate register
    volatile uint32_t ctl = interface->ctl;
    uint8_t err = ctl & (1 << 3);   // bit 3 -> set if error
    uint8_t rd = (ctl & 0xF00) >> 8; // bits 8-11 -> number of bytes read
    uint8_t tok = (ctl & 0xF0) >> 4; // bits 4-7 -> curr token

    if (err) {
        return -tok;
    } else {
        return rd;
    }
}

static inline int i2cStart() {
    printf("i2c: LIST PROCESSOR START\n");
    interface->ctl &= ~0x1;
    interface->ctl |= 0x1;
    if (!(interface->ctl & 0x1)) {
        sel4cp_dbg_puts("i2c: failed to set start bit!\n");
        return -1;
    }
    return 0;
}

static inline int i2cHalt() {
    printf("i2c: LIST PROCESSOR HALT\n");
    interface->ctl &= ~0x1;
    if ((interface->ctl & 0x1)) {
        sel4cp_dbg_puts("i2c: failed to halt!\n");
        return -1;
    }
    return 0;
}

static inline int i2cFlush() {
    printf("i2c: LIST PROCESSOR FLUSH\n");
    // Clear token list
    interface->tk_list0 = 0x0;
    interface->tk_list1 = 0x0;

    // // Clear wdata
    // interface->wdata0 = 0x0;
    // interface->wdata1 = 0x0;

    // // Clear rdata
    // interface->rdata0 = 0x0;
    // interface->rdata1 = 0x0;

    // // Clear address
    // interface->addr = 0x0;
    return 0;
}

static inline int i2cLoadTokens() {
    sel4cp_dbg_puts("driver: starting token load\n");
    i2c_token_t * tokens = (i2c_token_t *)i2c_ifState.current_req;
    printf("Tokens remaining in this req: %zu\n", i2c_ifState.remaining);
    
    // Extract second byte: address
    uint8_t addr = tokens[REQ_BUF_ADDR];
    if (addr > 0x7F) {
        sel4cp_dbg_puts("i2c: attempted to write to address > 7-bit range!\n");
        return -1;
    }
    COMPILER_MEMORY_FENCE();
    i2cFlush();
    // Load address into address register
    // Address goes into low 7 bits of address register
    interface->addr = interface->addr & ~(0x7F);
    interface->addr = interface->addr | ((addr& 0x7f) << 1);  // i2c hardware expects that the 7-bit address is shifted left by 1

    // Clear token buffer registers
    interface->tk_list0 = 0x0;
    interface->tk_list1 = 0x0;
    interface->wdata0 = 0x0;
    interface->wdata1 = 0x0;
    
    // Offset into token registers
    uint32_t tk_offset = 0;

    // Offset into wdata registers
    uint32_t wdat_offset = 0;

    // Offset into supplied buffer
    int i = i2c_ifState.current_req_len - i2c_ifState.remaining;
    // printf("Current offset into request: %d\n", i);
    while (tk_offset < 16 && wdat_offset < 8) {
        // Explicitly pad END tokens for empty space
        if (i >= i2c_ifState.current_req_len) {
            if (tk_offset < 8) {
                interface->tk_list0 |= (OC4_I2C_TK_END << (tk_offset * 4));
                tk_offset++;
            } else {
                interface->tk_list1 = interface->tk_list1 | (OC4_I2C_TK_END << ((tk_offset -8) * 4));
                tk_offset++;
            }
            i++;
            continue;
        }
        
        // Skip first three: client id and addr
        i2c_token_t tok = tokens[REQ_BUF_DAT_OFFSET + i];

        uint32_t odroid_tok = 0x0;
        // Translate token to ODROID token
        switch (tok) {
            case I2C_TK_END:
                odroid_tok = OC4_I2C_TK_END;
                break;
            case I2C_TK_START:
                odroid_tok = OC4_I2C_TK_START;
                break;
            case I2C_TK_ADDRW:
                odroid_tok = OC4_I2C_TK_ADDRW;
                i2c_ifState.ddr = 0;
                break;
            case I2C_TK_ADDRR:
                odroid_tok = OC4_I2C_TK_ADDRR;
                i2c_ifState.ddr = 1;
                break;
            case I2C_TK_DAT:
                odroid_tok = OC4_I2C_TK_DATA;
                break;
            case I2C_TK_DATA_END:
                odroid_tok = OC4_I2C_TK_DATA_END;
                break;
            case I2C_TK_STOP:
                odroid_tok = OC4_I2C_TK_STOP;
                break;
            default:
                printf("i2c: invalid data token in request! \"%x\"\n", tok);
                return -1;
        }
        // printf("Loading token %d: %d\n", i, odroid_tok);

        if (tk_offset < 8) {
            interface->tk_list0 |= ((odroid_tok & 0xF) << (tk_offset * 4));
            tk_offset++;
        } else {
            interface->tk_list1 = interface->tk_list1 | (odroid_tok << ((tk_offset -8) * 4));
            tk_offset++;
        }
        // If data token and we are writing, load data into wbuf registers
        if (odroid_tok == OC4_I2C_TK_DATA && !i2c_ifState.ddr) {
            if (wdat_offset < 4) {
                interface->wdata0 = interface->wdata0 | (tokens[2 + i + 1] << (wdat_offset * 8));
                wdat_offset++;
            } else {
                interface->wdata1 = interface->wdata1 | (tokens[2 + i + 1] << ((wdat_offset - 4) * 8));
                wdat_offset++;
            }
            // Since we grabbed the next token in the chain, increment offset
            i++;
        }
        i++;
    }

    // Data loaded. Update remaining tokens indicator and start list processor
    i2c_ifState.remaining = (i2c_ifState.current_req_len - i > 0) 
                                 ? i2c_ifState.current_req_len - i : 0;

    printf("driver: Tokens loaded. %zu remain for this request\n", i2c_ifState.remaining);
    i2cDump();
    // Start list processor
    i2cStart();
    COMPILER_MEMORY_FENCE();

    return 0;
}



void init(void) {
    setupi2c();
    i2cTransportInit(0);
    // Set up driver state
    i2c_ifState.current_req = NULL;
    i2c_ifState.current_ret = NULL;
    i2c_ifState.current_req_len = 0;
    i2c_ifState.remaining = 0;
    i2c_ifState.notified = 0;
    
    sel4cp_dbg_puts("Driver initialised.\n");
}

/**
 * Check if there is work to do for a given bus and dispatch it if so.
*/
static inline void checkBuf() {
    if (!reqBufEmpty()) {
        // If this interface is busy, skip notification and
        // set notified flag for later processing
        if (i2c_ifState.current_req) {
            sel4cp_dbg_puts("driver: request in progress, deferring notification\n");
            i2c_ifState.notified = 1;
            return;
        }
        // sel4cp_dbg_puts("driver: starting work for bus\n");
        // Otherwise, begin work. Start by extracting the request

        size_t sz = 0;
        req_buf_ptr_t req = popReqBuf(&i2c_ctx, &sz);

        if (!req) {
            return;   // If request was invalid, run away.
        }

        ret_buf_ptr_t ret = getRetBuf(&i2c_ctx);
        if (!ret) {
            printf("driver: no ret buf!\n");
            releaseReqBuf(&i2c_ctx, req);
            return;
        }

        // Load bookkeeping data into return buffer
        // Set client PD

        printf("driver: Loading request from client %u to address %x of sz %zu\n", req[0], req[1], sz);

        ret[RET_BUF_CLIENT] = req[REQ_BUF_CLIENT];      // Client PD
        
        // Set targeted i2c address
        ret[RET_BUF_ADDR] = req[REQ_BUF_ADDR];          // Address

        if (sz <= REQ_BUF_DAT_OFFSET || sz > I2C_BUF_SZ) {
            printf("Invalid request size: %zu!\n", sz);
            return;
        }
        // printf("ret buf first 4 bytes: %x %x %x %x\n", ret[0], ret[1], ret[2], ret[3]);
        i2c_ifState.current_req = req;
        i2c_ifState.current_req_len = sz - REQ_BUF_DAT_OFFSET;
        i2c_ifState.remaining = sz - REQ_BUF_DAT_OFFSET;    // Ignore client PD and address
        i2c_ifState.notified = 0;
        i2c_ifState.current_ret = ret;
        if (!i2c_ifState.current_ret) {
            sel4cp_dbg_puts("i2c: no ret buf!\n");
        }

        // Bytes 0 and 1 are for error code / location respectively and are set later

        // Trigger work start
        i2cLoadTokens();
    } else {
        sel4cp_dbg_puts("driver: called but no work available: resetting notified flag\n");
        // If nothing needs to be done, clear notified flag if it was set.
        i2c_ifState.notified = 0;
    }
}

/**
 * Handling for notifications from the server. Responsible for
 * checking ring buffers and dispatching requests to appropriate
 * interfaces.
*/
static inline void serverNotify(void) {
    // If we are notified, data should be available.
    // Check if the server has deposited something in the request rings
    // - note that we individually check each interface's ring since
    // they operate in parallel and notifications carry no other info.
    
    // If there is work to do, attempt to do it
    sel4cp_dbg_puts("i2c: driver notified!\n");
    for (int i = 2; i < 4; i++) {
        checkBuf(i);
    }
}

/**
 * IRQ handler for an i2c interface.
 * @param timeout Whether the IRQ was triggered by a timeout. 0 if not, 1 if so.
*/
static inline void i2cirq(int timeout) {
    printf("i2c: driver irq\n");
    // printf("notified = %d\n", i2c_ifState.notified);
    
    // IRQ landed: i2c transaction has either completed or timed out.
    if (timeout) {
        sel4cp_dbg_puts("i2c: timeout!\n");
        i2cHalt();
        if (i2c_ifState.current_ret) {
            i2c_ifState.current_ret[RET_BUF_ERR] = I2C_ERR_TIMEOUT;
            i2c_ifState.current_ret[RET_BUF_ERR_TK] = 0x0;
            pushRetBuf(i2c_ifState.current_ret, i2c_ifState.current_req_len);
        }
        if (i2c_ifState.current_req) {
            releaseReqBuf(i2c_ifState.current_req);
        }
        i2c_ifState.current_ret = NULL;
        i2c_ifState.current_req = 0x0;
        i2c_ifState.current_req_len = 0;
        i2c_ifState.remaining = 0;
        return;
    }

    i2cDump();
    i2cHalt();

    // Get result
    int err = i2cGetError();
    // If error is 0, successful write. If error >0, successful read of err bytes.
    // Prepare to extract data from the interface.
    ret_buf_ptr_t ret = i2c_ifState.current_ret;

    printf("ret %p\n", ret);
    // If there was an error, cancel the rest of this transaction and load the
    // error information into the return buffer.
    if (err < 0) {
        sel4cp_dbg_puts("i2c: error!\n");
        if (timeout) {
            ret[RET_BUF_ERR] = I2C_ERR_TIMEOUT;
        } else if (err == -I2C_TK_ADDRR) {
            ret[RET_BUF_ERR] = I2C_ERR_NOREAD;
        } else {
            ret[RET_BUF_ERR] = I2C_ERR_NACK;
        }
        ret[RET_BUF_ERR_TK] = -err;   // Token that caused error
    } else {
        // If there was a read, extract the data from the interface
        if (err > 0) {
            // Get read data
            // Copy data into return buffer
            for (int i = 0; i < err; i++) {
                if (i < 4) {
                    ret[RET_BUF_DAT_OFFSET+i] = (interface->rdata0 >> (i * 8)) & 0xFF;
                } else {
                    ret[RET_BUF_DAT_OFFSET+i] = (interface->rdata1 >> ((i - 4) * 8)) & 0xFF;
                }
            }
        }

        ret[RET_BUF_ERR] = I2C_ERR_OK;    // Error code
        ret[RET_BUF_ERR_TK] = 0x0;           // Token that caused error
    }
    // printf("IRQ2: ret buf first 4 bytes: %x %x %x %x\n", ret[0], ret[1], ret[2], ret[3]);

    // If request is completed or there was an error, return data to server and notify.
    if (err < 0 || !i2c_ifState.remaining) {
        printf("driver: request completed or error, returning to server\n");
        pushRetBuf(&i2c_ctx, i2c_ifState.current_ret, i2c_ifState.current_req_len);
        releaseReqBuf(&i2c_ctx, i2c_ifState.current_req);
        i2c_ifState.current_ret = NULL;
        i2c_ifState.current_req = 0x0;
        i2c_ifState.current_req_len = 0;
        i2c_ifState.remaining = 0;
        sel4cp_notify(SERVER_NOTIFY_ID);
        // Reset hardware
        i2cHalt();
    }

    // If the driver was notified while this transaction was in progress, immediately start working on the next one.
    // OR if there is still work to do, crack on with it.
    // NOTE: this incurs more stack depth than needed; could use flag instead?
    if (i2c_ifState.notified || i2c_ifState.remaining) {
        if (i2c_ifState.notified) printf("driver: notified while processing IRQ, starting next request\n");
        else { printf("driver: still work to do, starting next batch\n");}
        i2cLoadTokens();
    }
    printf("driver: END OF IRQ HANDLER - notified=%d\n", i2c_ifState.notified);
}



void notified(sel4cp_channel c) {
    switch (c) {
        case SERVER_NOTIFY_ID:
            serverNotify();
            break;
        case IRQ_I2C:
            i2cirq(0);
            sel4cp_irq_ack(IRQ_I2C);
            break;
        case IRQ_I2C_TO:
            i2cirq(1);
            sel4cp_irq_ack(IRQ_I2C_TO);
            break;

        default:
            sel4cp_dbg_puts("DRIVER|ERROR: unexpected notification!\n");
    }
}