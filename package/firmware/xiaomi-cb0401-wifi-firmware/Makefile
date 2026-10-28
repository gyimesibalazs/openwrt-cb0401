#
# Xiaomi CB0401 stock WiFi board data, extracted from the device's own stock
# firmware with tools/cb0401/extract-stock-firmware.sh (vendor binaries, not
# part of the tree): the IPQ5018 and QCN9074 board-2.bin files
# (bdwlan.b24 / bdwlan.ba0). The Q6/M3 firmware comes from
# ath11k-firmware-ipq5018-qcn6122 (multi-PD WLAN.HK.2.7.0.1).
#
# Installed as a rootfs overlay so they replace the ath11k-firmware files.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=xiaomi-cb0401-wifi-firmware
PKG_RELEASE:=2
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

STOCK_FILES:=$(CURDIR)/files

define Package/xiaomi-cb0401-wifi-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Xiaomi CB0401 stock WiFi board data
  DEPENDS:=@TARGET_qualcommax_ipq50xx
endef

define Package/xiaomi-cb0401-wifi-firmware/description
  The IPQ5018/QCN9074 board-2.bin files of the Xiaomi CB0401, taken from
  its stock firmware. Run tools/cb0401/extract-stock-firmware.sh on a backup
  of the stock rootfs before building.
endef

define Build/Compile
	@[ -f $(STOCK_FILES)/IPQ5018/board-2.bin ] && \
	 [ -f $(STOCK_FILES)/QCN9074/board-2.bin ] || { \
		echo "$(PKG_NAME): stock files missing in $(STOCK_FILES)," \
		     "run tools/cb0401/extract-stock-firmware.sh" >&2; \
		exit 1; }
endef

define Package/xiaomi-cb0401-wifi-firmware/install-overlay
	$(INSTALL_DIR) $(1)/lib/firmware/ath11k/IPQ5018/hw1.0
	$(INSTALL_DATA) $(STOCK_FILES)/IPQ5018/board-2.bin $(1)/lib/firmware/ath11k/IPQ5018/hw1.0/
	$(INSTALL_DIR) $(1)/lib/firmware/ath11k/QCN9074/hw1.0
	$(INSTALL_DATA) $(STOCK_FILES)/QCN9074/board-2.bin $(1)/lib/firmware/ath11k/QCN9074/hw1.0/
endef

$(eval $(call BuildPackage,xiaomi-cb0401-wifi-firmware))
