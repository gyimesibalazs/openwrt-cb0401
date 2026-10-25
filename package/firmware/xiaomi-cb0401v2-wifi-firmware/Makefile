#
# Xiaomi CB0401V2 stock WiFi files, extracted from the device's own stock
# firmware with tools/cb0401/extract-stock-firmware.sh (they are vendor
# binaries and are not part of the tree):
#
#  - IPQ5018 Q6/M3 firmware: the unit is secure-boot fused and TrustZone only
#    starts the Xiaomi signed WLAN.HK.2.6-02451 image on the WCSS
#  - IPQ5018 and QCN9074 board data (bdwlan.b23 / bdwlan.ba0)
#
# Installed as a rootfs overlay so they replace the ath11k-firmware files.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=xiaomi-cb0401v2-wifi-firmware
PKG_RELEASE:=1
PKG_FLAGS:=nonshared

include $(INCLUDE_DIR)/package.mk

STOCK_FILES:=$(CURDIR)/files

define Package/xiaomi-cb0401v2-wifi-firmware
  SECTION:=firmware
  CATEGORY:=Firmware
  TITLE:=Xiaomi CB0401V2 stock WiFi firmware and board data
  DEPENDS:=@TARGET_qualcommax_ipq50xx
endef

define Package/xiaomi-cb0401v2-wifi-firmware/description
  Signed IPQ5018 WCSS firmware and the IPQ5018/QCN9074 board-2.bin files of
  the Xiaomi CB0401V2, taken from its stock firmware. Run
  tools/cb0401/extract-stock-firmware.sh on a backup of the stock rootfs
  before building.
endef

define Build/Compile
	@[ -f $(STOCK_FILES)/IPQ5018/q6_fw.mdt ] && \
	 [ -f $(STOCK_FILES)/IPQ5018/board-2.bin ] && \
	 [ -f $(STOCK_FILES)/QCN9074/board-2.bin ] || { \
		echo "$(PKG_NAME): stock files missing in $(STOCK_FILES)," \
		     "run tools/cb0401/extract-stock-firmware.sh" >&2; \
		exit 1; }
endef

define Package/xiaomi-cb0401v2-wifi-firmware/install-overlay
	$(INSTALL_DIR) $(1)/lib/firmware/ath11k/IPQ5018/hw1.0
	$(INSTALL_DATA) $(STOCK_FILES)/IPQ5018/* $(1)/lib/firmware/ath11k/IPQ5018/hw1.0/
	$(INSTALL_DIR) $(1)/lib/firmware/ath11k/QCN9074/hw1.0
	$(INSTALL_DATA) $(STOCK_FILES)/QCN9074/board-2.bin $(1)/lib/firmware/ath11k/QCN9074/hw1.0/
endef

$(eval $(call BuildPackage,xiaomi-cb0401v2-wifi-firmware))
