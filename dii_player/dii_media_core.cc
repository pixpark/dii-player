//
//  dii_player.cpp
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#include "dii_media_core.h"
#include "dii_common.h"
#include "dii_ffplay.h"
#include "dii_rtmp/dii_rtmp_player.h"
#include "webrtc/video_frame.h"
#include "webrtc/media/engine/webrtcvideoframe.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "third_party/libyuv/include/libyuv.h"

#include <regex>

// dii message
#define DII_MSG_TICKTACK              8001
#define DII_MSG_FINISH                1000
#define DII_MSG_START                 1001
#define DII_MSG_PAUSE                 1002
#define DII_MSG_RESUME                1003
#define DII_MSG_STOP                  1004
#define DII_MSG_SEEK                  1005
#define DII_MSG_LOOP                  1006

namespace dii_media_kit  {
DiiMediaCore::DiiMediaCore(void* render, bool outputPcmForExternalMix) {
    _is_outputPcm_forMix = outputPcmForExternalMix;
    this->LogSdkInfo();
    if(render) {
        video_render_ = dii_media_kit::VideoRenderer::Create(render, 640, 480);
    }
    
    _role = dii_radar::_Role_Unknown;
    _userid = NULL;
    _report = true;
    
    dii_rtc::Thread::Start();
    dii_rtc::Thread::PostDelayed(RTC_FROM_HERE, 1000, this, DII_MSG_TICKTACK);
}

DiiMediaCore::~DiiMediaCore() {
    if(started_) {
        DII_LOG(LS_ERROR, stream_id_, 0) << "error, not call stop api befor dealloc class.";
        this->StopPlay();
    }
    /* wait all msg handle down.*/
    dii_rtc::Thread::Clear(this, DII_MSG_TICKTACK);
    
    dii_rtc::Thread::Stop();
	if (video_render_) {
		delete video_render_;
		video_render_ = nullptr;
	}
    
    if(_userid){
        free(_userid);
        _userid = NULL;
    }
}

void DiiMediaCore::StartAudioPlayout() {
    /** alloc audio device and start must in same thread.
     */
    if(!audio_manager_.get())
        audio_manager_ = DiiAudioManager::GetInstance();

#ifdef _WIN32
	if ('\0' != dev_id_[0]) {
		audio_manager_->SetPlayoutDevice(dev_id_);
	}
	if (255 != DiiMediaCore::dev_volume_) {
		//audio_manager_->SetSpeakerVolume(dev_volume_);//解决系统调整音量后再打开播放器音量自己改变的问题
	}
#endif
    
    if(! _is_outputPcm_forMix){
        audio_manager_->RegAudioTrack(this, real_stream_);
    }
}

void DiiMediaCore::StopAudioPlayout() {
	if (audio_manager_.get()) {
		audio_manager_->UnregAudioTrack(this);
	}
}

void DiiMediaCore::OnMessage(dii_rtc::Message* msg) {
    switch (msg->message_id) {
        case DII_MSG_START: {
            std::unique_lock<std::mutex> lck(mtx_);
            dii_rtc::TypedMessageData<std::string>* data =
            static_cast<dii_rtc::TypedMessageData<std::string>*>(msg->pdata);
            player_ = CreatePlayer(data->data().c_str());
            player_->Start(data->data().c_str(), this->play_pos_);
            this->StartAudioPlayout();
            break;
        } case DII_MSG_PAUSE : {
            if(player_)
                player_->Pause();
            this->StopAudioPlayout();
            break;
        } case DII_MSG_RESUME: {
            if(player_)
                player_->Resume();
            this->StartAudioPlayout();
            break;
        } case DII_MSG_LOOP: {
            if(player_)
                player_->SetLoop(loop_);
            break;
        } case DII_MSG_STOP: {
            this->StopAudioPlayout();
            std::unique_lock<std::mutex> lck(mtx_);
            if(player_) {
                player_->StopPlay();
                delete player_;
                player_ = nullptr;
            }
            break;
        } case DII_MSG_SEEK : {
            dii_rtc::TypedMessageData<int64_t>* data =
                static_cast<dii_rtc::TypedMessageData<int64_t>*>(msg->pdata);
            if(player_)
                player_->Seek(data->data());
            break;
        } case DII_MSG_FINISH: {
            // when stream finish, must stop audio playout, otherwise there will be some noise.
            this->StopAudioPlayout();
            break;
        } case DII_MSG_TICKTACK: {
            this->DoStatistics();
            break;
        } default: {
            break;
        }
   }
}

DiiPlayBase* DiiMediaCore::CreatePlayer(std::string url) {
    auto StrRegexMatch = [] (const std::string str, const std::string pattern) {
        std::regex re(pattern);
        return std::regex_search(str, re);
    };

    // check if video frame coming after 400ms.
    is_video_frame_coming_ = false;

    // create player.
    DiiPlayBase *player = nullptr;
    if(StrRegexMatch(url, "rtmp://.*")) {
        DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO)
        << "create player for with rtmp, this:" << this
        << ", url: " << url;
        
        real_stream_ = true;
        player = new DiiRtmplayer(stream_id_);
    } else {
        DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO)
        << "create player with ffplay, this:" << this
        << ", url: " << url;
        
        real_stream_ = false;
        player = new DiiFFPlayer(stream_id_);
    }
    DiiMediaBaseCallback callbacks;
    callbacks.state_callback_ = std::bind(&DiiMediaCore::OnPlayerState,
                                          this,
                                          std::placeholders::_1,
                                          std::placeholders::_2,
                                          std::placeholders::_3);
    
    callbacks.video_frame_callback_ = std::bind(&DiiMediaCore::OnVideoFrame,
                                                this,
                                                std::placeholders::_1);
    
    callbacks.rtmp_sync_time_callback_ = std::bind(&DiiMediaCore::OnStreamSyncTime,
                                                   this,
                                                   std::placeholders::_1);
    player->SetCallback(callbacks);
    return player;
}

int32_t DiiMediaCore::Start(int32_t stream_id, const char* url, int64_t pos, bool pause) {
    this->stream_id_ = stream_id;
    int ret = DII_DONE;
	if (!url || pos < 0) {
		return DII_PARAMETER_ERROR;
	}
    
    if(started_) {
        this->StopPlay();
    }
	start_to_render_time_ = 0;
	start_time_ = DiiUnixTimestampMs();
	render_time_flg_ = true;
    started_ = true;
    last_play_audio_frame_ts_ = DiiUnixTimestampMs();
    last_render_video_frame_ts_ = last_play_audio_frame_ts_;
    
    this->play_uri_ = url;
    this->play_pos_ = pos;
    
    // must reset some member var.
    frame_width_  = 0;
    frame_height_ = 0;
    
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_START, new dii_rtc::TypedMessageData<std::string>(url));
    
    // sometime, user need pause at one picture.
    pause ? ret = this->Pause() : ret = 0;
    return ret;
}

int32_t DiiMediaCore::Pause() {
    if(!started_ || paused_ || real_stream_) {
        return DII_ERROR;
    }
    paused_ = true;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_PAUSE);
    return 0;
}

int32_t DiiMediaCore::Resume() {
    if(!started_ || !paused_ || real_stream_) {
        return DII_ERROR;
    }
    paused_ = false;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_RESUME);
    return DII_DONE;
}

int32_t DiiMediaCore::SetLoop(bool loop) {
    if(!started_ || real_stream_) {
        return DII_ERROR;
    }
    loop_ = loop;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_LOOP);
    return DII_DONE;
}

int32_t DiiMediaCore::StopPlay() {
    if(!started_) {
        return DII_DONE;
    }
    
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_STOP);
    
    started_    = false;
    paused_     = false;
	return DII_DONE;
}

int32_t DiiMediaCore::Seek(int64_t pos) {
    if(!started_ || real_stream_) {
        return DII_ERROR;
    }
    
    int64_t dur = this->Duration();
    if(pos < 0 || pos > dur) {
        return DII_ERROR;
    }
    
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, DII_MSG_SEEK, new dii_rtc::TypedMessageData<int64_t>(pos));
    return DII_DONE;
}

void DiiMediaCore::SetMute(const bool mute) {
    mute_ = mute;
}

int64_t DiiMediaCore::Position() {
    std::unique_lock<std::mutex> lck(mtx_);
    if(!player_)
        return DII_ERROR;
    return player_->Position();
}

int64_t DiiMediaCore::Duration() {
    std::unique_lock<std::mutex> lck(mtx_);
    if(!player_)
        return DII_ERROR;
    return player_->Duration();
}

int DiiMediaCore::OnNeedPlayAudio(void* audioSamples, size_t samplesPerSec, size_t nChannels) {
    std::unique_lock<std::mutex> lck(mtx_);
    if(!player_) {
        return 0;
    }
    
    last_play_audio_frame_ts_ = DiiUnixTimestampMs();
    
    int len =  player_->GetMoreAudioData(audioSamples, samplesPerSec, nChannels);
    if(mute_) {
        memset(audioSamples, 0, samplesPerSec / 100 * sizeof(int16_t) * nChannels);
    }
    
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "dii media core, need more audio: " << len << "this" << this;
    return len;
}

void DiiMediaCore::OnPlayerState(int state, int code, const char* msg) {
    player_cur_stat_ = (DiiPlayerState)state ;
    switch (player_cur_stat_) {
        case DII_STATE_PLAYING:
            DII_LOG(LS_INFO, stream_id_, 0)<< "stream state playing.";
            break;
        case DII_STATE_STOPPED:
            DII_LOG(LS_INFO, stream_id_, 0)<< "stream state stopped.";
            break;
        case DII_STATE_FINISH:
            DII_LOG(LS_INFO, stream_id_, 0)<< "stream state finished.";
			this->StopAudioPlayout();
            break;
        case DII_STATE_PAUSED:
            DII_LOG(LS_INFO, stream_id_, 0)<< "stream state pause.";
        case DII_STATE_ERROR: {
            frame_width_  = 0;
            frame_height_ = 0;
            break;
        } case DII_STATE_BUFFERING: {
            DII_LOG(LS_INFO, stream_id_, 0)<< "stream state buffering.";
            break;
        } default:
            break;
    }
    
	if (callback_.state_callback) {
		callback_.state_callback(player_cur_stat_, code, msg, callback_.custom_data);
	}
}

void DiiMediaCore::DoStatistics() {
    dii_rtc::Thread::PostDelayed(RTC_FROM_HERE, 1000, this, DII_MSG_TICKTACK);
    // realtime stream statistics
    if(player_ && real_stream_) {
        statistics_.stream_id = stream_id_;
        player_->DoStatistics(statistics_);
		statistics_.start_to_render_time_ = start_to_render_time_;
        DII_LOG(LS_INFO, stream_id_, 0)
                    << "dii player statistics"
                    << ", stream id: "              << statistics_.stream_id
                    << ", video render framerate: " << statistics_.video_render_framerate
                    << ", video decode framerate: " << statistics_.video_decode_framerate
                    << ", video width: "            << statistics_.video_width_
                    << ", video height: "           << statistics_.video_height_
                    << ", audio samplerate: "       << statistics_.audio_samplerate_
                    << ", play cache len: "         << statistics_.cache_len_
                    << ", audio bps: "              << statistics_.audio_bps_
                    << ", video bps: "              << statistics_.video_bps_ ;
        
        if(callback_.statistics_callback)
            callback_.statistics_callback(statistics_);
        
        if(_report){
            // callback to extern statistics callback
            dii_media_kit::DiiUtil::Instance()->ExternalStatisticsCallback(statistics_);
        }
        
        // if video stream and audio stream bps not 0, we think have video or audio;
        bool has_video = statistics_.video_bps_ > 0;
        bool has_audio = statistics_.audio_bps_ > 0;
        
        if(has_audio && DiiUnixTimestampMs() - last_play_audio_frame_ts_ > 10*1000) {
           last_play_audio_frame_ts_ = DiiUnixTimestampMs();
           DII_LOG(LS_ERROR, stream_id_, 2002015) << "no audio packet played for more than 10 seconds.";
        }
           
        if(has_video && DiiUnixTimestampMs() - last_render_video_frame_ts_  > 10*1000) {
           last_render_video_frame_ts_ = DiiUnixTimestampMs();
           DII_LOG(LS_ERROR, stream_id_, 2002016) << "no video frame render for more than 10 seconds.";
        }
    }
}

void DiiMediaCore::OnStreamSyncTime(uint64_t ts) {
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO)<< "dii player stream sync time: " << ts;;
    if(callback_.sync_ts_callback)
        callback_.sync_ts_callback(ts);
}

int32_t DiiMediaCore::ClearDisplayWithColor(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b) {
    if(!started_) {
        return DII_ERROR;
    }
    float Y = 0.299*r + 0.587*g + 0.114*b;
    float Cb = -0.1687*r - 0.3313*g + 0.5*b + 128;
    float Cr = 0.5*r - 0.4187*g - 0.0813*b + 128;
    
    // NOTE: must alloc heap, should't stack, stack small, heap larger
    int32_t size_Y = width*height;
    uint8_t* data[4] = {0};
    data[0] = (uint8_t*)(malloc(size_Y));
    memset(data[0], (int)Y, size_Y);
    data[1] = (uint8_t*)(malloc(size_Y/4));
    memset(data[1], (int)Cb, size_Y/4);
    data[2] = (uint8_t*)(malloc(size_Y/4));
    memset(data[2], (int)Cr, size_Y/4);

    int linesize[4] = {0};
    linesize[0] = width;
    linesize[1] = width/2;
    linesize[2] = width/2;
    dii_media_kit::VideoFrame frame;
    frame.CreateFrame(data[0],
                      data[1],
                      data[2],
                      width,
                      height,
                      linesize[0],
                      linesize[1],
                      linesize[2],
                      dii_media_kit::kVideoRotation_0);
    this->OnVideoFrame(frame);
    
    for(auto it : data) {
        if(it) free(it);
    }
    return DII_DONE;
}

void DiiMediaCore::OnVideoFrame(dii_media_kit::VideoFrame& frame) {
    is_video_frame_coming_ = true;
    last_render_video_frame_ts_ = DiiUnixTimestampMs();
	if (render_time_flg_) {
		end_time_ = DiiUnixTimestampMs();
		render_time_flg_ = false;
		start_to_render_time_ = static_cast<int64_t>(end_time_ - start_time_);
        
        if(real_stream_ && _report){
            dii_radar::DiiRadarCallback callback = dii_media_kit::DiiUtil::Instance()->GetRadarCallback();
            if (callback.RenderVideoCallback) {
                dii_radar::VideoFrameMatedata v_matedata;
                v_matedata.role = _role;
                if(_userid){
                    v_matedata.userid = std::string(_userid);
                }else{
                    v_matedata.userid = "";
                }
                v_matedata.width = frame.width();
                v_matedata.height = frame.height();
                v_matedata.streamid = "1000";
                v_matedata.render_time_flg = true;
                v_matedata.start_to_render_time = start_to_render_time_;
                v_matedata.rotation = frame.rotation();
                v_matedata.pts = 0;
                callback.RenderVideoCallback(v_matedata);  //2002018避免重复上报雷达数据，暂时先注释掉
            }
        }
	}
    if(callback_.resolution_callback) {
        if(frame_width_ != frame.width() || frame_height_ != frame.height()) {
            frame_width_ = frame.width();
            frame_height_ = frame.height();
            callback_.resolution_callback(frame_width_, frame_height_);
        }
    }
    
    if (callback_.video_frame_callback) {
        std::unique_lock<std::mutex> lck(set_video_size_mtx_);
        if(scale_width_ == 0 || scale_height_ == 0) {
            scale_width_ = frame.width();
            scale_height_ = frame.height();
        }
        
		if (src_rgba_frame_buf_.size() < frame.width()*frame.height()*4) {
            src_rgba_frame_buf_.resize(frame.width()*frame.height()*4);
        }
        
        if (dst_rgba_frame_buf_.size() < scale_width_ * scale_height_ * 4) {
            dst_rgba_frame_buf_.resize(scale_width_ * scale_height_ * 4);
        }
        
           
        //convert
        dii_media_kit::ConvertFromI420(frame, dii_media_kit::kABGR, 0, src_rgba_frame_buf_.data());

        //回调
        dii_media_kit::DiiVideoFrame dst;
        dst.type = dii_media_kit::DiiVideoFrameType::TYPE_RGBA32;
        dst.render_time_ms = frame.render_time_ms();
        dst.rotation = frame.rotation();
        dst.rgba_buffer = dst_rgba_frame_buf_.data();
        //FIXME: need mutex， if sacla_width or heigth change, up level may crash
        dst.rgba_buffer_len = scale_width_ * scale_height_ * 4;
        dst.width = scale_width_;
        dst.height = scale_height_;
        
        DII_LOG(LS_VERBOSE, stream_id_, 0) << "origin width:" <<  frame.width() << ", height:" << frame.height() << ", dst width:" << dst.width << ",dst height:" <<  dst.height << "buffer:" << dst.rgba_buffer;
        callback_.video_frame_callback(dst, callback_.custom_data);

        orig_width_ = frame.width();
        orig_height_ = frame.height();
    }
    
    // Do render video frame
    if (video_render_) {
        const cricket::WebRtcVideoFrame render_frame(frame.video_frame_buffer(), 0, frame.rotation());
        video_render_->OnFrame(render_frame);
    }
}

int32_t DiiMediaCore::SetPlayerCallback(DiiPlayerCallback* callback) {
    if(!callback) {
        return -1;
    }
	callback_ = *callback;
    return DII_DONE;
}

uint32_t DiiMediaCore::dev_volume_ = 255;
char DiiMediaCore::dev_id_[128] = {0};
int32_t DiiMediaCore::SetPlayoutVolume(uint32_t vol) {
	dev_volume_ = vol;
    DiiAudioManager::GetInstance()->SetSpeakerVolume(dev_volume_);
	return DII_DONE;
}

int32_t DiiMediaCore::SetPlayoutDevice(const char* deviceId) {
	if (nullptr == deviceId) {
		return DII_PARAMETER_ERROR;
	}
	strncpy(dev_id_, deviceId, 127);
    int ret = DiiAudioManager::GetInstance()->SetPlayoutDevice(deviceId);
	DII_LOG(LS_INFO, 0, DII_CODE_COMMON_INFO) << " SetPlayoutDevice ret " << ret;
	return DII_DONE;
}

void DiiMediaCore::LogSdkInfo() {
    LOG(LS_INFO) << "*** av stream start ***";
    LOG(LS_INFO) << "*** " << DII_MEDIA_KIT_VERSION << " ***";
}
}
