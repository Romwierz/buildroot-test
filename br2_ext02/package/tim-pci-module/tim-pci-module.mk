################################################################################
#
# TIM_PCI-module
#
################################################################################

TIM_PCI_MODULE_VERSION = 1.0
TIM_PCI_MODULE_SITE    = $(BR2_EXTERNAL_TIMER_PATH)/src/tim-pci
TIM_PCI_MODULE_SITE_METHOD = local
TIM_PCI_MODULE_LICENSE = LGPLv2.1/GPLv2 

$(eval $(kernel-module))
$(eval $(generic-package))
