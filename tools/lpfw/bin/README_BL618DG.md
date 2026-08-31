Release Date: 2026-08-31
Release Author: whuan
Git Commit Version: f6a93a5f33babccb1b2036cb1b432842e5748768

Submodule Information:
-3bc6321603b0ab07c39ebdbe6ad1c65e0f4beb45 ../../../../components/crypto/mbedtls/mbedtls
-4c3acca7109deb4dca6956dd5f3ceaf0947dee58 ../../../../components/crypto/mbedtls/mbedtls_v3
-40632e430ee7e96df00876b38586034304c718c4 ../../../../components/fs
-b6edf14d79d23ba6974e56730b0f936177cac782 ../../../../components/graphics
-8431fe111a8d0f09a463a8d1f3eb42a7de753892 ../../../../components/ipc/libmetal/libmetal
-76a12961fceda27871eb9338ae802c2797babad9 ../../../../components/ipc/open-amp/open-amp
-c9796cd89421a9e48053efb4144c5ebfeadca4ec ../../../../components/multimedia
-0869c85c0d5f2868456db533fa17722cb1028139 ../../../../components/net/lwip/lwip
-67adfe561667b4ae40f4ff26a4de069c2200e5b0 ../../../../components/net/netbus/atmodule
-41b066b0b68fe113c7a808546454bb3f1b9b1205 ../../../../components/net/netbus/host_router
-47765b8917a01ff6e2bfbab891f151291b63e028 ../../../../components/net/nethub/host_linux
-a5c3e0808e3dc188c49e75b5fe482459c29447da ../../../../components/usb/cherryusb
-ab3e6d96e00da02beee1372f00d3f53811c8c48e ../../../../components/wireless/bl3141_drv
-ff6b295ec4c03cb3ee6560a3e1617063af45e9ff ../../../../components/wireless/bluetooth
-731baacb5a0149d33209b60e4d7693ee78af68e5 ../../../../components/wireless/lmac154
-4bae60938954b9b2b9643ed8a057fb1644dcbebc ../../../../components/wireless/macsw
-f15f28f97477c5e60e685ca33d1711ac321e1d1c ../../../../components/wireless/matter/mfd
-850c00de701900e63a3c522439cf6bf310e4824c ../../../../components/wireless/thread
-820badeede80b2f95c271b0446af4725f8edcc0a ../../../../components/wireless/wifi4/firmware
-77df3e0ab53ebc9c5375c14f538b4e01470da5b3 ../../../../components/wireless/wifi4/manager
-dd5556963b16d0e7d566360e08dac0175a528d88 ../../../../components/wireless/wifi6
-32e4c4d8d7d88eab758f79d3c59e2a6eb55a95c1 ../../../../components/wireless/wl80211
-7d2ba7c17495779163accc55a6a3f96d39b3cc4d ../../../../components/wireless/wlan/linux_driver
-6d363aa04be189f278e9fa6c87b1b8032d46d1cb ../../../../components/wireless/wlan/wlan_mac
-dd9bd2e7139dc9f7b98d0f2a237598d2d8e3ada3 ../../../../components/wireless/zigbee
-f8aebf60ffdff6ade4c7bd1ac1a11ea2a892b634 ../../../../drivers/lhal
-25ffb329a3b97c5d4fc79d66c60e0b988882d996 ../../../../drivers/soc/bl602/phyrf
-1d253d273aa044ec80c3b0a0f17a7a6f0a0531e1 ../../../../drivers/soc/bl602/std
-450aef3d18fe1eb9333f833f9e3cde8ba056e3a3 ../../../../drivers/soc/bl616/phyrf
-73d98a0bd2c7f183f09afa40d230e4a960d32a2e ../../../../drivers/soc/bl616/std
-a0b68bbcafdda0936cd5ae0971631d7f66fa1838 ../../../../drivers/soc/bl616cl/phyrf
-a9630593c6c771f83b18bdafa288a2112395595c ../../../../drivers/soc/bl616cl/std
-79f890ffd5a550e3e19e8aefdccc8e562c0ade43 ../../../../drivers/soc/bl618dg/phyrf
-fe2634e32d00ce2fcfbe3713a64e9212dabb9393 ../../../../drivers/soc/bl618dg/std
-712e35f8da9fc43c920cd0259a04ed9a78276d01 ../../../../drivers/soc/bl702/phyrf
-63ff630710365510a76d5884f9263d95434b3263 ../../../../drivers/soc/bl702/std
-89a6a4072e1f48b1d245deced6c0cd44f65b77fb ../../../../drivers/soc/bl702l/phyrf
-829debea7cc1af9cd07da9f1b3397d3c703533db ../../../../drivers/soc/bl702l/std
-8e53f32692b48c9f304cb98d79059c3aeed92b55 ../../../../drivers/sys
-060fc2d67573a2acdedeb7fcc41cac38d6557b4c ../../../../tools/autotest
-8ff87c51ebcc87ad3edfd4e58029e819a8983d81 ../../../../tools/bflb_tools

Validation Baseline:
- Target: BL618DG B0
- Build command: make CHIP=bl618dg BOARD=bl618dg_lpfw_board CPU_ID=ap CPU_MODEL=b0 MAKE_LPFW=y LOG_SEL=2 CONFIG_ROMAPI=y
- LPFW binary: lpfw_bin/bl618dg_lp_fw.bin
- Binary size: 42304 bytes
- SHA256: baa189045a36cb629a58adfd56421ce12e843fac574cc40e3d93f7365f14fcb2
- Link memory: check build output and build/build_out/lp_fw_bl618dg_ap.map
- Sync target: tools/lpfw/bin/bl618dg_lp_fw.bin
- Consumer app: examples/pmu/wl_lp
- App build command: make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap CPU_MODEL=b0

Board validation still needs to record boot logs, PDS entry, wakeup logs, Wi-Fi low-power RX behavior, stress result, and current measurement.
