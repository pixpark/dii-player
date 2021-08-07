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
#ifndef __APOLLO_RTMP_PULL_H__
#define __APOLLO_RTMP_PULL_H__

#include "dii_common.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/thread.h"
#include "srs_kernel_codec.h"

enum RTMPLAYER_STATUS
{
	RS_PLY_Init,		
	RS_PLY_Handshaked,	
	RS_PLY_Connected,	
	RS_PLY_Played,		
	RS_PLY_Closed		
};

typedef struct DemuxData
{
	DemuxData(int size) : _data(NULL), _data_len(0), _data_size(size){
		_data = new char[_data_size];
	}
	virtual ~DemuxData(void){ delete[] _data; }
	void reset() {
		_data_len = 0;
	}
	int append(const char* pData, int len){
		if (_data_len + len > _data_size)
			return 0;
		memcpy(_data + _data_len, pData, len);
		_data_len += len;
		return len;
	}

	char*_data;
	int _data_len;
	int _data_size;
} DemuxData;

class DiiPullerCallback
{
public:
	DiiPullerCallback(void){};
	virtual ~DiiPullerCallback(void){};

	virtual void OnServerConnected() = 0;
	virtual void OnPullFailed(int32_t errCode, int32_t eventid, const char * errmsg) = 0;
	virtual void OnPullVideoData(const uint8_t*pdata, int len, uint32_t ts) = 0;
	virtual void OnPullAudioData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts) = 0;
};

class DiiRtmpPuller : public dii_rtc::Thread {
public:
	DiiRtmpPuller(int32_t stream_id, DiiPullerCallback&callback, bool report);
	virtual ~DiiRtmpPuller(void);
    void StartPull(const std::string& url, bool report);
    void Shutdown();
protected:
    //* For Thread
    virtual void Run() override;

	int32_t DoReadData();
	int GotVideoSample(uint32_t timestamp, SrsCodecSample *sample);
	int GotAudioSample(uint32_t timestamp, SrsCodecSample *sample, uint64_t sync_ts);
    void RescanVideoframe(const char*pdata, int len, uint32_t timestamp);

	void CallConnect();

private:
    int32_t stream_id_ = -1;
    
	DiiPullerCallback&	callback_;
	SrsAvcAacCodec*		srs_codec_;
	bool				running_;
	std::string			str_url_;
    
	RTMPLAYER_STATUS	rtmp_status_;
	void*				rtmp_;
	DemuxData*			audio_payload_;
	DemuxData*			video_payload_;
    uint64_t            metadata_sync_ts_ = 0;
    uint64_t            rtmp_metadata_packet_ts_ = 0;
    uint64_t            lastest_audio_ts_ = 0;
    uint32_t            pre_pkt_ts_ = 0;
    uint32_t            error_ts_pkt_count_ = 0;
    
    uint32_t   audio_bitrate_ = 0;
    uint32_t   video_bitrate_ = 0;
    dii_radar::DiiRole _role;
    char * _userId;
    bool _report;
};
#endif	// __APOLLO_RTMP_PULL_H__
