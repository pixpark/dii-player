//
//  dii_play_base.h
//  DiiMediaCore
//
//  Created by devzhaoyou on 2019/11/20.
//  Copyright © 2019 pixpark. All rights reserved.
//

#ifndef dii_media_interface_h
#define dii_media_interface_h


#include "dii_common.h"
#include "webrtc/video_frame.h"

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace dii_media_kit  {
    typedef std::function<void (dii_media_kit::VideoFrame& frame)> VideoFrameCallback;
    typedef std::function<void (int state, int code, const char* msg)> StateCallback;
    typedef std::function<void (int buffer_len, int net_speed)> NetworkStatisticsCallback;
    typedef std::function<void (uint64_t ts)> RtmpSyncTimeCallback;

    typedef struct {
        VideoFrameCallback video_frame_callback_                = nullptr;
        StateCallback state_callback_                           = nullptr;
        RtmpSyncTimeCallback rtmp_sync_time_callback_           = nullptr;
    } DiiMediaBaseCallback;

    class DiiPlayBase {
    public:
        virtual ~DiiPlayBase() {};
        virtual int32_t Start(const char* url, int64_t pos = 0, bool pause = false) = 0;
        virtual int32_t Pause() = 0;
        virtual int32_t Resume() = 0;
        virtual int32_t StopPlay() = 0;
        virtual int32_t SetLoop(bool loop) = 0;
        virtual int32_t Seek(int64_t pos) = 0;

        virtual int64_t Position() = 0;
        virtual int64_t Duration() = 0;
        virtual int32_t GetMoreAudioData(void *stream, size_t sample_rate, size_t channel) = 0;
        virtual int32_t SetCallback(DiiMediaBaseCallback callback) = 0;
        virtual void DoStatistics(DiiPlayerStatistics& statistics) = 0;
    };
}
#endif /* dii_media_interface_h */
