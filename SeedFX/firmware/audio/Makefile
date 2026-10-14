TARGET = seedfx
CPP_STANDARD = -std=gnu++17
LIBDAISY_DIR = ../../../Software/GuitarPedal/dependencies/libDaisy
APP_TYPE = BOOT_SRAM
OPT = -O3
CPP_SOURCES = main.cpp ../../../Software/GuitarPedal/Effect-Modules/ImpulseResponse/ir_data.cpp
C_SOURCES = $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/FilteringFunctions/arm_fir_f32.c $(LIBDAISY_DIR)/Drivers/CMSIS-DSP/Source/FilteringFunctions/arm_fir_init_f32.c
C_DEFS = -DARM_MATH_LOOPUNROLL
GCC_PATH ?= /workspace/daisy-environment/toolchains/xpack-arm-none-eabi-gcc-10.3.1-2.3/bin
LDSCRIPT = stm32h750_seedfx_sram.lds
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
C_INCLUDES += -I../common
# Do not erase the preset journal at 0x907F0000 when updating firmware.

.PHONY: program-dfu-wait
program-dfu-wait:
	dfu-util -w -a 0 -s $(FLASH_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:$(USBPID)
