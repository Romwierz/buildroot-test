################################################################################
#
# WZFIR-module
#
################################################################################

WZFIR_MODULE_VERSION = 1.0
WZFIR_MODULE_SITE    = $(BR2_EXTERNAL_MODULES_PATH)/src/wzfir
WZFIR_MODULE_SITE_METHOD = local
WZFIR_MODULE_LICENSE = LGPLv2.1/GPLv2 

$(eval $(kernel-module))
$(eval $(generic-package))
