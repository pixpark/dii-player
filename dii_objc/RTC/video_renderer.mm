/*
 *  Copyright (c) 2013 The devzhaoyou@dii_media project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video_renderer.h"
#import "RTCVideoRendererAdapter+Private.h"
#import "RTCVideoFrame+Private.h"
// TODO(pbos): Android renderer

#include "webrtc/typedefs.h"
#include "webrtc/media/engine/webrtcvideoframe.h"

namespace dii_media_kit {

class NullRenderer : public VideoRenderer {
	void OnFrame(const cricket::VideoFrame& video_frame) override {}
};
    
class RenderWrapper : public VideoRenderer {
public: // Copy form VideoRendererAdapter.mm
    RenderWrapper(id<RTCVideoRenderer> render){
        video_renderer_ = render;
        size_ = CGSizeZero;
    };
    virtual ~RenderWrapper(void){};

    void OnFrame(const cricket::VideoFrame& nativeVideoFrame) override {
        DiiRTCVideoFrame *videoFrame = nil;
        // Rotation of native handles is unsupported right now. Convert to CPU
        // I420 buffer for rotation before calling the rotation method otherwise
        // it will hit a DCHECK.
        if (nativeVideoFrame.rotation() != dii_media_kit::kVideoRotation_0 &&
            nativeVideoFrame.video_frame_buffer()->native_handle()) {
            dii_rtc::scoped_refptr<dii_media_kit::VideoFrameBuffer> i420Buffer =
            nativeVideoFrame.video_frame_buffer()->NativeToI420Buffer();
            std::unique_ptr<cricket::VideoFrame> cpuFrame(
                                                          new cricket::WebRtcVideoFrame(i420Buffer,
                                                                                        nativeVideoFrame.rotation(),
                                                                                        nativeVideoFrame.timestamp_us()));
            const cricket::VideoFrame *rotatedFrame =
            cpuFrame->GetCopyWithRotationApplied();
            videoFrame = [[DiiRTCVideoFrame alloc] initWithNativeFrame:rotatedFrame];
        } else {
            const cricket::VideoFrame *rotatedFrame =
            nativeVideoFrame.GetCopyWithRotationApplied();
            videoFrame = [[DiiRTCVideoFrame alloc] initWithNativeFrame:rotatedFrame];
        }
        CGSize current_size = CGSizeMake(videoFrame.width, videoFrame.height);
        if (!CGSizeEqualToSize(size_, current_size)) {
            size_ = current_size;
            [video_renderer_ setSize:size_];
        }
        [video_renderer_ renderFrame:videoFrame];
    }
    
public:
    bool Init(size_t width, size_t height){return true;};
    void Resize(size_t width, size_t height){};
    void Destroy(){};
    
private:
    id<RTCVideoRenderer> video_renderer_;
    CGSize size_;
};
    
VideoRenderer* VideoRenderer::CreatePlatformRenderer(const void* hwnd,
                                     size_t width,
                                     size_t height)
{
    RenderWrapper* render = new RenderWrapper((__bridge id<RTCVideoRenderer>)hwnd);
    render->Init(width, height);
    return render;
}

VideoRenderer* VideoRenderer::Create(const void* hwnd,
                                     size_t width,
                                     size_t height) {
  VideoRenderer* renderer = CreatePlatformRenderer(hwnd, width, height);
  if (renderer != NULL) {
    // TODO(mflodman) Add a warning log.
    return renderer;
  }

  return new NullRenderer();
}
}  // namespace dii_media_kit
