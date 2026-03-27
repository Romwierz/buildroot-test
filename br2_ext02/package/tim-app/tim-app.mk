################################################################################
#
# TIM-application
#
################################################################################

TIM_APP_VERSION = 1.0
TIM_APP_SITE    = $(BR2_EXTERNAL_TIMER_PATH)/src/tim-pci
TIM_APP_SITE_METHOD = local
TIM_APP_LICENSE = LGPLv2.1/GPLv2 

define TIM_APP_BUILD_CMDS
$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) demo -C $(@D)
endef
define TIM_APP_INSTALL_TARGET_CMDS
   $(INSTALL) -D -m 0755 $(@D)/user_tim1 $(TARGET_DIR)/usr/bin
endef
$(eval $(generic-package))
