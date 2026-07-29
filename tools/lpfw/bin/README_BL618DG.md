Release Date: 2026-07-20
Release Author: mlwang
Git Commit Version: 0183eb445e45c6da87ac2e21e6793a61d587da5e

Submodule Information:
-3bc6321603b0ab07c39ebdbe6ad1c65e0f4beb45 ../../../../components/crypto/mbedtls/mbedtls
-4c3acca7109deb4dca6956dd5f3ceaf0947dee58 ../../../../components/crypto/mbedtls/mbedtls_v3
-5e709d00c86de2ab064a16a848c87f941d72cab9 ../../../../components/fs
-34a853821e47bdae5f74aa1069c7367cf642372b ../../../../components/graphics
-8431fe111a8d0f09a463a8d1f3eb42a7de753892 ../../../../components/ipc/libmetal/libmetal
-76a12961fceda27871eb9338ae802c2797babad9 ../../../../components/ipc/open-amp/open-amp
-04f088698ad94bb2e88a90efacd8af58ab65fcab ../../../../components/multimedia
-eebdb1e454248f49a61e279910a99fb304137d8a ../../../../components/net/lwip/lwip
-7856a837f8fd955708148ba3920dae1036f22290 ../../../../components/net/netbus/atmodule
-41b066b0b68fe113c7a808546454bb3f1b9b1205 ../../../../components/net/netbus/host_router
-47765b8917a01ff6e2bfbab891f151291b63e028 ../../../../components/net/nethub/host_linux
-621ccd5eedb262f1b7e01ab4bca7131bda3034a5 ../../../../components/usb/cherryusb
-ab3e6d96e00da02beee1372f00d3f53811c8c48e ../../../../components/wireless/bl3141_drv
-9a6ec27de5eb3f9209be316e56c836aee869a777 ../../../../components/wireless/bluetooth
-d449add7c7384ccaaccb0687b3dcb76c6091c481 ../../../../components/wireless/lmac154
-dc0e651095854c2612d85ef61bd04a62a2bcf82e ../../../../components/wireless/macsw
-f15f28f97477c5e60e685ca33d1711ac321e1d1c ../../../../components/wireless/matter/mfd
-a31c373320deb859543f5e3883437a7e72b91054 ../../../../components/wireless/thread
-b6eb2298ceac2e5a90d68284501b895ed6e0352f ../../../../components/wireless/wifi4/firmware
-0d5268129950681791c2e12ffa2a045ab68a822a ../../../../components/wireless/wifi4/manager
-0c837e421e1116272db4c1ae19f8de7b17a8bb50 ../../../../components/wireless/wifi6
-82913cad570be7a344ec00c0e51a961c9e5e7167 ../../../../components/wireless/wl80211
-3a8b7753d7ec4325ca1e0488361fcc1b63f132df ../../../../components/wireless/wlan/linux_driver
-ae603b4bbec1819f5cf61996dd246a961a970b5d ../../../../components/wireless/wlan/wlan_mac
-a82fe317de9045aed2d6857ebec5a1467049bfce ../../../../components/wireless/zigbee
-2b89652a1ba29ccc1f672a5439035af0a89e2ba8 ../../../../drivers/lhal
-25ffb329a3b97c5d4fc79d66c60e0b988882d996 ../../../../drivers/soc/bl602/phyrf
-1d253d273aa044ec80c3b0a0f17a7a6f0a0531e1 ../../../../drivers/soc/bl602/std
-f910c86e6083dee8946203ca9d582c2c91ebd169 ../../../../drivers/soc/bl616/phyrf
-790d43579ade7f78e481e830b2c6f08346f808cc ../../../../drivers/soc/bl616/std
-6d6bf83b664ae544e1323333981b8fa61a583eda ../../../../drivers/soc/bl616cl/phyrf
-7fc4ee0456f36008410b66c1ae980352671adc96 ../../../../drivers/soc/bl616cl/std
-0b5106db2b2d56bf696c520be617c7cfbdcedd22 ../../../../drivers/soc/bl618dg/phyrf
-fb56b2000fafe6cddb70ea97c62ba845634f1e22 ../../../../drivers/soc/bl618dg/std
-712e35f8da9fc43c920cd0259a04ed9a78276d01 ../../../../drivers/soc/bl702/phyrf
-63ff630710365510a76d5884f9263d95434b3263 ../../../../drivers/soc/bl702/std
-89a6a4072e1f48b1d245deced6c0cd44f65b77fb ../../../../drivers/soc/bl702l/phyrf
-829debea7cc1af9cd07da9f1b3397d3c703533db ../../../../drivers/soc/bl702l/std
-1eefd9cde1bfbf40587fb5c42536827ab96ce1ce ../../../../drivers/sys
-060fc2d67573a2acdedeb7fcc41cac38d6557b4c ../../../../tools/autotest
-11d409a9ff451ba3d8658d4ed4e6f3b9da587193 ../../../../tools/bflb_tools

Validation Baseline:
- Target: BL618DG B0
- Build command: make CHIP=bl618dg BOARD=bl618dg_lpfw_board CPU_ID=ap CPU_MODEL=b0 MAKE_LPFW=y LOG_SEL=2 CONFIG_ROMAPI=y
- LPFW binary: lpfw_bin/bl618dg_lp_fw.bin
- Binary size: 37792 bytes
- SHA256: 71089e6c13843672adccd2bc34e79c95956f1ded5640f402761984302eb2c83a
- Link memory: check build output and build/build_out/lp_fw_bl618dg_ap.map
- Sync target: tools/lpfw/bin/bl618dg_lp_fw.bin
- Consumer app: examples/pmu/wl_lp
- App build command: make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap CPU_MODEL=b0

Board validation still needs to record boot logs, PDS entry, wakeup logs, Wi-Fi low-power RX behavior, stress result, and current measurement.
