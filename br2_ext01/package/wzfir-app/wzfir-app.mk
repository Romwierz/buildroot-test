################################################################################
#
# WZFIR-application
#
################################################################################

WZFIR_APP_VERSION = 1.0
WZFIR_APP_SITE    = $(BR2_EXTERNAL_MODULES_PATH)/src/wzfirapp
WZFIR_APP_SITE_METHOD = local
WZFIR_APP_LICENSE = LGPLv2.1/GPLv2 

define WZFIR_APP_BUILD_CMDS
$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) all -C $(@D)
endef
define WZFIR_APP_INSTALL_TARGET_CMDS
   $(INSTALL) -D -m 0755 $(@D)/wzfir-app $(TARGET_DIR)/usr/bin
endef
$(eval $(generic-package))
