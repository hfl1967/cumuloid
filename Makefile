# Project Name
TARGET = Clouds

# Boot type - puts entire program in QSPI flash
APP_TYPE = BOOT_QSPI

# Sources
CPP_SOURCES = Clouds.cpp

# Includes
C_INCLUDES += -I. -I..

# Library Locations
LIBDAISY_DIR = ../../DaisyExamples/libDaisy
DAISYSP_DIR = ../../DaisyExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
