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
#include "dii_rtmp_player.h"
#include "dii_audio_manager.h"
#include "srs_librtmp.h"
#include "dii_media_utils.h"
#include "webrtc/base/logging.h"
#include "webrtc/media/base/videoframe.h"

#define DII_MSG_REPULL      1000

namespace dii_media_kit {
DiiRtmplayer::DiiRtmplayer(int32_t stream_id) {
    this->stream_id_ = stream_id;
    
    _role = dii_radar::_Role_Unknown;
    _userid = NULL;
    _report = true;
    
    av_decoder_ = new DiiRtmpDecoder(stream_id, _report);
    rtmp_puller_ = new DiiRtmpPuller(stream_id, *this, _report);
    dii_rtc::Thread::Start();
}

DiiRtmplayer::~DiiRtmplayer(void)
{
    dii_rtc::Thread::Clear(this, DII_MSG_REPULL);
    dii_rtc::Thread::Stop();
    if (rtmp_puller_) {
        delete rtmp_puller_;
        rtmp_puller_ = NULL;
    }
    if (av_decoder_) {
        delete av_decoder_;
        av_decoder_ = NULL;
    }
    
    if(_userid){
        free(_userid);
        _userid = NULL;
    }
}

void DiiRtmplayer::OnMessage(dii_rtc::Message* msg) {
    switch (msg->message_id) {
        case DII_MSG_REPULL: {
            std::unique_lock<std::mutex> lck(mtx_);
            if (running_ && rtmp_puller_) {
                rtmp_puller_->Shutdown();
                rtmp_puller_->StartPull(url_, _report);
            }
            break;
        } default: {
            break;
        }
    }
}

int32_t DiiRtmplayer::Start(const char* url, int64_t pos, bool pause) {
    std::unique_lock<std::mutex> lck(mtx_);
    DII_LOG(LS_INFO, stream_id_, 2002001) << "DiiRtmplayer Start play rtmp url: " << url << ", stream id:" << stream_id_;
    if(running_)
        return 0;
    running_ = true;
    url_ = url;

    this->av_decoder_->Start(_report);
    this->rtmp_puller_->StartPull(url_, _report);
    return 0;
}
int32_t DiiRtmplayer::StopPlay() {
    std::unique_lock<std::mutex> lck(mtx_);
    if(!running_)
        return 0;
    DII_LOG(LS_INFO, stream_id_, 2002002) << "DiiRtmplayer Stop play rtmp, stream id: " << stream_id_;
    
    running_ = false;
    if (rtmp_puller_) {
        rtmp_puller_->Shutdown();
    }

    if (av_decoder_) {
        av_decoder_->Shutdown();
    }
    callback_.state_callback_(DII_STATE_STOPPED, 0, "stop");
    return 0;
}

int32_t DiiRtmplayer::GetMoreAudioData(void *stream, size_t sample_rate, size_t channel) {
    if (av_decoder_) {
        uint64_t sync_ts = 0;
        int ret = av_decoder_->GetMorePcmData(stream, sample_rate, channel, sync_ts);
        if( sync_ts > 0) {
            if(sync_ts != previous_sync_ts_) {
                previous_sync_ts_ = sync_ts;
                callback_.rtmp_sync_time_callback_(sync_ts);
            }
        }
        
        if(ret > 0) {
              if(need_callback_) {
                  callback_.state_callback_(DII_STATE_PLAYING, 0, "playing");
                  need_callback_ = false;
              }
        }
        
        return ret;
    }
    return 0;
}

int32_t DiiRtmplayer::SetCallback(DiiMediaBaseCallback callback) {
    callback_ = callback;
    av_decoder_->SetVideoFrameCallback(callback_.video_frame_callback_);
    return 0;
}

void DiiRtmplayer::OnPullVideoData(const uint8_t*pdata, int len, uint32_t ts) {
	if (av_decoder_) {
        av_decoder_->CacheAvcData(pdata, len, ts);
	}
}

void DiiRtmplayer::OnPullAudioData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts) {
	if (av_decoder_) {
        av_decoder_->CacheAacData(pdata, len, ts, sync_ts);
	}
}
    
void DiiRtmplayer::DoStatistics(DiiPlayerStatistics& statistics) {
    if (av_decoder_) {
        av_decoder_->DoStatistics(statistics);
    }
}

void DiiRtmplayer::OnServerConnected() {
   // DII_LOG(LS_INFO, stream_id_, 2002000) << "rtmp server connected ok, url: " << url_;
    //callback_.state_callback_(DII_STATE_PLAYING, 0, "playing");
}

void DiiRtmplayer::OnPullFailed(int32_t errCode,int32_t eventid,const char * errmsg) {
    if(running_) {
        need_callback_ = true;

        dii_rtc::Thread::PostDelayed(RTC_FROM_HERE, 1000, this, DII_MSG_REPULL);
        retry_cnt_++;  
		DII_LOG(LS_ERROR, stream_id_, eventid) << "rtmp repull url:" << url_ << errmsg << " ,err code:" << errCode;
        if(retry_cnt_%3 != 0) {
            return;
        }
        callback_.state_callback_(DII_STATE_ERROR, errCode, "rtmp pull failed");//error
    }
}
} // namespace dii_media_kit
