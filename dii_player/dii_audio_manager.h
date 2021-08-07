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
#ifndef __DII_AUDIO_MANAGER_H__
#define __DII_AUDIO_MANAGER_H__

#include "dii_common.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/thread.h"
#include "webrtc/common_audio/ring_buffer.h"
#include "webrtc/media/engine/webrtcvideodecoderfactory.h"
#include "webrtc/media/engine/webrtcvideoencoderfactory.h"
#include "webrtc/modules/audio_coding/acm2/acm_resampler.h"
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"


#include "dii_audio_mixer_io.h"
#include "webrtc/modules/audio_mixer/audio_mixer_impl.h"
 
#include "webrtc/base/messagehandler.h"
 
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace dii_media_kit {

class DiiAudioTracker {
public:
    DiiAudioTracker(void){};
    virtual ~DiiAudioTracker(void){};

    virtual int32_t OnNeedPlayAudio(void* audioSamples, size_t samplesPerSec, size_t nChannels) = 0;
};

 
class DiiAudioManager : public dii_media_kit::AudioTransport,
                          public dii_rtc::Thread,
                          public dii_rtc::MessageHandler {

private:
    DiiAudioManager();
    static std::mutex ins_mtx_;
    static std::shared_ptr<DiiAudioManager> audio_manager_ins_;
    DiiAudioManager(const DiiAudioManager&);
    DiiAudioManager& operator= (const DiiAudioManager&);
                              
public:
    virtual ~DiiAudioManager();
    static std::shared_ptr<DiiAudioManager> GetInstance();
    static int32_t Release();
	void StartAudioRecord(DiiAudioRecorderCallback callback, int sampleHz, int channel);
	void StopAudioRecord();
    
    void StartPlay();
    void StopPlay();
	void RegAudioTrack(DiiAudioTracker* tracker, bool restart);
	void UnregAudioTrack(DiiAudioTracker* tracker);
    int32_t SetSpeakerVolume(uint32_t volume);
	int32_t SetPlayoutDevice(const char* deviceId);

    // Handles messages from posts.
    void OnMessage(dii_rtc::Message *msg) override;
	void SetExternalVideoEncoderFactory(cricket::WebRtcVideoEncoderFactory* factory);
	cricket::WebRtcVideoEncoderFactory* ExternalVideoEncoderFactory();
private:
    int32_t SwitchPlayoutDevice(std::string device_id);
    int32_t GetPlayoutDeviceIdex(std::string devid);
    void StopAudioDevice();
                              
protected:
	//* For dii_media_kit::AudioTransport
	virtual int32_t RecordedDataIsAvailable(const void* audioSamples,
                                            const size_t nSamples, 
                                            const size_t nBytesPerSample,
                                            const size_t nChannels,
                                            const uint32_t samplesPerSec,
                                            const uint32_t totalDelayMS,
                                            const int32_t clockDrift,
                                            const uint32_t currentMicLevel,
                                            const bool keyPressed,uint32_t& newMicLevel) override;

	virtual int32_t NeedMorePlayData(const size_t nSamples,
                                     const size_t nBytesPerSample,
                                     const size_t nChannels,
                                     const uint32_t samplesPerSec,
                                     void* audioSamples,
                                     size_t& nSamplesOut,
                                     int64_t* elapsed_time_ms,
                                     int64_t* ntp_time_ms,
									 int32_t delayMs = 0) override;

protected:
	dii_rtc::scoped_refptr<AudioDeviceModule> audio_device_ptr_;
	dii_rtc::scoped_ptr<cricket::WebRtcVideoEncoderFactory> video_encoder_factory_;
    
	//* For audio record
	dii_rtc::CriticalSection	cs_audio_record_;
	dii_media_kit::acm2::ACMResampler resampler_record_;
	DiiAudioRecorderCallback audio_record_callback_;
	int						audio_record_sample_hz_;
	int						audio_record_channels_;

    // audio device setting
    int32_t audio_vol_                   = -1;
    std::string audio_device_id_         = "";
    bool playing_ = false;
                              
    dii_rtc::scoped_refptr<AudioMixerImpl> mixer_ptr;
    int32_t                     audio_tracker_id_ = 0;
    std::mutex                  tracker_map_mtx_;
    std::map<DiiAudioTracker*, AudioMixer::Source*> mixer_tacker_map_;
    int                        audio_play_sample_hz_ = 48000;
    int                        audio_play_channels_ = 1;
                              
    dii_media_kit::acm2::ACMResampler resampler_playout_;
};

}	// namespace dii_media_kit

#endif	// __DII_AUDIO_MANAGER_H__
