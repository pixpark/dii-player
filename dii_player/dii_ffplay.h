//
//  ffplay_main.hpp
//  ffplay_test
//
//  Created by devzhaoyou on 2019/10/23.
//  Copyright © 2019 devzhaoyou. All rights reserved.
//

#ifndef dii_media_kit_DII_FFPLAY
#define dii_media_kit_DII_FFPLAY

#include "dii_play_base.h"
#include <mutex>

//#define CHECK_FFPLAY(ptr)  if(!ptr)  return -1;

enum WorkStat {
	WORK_NONE,
	WORK_OK,
	WORK_FINISH
};
namespace dii_media_kit  {
    class DiiFFPlayer : public DiiPlayBase {
    public:
        DiiFFPlayer(int32_t stream_id);
        ~DiiFFPlayer();
        int32_t Start(const char* url, int64_t pos = 0, bool pause = false) override;
        int32_t Pause() override;
        int32_t Resume() override;
        int32_t StopPlay() override;
        int32_t SetLoop(bool loop) override;
        int32_t Seek(int64_t pos) override;
        int64_t Position() override;
        int64_t Duration() override;
        int32_t GetMoreAudioData(void *stream, size_t sample_rate, size_t channel) override;
        int32_t SetCallback(DiiMediaBaseCallback callback) override;
        void DoStatistics(DiiPlayerStatistics& statistics) override;
    private:
        std::mutex mtx_;
        void* dii_ffplayer_ = nullptr;
        DiiMediaBaseCallback callback_;
        int32_t stream_id_ = -1;
        
        dii_radar::DiiRole _role;
        char * _userid;
        bool _report;
    };
}
#endif /* dii_media_kit_DII_FFPLAY */
