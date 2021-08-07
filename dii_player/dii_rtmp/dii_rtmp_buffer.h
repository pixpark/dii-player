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
#ifndef __PLAYER_BUFER_H__
#define __PLAYER_BUFER_H__

#include "webrtc/video_frame.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/modules/audio_coding/acm2/acm_resampler.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/thread.h"

#include <list>
#include <queue>
#include <stdint.h>


typedef struct PlyPacket {
	PlyPacket(bool isvideo) : _data(NULL), _data_len(0),
							  _b_video(isvideo), _pts(0), _sync_ts(0) {}

	virtual ~PlyPacket(void){
		if (_data)
			delete[] _data;
	}
	void SetData(const uint8_t*pdata, int len, uint32_t ts) {
		_pts = ts;
		if (len > 0 && pdata != NULL) {
			if (_data)
				delete[] _data;
			if (_b_video)
				_data = new uint8_t[len + 8];
			else
				_data = new uint8_t[len];
			memcpy(_data, pdata, len);
			_data_len = len;
		}
	}
    
    void SetData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts) {
        _pts = ts;
        _sync_ts = sync_ts;
        if (len > 0 && pdata != NULL) {
            if (_data)
                delete[] _data;
            if (_b_video)
                _data = new uint8_t[len + 8];
            else
                _data = new uint8_t[len];
            memcpy(_data, pdata, len);
            _data_len = len;
        }
    }
    
	uint8_t*_data;
	int _data_len;
	bool _b_video;
	uint32_t _pts;
    uint64_t _sync_ts;
} PlyPacket;

enum BufferState {
	Buffering = 0,
    BufferReady,
};

class PlyBufferCallback {
public:
	PlyBufferCallback(void){};
	virtual ~PlyBufferCallback(void){};
	virtual void OnNeedDecodeFrame(PlyPacket* pkt) = 0;
};

class DiiRtmpBuffer : public dii_rtc::Thread {
public:
	DiiRtmpBuffer(int32_t stream_id, PlyBufferCallback&callback);
	virtual ~DiiRtmpBuffer();
	int GetMorePcmData(void *audioSamples, size_t samplesPerSec, size_t nChannels, uint64_t &sync_ts);
    BufferState PlayerStatus(){return buffer_state_;};
	int32_t GetPlayCacheTime(){return cache_time_len_;};
    int32_t PlayReadyBufferLen() const { return buffer_ready_len_;};
	void CacheH264Frame(PlyPacket* pkt, int type); //dii_media_kit::VideoFrame* frame
	void CachePcmData(const uint8_t* pdata, int len, int sample_rate, int channel_cnt, uint32_t ts, uint64_t sync_ts);
    void ClearCache();
    
private:
    //* For Thread
    virtual void Run() override;
    
	void DoSyncAudioVideo();
    void InitSoundTouch(uint16_t sample_rate, uint8_t channel_count);
private:
    int32_t stream_id_ = 0;
    bool                    processing_ = false;
    
    dii_rtc::CriticalSection a_mtx_;
    dii_rtc::CriticalSection v_mtx_;
    
	PlyBufferCallback		&callback_;
    bool					got_audio_ = false;
    bool                    got_video_ = false;
	uint64_t			    cache_delta_ = 0;
	int32_t                	cache_time_len_ = 0;
	BufferState				buffer_state_ = Buffering;
	int64_t				    first_pkt_real_ts_ = 0;
	int64_t				    first_rtmp_pkt_ts_ = 0;
	int64_t				    rtmp_cache_time_ = 0;
	int64_t                 sync_clock_ = 0;

	std::queue<PlyPacket*>	audio_pcm_queue_;

    std::queue<PlyPacket*>          h264_frame_queue_;
    
    int32_t buffer_ready_len_ = 300;
    int32_t caton_cnt_ = 0;
    
    
    int32_t                 pcm_packets_count_ = 0;
    uint32_t                pre_pkt_ts_ = 0;
    bool                    cmpt_render_dely_ = true;
    
    // resampler
    dii_media_kit::acm2::ACMResampler audio_resampler_;
    int32_t src_sample_rate_ = 44100;
    int32_t src_channel_count_ = 1;
};

#endif	// __PLAYER_BUFER_H__
