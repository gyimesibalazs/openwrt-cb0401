#
# Xiaomi CB0401V2: drive the Quectel RG520N-EB over PCIe/MHI, as the stock
# firmware does. QMI runs on QRTR (node 3) and data on the raw-IP mhi_hwip0
# channel, through ModemManager's qcom-soc plugin.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=xiaomi-cb0401v2-modem
PKG_RELEASE:=2

include $(INCLUDE_DIR)/package.mk

define Package/xiaomi-cb0401v2-modem
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Xiaomi CB0401V2 PCIe 5G modem support
  DEPENDS:=@TARGET_qualcommax_ipq50xx +modemmanager +kmod-mhi-pci-generic \
	+kmod-mhi-net +kmod-qrtr-mhi +kmod-rmnet +ethtool
  PKGARCH:=all
endef

define Package/xiaomi-cb0401v2-modem/description
  udev rules that make ModemManager build the RG520N-EB from its QRTR node
  and MHI data netdev (and leave its USB side to the AT tools), and a boot
  service that rescans PCIe until the modem, switched on after the PCIe
  probe, shows up.
endef

define Build/Compile
endef

define Package/xiaomi-cb0401v2-modem/install
	$(INSTALL_DIR) $(1)/lib/udev/rules.d $(1)/etc/init.d
	$(INSTALL_DATA) ./files/78-mm-cb0401v2.rules $(1)/lib/udev/rules.d/
	$(INSTALL_BIN) ./files/cb0401v2-pcie-modem.init $(1)/etc/init.d/cb0401v2-pcie-modem
endef

$(eval $(call BuildPackage,xiaomi-cb0401v2-modem))
