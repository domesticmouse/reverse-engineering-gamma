# Target Name
TARGET = phase4_keys_encoder

# App Type for Daisy Bootloader (runs from internal SRAM, loaded from QSPI flash)
APP_TYPE = BOOT_SRAM

# Sources
CPP_SOURCES = main.cpp

# Optimization and debug
OPT = -O2

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
