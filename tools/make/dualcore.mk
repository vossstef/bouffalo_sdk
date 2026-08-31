all:
	make -C $(APP_NAME)_np BUILD_DIR=../$(BUILD_DIR)/np CPU_ID=np;				      \
	make -C $(APP_NAME)_ap BUILD_DIR=../$(BUILD_DIR)/ap CPU_ID=ap CONFIG_DUALCORE_NP_IMAGE=../np/build_out/$(APP_NAME)_$(CHIP)_np.bin;                                                                                     \
	cp $(BUILD_DIR)/ap/build_out/$(APP_NAME)_$(CHIP)_ap.bin $(BUILD_DIR)/$(APP_NAME)_$(CHIP).bin

clean:
	make -C $(APP_NAME)_np clean
	make -C $(APP_NAME)_ap clean
	cmake -E remove_directory $(BUILD_DIR)

cmake_cache:
	make -C $(APP_NAME)_np $@
	make -C $(APP_NAME)_ap $@

IS_WSL2 = $(findstring WSL2,$(shell uname -r))
ifneq ($(IS_WSL2),)
	# WSL2 - use Windows version for COM ports
	FLASH_CMD = $(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand.exe
else
	# Native Linux or others
	UNAME_S = $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		FLASH_CMD = $(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu
	else
		FLASH_CMD = $(BL_SDK_BASE)/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand.exe
	endif
endif

WHOLE_APP_BIN = $(BUILD_DIR)/$(APP_NAME)_$(CHIP).bin
WHOLE_BUILD_OUT = $(BUILD_DIR)/ap/build_out
WHOLE_BOOT2_BIN = $(firstword $(wildcard $(WHOLE_BUILD_OUT)/boot2_*_isp_*.bin))
WHOLE_PARTITION_BIN = $(WHOLE_BUILD_OUT)/partition.bin

-include $(SDK_DEMO_PATH)/defconfig
ifeq ($(CONFIG_STD_BOOT2APP),y)
whole:
	python3 $(BL_SDK_BASE)/tools/bflb_whole_bin.py \
		--app $(WHOLE_APP_BIN) \
		--boot2 $(WHOLE_BOOT2_BIN) \
		--pt $(WHOLE_PARTITION_BIN) \
		--output $(BUILD_DIR)/whole_flash_data.bin
else
whole:
	cmake -E copy $(WHOLE_APP_BIN) $(BUILD_DIR)/whole_flash_data.bin
endif

flash: whole
	$(FLASH_CMD) --chip=$(CHIP) --port $(FLASH_COMX) --whole_chip --firmware $(BUILD_DIR)/whole_flash_data.bin

ota: whole
	$(BL_SDK_BASE)/tools/bflb_tools/bflb_fw_post_proc/bflb_fw_post_proc-ubuntu --chipname=$(CHIP) --imgfile=$(BUILD_DIR)/$(APP_NAME)_$(CHIP).bin --appkeys=shared --brdcfgdir=$(BL_SDK_BASE)/bsp/board/$(BOARD)/config

.PHONY: clean
