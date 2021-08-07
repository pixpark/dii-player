/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "webrtc/modules/video_coding/codecs/h264/h264_video_toolbox_encoder.h"

#if defined(WEBRTC_VIDEO_TOOLBOX_SUPPORTED)

#include <memory>
#include <string>
#include <vector>

#if defined(WEBRTC_IOS)
#include "RTCUIApplication.h"
#endif
#include "libyuv/convert_from.h"
#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/modules/video_coding/codecs/h264/h264_video_toolbox_nalu.h"
#include "webrtc/system_wrappers/include/clock.h"

namespace eclive_internal {

// Convenience function for creating a dictionary.
inline CFDictionaryRef CreateCFDictionary(CFTypeRef* keys,
                                          CFTypeRef* values,
                                          size_t size) {
  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, size,
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

// Copies characters from a CFStringRef into a std::string.
std::string CFStringToString(const CFStringRef cf_string) {
  RTC_DCHECK(cf_string);
  std::string std_string;
  // Get the size needed for UTF8 plus terminating character.
  size_t buffer_size =
      CFStringGetMaximumSizeForEncoding(CFStringGetLength(cf_string),
                                        kCFStringEncodingUTF8) +
      1;
  std::unique_ptr<char[]> buffer(new char[buffer_size]);
  if (CFStringGetCString(cf_string, buffer.get(), buffer_size,
                         kCFStringEncodingUTF8)) {
    // Copy over the characters.
    std_string.assign(buffer.get());
  }
  return std_string;
}

// Convenience function for setting a VT property.
void SetVTSessionProperty(VTSessionRef session,
                          CFStringRef key,
                          int32_t value) {
  CFNumberRef cfNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  OSStatus status = VTSessionSetProperty(session, key, cfNum);
  CFRelease(cfNum);
  if (status != noErr) {
    std::string key_string = CFStringToString(key);
    LOG(LS_ERROR) << "VTSessionSetProperty failed to set: " << key_string
                  << " to " << value << ": " << status;
  }
}

// Convenience function for setting a VT property.
void SetVTSessionProperty(VTSessionRef session,
                          CFStringRef key,
                          uint32_t value) {
  int64_t value_64 = value;
  CFNumberRef cfNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value_64);
  OSStatus status = VTSessionSetProperty(session, key, cfNum);
  CFRelease(cfNum);
  if (status != noErr) {
    std::string key_string = CFStringToString(key);
    LOG(LS_ERROR) << "VTSessionSetProperty failed to set: " << key_string
                  << " to " << value << ": " << status;
  }
}

// Convenience function for setting a VT property.
void SetVTSessionProperty(VTSessionRef session, CFStringRef key, bool value) {
  CFBooleanRef cf_bool = (value) ? kCFBooleanTrue : kCFBooleanFalse;
  OSStatus status = VTSessionSetProperty(session, key, cf_bool);
  if (status != noErr) {
    std::string key_string = CFStringToString(key);
    LOG(LS_ERROR) << "VTSessionSetProperty failed to set: " << key_string
                  << " to " << value << ": " << status;
  }
}

// Convenience function for setting a VT property.
void SetVTSessionProperty(VTSessionRef session,
                          CFStringRef key,
                          CFStringRef value) {
  OSStatus status = VTSessionSetProperty(session, key, value);
  if (status != noErr) {
    std::string key_string = CFStringToString(key);
    std::string val_string = CFStringToString(value);
    LOG(LS_ERROR) << "VTSessionSetProperty failed to set: " << key_string
                  << " to " << val_string << ": " << status;
  }
}

// Struct that we pass to the encoder per frame to encode. We receive it again
// in the encoder callback.
struct FrameEncodeParams {
  FrameEncodeParams(dii_media_kit::ECLiveH264VideoToolboxEncoder* e,
                    const dii_media_kit::CodecSpecificInfo* csi,
                    int32_t w,
                    int32_t h,
                    int64_t rtms,
                    uint32_t ts,
                    dii_media_kit::VideoRotation r)
      : encoder(e),
        width(w),
        height(h),
        render_time_ms(rtms),
        timestamp(ts),
        rotation(r) {
    if (csi) {
      codec_specific_info = *csi;
    } else {
      codec_specific_info.codecType = dii_media_kit::kVideoCodecH264;
    }
  }

  dii_media_kit::ECLiveH264VideoToolboxEncoder* encoder;
  dii_media_kit::CodecSpecificInfo codec_specific_info;
  int32_t width;
  int32_t height;
  int64_t render_time_ms;
  uint32_t timestamp;
  dii_media_kit::VideoRotation rotation;
};

// We receive I420Frames as input, but we need to feed CVPixelBuffers into the
// encoder. This performs the copy and format conversion.
// TODO(tkchin): See if encoder will accept i420 frames and compare performance.
bool CopyVideoFrameToPixelBuffer(
    const dii_rtc::scoped_refptr<dii_media_kit::VideoFrameBuffer>& frame,
    CVPixelBufferRef pixel_buffer) {
  RTC_DCHECK(pixel_buffer);
  RTC_DCHECK(CVPixelBufferGetPixelFormatType(pixel_buffer) ==
             kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);
  RTC_DCHECK(CVPixelBufferGetHeightOfPlane(pixel_buffer, 0) ==
             static_cast<size_t>(frame->height()));
  RTC_DCHECK(CVPixelBufferGetWidthOfPlane(pixel_buffer, 0) ==
             static_cast<size_t>(frame->width()));

  CVReturn cvRet = CVPixelBufferLockBaseAddress(pixel_buffer, 0);
  if (cvRet != kCVReturnSuccess) {
    LOG(LS_ERROR) << "Failed to lock base address: " << cvRet;
    return false;
  }
  uint8_t* dst_y = reinterpret_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 0));
  int dst_stride_y = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 0);
  uint8_t* dst_uv = reinterpret_cast<uint8_t*>(
      CVPixelBufferGetBaseAddressOfPlane(pixel_buffer, 1));
  int dst_stride_uv = CVPixelBufferGetBytesPerRowOfPlane(pixel_buffer, 1);
  // Convert I420 to NV12.
  int ret = dii_libyuv::I420ToNV12(
      frame->DataY(), frame->StrideY(),
      frame->DataU(), frame->StrideU(),
      frame->DataV(), frame->StrideV(),
      dst_y, dst_stride_y, dst_uv, dst_stride_uv,
      frame->width(), frame->height());
  CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);
  if (ret) {
    LOG(LS_ERROR) << "Error converting I420 VideoFrame to NV12 :" << ret;
    return false;
  }
  return true;
}

// This is the callback function that VideoToolbox calls when encode is
// complete. From inspection this happens on its own queue.
void VTCompressionOutputCallback(void* encoder,
                                 void* params,
                                 OSStatus status,
                                 VTEncodeInfoFlags info_flags,
                                 CMSampleBufferRef sample_buffer) {
  std::unique_ptr<FrameEncodeParams> encode_params(
      reinterpret_cast<FrameEncodeParams*>(params));
  encode_params->encoder->OnEncodedFrame(
      status, info_flags, sample_buffer, encode_params->codec_specific_info,
      encode_params->width, encode_params->height,
      encode_params->render_time_ms, encode_params->timestamp,
      encode_params->rotation);
}

}  // namespace internal

namespace dii_media_kit {

// .5 is set as a mininum to prevent overcompensating for large temporary
// overshoots. We don't want to degrade video quality too badly.
// .95 is set to prevent oscillations. When a lower bitrate is set on the
// encoder than previously set, its output seems to have a brief period of
// drastically reduced bitrate, so we want to avoid that. In steady state
// conditions, 0.95 seems to give us better overall bitrate over long periods
// of time.
ECLiveH264VideoToolboxEncoder::ECLiveH264VideoToolboxEncoder()
    : callback_(nullptr),
      compression_session_(nullptr),
      bitrate_adjuster_(Clock::GetRealTimeClock(), .5, .95) , fps_(20){}

ECLiveH264VideoToolboxEncoder::~ECLiveH264VideoToolboxEncoder() {
  DestroyCompressionSession();
}

int ECLiveH264VideoToolboxEncoder::InitEncode(const VideoCodec* codec_settings,
                                        int number_of_cores,
                                        size_t max_payload_size) {
  RTC_DCHECK(codec_settings);
  RTC_DCHECK_EQ(codec_settings->codecType, kVideoCodecH264);
  {
    dii_rtc::CritScope lock(&quality_scaler_crit_);
    quality_scaler_.Init(QualityScaler::kLowH264QpThreshold,
                         QualityScaler::kBadH264QpThreshold,
                         codec_settings->startBitrate, codec_settings->width,
                         codec_settings->height, codec_settings->maxFramerate);
    QualityScaler::Resolution res = quality_scaler_.GetScaledResolution();
    // TODO(tkchin): We may need to enforce width/height dimension restrictions
    // to match what the encoder supports.
    width_ = res.width;
    height_ = res.height;
  }
  // We can only set average bitrate on the HW encoder.
  target_bitrate_bps_ = codec_settings->startBitrate * 1000;
  fps_ = codec_settings->maxFramerate;
  bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);

  // TODO(tkchin): Try setting payload size via
  // kVTCompressionPropertyKey_MaxH264SliceBytes.

  return ResetCompressionSession();
}

dii_rtc::scoped_refptr<VideoFrameBuffer>
ECLiveH264VideoToolboxEncoder::GetScaledBufferOnEncode(
    const dii_rtc::scoped_refptr<VideoFrameBuffer>& frame) {
  dii_rtc::CritScope lock(&quality_scaler_crit_);
  quality_scaler_.OnEncodeFrame(frame->width(), frame->height());
  return quality_scaler_.GetScaledBuffer(frame);
}

int ECLiveH264VideoToolboxEncoder::Encode(
    const VideoFrame& frame,
    const CodecSpecificInfo* codec_specific_info,
    const std::vector<FrameType>* frame_types) {
  RTC_DCHECK(!frame.IsZeroSize());
  if (!callback_ || !compression_session_) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
#if defined(WEBRTC_IOS)
  if (!RTCIsUIApplicationActive()) {
    // Ignore all encode requests when app isn't active. In this state, the
    // hardware encoder has been invalidated by the OS.
    return WEBRTC_VIDEO_CODEC_OK;
  }
#endif
  bool is_keyframe_required = false;
  dii_rtc::scoped_refptr<VideoFrameBuffer> input_image(
      GetScaledBufferOnEncode(frame.video_frame_buffer()));

  if (input_image->width() != width_ || input_image->height() != height_) {
    width_ = input_image->width();
    height_ = input_image->height();
    int ret = ResetCompressionSession();
    if (ret < 0)
      return ret;
  }

  // Get a pixel buffer from the pool and copy frame data over.
  CVPixelBufferPoolRef pixel_buffer_pool =
      VTCompressionSessionGetPixelBufferPool(compression_session_);
#if defined(WEBRTC_IOS)
  if (!pixel_buffer_pool) {
    // Kind of a hack. On backgrounding, the compression session seems to get
    // invalidated, which causes this pool call to fail when the application
    // is foregrounded and frames are being sent for encoding again.
    // Resetting the session when this happens fixes the issue.
    // In addition we request a keyframe so video can recover quickly.
    ResetCompressionSession();
    pixel_buffer_pool =
        VTCompressionSessionGetPixelBufferPool(compression_session_);
    is_keyframe_required = true;
  }
#endif
  if (!pixel_buffer_pool) {
    LOG(LS_ERROR) << "Failed to get pixel buffer pool.";
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  CVPixelBufferRef pixel_buffer = nullptr;
  CVReturn ret = CVPixelBufferPoolCreatePixelBuffer(nullptr, pixel_buffer_pool,
                                                    &pixel_buffer);
  if (ret != kCVReturnSuccess) {
    LOG(LS_ERROR) << "Failed to create pixel buffer: " << ret;
    // We probably want to drop frames here, since failure probably means
    // that the pool is empty.
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  RTC_DCHECK(pixel_buffer);
  if (!eclive_internal::CopyVideoFrameToPixelBuffer(input_image, pixel_buffer)) {
    LOG(LS_ERROR) << "Failed to copy frame data.";
    CVBufferRelease(pixel_buffer);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  // Check if we need a keyframe.
  if (!is_keyframe_required && frame_types) {
    for (auto frame_type : *frame_types) {
      if (frame_type == kVideoFrameKey) {
        is_keyframe_required = true;
        break;
      }
    }
  }

  CMTime presentation_time_stamp =
      CMTimeMake(frame.render_time_ms(), 1000);
  CFDictionaryRef frame_properties = nullptr;
  if (is_keyframe_required) {
    CFTypeRef keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    CFTypeRef values[] = {kCFBooleanTrue};
    frame_properties = eclive_internal::CreateCFDictionary(keys, values, 1);
  }
  std::unique_ptr<eclive_internal::FrameEncodeParams> encode_params;
  encode_params.reset(new eclive_internal::FrameEncodeParams(
      this, codec_specific_info, width_, height_, frame.render_time_ms(),
      frame.timestamp(), frame.rotation()));

  // Update the bitrate if needed.
  SetBitrateBps(bitrate_adjuster_.GetAdjustedBitrateBps());

  OSStatus status = VTCompressionSessionEncodeFrame(
      compression_session_, pixel_buffer, presentation_time_stamp,
      kCMTimeInvalid, frame_properties, encode_params.release(), nullptr);
  if (frame_properties) {
    CFRelease(frame_properties);
  }
  if (pixel_buffer) {
    CVBufferRelease(pixel_buffer);
  }
  if (status != noErr) {
    LOG(LS_ERROR) << "Failed to encode frame with code: " << status;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

int ECLiveH264VideoToolboxEncoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

void ECLiveH264VideoToolboxEncoder::OnDroppedFrame() {
  dii_rtc::CritScope lock(&quality_scaler_crit_);
  quality_scaler_.ReportDroppedFrame();
}

int ECLiveH264VideoToolboxEncoder::SetChannelParameters(uint32_t packet_loss,
                                                  int64_t rtt) {
  // Encoder doesn't know anything about packet loss or rtt so just return.
  return WEBRTC_VIDEO_CODEC_OK;
}

int ECLiveH264VideoToolboxEncoder::SetRates(uint32_t new_bitrate_kbit,
                                      uint32_t frame_rate) {
  target_bitrate_bps_ = 1000 * new_bitrate_kbit;
  fps_ = frame_rate;
  bitrate_adjuster_.SetTargetBitrateBps(target_bitrate_bps_);
  SetBitrateBps(bitrate_adjuster_.GetAdjustedBitrateBps());

  dii_rtc::CritScope lock(&quality_scaler_crit_);
  quality_scaler_.ReportFramerate(frame_rate);

  return WEBRTC_VIDEO_CODEC_OK;
}

int ECLiveH264VideoToolboxEncoder::Release() {
  // Need to reset so that the session is invalidated and won't use the
  // callback anymore. Do not remove callback until the session is invalidated
  // since async encoder callbacks can occur until invalidation.
  int ret = ResetCompressionSession();
  callback_ = nullptr;
  return ret;
}

int ECLiveH264VideoToolboxEncoder::ResetCompressionSession() {
  DestroyCompressionSession();

  // Set source image buffer attributes. These attributes will be present on
  // buffers retrieved from the encoder's pixel buffer pool.
  const size_t attributes_size = 3;
  CFTypeRef keys[attributes_size] = {
#if defined(WEBRTC_IOS)
    kCVPixelBufferOpenGLESCompatibilityKey,
#elif defined(WEBRTC_MAC)
    kCVPixelBufferOpenGLCompatibilityKey,
#endif
    kCVPixelBufferIOSurfacePropertiesKey,
    kCVPixelBufferPixelFormatTypeKey
  };
  CFDictionaryRef io_surface_value =
      eclive_internal::CreateCFDictionary(nullptr, nullptr, 0);
  int64_t nv12type = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
  CFNumberRef pixel_format =
      CFNumberCreate(nullptr, kCFNumberLongType, &nv12type);
  CFTypeRef values[attributes_size] = {kCFBooleanTrue, io_surface_value,
                                       pixel_format};
  CFDictionaryRef source_attributes =
      eclive_internal::CreateCFDictionary(keys, values, attributes_size);
  if (io_surface_value) {
    CFRelease(io_surface_value);
    io_surface_value = nullptr;
  }
  if (pixel_format) {
    CFRelease(pixel_format);
    pixel_format = nullptr;
  }
  OSStatus status = VTCompressionSessionCreate(
      nullptr,  // use default allocator
      width_, height_, kCMVideoCodecType_H264,
      nullptr,  // use default encoder
      source_attributes,
      nullptr,  // use default compressed data allocator
      eclive_internal::VTCompressionOutputCallback, this, &compression_session_);
  if (source_attributes) {
    CFRelease(source_attributes);
    source_attributes = nullptr;
  }
  if (status != noErr) {
    LOG(LS_ERROR) << "Failed to create compression session: " << status;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }
  ConfigureCompressionSession();
  return WEBRTC_VIDEO_CODEC_OK;
}

void ECLiveH264VideoToolboxEncoder::ConfigureCompressionSession() {
  RTC_DCHECK(compression_session_);
  eclive_internal::SetVTSessionProperty(compression_session_,
                                 kVTCompressionPropertyKey_RealTime, true);
  eclive_internal::SetVTSessionProperty(compression_session_,
                                 kVTCompressionPropertyKey_ProfileLevel,
                                 kVTProfileLevel_H264_Baseline_AutoLevel);
  eclive_internal::SetVTSessionProperty(compression_session_,
                                 kVTCompressionPropertyKey_AllowFrameReordering,
                                 false);
  eclive_internal::SetVTSessionProperty(compression_session_, kVTCompressionPropertyKey_ExpectedFrameRate, fps_);    //@Eric - add for 20 fps
  SetEncoderBitrateBps(target_bitrate_bps_);
  // TODO(tkchin): Look at entropy mode and colorspace matrices.
  // TODO(tkchin): Investigate to see if there's any way to make this work.
  // May need it to interop with Android. Currently this call just fails.
  // On inspecting encoder output on iOS8, this value is set to 6.
  // internal::SetVTSessionProperty(compression_session_,
  //     kVTCompressionPropertyKey_MaxFrameDelayCount,
  //     1);
  // Set a relatively large value for keyframe emission (7200 frames or
  // 4 minutes).
   eclive_internal::SetVTSessionProperty(
       compression_session_,
       kVTCompressionPropertyKey_MaxKeyFrameInterval, fps_*3);
  eclive_internal::SetVTSessionProperty(
      compression_session_,
      kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, 3);
}

void ECLiveH264VideoToolboxEncoder::DestroyCompressionSession() {
  if (compression_session_) {
    VTCompressionSessionInvalidate(compression_session_);
    CFRelease(compression_session_);
    compression_session_ = nullptr;
  }
}

const char* ECLiveH264VideoToolboxEncoder::ImplementationName() const {
  return "VideoToolbox";
}

void ECLiveH264VideoToolboxEncoder::SetBitrateBps(uint32_t bitrate_bps) {
  if (encoder_bitrate_bps_ != bitrate_bps) {
    SetEncoderBitrateBps(bitrate_bps);
  }
}

void ECLiveH264VideoToolboxEncoder::SetEncoderBitrateBps(uint32_t bitrate_bps) {
  if (compression_session_) {
    eclive_internal::SetVTSessionProperty(compression_session_,
                                   kVTCompressionPropertyKey_AverageBitRate,
                                   bitrate_bps);

    // TODO(tkchin): Add a helper method to set array value.
    int64_t bytes_per_second_value = bitrate_bps / 8;
    CFNumberRef bytes_per_second =
        CFNumberCreate(kCFAllocatorDefault,
                       kCFNumberSInt64Type,
                       &bytes_per_second_value);
    int64_t one_second_value = 1;
    CFNumberRef one_second =
        CFNumberCreate(kCFAllocatorDefault,
                       kCFNumberSInt64Type,
                       &one_second_value);
    const void* nums[2] = { bytes_per_second, one_second };
    CFArrayRef data_rate_limits =
        CFArrayCreate(nullptr, nums, 2, &kCFTypeArrayCallBacks);
    OSStatus status =
        VTSessionSetProperty(compression_session_,
                             kVTCompressionPropertyKey_DataRateLimits,
                             data_rate_limits);
    if (bytes_per_second) {
      CFRelease(bytes_per_second);
    }
    if (one_second) {
      CFRelease(one_second);
    }
    if (data_rate_limits) {
      CFRelease(data_rate_limits);
    }
    if (status != noErr) {
      LOG(LS_ERROR) << "Failed to set data rate limit";
    }

    encoder_bitrate_bps_ = bitrate_bps;
  }
}

void ECLiveH264VideoToolboxEncoder::OnEncodedFrame(
    OSStatus status,
    VTEncodeInfoFlags info_flags,
    CMSampleBufferRef sample_buffer,
    CodecSpecificInfo codec_specific_info,
    int32_t width,
    int32_t height,
    int64_t render_time_ms,
    uint32_t timestamp,
    VideoRotation rotation) {
  if (status != noErr) {
    LOG(LS_ERROR) << "H264 encode failed.";
    return;
  }
  if (info_flags & kVTEncodeInfo_FrameDropped) {
    LOG(LS_INFO) << "H264 encode dropped frame.";
    dii_rtc::CritScope lock(&quality_scaler_crit_);
    quality_scaler_.ReportDroppedFrame();
    return;
  }

  bool is_keyframe = false;
  CFArrayRef attachments =
      CMSampleBufferGetSampleAttachmentsArray(sample_buffer, 0);
  if (attachments != nullptr && CFArrayGetCount(attachments)) {
    CFDictionaryRef attachment =
        static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    is_keyframe =
        !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
  }

  if (is_keyframe) {
    LOG(LS_INFO) << "Generated keyframe";
  }

  // Convert the sample buffer into a buffer suitable for RTP packetization.
  // TODO(tkchin): Allocate buffers through a pool.
  std::unique_ptr<dii_rtc::Buffer> buffer(new dii_rtc::Buffer());
  std::unique_ptr<dii_media_kit::RTPFragmentationHeader> header;
  {
    dii_media_kit::RTPFragmentationHeader* header_raw;
    bool result = H264CMSampleBufferToAnnexBBuffer(sample_buffer, is_keyframe,
                                                   buffer.get(), &header_raw);
    header.reset(header_raw);
    if (!result) {
      return;
    }
  }
  dii_media_kit::EncodedImage frame(buffer->data(), buffer->size(), buffer->size());
  frame._encodedWidth = width;
  frame._encodedHeight = height;
  frame._completeFrame = true;
  frame._frameType =
      is_keyframe ? dii_media_kit::kVideoFrameKey : dii_media_kit::kVideoFrameDelta;
  frame.capture_time_ms_ = render_time_ms;
  frame._timeStamp = timestamp;
  frame.rotation_ = rotation;

  h264_bitstream_parser_.ParseBitstream(buffer->data(), buffer->size());
  int qp;
  if (h264_bitstream_parser_.GetLastSliceQp(&qp)) {
    dii_rtc::CritScope lock(&quality_scaler_crit_);
    quality_scaler_.ReportQP(qp);
  }

  int result = callback_->Encoded(frame, &codec_specific_info, header.get());
  if (result != 0) {
    LOG(LS_ERROR) << "Encode callback failed: " << result;
    return;
  }
  bitrate_adjuster_.Update(frame._size);
}

}  // namespace dii_media_kit

#endif  // defined(WEBRTC_VIDEO_TOOLBOX_SUPPORTED)
