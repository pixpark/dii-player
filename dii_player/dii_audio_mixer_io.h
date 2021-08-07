//
//  dii_audio_mixer_io.h
//  DiiMediaKit
//
//  Created by devzhaoyou on 2019/11/30.
//  Copyright © 2019 pixpark. All rights reserved.
//

#ifndef _DII_AUDIO_MIXER_IO_
#define _DII_AUDIO_MIXER_IO_

#include "webrtc/api/audio/audio_mixer.h"
#include "webrtc/modules/audio_mixer/output_rate_calculator.h"

namespace dii_media_kit {
    class DiiAudioTracker;
    class DiiAudioSource : public dii_media_kit::AudioMixer::Source {
    public:
        DiiAudioSource(DiiAudioTracker *tracker, int sample_rate, int channels, int ssrc);
        virtual ~DiiAudioSource();
        AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz, AudioFrame* audio_frame) override ;
        int Ssrc() const override ;
        // A way for this source to say that GetAudioFrameWithInfo called
        // with this sample rate or higher will not cause quality loss.
        int PreferredSampleRate() const override ;

    private:
        DiiAudioTracker* audio_tracker_;
        int ssrc_;
        int sample_rate_;
        int channel_nb_;
        int frame_id_;
    };

    class DiiAudioOutput : public dii_media_kit::OutputRateCalculator {
    public:
        DiiAudioOutput(int sample_rate) : sample_rate_(sample_rate) {

        }

        ~DiiAudioOutput() {

        }

        virtual int CalculateOutputRate(const std::vector<int>& preferred_sample_rates) override {
            return sample_rate_;
        }

    private:
        int32_t sample_rate_;
        
    };


}
#endif /* _DII_AUDIO_MIXER_IO_ */

