cmake_minimum_required(VERSION 3.15)

include(proj.conf)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(.)

sdk_add_include_directories(CherryUSB/common)
sdk_add_include_directories(CherryUSB/osal)
sdk_add_include_directories(CherryUSB/core)
sdk_add_include_directories(CherryUSB/class/cdc)
sdk_add_include_directories(CherryUSB/class/hid)
sdk_add_include_directories(CherryUSB/class/msc)
sdk_add_include_directories(CherryUSB/class/audio)
sdk_add_include_directories(CherryUSB/class/video)
sdk_add_include_directories(CherryUSB/class/hub)
sdk_add_include_directories(CherryUSB/class/wireless)
sdk_add_include_directories(CherryUSB/class/vendor/net)
sdk_add_include_directories(CherryUSB/class/vendor/serial)

target_sources(app PRIVATE CherryUSB/core/usbh_core.c)
target_sources(app PRIVATE CherryUSB/port/ehci/usb_hc_ehci.c)
target_sources(app PRIVATE CherryUSB/port/ehci/usb_glue_bouffalo.c)
target_sources(app PRIVATE CherryUSB/osal/usb_osal_freertos.c)
target_sources(app PRIVATE CherryUSB/class/cdc/usbh_cdc_acm.c)
target_sources(app PRIVATE CherryUSB/class/cdc/usbh_cdc_ecm.c)
target_sources(app PRIVATE CherryUSB/class/cdc/usbh_cdc_ncm.c)
target_sources(app PRIVATE CherryUSB/class/msc/usbh_msc.c)
target_sources(app PRIVATE CherryUSB/class/hid/usbh_hid.c)
target_sources(app PRIVATE CherryUSB/class/hub/usbh_hub.c)
target_sources(app PRIVATE CherryUSB/class/video/usbh_video.c)
target_sources(app PRIVATE CherryUSB/class/audio/usbh_audio.c)
target_sources(app PRIVATE CherryUSB/class/wireless/usbh_rndis.c)
target_sources(app PRIVATE CherryUSB/class/wireless/usbh_bluetooth.c)
target_sources(app PRIVATE CherryUSB/class/vendor/net/usbh_asix.c)

set(BLUETOOTH_PATH CherryUSB/third_party/zephyr_bluetooth-2.7.5)

add_subdirectory(${BLUETOOTH_PATH}/zephyr_bluetooth)
target_link_libraries(zephyr_bluetooth sdk_intf_lib)
target_link_libraries(app zephyr_bluetooth)

target_sources(app PRIVATE 
${BLUETOOTH_PATH}/ble_hci_usbh.c 
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/beacon/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/central/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/central_hr/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/central_ht/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/central_multilink/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/central_multilink/src/central_multilink.c
# ${BLUETOOTH_PATH}/zephyr_bluetooth/examples/handsfree/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/ibeacon/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral/src/cts.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_csc/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_dis/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_esp/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_hids/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_hids/src/hog.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_hr/src/main.c
# ${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_ht/src/main.c
# ${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_ht/src/hts.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_identity/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_identity/src/peripheral_identity.c
# ${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_ots/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/peripheral_sc_only/src/main.c
${BLUETOOTH_PATH}/zephyr_bluetooth/examples/scan_adv/src/main.c
)

target_sources(app PRIVATE usb_host.c)
sdk_set_main_file(main.c)
sdk_set_linker_script(bl616_flash.ld)
project(cherryusb)
