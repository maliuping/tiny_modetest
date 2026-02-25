
# ifeq ($(BUILD_IM_HAL_G2D), y)

LOCAL_PATH    := $(call my-dir)

######## build im_vo_unit ########
include $(CLEAR_VARS)

LOCAL_TARGET  := im_vo_unit

LOCAL_SRCS	:= $(LOCAL_PATH)/src/im_vo_unit.c
LOCAL_SRCS	+= $(LOCAL_PATH)/src/im_vo_drm_display.c
LOCAL_SRCS	+= $(LOCAL_PATH)/src/im_vo_helper.c
LOCAL_SRCS	+= $(LOCAL_PATH)/src/im_vo_subsys_func_crc.c
LOCAL_SRCS	+= $(LOCAL_PATH)/src/im_vo_subsys_fun_polarity.c

LOCAL_CFLAGS += -I$(IM_SDK_BASE_DIR)/pkgs/third-party/libdrm/include/drm/ \
				-I$(IM_SDK_BASE_DIR)/pkgs/third-party/libdrm/ \
				-I$(LOCAL_PATH)/include

LOCAL_LIBS	:= libdrm.so \
				libimhal_mem.so \
				libimosal.so \
				libimhal_g2d.so \
				libimhal_fb.so \

EXTRA_CFLAGS += -Wno-unused
EXTRA_CFLAGS += -Werror -Wall

LOCAL_LDFLAGS  := -lpthread -lm -lz
LOCAL_LDFLAGS  += -lliblog

include $(BUILD_APP)

.PHONY: $(LOCAL_TARGET)

$(LOCAL_TARGET): $(LOCAL_MODULE)
	@mkdir -p $(SYSTEM_OUT_DIR)/bin
	@cp -dpRf $< $(SYSTEM_OUT_DIR)/bin
	@echo "Build $@ Done."

$(call add-target-into-build, $(LOCAL_TARGET))
######## done im_vo_unit ########

# endif
