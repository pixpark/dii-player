/*
 *  Copyright 2015 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "WebRTC/DiiRTCVideoFrame.h"

#include "webrtc/media/base/videoframe.h"

NS_ASSUME_NONNULL_BEGIN

@interface DiiRTCVideoFrame ()

@property(nonatomic, readonly)
    dii_rtc::scoped_refptr<dii_media_kit::VideoFrameBuffer> i420Buffer;

- (instancetype)initWithNativeFrame:(const cricket::VideoFrame *)nativeFrame
    NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
