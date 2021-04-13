LOCAL_PATH:= $(call my-dir)

# Record app
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	ozoencapp.cpp

LOCAL_SHARED_LIBRARIES := \
	libmediaplayerservice \
	libstagefright \
	liblog \
	libutils \
	libbinder \
	libstagefright_foundation \
	libmedia \
	libgui \
	libcutils \
	libui \
	libc

LOCAL_C_INCLUDES:= \
	frameworks/av/camera/include \
	frameworks/av/media/libaudiohal/include \
	frameworks/av/media/libstagefright \
	frameworks/av/media/libmediametrics/include \
	frameworks/av/media/libmediaplayerservice \
	$(TOP)/system/media/audio/include/system \
	$(TOP)/frameworks/native/include/media/hardware

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= ozoencapp

LOCAL_32_BIT_ONLY := true

include $(BUILD_EXECUTABLE)

# Effects app
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	ozoeffectapp.cpp

LOCAL_SHARED_LIBRARIES := \
	libmediaplayerservice \
	liblog \
	libutils \
	libbinder \
	libcutils \
	libui \
	libc \
	libaudiohal \
	libstagefright_foundation \
	libaudioutils

LOCAL_C_INCLUDES:= \
	frameworks/av/media/libaudiohal/include \
	frameworks/av/media/libmediametrics/include \
	frameworks/av/media/libmediaplayerservice \
	$(TOP)/system/media/audio/include/system \
	$(TOP)/frameworks/native/include/media/hardware \
	$(TOP)/frameworks/av/include \
	system/media/audio_utils/include \
	frameworks/av/media/libozoaudio/include \
	frameworks/av/media/libozoaudio \
	frameworks/av/media/libeffects

LOCAL_CFLAGS += -Wno-multichar

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= ozoeffectsapp
LOCAL_32_BIT_ONLY := true

include $(BUILD_EXECUTABLE)
