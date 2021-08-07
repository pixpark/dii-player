# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.cpprg/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################
# Ucc Jni
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE    := dii_media_kit
LOCAL_SRC_FILES := ./jni_util/classreferenceholder.cc \
		./jni_util/jni_helpers.cc \
		./jni_util/jni_onload.cc \
		./jni_util/native_handle_impl.cc \
		./androidmediaencoder_jni.cc \
		./androidvideocapturer_jni.cc \
		./surfacetexturehelper_jni.cc \
		./japp_jni.cc \
		./video_render.cc \
	
LOCAL_LDLIBS := -llog -lz -lOpenSLES
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)  
LOCAL_LDLIBS += -L$(call host-path,$(LOCAL_PATH)/../../third_party/ffmpeg/lib/android/armv7-a) -lavformat -lavcodec -lavutil -lswresample -lswscale
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)  
LOCAL_LDLIBS += -L$(call host-path,$(LOCAL_PATH)/../../third_party/ffmpeg/lib/android/armv8-a) -lavformat -lavcodec -lavutil -lswresample -lswscale
else ifeq ($(TARGET_ARCH_ABI),x86)  
LOCAL_LDLIBS += -L$(call host-path,$(LOCAL_PATH)/../../third_party/ffmpeg/lib/android/x86) -lavformat -lavcodec -lavutil -lswresample -lswscale
else ifeq ($(TARGET_ARCH_ABI),x86_64)  
LOCAL_LDLIBS += -L$(call host-path,$(LOCAL_PATH)/../../third_party/ffmpeg/lib/android/x86-64) -lavformat -lavcodec -lavutil -lswresample -lswscale
else
LOCAL_LDLIBS += -L$(call host-path,$(LOCAL_PATH)/../../third_party/ffmpeg/lib/android/armv7-a) -lavformat -lavcodec -lavutil -lswresample -lswscale
endif  	

LOCAL_C_INCLUDES += $(NDK_STL_INC) \
					$(LOCAL_PATH)/../../ \
					$(LOCAL_PATH)/../../dii_player \
					$(LOCAL_PATH)/../../video_renderer \
					$(LOCAL_PATH)/../../third_party/ffmpeg/include \
					$(LOCAL_PATH)/avstreamer \
					$(LOCAL_PATH)/jni_util \
					
LOCAL_CPPFLAGS := -std=gnu++11 -DWEBRTC_POSIX -DWEBRTC_ANDROID -D__STDC_CONSTANT_MACROS
 
LOCAL_STATIC_LIBRARIES := dii_media_player
LOCAL_STATIC_LIBRARIES += webrtc
LOCAL_STATIC_LIBRARIES += yuv_static
LOCAL_STATIC_LIBRARIES += faac
LOCAL_STATIC_LIBRARIES += faad2
LOCAL_STATIC_LIBRARIES += SoundTouch
include $(BUILD_SHARED_LIBRARY)