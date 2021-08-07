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
ANYCORE := ./
include $(CLEAR_VARS)

LOCAL_MODULE    := dii_media_player
			
LOCAL_SRC_FILES := \
        $(LOCAL_PATH)/dii_ffplay.cc \
        $(LOCAL_PATH)/dii_log_manager.cc \
        $(LOCAL_PATH)/dii_media_core.cc \
        $(LOCAL_PATH)/dii_media_utils.cc \
        $(LOCAL_PATH)/dii_player.cc \
        $(LOCAL_PATH)/dii_audio_manager.cc \
        $(LOCAL_PATH)/dii_audio_mixer_io.cc \
        $(LOCAL_PATH)/dii_rtmp/dii_rtmp_player.cc \
        $(LOCAL_PATH)/dii_rtmp/dii_rtmp_puller.cc \
        $(LOCAL_PATH)/dii_rtmp/aacdecode.cc \
        $(LOCAL_PATH)/dii_rtmp/aacencode.cc \
        $(LOCAL_PATH)/dii_rtmp/dii_rtmp_buffer.cc \
        $(LOCAL_PATH)/dii_rtmp/dii_rtmp_decoder.cc \
        $(LOCAL_PATH)/dii_rtmp/avcodec.cc \
        $(LOCAL_PATH)/dii_rtmp/videofilter.cc \
        $(LOCAL_PATH)/../third_party/srs_librtmp/srs_librtmp.cpp \
	
LOCAL_CPPFLAGS := -std=gnu++11 -D__cplusplus=201103L -frtti -Wno-literal-suffix -DWEBRTC_POSIX -DWEBRTC_ANDROID -DWEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE -D__STDC_CONSTANT_MACROS
#

LOCAL_C_INCLUDES += $(NDK_STL_INC) \
		$(LOCAL_PATH)/../ \
		$(LOCAL_PATH)/../video_renderer \
		$(LOCAL_PATH)/dii_rtmp\
		$(LOCAL_PATH)/../third_party/srs_librtmp \
		$(LOCAL_PATH)/../third_party/ffmpeg/include \
		$(LOCAL_PATH)/../third_party/libyuv/include \
		$(LOCAL_PATH)/../third_party/faac/include \
		$(LOCAL_PATH)/../third_party/faad/include \
					
include $(BUILD_STATIC_LIBRARY)