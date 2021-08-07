LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE:= libSoundTouch
LOCAL_MODULE_TAGS := optional
				 

LOCAL_SRC_FILES := \
	AAFilter.cpp \
	BPMDetect.cpp \
	FIFOSampleBuffer.cpp \
	FIRFilter.cpp \
	InterpolateCubic.cpp \
	InterpolateLinear.cpp \
	InterpolateShannon.cpp \
	PeakFinder.cpp \
	RateTransposer.cpp \
	SoundTouch.cpp \
	TDStretch.cpp \
	cpu_detect_x86.cpp \
	mmx_optimized.cpp \
	sse_optimized.cpp \

# for native audio
# LOCAL_SHARED_LIBRARIES += -lgcc 
# --whole-archive -lgcc 
# for logging
LOCAL_LDLIBS    += -llog
# for native asset manager
#LOCAL_LDLIBS    += -landroid

# Custom Flags: 
# -fvisibility=hidden : don't export all symbols
LOCAL_CPPFLAGS += -std=gnu++11 -frtti -fvisibility=hidden -fdata-sections -ffunction-sections

# OpenMP mode : enable these flags to enable using OpenMP for parallel computation 
#LOCAL_CFLAGS += -fopenmp
#LOCAL_LDFLAGS += -fopenmp


# Use ARM instruction set instead of Thumb for improved calculation performance in ARM CPUs	
LOCAL_ARM_MODE := arm

include $(BUILD_STATIC_LIBRARY)


