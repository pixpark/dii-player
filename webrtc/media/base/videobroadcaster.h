/*
 *  Copyright (c) 2016 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MEDIA_BASE_VIDEOBROADCASTER_H_
#define WEBRTC_MEDIA_BASE_VIDEOBROADCASTER_H_

#include <memory>
#include <utility>
#include <vector>

#include "webrtc/base/criticalsection.h"
#include "webrtc/base/thread_checker.h"
#include "webrtc/media/base/videoframe.h"
#include "webrtc/media/base/videosinkinterface.h"
#include "webrtc/media/base/videosourcebase.h"
#include "webrtc/media/engine/webrtcvideoframe.h"

namespace dii_rtc {

// VideoBroadcaster broadcast video frames to sinks and combines
// VideoSinkWants from its sinks. It does that by implementing
// dii_rtc::VideoSourceInterface and dii_rtc::VideoSinkInterface.
// Sinks must be added and removed on one and only one thread.
// Video frames can be broadcasted on any thread. I.e VideoBroadcaster::OnFrame
// can be called on any thread.
class VideoBroadcaster : public VideoSourceBase,
                         public VideoSinkInterface<cricket::VideoFrame> {
 public:
  VideoBroadcaster();
  void AddOrUpdateSink(VideoSinkInterface<cricket::VideoFrame>* sink,
                       const VideoSinkWants& wants) override;
  void RemoveSink(VideoSinkInterface<cricket::VideoFrame>* sink) override;

  // Returns true if the next frame will be delivered to at least one sink.
  bool frame_wanted() const;

  // Returns VideoSinkWants a source is requested to fulfill. They are
  // aggregated by all VideoSinkWants from all sinks.
  VideoSinkWants wants() const;

  void OnFrame(const cricket::VideoFrame& frame) override;

 protected:
  void UpdateWants() EXCLUSIVE_LOCKS_REQUIRED(sinks_and_wants_lock_);
  const dii_rtc::scoped_refptr<dii_media_kit::VideoFrameBuffer>& GetBlackFrameBuffer(
      int width, int height)
      EXCLUSIVE_LOCKS_REQUIRED(sinks_and_wants_lock_);

  ThreadChecker thread_checker_;
  dii_rtc::CriticalSection sinks_and_wants_lock_;

  VideoSinkWants current_wants_ GUARDED_BY(sinks_and_wants_lock_);
  dii_rtc::scoped_refptr<dii_media_kit::VideoFrameBuffer> black_frame_buffer_;
};

}  // namespace dii_rtc

#endif  // WEBRTC_MEDIA_BASE_VIDEOBROADCASTER_H_
