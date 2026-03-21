MY_APP_VERSION = 1.0
MY_APP_SITE = $(BR2_EXTERNAL_TEST_PATH)/src/my-app
MY_APP_SITE_METHOD = local

define MY_APP_BUILD_CMDS
$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) all -C $(@D)
endef
define MY_APP_INSTALL_TARGET_CMDS 
   $(INSTALL) -D -m 0755 $(@D)/my-app $(TARGET_DIR)/usr/bin/my-app
endef

$(eval $(generic-package))
