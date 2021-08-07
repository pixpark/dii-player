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
#include "dii_com_def.h"
#include "dii_rtmp_buffer.h"
#include "webrtc/base/logging.h"

#include <cmath>
#include <cstdlib>

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMAX3(a,b,c) FFMAX(FFMAX(a,b),c)
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FFMIN3(a,b,c) FFMIN(FFMIN(a,b),c)

#define AV_SYNC_THRESHOLD_MIN               40
#define AV_SYNC_THRESHOLD_MAX               100
#define AV_SYNC_FRAMEDUP_THRESHOLD          100
#define MAX_FRAME_DURATION                  3600

#define BUFFERING_TIME_LEN              50         // lower than PLY_MIN_LIMIT_CACHE_TIME buffering
#define AUDIO_PACKET_TIME_LEN           10         // 10 ms   
#define VIDEO_PACKET_TIME_LEN           66         // 40 ms
#define BUFFERING_INTERVAL_LEN          1000

DiiRtmpBuffer::DiiRtmpBuffer(int32_t stream_id, PlyBufferCallback&callback)
	: callback_(callback)
	, got_audio_(false)
    , cache_time_len_(0)
	, first_pkt_real_ts_(0)
	, first_rtmp_pkt_ts_(0)
	, rtmp_cache_time_(0)
	, sync_clock_(0)
    , pcm_packets_count_(0) {
        this->stream_id_ = stream_id;
        processing_ = true;
        dii_rtc::Thread::Start();
}

DiiRtmpBuffer::~DiiRtmpBuffer()
{
    processing_ = false;
    dii_rtc::Thread::Stop();
    this->ClearCache();
}

int DiiRtmpBuffer::GetMorePcmData(void *audioSamples, size_t samplesPerSec, size_t nChannels, uint64_t &sync_ts) {
    int ret = 0;
    PlyPacket* pkt_front = nullptr;
    {
        dii_rtc::CritScope cs(&a_mtx_);
        if (!audio_pcm_queue_.empty()) {
            pkt_front = audio_pcm_queue_.front();
            audio_pcm_queue_.pop();
        }
    }
    
    if(pkt_front) {
        ret = pkt_front->_data_len;
        sync_clock_ = pkt_front->_pts;
        sync_ts = pkt_front->_sync_ts;
        int16_t res_out[3840];
        int samples_out = audio_resampler_.Resample10Msec((int16_t*)pkt_front->_data,
                                                                   src_sample_rate_*src_channel_count_,
                                                                   samplesPerSec*nChannels,
                                                                   1,
                                                                   3840,
                                                                   (int16_t*)res_out);
        
        memcpy(audioSamples, res_out, samples_out*2);
        delete pkt_front;
    }
	return ret;
}

void DiiRtmpBuffer::CacheH264Frame(PlyPacket* pkt, int type) {
    got_video_ = true;
    
    if (first_pkt_real_ts_ == 0) {
        first_pkt_real_ts_ = dii_rtc::TimeMillis();
        first_rtmp_pkt_ts_ = pkt->_pts;
    }

    dii_rtc::CritScope cs(&v_mtx_);
    int32_t size = (int32_t)h264_frame_queue_.size();
    if(size > 500) {
        DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "H264 sync queue too large, have " << size << "+ frames";
    }
       
    h264_frame_queue_.push(pkt);
    if(!got_audio_) {
        cache_time_len_ = size * VIDEO_PACKET_TIME_LEN;
    }
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Add h264 data to cache, buffer size: " << size;;
}

void DiiRtmpBuffer::CachePcmData(const uint8_t* pdata, int len, int sample_rate, int channel_cnt, uint32_t ts, uint64_t sync_ts) {
    if (first_pkt_real_ts_ == 0) {
        first_pkt_real_ts_ = dii_rtc::Time();
        first_rtmp_pkt_ts_     = ts;
    }
    
    src_sample_rate_ =  sample_rate;
    src_channel_count_ = channel_cnt;
    
    got_audio_ = true;
    
    // push packet
	PlyPacket* pkt = new PlyPacket(false);
	pkt->SetData(pdata, len, ts, sync_ts);
   
	dii_rtc::CritScope cs(&a_mtx_);
	audio_pcm_queue_.push(pkt);
    int32_t size = (int32_t)audio_pcm_queue_.size();
    cache_time_len_ = size * AUDIO_PACKET_TIME_LEN;
    if (cache_time_len_ <= BUFFERING_TIME_LEN && buffer_state_ != Buffering) {
        buffer_state_ = Buffering;
        caton_cnt_++;
        if(caton_cnt_ >= 2) {
            if(buffer_ready_len_ < 5000) {
                buffer_ready_len_ += 1000;
            }
            caton_cnt_ = 0;
        }
    }
    
    if (cache_time_len_ >= buffer_ready_len_ && buffer_state_ != BufferReady) {
        buffer_state_ = BufferReady;
    }
    
    if(!got_video_ && cache_time_len_ > 15*1000) {
        DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "audio pc queue too large, len: " << cache_time_len_;
    }
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Add pcm data to cache, length: " << len
                    << "ts: " << ts
                    << "audio queue size: " << size;;

}

void DiiRtmpBuffer::ClearCache() {
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "DiiRtmpBuffer: clear play buffer.";
    // clear audio queue
    {
        dii_rtc::CritScope cs(&a_mtx_);
        while (!audio_pcm_queue_.empty()) {
           auto it = audio_pcm_queue_.front();
           delete it;
           audio_pcm_queue_.pop();
        }
    }
    
    // clear video queue
    {
        dii_rtc::CritScope cs(&v_mtx_);
        while (!h264_frame_queue_.empty()) {
            auto it = h264_frame_queue_.front();
            h264_frame_queue_.pop();
            delete it;
        }
    }
}

void DiiRtmpBuffer::Run() {
    while(processing_) {
        dii_rtc::Thread::SleepMs(5);
        DoSyncAudioVideo();
    }
}

// audio and video sync
void DiiRtmpBuffer::DoSyncAudioVideo()
{
    if (first_pkt_real_ts_ == 0 || buffer_state_ != BufferReady) {
		return;
    }
    
    PlyPacket* pkt = NULL;
    {
        dii_rtc::CritScope cs(&v_mtx_);
        if (!h264_frame_queue_.empty()) {
            pkt = h264_frame_queue_.front();
            
            int64_t dt = pkt->_pts - sync_clock_;
            if(dt <= 0) {
                callback_.OnNeedDecodeFrame(pkt);
                h264_frame_queue_.pop();
            } else if(std::abs(dt) >= 4000) { //防止异常跳变的时间戳, 但对于连续跳变的时间戳，此逻辑无效
                Thread::SleepMs(66);
                callback_.OnNeedDecodeFrame(pkt);
                h264_frame_queue_.pop();
            }
        }
    }
}
