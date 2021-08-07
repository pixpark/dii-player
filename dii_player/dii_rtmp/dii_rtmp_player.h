/*
*  Copyright (c) 2016 The rtmp_live_kit project authors. All Rights Reserved.
*
*  Please visit https://https://github.com/PixPark/DiiPlayer for detail.
*
* The GNU General Public License is a free, copyleft license for
* software and other kinds of works.
*
* The licenses for most software and other practical works are designed
* to take away your freedom to share and change the works.  By contrast,
* the GNU General Public License is intended to guarantee your freedom to
* share and change all versions of a program--to make sure it remains free
* software for all its users.  We, the Free Software Foundation, use the
* GNU General Public License for most of our software; it applies also to
* any other work released this way by its authors.  You can apply it to
* your programs, too.
* See the GNU LICENSE file for more info.
*/
#ifndef __DII_RTMP_PLAYER_H__
#define __DII_RTMP_PLAYER_H__

#include "dii_play_base.h"
#include "dii_common.h"
#include "dii_media_utils.h"
#include "dii_rtmp_puller.h"
#include "dii_rtmp_decoder.h"

#include "webrtc/base/messagehandler.h"
#include "webrtc/api/mediastreaminterface.h"

namespace dii_media_kit {
class DiiRtmplayer :  public DiiPlayBase,
                        public DiiPullerCallback,
                        public dii_rtc::Thread,
                        public dii_rtc::MessageHandler {
public:
	DiiRtmplayer(int32_t stream_id);
	~DiiRtmplayer(void);
	int32_t Start(const char* url, int64_t pos = 0, bool pause = false) override;
    int32_t StopPlay() override;
    int32_t SetLoop(bool loop) override {return 0;};
    int32_t GetMoreAudioData(void *stream, size_t sample_rate, size_t channel) override;
    int32_t SetCallback(DiiMediaBaseCallback callback) override;
    void DoStatistics(DiiPlayerStatistics& statistics) override;
    
    int32_t Pause() override {return 0;};
    int32_t Resume() override {return 0;};
    int32_t Seek(int64_t pos) override {return 0;};
    int64_t Position() override {return 0;};
    int64_t Duration() override {return 0;};
                        
protected:
	void OnServerConnected() override;
    void OnPullFailed(int32_t errCode, int32_t eventid, const char * errmsg) override;
	void OnPullVideoData(const uint8_t*pdata, int len, uint32_t ts) override;
	void OnPullAudioData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts) override;
private:
    //* For MessageHandler
    virtual void OnMessage(dii_rtc::Message* msg) override;
private:
    std::mutex mtx_;
    bool running_ = false;
    int32_t stream_id_  = -1;
    
    DiiMediaBaseCallback      callback_;
	DiiRtmpPuller*            rtmp_puller_ = nullptr;
	DiiRtmpDecoder*           av_decoder_ = nullptr;
                            
	std::string			url_;
    uint64_t            previous_sync_ts_ = 0;
    int32_t             retry_cnt_ = 0;
                            
    bool need_callback_ = true;
                            
    dii_radar::DiiRole _role;
    char * _userid;
    bool _report;
};

}	// namespace dii_media_kit

#endif	// __DII_RTMP_PLAYER_H__
