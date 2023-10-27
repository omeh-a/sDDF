#
# Copyright 2021, Breakaway Consulting Pty. Ltd.
#
# SPDX-License-Identifier: BSD-2-Clause
#
ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif

ifeq ($(strip $(SEL4CP_SDK)),)
$(error SEL4CP_SDK must be specified)
endif

ifeq ($(strip $(SEL4CP_BOARD)),)
$(error SEL4CP_BOARD must be specified)
endif

ifeq ($(strip $(SEL4CP_CONFIG)),)
$(error SEL4CP_CONFIG must be specified)
endif

TOOLCHAIN := x86_64-linux-gnu

ARCH := skylake

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
SEL4CP_TOOL ?= $(SEL4CP_SDK)/bin/sel4cp

SDDF=$(abspath ../../)
UTIL=$(SDDF)/util

PCIELIB := $(abspath ../../util/pcielib/)
PCIE_FILES := x550t-pcie.c
COMMONFILES := $(UTIL)/printf/printf.c
PCIE_OBJS := $(PCIE_FILES:.c=.o) $(COMMONFILES:.c=.o)
BOARD_DIR := $(SEL4CP_SDK)/board/$(SEL4CP_BOARD)/$(SEL4CP_CONFIG)

IMAGES := x550t-pcie.elf
CFLAGS := -march=$(ARCH) -DARCH_x86_64 -nostdlib -ffreestanding -g3 -O3 -Wall  -Wno-unused-function -Werror -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lsel4cp -Tsel4cp.ld

CFLAGS += -I$(PCIELIB)/include	\
	-I include	\
	-I$(PCIELIB)/include \
	-Iinclude/arch	\
	-I$(UTIL)/include \
	-I$(UTIL)/printf \
	-MD \
	-MP

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

all: $(IMAGE_FILE)

OBJS := $(sort $(addprefix $(BUILD_DIR)/, $(PCIE_OBJS)))
DEPS := $(OBJS:.o=.d)

all: $(IMAGE_FILE)
-include $(DEPS)

$(BUILD_DIR)/%.d $(BUILD_DIR)/%.o: %.c Makefile
	mkdir -p `dirname $(BUILD_DIR)/$*.o`
	$(CC) -c $(CFLAGS) $< -o $(BUILD_DIR)/$*.o

$(BUILD_DIR)/%.o: %.s Makefile
	$(AS) -g3 -mcpu=$(CPU) $< -o $@

$(BUILD_DIR)/x550t-pcie.elf: $(addprefix $(BUILD_DIR)/, $(PCIE_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) pcie.system
	$(SEL4CP_TOOL) pcie.system --search-path $(BUILD_DIR) --board $(SEL4CP_BOARD) --config $(SEL4CP_CONFIG) --x86-machine machine.yml -o $(IMAGE_FILE) -r $(REPORT_FILE)

.PHONY: all compile clean

%.o:
	$(CC) $(CFLAGS) -c $(@:.o=.c) -o $@

clean:
	rm -f *.o *.elf .depend*
	find . -name \*.o |xargs --no-run-if-empty rm
