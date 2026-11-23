# K5-APRS - APRS station firmware for the Quansheng UV-K5 (DP32G030)

TARGET    ?= k5-aprs
APP_NAME  := K5-APRS
VERSION   ?= 0.9.2a
BUILD_ID  ?= $(shell git describe --always --dirty 2>/dev/null || echo dev)
BUILD     := build
# ENABLE_TX=0 builds a receive-only firmware (the transmitter is never keyed).
ENABLE_TX ?= 1

CROSS     := arm-none-eabi-
CC        := $(CROSS)gcc
AS        := $(CROSS)gcc
OBJCOPY   := $(CROSS)objcopy
SIZE      := $(CROSS)size
PYTHON    ?= python3

SRCS := \
	startup/init.c \
	driver/adc.c \
	driver/bk4819.c \
	driver/eeprom.c \
	driver/i2c.c \
	driver/keyboard.c \
	driver/spi.c \
	driver/st7565.c \
	driver/system.c \
	driver/systick.c \
	driver/uart.c \
	src/app.c \
	src/aprs.c \
	src/aprs_parse.c \
	src/battery.c \
	src/beacon.c \
	src/beep.c \
	src/ax25.c \
	src/board.c \
	src/channel.c \
	src/console.c \
	src/digi.c \
	src/font.c \
	src/geo.c \
	src/gfx.c \
	src/hdlc_tx.c \
	src/input.c \
	src/kiss.c \
	src/inbox.c \
	src/main.c \
	src/radio.c \
	src/settings.c \
	src/store.c \
	src/memory.c \
	src/menu.c \
	src/msg.c \
	src/station.c \
	src/textedit.c \
	src/tx.c \
	src/txq.c

ASRCS := startup/start.S

OBJS := $(ASRCS:%.S=$(BUILD)/%.o) $(SRCS:%.c=$(BUILD)/%.o)
DEPS := $(OBJS:.o=.d)

INC := -I . -I src \
	-I external/CMSIS/Core/Include \
	-I external/CMSIS/Device/ARMCM0/Include

CFLAGS  := -mcpu=cortex-m0 -mthumb -std=c2x -Oz -g \
	-Wall -Wextra -Werror \
	-fshort-enums -fno-delete-null-pointer-checks \
	-ffunction-sections -fdata-sections \
	-MMD $(INC)
ifeq ($(ENABLE_TX),1)
CFLAGS  += -DENABLE_TX
endif
ASFLAGS := -mcpu=cortex-m0 -mthumb -c $(INC)
LDFLAGS := -mcpu=cortex-m0 -mthumb -nostartfiles --specs=nano.specs \
	-Wl,-T,firmware.ld -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map -Wl,--no-wchar-size-warning -z noexecstack

.PHONY: all clean

all: $(BUILD)/$(TARGET).packed.bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/version.h: FORCE
	@mkdir -p $(@D)
	@printf '#define APP_NAME "%s"\n#define VERSION_STRING "%s"\n#define BUILD_ID "%s"\n' \
		'$(APP_NAME)' '$(VERSION)' '$(BUILD_ID)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp

$(BUILD)/$(TARGET).elf: $(OBJS)
	@echo LD $@
	@$(CC) $(LDFLAGS) $^ -o $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(OBJCOPY) -O binary $< $@

# Packed image for the stock bootloader / web flashers (obfuscated + CRC).
$(BUILD)/$(TARGET).packed.bin: $(BUILD)/$(TARGET).bin
	@echo PACK $@
	@$(PYTHON) tools/fw-pack.py $< $(APP_NAME) $(VERSION) $@

$(BUILD)/%.o: %.c $(BUILD)/version.h
	@mkdir -p $(@D)
	@echo CC $<
	@$(CC) $(CFLAGS) -I $(BUILD) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(@D)
	@echo AS $<
	@$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -rf $(BUILD)

FORCE:

-include $(DEPS)
