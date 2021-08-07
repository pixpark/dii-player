/*
 *  Copyright (c) 2011 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/system_wrappers/include/cpu_info.h"

#include "webrtc/base/systeminfo.h"

namespace dii_media_kit {

uint32_t CpuInfo::DetectNumberOfCores() {
  return static_cast<uint32_t>(dii_rtc::SystemInfo::GetMaxCpus());
}

}  // namespace dii_media_kit
