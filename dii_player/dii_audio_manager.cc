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
#include "dii_audio_manager.h"
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/base/logging.h"
#ifdef WIN32
#include "webrtc/base/win32socketserver.h"
#else
#include <unistd.h>
#endif

#define AUDIO_MSG_START_PLAY     3000
#define AUDIO_MSG_STOP_PLAY      3001
#define AUDIO_MSG_VOL	         3002
#define AUDIO_MSG_DEVICE         3003
#define AUDIO_MSG_START_REC      3004
#define AUDIO_MSG_STOP_REC       3005

static const size_t kMaxDataSizeSamples = 3840;
namespace dii_media_kit {
std::shared_ptr<DiiAudioManager> DiiAudioManager::audio_manager_ins_ = nullptr;
std::mutex DiiAudioManager::ins_mtx_;
std::shared_ptr<DiiAudioManager> DiiAudioManager::GetInstance() {
    if (audio_manager_ins_.get() == nullptr) {
        ins_mtx_.lock();
        if (audio_manager_ins_.get() == nullptr) {
            audio_manager_ins_.reset(new DiiAudioManager());
        }
        ins_mtx_.unlock();
    }
    return audio_manager_ins_;
}


int32_t DiiAudioManager::Release() {
    if(audio_manager_ins_.get()) {
        audio_manager_ins_->StopAudioDevice();
    }
    audio_manager_ins_.reset();
    audio_manager_ins_ = nullptr;
    return 0;
}

DiiAudioManager::DiiAudioManager()
    : audio_device_ptr_(NULL)
	, audio_record_callback_(NULL)
	, audio_record_sample_hz_(44100)
    , audio_record_channels_(2) {
    dii_rtc::Thread::Start();
}

DiiAudioManager::~DiiAudioManager() {
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_PLAY);
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_REC);
    dii_rtc::Thread::Stop();
    
    std::unique_lock<std::mutex> lck(tracker_map_mtx_);
    auto it = mixer_tacker_map_.begin();
    for(; it!= mixer_tacker_map_.end(); ) {
        mixer_ptr->RemoveSource(it->second);
        delete it->second;
        mixer_tacker_map_.erase(it++);
    }
    mixer_ptr.release();
}

void DiiAudioManager::OnMessage(dii_rtc::Message *msg) {
    switch (msg->message_id) {
        case AUDIO_MSG_START_PLAY:
            this->StartPlay();
            break;
        case AUDIO_MSG_STOP_PLAY :
            this->StopPlay();
            break;
		case AUDIO_MSG_VOL:
            if (audio_device_ptr_) {
                audio_device_ptr_->SetSpeakerVolume(audio_vol_);
            }
			break;
        case AUDIO_MSG_DEVICE:
           this->SwitchPlayoutDevice(audio_device_id_);
            break;
        case AUDIO_MSG_START_REC:
            if(audio_device_ptr_.get() == nullptr) {
                audio_device_ptr_ = AudioDeviceModule::Create(0, AudioDeviceModule::kPlatformDefaultAudio);
                audio_device_ptr_->Init();
                audio_device_ptr_->AddRef();
                audio_device_ptr_->RegisterAudioCallback(this);
            }
            
            if (!audio_device_ptr_->Recording()) {
                audio_device_ptr_->InitRecording();
                audio_device_ptr_->StartRecording();
            }
            break;
        case AUDIO_MSG_STOP_REC:
            if (audio_device_ptr_.get() && audio_device_ptr_->Recording()) {
                audio_device_ptr_->StopRecording();
            }
            break;
        default:
            break;
   }
}

void DiiAudioManager::StartPlay() {
    if(playing_) {
        return;
    }
    playing_ = true;
#ifdef WEBRTC_ANDROID
    audio_device_ptr_ = AudioDeviceModule::Create(0, AudioDeviceModule::kAndroidJavaAudio);
#else
    audio_device_ptr_ = AudioDeviceModule::Create(0, AudioDeviceModule::kPlatformDefaultAudio);
#endif

    
    audio_device_ptr_->Init();
    audio_device_ptr_->AddRef();
    audio_device_ptr_->RegisterAudioCallback(this);

    int idx = 0;
    if(!audio_device_id_.empty()) {
        idx = GetPlayoutDeviceIdex(audio_device_id_);
    }
    audio_device_ptr_->SetPlayoutDevice(idx);
    audio_device_ptr_->InitPlayout();
    // bugfix: call DiiAudioManager::SetSpeakerVolume API, no effect, before audio_device has create.
    if(audio_vol_ >=0 ) {
        audio_device_ptr_->SetSpeakerVolume(audio_vol_);
    }
    audio_device_ptr_->StartPlayout();
}

void DiiAudioManager::StopPlay() {
    if(!playing_) {
        return;
    }
    playing_ = false;
    
    if(audio_device_ptr_ == nullptr)
        return;
    
    if (audio_device_ptr_->Playing()) {
        audio_device_ptr_->StopPlayout();
    }
    
    audio_device_ptr_->RegisterAudioCallback(NULL);
    audio_device_ptr_->Release();
    audio_device_ptr_ = nullptr;
}


void DiiAudioManager::SetExternalVideoEncoderFactory(cricket::WebRtcVideoEncoderFactory* factory) {
	video_encoder_factory_.reset(factory);
}

cricket::WebRtcVideoEncoderFactory* DiiAudioManager::ExternalVideoEncoderFactory() {
	return video_encoder_factory_.get();
}
    
void DiiAudioManager::StartAudioRecord(DiiAudioRecorderCallback callback, int sampleHz, int channel) {
    dii_rtc::CritScope cs(&cs_audio_record_);
    audio_record_callback_ = callback;
    audio_record_sample_hz_ = sampleHz;
    audio_record_channels_ = channel;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_START_REC);
	
}
void DiiAudioManager::StopAudioRecord() {
    dii_rtc::CritScope cs(&cs_audio_record_);
    audio_record_callback_ = NULL;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_REC);
}

void DiiAudioManager::StopAudioDevice() {
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_PLAY);
}

void DiiAudioManager::RegAudioTrack(DiiAudioTracker* tracker, bool restart) {
    std::unique_lock<std::mutex> tracker_lck(tracker_map_mtx_);
    if(mixer_tacker_map_.size() == 0) {
        if(restart) {
            dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_PLAY);
        }
        dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_START_PLAY);
    }
 
    for(auto it : mixer_tacker_map_) {
        if(it.first == tracker) {
           return;
        }
    }
    
    audio_tracker_id_++;
    dii_media_kit::AudioMixer::Source *audio_src = new DiiAudioSource(tracker,
                                                                             audio_play_sample_hz_,
                                                                             audio_play_channels_,
                                                                             audio_tracker_id_);
    if(mixer_ptr.get() == nullptr) {
        mixer_ptr =  dii_media_kit::AudioMixerImpl::Create(
                                                   std::unique_ptr<DiiAudioOutput>(new DiiAudioOutput(audio_play_sample_hz_)),
                                                   false);
    }
    mixer_ptr->AddSource(audio_src);
    mixer_tacker_map_.insert(std::pair<DiiAudioTracker*, AudioMixer::Source*>(tracker, audio_src));
}
 
void DiiAudioManager::UnregAudioTrack(DiiAudioTracker* tracker) {
    std::unique_lock<std::mutex> tracker_lck(tracker_map_mtx_);
    auto it = mixer_tacker_map_.begin();
    for(; it!= mixer_tacker_map_.end(); ) {
        if(it->first == tracker) {
            mixer_ptr->RemoveSource(it->second);
            delete it->second;
            mixer_tacker_map_.erase(it++);
            break;
        } else {
            it++;
        }
    }
     if(mixer_tacker_map_.size() == 0) {
#ifndef WEBRTC_IOS
        dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_STOP_PLAY);
#endif
     }
}

int32_t DiiAudioManager::SetSpeakerVolume(uint32_t volume) {
    this->audio_vol_ = volume;
	dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_VOL);
	return 0;
}

int32_t DiiAudioManager::GetPlayoutDeviceIdex(std::string id) {
    int index = 0;
    char name[kAdmMaxDeviceNameSize] = { 0 };
    char devid[kAdmMaxGuidSize] = { 0 };

    int count = audio_device_ptr_->PlayoutDevices();
    
    for (int i = 0; i < count; i++) {
      audio_device_ptr_->PlayoutDeviceName(i, name, devid);
      if (0 == strcmp(devid, id.c_str())) {
          index = i;
          break;
      }
    }

    if (index < 0 || index >= count) {
      LOG(LS_INFO) << "SetPlayoutDevice index error " << index;
      return 0;
    }
    return index;
}

int32_t DiiAudioManager::SetPlayoutDevice(const char* deviceId) {
    LOG(LS_INFO) << "SetPlayoutDevice, device id:" << deviceId;
	if (nullptr == deviceId)
		return -1;
	
	if ( this->audio_device_id_ == deviceId) {
		LOG(LS_WARNING) << "palyout device alreay set, device id:" << deviceId;
		return 1;
	}
    this->audio_device_id_ = deviceId;
    dii_rtc::Thread::Post(RTC_FROM_HERE, this, AUDIO_MSG_DEVICE);
	return 0;
}

int32_t DiiAudioManager::SwitchPlayoutDevice(std::string device_id) {
    LOG(LS_INFO) << "SwitchPlayoutDevice, id:" << device_id;
	if (audio_device_ptr_ == nullptr) {
		LOG(LS_WARNING) << "SetPlayoutDevice audio_device_ptr_ is null ";
		return -1;
	}
    if (audio_device_ptr_->Playing()) {
        audio_device_ptr_->StopPlayout();
    }
    
    int idx = GetPlayoutDeviceIdex(device_id);
	audio_device_ptr_->SetPlayoutDevice(idx);
	audio_device_ptr_->InitPlayout();
	audio_device_ptr_->StartPlayout();

    return 0;
}
    
int32_t DiiAudioManager::RecordedDataIsAvailable(const void* audioSamples, const size_t nSamples,
	const size_t nBytesPerSample, const size_t nChannels, const uint32_t samplesPerSec, const uint32_t totalDelayMS,
	const int32_t clockDrift, const uint32_t currentMicLevel, const bool keyPressed, uint32_t& newMicLevel)
{
	dii_rtc::CritScope cs(&cs_audio_record_);
	if (audio_record_callback_) {
		if (audio_record_sample_hz_ != samplesPerSec || nChannels != audio_record_channels_) {
			int16_t output[kMaxDataSizeSamples];
			int samples_per_channel_int = resampler_record_.Resample10Msec((int16_t*)audioSamples,
                                                                           samplesPerSec * nChannels,
                                                                           audio_record_sample_hz_ * audio_record_channels_,
                                                                           1,
                                                                           kMaxDataSizeSamples,
                                                                           output);
			audio_record_callback_(output,
                                   audio_record_sample_hz_ / 100,
                                   nBytesPerSample,
                                   audio_record_channels_,
                                   audio_record_sample_hz_,
                                   totalDelayMS);
		}
		else {
			audio_record_callback_(audioSamples,
                                   nSamples,
                                   nBytesPerSample,
                                   audio_record_channels_,
                                   samplesPerSec,
                                   totalDelayMS);
		}
	}
	return 0;
}

//
int32_t DiiAudioManager::NeedMorePlayData(const size_t nSamples,
                                                const size_t nBytesPerSample,
                                                const size_t nChannels,
                                                const uint32_t samplesPerSec,
                                                void* audioSamples,
                                                size_t& nSamplesOut,
                                                int64_t* elapsed_time_ms,
                                                int64_t* ntp_time_ms, 
												int32_t delayMs) {
    if(mixer_ptr.get()) {
        *elapsed_time_ms = 0;
        *ntp_time_ms = 0;
        dii_media_kit::AudioFrame frame;
        mixer_ptr->Mix(audio_play_channels_, &frame);

		int samples_per_channel_int = samplesPerSec / 100;
		if (samples_per_channel_int > 0) {
			memset(audioSamples, 0, samples_per_channel_int * sizeof(int16_t) * nChannels);
		}

        int16_t res_out[kMaxDataSizeSamples];
        int samples_out = resampler_playout_.Resample10Msec((int16_t*)frame.data_,
                                                            frame.sample_rate_hz_*frame.num_channels_,
                                                            samplesPerSec*nChannels,
                                                            1,
                                                            kMaxDataSizeSamples,
                                                            (int16_t*)res_out);

        memcpy(audioSamples, (uint8_t*)res_out, samples_out * sizeof(uint16_t));
		nSamplesOut = samples_out/ nChannels;
		
	} else {
		memset(audioSamples, 0, samplesPerSec / 100 * sizeof(int16_t) * nChannels);
		nSamplesOut = samplesPerSec / 100;
	}

    return 0;
}
}	// namespace dii_media_kit
