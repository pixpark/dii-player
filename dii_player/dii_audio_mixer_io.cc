//
//  dii_audio_mixer.cpp
//  DiiMediaKit
//
//  Created by devzhaoyou on 2019/11/30.
//  Copyright © 2019 pixpark. All rights reserved.
//

#include "dii_audio_mixer_io.h"
#include "dii_audio_manager.h"
#include "dii_media_utils.h"

namespace dii_media_kit {

    DiiAudioSource::DiiAudioSource(DiiAudioTracker *tracker, int sample_rate, int channels, int ssrc)
            : ssrc_(ssrc)
            , sample_rate_(sample_rate)
            , audio_tracker_(tracker)
            , channel_nb_(channels)
            , frame_id_(0) {
            
    }

    DiiAudioSource::~DiiAudioSource() {
            
    }

    AudioMixer::Source::AudioFrameInfo DiiAudioSource::GetAudioFrameWithInfo(int sample_rate_hz, AudioFrame* audio_frame) {
        int readed_bytes = 0;
        if (audio_tracker_ != NULL) {
                int16_t buffer[2000] = {0};
                readed_bytes = audio_tracker_->OnNeedPlayAudio(buffer,  sample_rate_, channel_nb_);
                audio_frame->UpdateFrame(frame_id_++,
                                         (int32_t)DiiUnixTimestampMs(),
                                         buffer,
                                         sample_rate_/100,
                                         sample_rate_,
                                         AudioFrame::SpeechType::kNormalSpeech,
                                         AudioFrame::VADActivity::kVadUnknown,
										 channel_nb_);
               
            }
            
            return AudioMixer::Source::AudioFrameInfo::kNormal;
        }

        // A way for a mixer implementation to distinguish participants.
        int DiiAudioSource::Ssrc() const {
            return ssrc_;
        }

        // A way for this source to say that GetAudioFrameWithInfo called
        // with this sample rate or higher will not cause quality loss.
        int DiiAudioSource::PreferredSampleRate() const {
            return sample_rate_;
        }
}

