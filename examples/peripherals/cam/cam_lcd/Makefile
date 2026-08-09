SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath  ./../../../..)

export BL_SDK_BASE

NOW_REAL_PATH := $(realpath ./)

ifeq ($(BOARD),bl616_ifa_board)
BOARD_DIR := $(NOW_REAL_PATH)
endif


include $(BL_SDK_BASE)/project.build
