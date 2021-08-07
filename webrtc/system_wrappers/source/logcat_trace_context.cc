/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/system_wrappers/include/logcat_trace_context.h"

#include <android/log.h>
#include <assert.h>

#include "webrtc/system_wrappers/include/logging.h"

namespace dii_media_kit {

static android_LogPriority AndroidLogPriorityFromWebRtcLogLevel(
    TraceLevel webrtc_level) {
  // NOTE: this mapping is somewhat arbitrary.  StateInfo and Info are mapped
  // to DEBUG because they are highly verbose in webrtc code (which is
  // unfortunate).
  switch (webrtc_level) {
    case dii_media_kit::kTraceStateInfo: return ANDROID_LOG_DEBUG;
    case dii_media_kit::kTraceWarning: return ANDROID_LOG_WARN;
    case dii_media_kit::kTraceError: return ANDROID_LOG_ERROR;
    case dii_media_kit::kTraceCritical: return ANDROID_LOG_FATAL;
    case dii_media_kit::kTraceApiCall: return ANDROID_LOG_VERBOSE;
    case dii_media_kit::kTraceModuleCall: return ANDROID_LOG_VERBOSE;
    case dii_media_kit::kTraceMemory: return ANDROID_LOG_VERBOSE;
    case dii_media_kit::kTraceTimer: return ANDROID_LOG_VERBOSE;
    case dii_media_kit::kTraceStream: return ANDROID_LOG_VERBOSE;
    case dii_media_kit::kTraceDebug: return ANDROID_LOG_DEBUG;
    case dii_media_kit::kTraceInfo: return ANDROID_LOG_DEBUG;
    case dii_media_kit::kTraceTerseInfo: return ANDROID_LOG_INFO;
    default:
      LOG(LS_ERROR) << "Unexpected log level" << webrtc_level;
      return ANDROID_LOG_FATAL;
  }
}

LogcatTraceContext::LogcatTraceContext() {
  dii_media_kit::Trace::CreateTrace();
  if (dii_media_kit::Trace::SetTraceCallback(this) != 0)
    assert(false);
}

LogcatTraceContext::~LogcatTraceContext() {
  if (dii_media_kit::Trace::SetTraceCallback(NULL) != 0)
    assert(false);
  dii_media_kit::Trace::ReturnTrace();
}

void LogcatTraceContext::Print(TraceLevel level,
                               const char* message,
                               int length) {
  __android_log_print(AndroidLogPriorityFromWebRtcLogLevel(level),
                      "WEBRTC", "%.*s", length, message);
}

}  // namespace dii_media_kit
