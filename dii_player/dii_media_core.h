//
//  dii_player.hpp
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#ifndef DII_MEDIA_PLAYER_KIT_DII_MEDIA_CORE
#define DII_MEDIA_PLAYER_KIT_DII_MEDIA_CORE

#include "dii_common.h"
#include "dii_play_base.h"
#include "video_renderer.h"
#include "dii_audio_manager.h"
#include "dii_media_utils.h"

namespace dii_media_kit  {
    class DiiMediaCore : public dii_media_kit::DiiAudioTracker,
                           public dii_rtc::Thread,
                           public dii_rtc::MessageHandler {
    public:
        DiiMediaCore(void* render, bool outputPcmForExternalMix = false);
        ~DiiMediaCore();

        int32_t Start(int32_t stream_id, const char* url, int64_t pos = 0, bool pause = false);
        int32_t Pause();
        int32_t Resume();
        int32_t SetLoop(bool loop);
        int32_t StopPlay();
        int32_t Seek(int64_t pos);
        void SetMute(const bool mute);
        int64_t Position();
        int64_t Duration();

        int32_t SetPlayerCallback(DiiPlayerCallback* callback);
        int32_t ClearDisplayWithColor(int32_t width, int32_t height, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0);
        
        int32_t OnNeedPlayAudio(void* audioSamples, size_t samplesPerSec, size_t nChannels) override;
		// only support for windows
		static int32_t SetPlayoutVolume(uint32_t vol);
		static int32_t SetPlayoutDevice(const char* deviceId);
       
        //* For MessageHandler
        virtual void OnMessage(dii_rtc::Message* msg) override;

    private:
        void OnVideoFrame(dii_media_kit::VideoFrame& frame);
		void OnPlayerState(int state, int code, const char* msg);
        void DoStatistics();
        void OnStreamSyncTime(uint64_t ts);
  
        void StartAudioPlayout();
        void StopAudioPlayout();
        DiiPlayBase* CreatePlayer(std::string url);
        
        void LogSdkInfo();
                               
    private:
        bool real_stream_ = false;
        int32_t stream_id_;
        std::mutex mtx_;
        
        std::mutex set_video_size_mtx_;
        std::shared_ptr<DiiAudioManager> audio_manager_;
        std::string play_uri_ = "";
        int64_t play_pos_ = 0;
        
        bool started_ = false;
        bool paused_  = false;
        bool loop_    = false;
        bool mute_    = false;
		bool render_time_flg_ = false;
        
        DiiPlayBase* player_ = nullptr;
        dii_rtc::VideoSinkInterface<cricket::VideoFrame>*  video_render_ = nullptr;

		int scale_width_    = 0;
		int scale_height_   = 0;
		int orig_width_     = 0;
		int orig_height_    = 0;
        
		int64_t start_time_ = 0;
		int64_t end_time_ = 0;
        std::vector<uint8_t> src_rgba_frame_buf_;
        std::vector<uint8_t> dst_rgba_frame_buf_;
        std::vector<uint8_t> scale_rgba_frame_buf_;
        int32_t frame_width_     = 0;
        int32_t frame_height_    = 0;
		int64_t start_to_render_time_ = 0;

        DiiPlayerCallback callback_;
        DiiPlayerStatistics statistics_;
		DiiPlayerState player_cur_stat_ = DII_STATE_STOPPED;
        
		static uint32_t dev_volume_;
		static char dev_id_[128];
        
        int64_t last_play_audio_frame_ts_;
        int64_t last_render_video_frame_ts_;
        
        bool is_video_frame_coming_ = false;
                               
        bool _is_outputPcm_forMix;
                               
        dii_radar::DiiRole _role;
        char * _userid;
        bool _report;
    };

}


#endif /* DII_MEDIA_PLAYER_KIT_DII_MEDIA_CORE */
