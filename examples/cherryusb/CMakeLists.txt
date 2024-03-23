cmake_minimum_required(VERSION 3.15)

include(proj.conf)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

sdk_add_include_directories(.)

set(CONFIG_CHERRYUSB_HOST 1)
set(CONFIG_CHERRYUSB_HOST_CDC_ACM 1)
set(CONFIG_CHERRYUSB_HOST_CDC_ECM 1)
set(CONFIG_CHERRYUSB_HOST_CDC_NCM 1)
set(CONFIG_CHERRYUSB_HOST_HID 1)
set(CONFIG_CHERRYUSB_HOST_MSC 1)
set(CONFIG_CHERRYUSB_HOST_VIDEO 1)
set(CONFIG_CHERRYUSB_HOST_AUDIO 1)
set(CONFIG_CHERRYUSB_HOST_CDC_RNDIS 1)
set(CONFIG_CHERRYUSB_HOST_BLUETOOTH 1)
set(CONFIG_CHERRYUSB_HOST_ASIX 1)
set(CONFIG_CHERRYUSB_HOST_RTL8152 1)
set(CONFIG_CHERRYUSB_OSAL "freertos")
set(CONFIG_CHERRYUSB_HOST_HCD "ehci_bouffalo")

include(cherryusb/cherryusb.cmake)
target_sources(app PRIVATE ${cherryusb_srcs})
sdk_add_include_directories(${cherryusb_incs})

target_sources(app PRIVATE usb_host.c)
sdk_set_main_file(main.c)
sdk_set_linker_script(bl616_flash.ld)
project(cherryusb)
