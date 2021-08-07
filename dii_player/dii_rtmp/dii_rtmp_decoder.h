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
#ifndef __PLAYER_DECODER_H__
#define __PLAYER_DECODER_H__
#include "dii_rtmp_buffer.h"
#include "pluginaac.h"
#include "dii_common.h"
#include "dii_play_base.h"
#include "webrtc/base/thread.h"
#include "webrtc/common_audio/ring_buffer.h"
#include "webrtc/modules/audio_coding/acm2/acm_resampler.h"
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/modules/audio_device/include/audio_device_defines.h"
#include "webrtc/modules/video_coding/codecs/h264/include/h264.h"
#include "webrtc/api/mediastreaminterface.h"
#include "third_party/SoundTouch/SoundTouch/SoundTouch.h"

extern "C" {
    #include "libavutil/avstring.h"
    #include "libavutil/eval.h"
    #include "libavutil/mathematics.h"
    #include "libavutil/pixdesc.h"
    #include "libavutil/imgutils.h"
    #include "libavutil/dict.h"
    #include "libavutil/parseutils.h"
    #include "libavutil/samplefmt.h"
    #include "libavutil/avassert.h"
    #include "libavutil/time.h"
    #include "libavformat/avformat.h"
    #include "libavdevice/avdevice.h"
    #include "libswscale/swscale.h"
    #include "libavutil/opt.h"
    #include "libavcodec/avfft.h"
    #include "libswresample/swresample.h"

    #include "libavfilter/avfilter.h"
    #include "libavfilter/buffersink.h"
    #include "libavfilter/buffersrc.h"
    
    #include "libavutil/timestamp.h"
}

namespace dii_media_kit {
    typedef enum {
        SPEED_DOWN,
        SPEED_NORMAL,
        SPEED_UP,
    } RtmpPlaySpeed;

    class DiiRtmpDecoder : PlyBufferCallback, public DecodedImageCallback {
         friend class PlySyncMultiStream;
    public:
        DiiRtmpDecoder(int32_t stream_id, bool report);
        virtual ~DiiRtmpDecoder();
        void Start(bool report);
        void Shutdown();
        void SetVideoFrameCallback(VideoFrameCallback callback);
        bool IsPlaying();
        int32_t  GetCacheTime();

        void CacheAvcData(const uint8_t*pdata, int len, uint32_t ts);
        void CacheAacData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts);
        int GetMorePcmData(void *audioSamples, size_t samplesPerSec, size_t nChannels, uint64_t &sync_ts);
        void ClearCache();
        void DoStatistics(DiiPlayerStatistics& statistics);
    
        void OnNeedDecodeFrame(PlyPacket* pkt) override;
        int32_t Decoded(dii_media_kit::VideoFrame& decodedImage) override;
    private:
        void VideoDecodeThread();
        void AudioDecodeThread();
        void InitAACDecoder(uint8_t*data, int32_t len);
        void InitSoundTouch(uint16_t sample_rate, uint8_t channel_count);
        void DoSoundtouch(uint8_t*data, int32_t len);
        void ChunkAndCacheAudioData(uint32_t pts, uint64_t sync_ts );
    private:
        int32_t stream_id_ = -1;
        // video decode thread
        std::thread* v_decode_thread_   = nullptr;
        std::mutex v_mtx_;
        std::condition_variable         v_cond_;
        
        std::queue<PlyPacket*>          h264_queue_;
        dii_media_kit::VideoDecoder*  h264_decoder_;
        
        // audio decode thread
        std::thread* a_decode_thread_ = nullptr;
        std::mutex a_mtx_;
        std::condition_variable     a_cond_;
        std::queue<PlyPacket*>      aac_queue_;
        
        
        bool			        running_;
        DiiRtmpBuffer*		ply_buffer_;
        
        int32_t                 video_frame_observer_uid_;

        // audio
        aac_dec_t		aac_decoder_;
        uint8_t			audio_cache_[8192] = {0};
        int				a_cache_len_ = 0;
        uint32_t		encoded_audio_sample_rate_ = 0;
        uint8_t			encoded_audio_ch_nb_;
    
        int32_t         audio_bitrate_ = 0;
        int32_t         video_bitrate_ = 0;
        int64_t         cur_sync_ts_ = 0;
        // decoded video resolution
        int32_t  frame_height_ = 0;
        int32_t  frame_width_ = 0;
        
        int64_t    last_statistic_ts_ = 0;
        int32_t    decode_fps_ = 0;
        int32_t    render_fps_ = 0;

        float cur_audio_speed_ = 1.0;
        float pre_audio_speed_ = 1.0;
        int32_t decoder_index_ = 0;
        bool        got_keyframe_ = false;
        VideoFrameCallback video_frame_callback_ = nullptr;
        
        uint32_t pre_pts_ = 0;
        
        // soundtouch
        dii_soundtouch::SoundTouch *sound_touch_ = nullptr;
        int32_t current_audio_play_speed_ = 1;
        dii_radar::DiiRole _role;
        char * _userId;
        bool _report;
    };
}
#endif	// __PLAYER_DECODER_H__

