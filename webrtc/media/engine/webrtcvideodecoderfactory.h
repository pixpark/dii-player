/*
 *  Copyright (c) 2013 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MEDIA_ENGINE_WEBRTCVIDEODECODERFACTORY_H_
#define WEBRTC_MEDIA_ENGINE_WEBRTCVIDEODECODERFACTORY_H_

#include "webrtc/base/refcount.h"
#include "webrtc/common_types.h"

namespace dii_media_kit {
class VideoDecoder;
}

namespace cricket {

struct VideoDecoderParams {
  std::string receive_stream_id;
};

class WebRtcVideoDecoderFactory {
 public:
  // Caller takes the ownership of the returned object and it should be released
  // by calling DestroyVideoDecoder().
  virtual dii_media_kit::VideoDecoder* CreateVideoDecoder(
      dii_media_kit::VideoCodecType type) = 0;
  virtual dii_media_kit::VideoDecoder* CreateVideoDecoderWithParams(
      dii_media_kit::VideoCodecType type,
      VideoDecoderParams params) {
    return CreateVideoDecoder(type);
  }
  virtual ~WebRtcVideoDecoderFactory() {}

  virtual void DestroyVideoDecoder(dii_media_kit::VideoDecoder* decoder) = 0;
};

}  // namespace cricket

#endif  // WEBRTC_MEDIA_ENGINE_WEBRTCVIDEODECODERFACTORY_H_
