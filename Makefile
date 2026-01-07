# Project Name
TARGET = AcidusVersio

#Open303
OPEN303 = ./open303
OPEN303_SOURCE = ${OPEN303}/Source/DSPCode
CXXFLAGS += -I{OPEN303_SOURCE}

# Sources
CPP_SOURCES = AcidusVersio.cpp
CPP_SOURCES +=${wildcard open303/Source/DSPCode/*.cpp}

#DEBUG=1

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Linker flags
# This is not really required, used only for profiling! Increases executable size by ~8kB
# LDFLAGS = -u _printf_float

C_INCLUDES += -I${OPEN303_SOURCE}

# Core location, and generic Makefile.
OPT = -Os
APP_TYPE = BOOT_SRAM
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include ${SYSTEM_FILES_DIR}/Makefile
