# Project Name
TARGET = NitroTron3

# Uncomment to use LGPL (like ReverbSc, etc.)
#USE_DAISYSP_LGPL=1

# Sources and Hothouse header files
CPP_SOURCES = src/NitroTron3.cpp lib/HothouseExamples/src/hothouse.cpp
C_INCLUDES = -Isrc -Ilib/HothouseExamples/src

# Library Locations
LIBDAISY_DIR = lib/HothouseExamples/libDaisy
DAISYSP_DIR = lib/HothouseExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
