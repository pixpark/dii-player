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
#include "dii_rtmp_decoder.h"
#include "webrtc/base/logging.h"
#include "webrtc/media/engine/webrtcvideoframe.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "dii_media_utils.h"

namespace dii_media_kit {
#ifndef WEBRTC_WIN
enum Frametype {
    FRAME_I  = 15,
    FRAME_P  = 16,
    FRAME_B  = 17
};

typedef struct Tag_bs_t {
    unsigned char *p_start;                //
    unsigned char *p;                      //
    unsigned char *p_end;                  //
    int     i_left;                        //
}bs_t;

void bs_init( bs_t *s, void *p_data, int i_data ) {
    s->p_start = (unsigned char *)p_data;        //
    s->p       = (unsigned char *)p_data;        //
    s->p_end   = s->p + i_data;                  //
    s->i_left  = 8;                              //
}

int bs_read( bs_t *s, int i_count ) {
    static uint32_t i_mask[33] ={0x00,
        0x01,      0x03,      0x07,      0x0f,
        0x1f,      0x3f,      0x7f,      0xff,
        0x1ff,     0x3ff,     0x7ff,     0xfff,
        0x1fff,    0x3fff,    0x7fff,    0xffff,
        0x1ffff,   0x3ffff,   0x7ffff,   0xfffff,
        0x1fffff,  0x3fffff,  0x7fffff,  0xffffff,
        0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff,
        0x1fffffff,0x3fffffff,0x7fffffff,0xffffffff};
    int      i_shr;             //
    int i_result = 0;           //
    
    while( i_count > 0 )     //
    {
        if( s->p >= s->p_end ) //
        {                       //
            break;
        }
        
        if( ( i_shr = s->i_left - i_count ) >= 0 )
        {
            i_result |= ( *s->p >> i_shr )&i_mask[i_count];
     
            s->i_left -= i_count;
            if( s->i_left == 0 )
            {
                s->p++;
                s->i_left = 8;
            }
            return( i_result );
        }
        else
        {
            i_result |= (*s->p&i_mask[s->i_left]) << -i_shr;
            i_count  -= s->i_left;
            s->p++;  //
            s->i_left = 8;   //
        }
    }
    
    return( i_result );
}
int bs_read1( bs_t *s ) {
    if( s->p < s->p_end )
    {
        unsigned int i_result;
        
        s->i_left--;                           //
        i_result = ( *s->p >> s->i_left )&0x01;//
        if( s->i_left == 0 )                   //
        {
            s->p++;                             //
            s->i_left = 8;                     //
        }
        return i_result;                       // unsigned int
    }
    
    return 0;                                  //
}

int bs_read_ue( bs_t *s ) {
    int i = 0;
    
    while( bs_read1( s ) == 0 && s->p < s->p_end && i < 32 )
    {
        i++;
    }
    return( ( 1 << i) - 1 + bs_read( s, i ) );
}
#endif


/**
 *  PlyDecoder
 */
DiiRtmpDecoder::DiiRtmpDecoder(int32_t stream_id, bool report)
	: running_(false)
	, h264_decoder_(NULL)
	, aac_decoder_(NULL)
	, a_cache_len_(0)
	, encoded_audio_ch_nb_(2)
    , cur_audio_speed_(1.0)
    , _role(dii_radar::_Role_Unknown)
    , _userId(NULL)
    , _report(report)
{
        this->stream_id_ = stream_id;
}

DiiRtmpDecoder::~DiiRtmpDecoder()
{
    this->Shutdown();
    if(_userId){
        free(_userId);
        _userId = NULL;
    }
}

void DiiRtmpDecoder::Start(bool report) {
    if(running_) {
        return;
    }
    _report = report;
    got_keyframe_ = false;
    h264_decoder_ = dii_media_kit::H264Decoder::Create();
    dii_media_kit::VideoCodec codecSetting;
    codecSetting.codecType = dii_media_kit::kVideoCodecH264;
    codecSetting.width = 320;
    codecSetting.height = 240;
    h264_decoder_->InitDecode(&codecSetting, 1);
    h264_decoder_->RegisterDecodeCompleteCallback(this);
    
    running_ = true;
    
//    last_statistic_ts_ = dii_rtc::Time();
    ply_buffer_ = new DiiRtmpBuffer(stream_id_, *this);
    v_decode_thread_ = new std::thread(&DiiRtmpDecoder::VideoDecodeThread, this);
    a_decode_thread_ = new std::thread(&DiiRtmpDecoder::AudioDecodeThread, this);
    
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "Play decoder start.";
}

void DiiRtmpDecoder::Shutdown() {
    if(!running_) {
        return;
    }
    running_ = false;

    // shutdown audio decode thread
    a_decode_thread_->join();
    delete a_decode_thread_;
   
    // shutdown video decode thread
    v_decode_thread_->join();
    delete v_decode_thread_;

    if (ply_buffer_) {
        delete ply_buffer_;
        ply_buffer_ = NULL;
    }

    if (aac_decoder_) {
        aac_decoder_close(aac_decoder_);
        aac_decoder_ = NULL;
    }

    if (h264_decoder_) {
        delete h264_decoder_;
        h264_decoder_ = NULL;
    }
    
    if (sound_touch_) {
        sound_touch_->clear();
        delete sound_touch_;
        sound_touch_ = nullptr;
    }

    frame_width_ = 0;
    frame_height_ = 0;
    ClearCache();
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "Shutdown play decoder";
}

void DiiRtmpDecoder::SetVideoFrameCallback(VideoFrameCallback callback) {
    video_frame_callback_ = callback;
}

bool DiiRtmpDecoder::IsPlaying()
{
    if (!ply_buffer_) {
        return false;
    }
    if (ply_buffer_->PlayerStatus() == Buffering) {
        return false;
    }
    return true;
}

int32_t DiiRtmpDecoder::GetCacheTime() {
    // FIX ME:
    int32_t cache_len = 0;
    if (ply_buffer_)
        cache_len = ply_buffer_->GetPlayCacheTime();
    
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "audio stream play cache" << cache_len;
    return cache_len;
}

void DiiRtmpDecoder::CacheAvcData(const uint8_t* pdata, int len, uint32_t ts)
{
    video_bitrate_ += len;

    PlyPacket* pkt = new PlyPacket(true);
    pkt->SetData(pdata, len, ts);

#ifndef WEBRTC_WIN
    bs_t s;
    bs_init(&s, (void *)(pkt->_data + 4 + 1), pkt->_data_len - 4 -1);
    /* i_first_mb */
    bs_read_ue( &s );
    /* picture type */
    int frame_type =  bs_read_ue( &s );
    Frametype ft = FRAME_P;
    switch(frame_type) {
        case 0: case 5: /* P */
            ft = FRAME_P;
            break;
        case 1: case 6: /* B */
            ft = FRAME_B;
            break;
        case 3: case 8: /* SP */
            ft = FRAME_P;
            break;
        case 2: case 7: /* I */
            ft = FRAME_I;
            break;
        case 4: case 9: /* SI */
            ft = FRAME_I;
            break;
    }
    if(ft == FRAME_B) {
        DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "Not support decode P video frame, drop it.";
        return;
    }
#endif
    int type = pkt->_data[4] & 0x1f;
    
    if (type == 7) { // keyframe
        got_keyframe_ = true;
    }
    
    if(!got_keyframe_) {
        DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "Not keyframe. Drop it before keyframe coming.";
        return;
    }
    
    if(ply_buffer_) {
        ply_buffer_->CacheH264Frame(pkt, type);
    }
}

void DiiRtmpDecoder::AudioDecodeThread() {
    while (running_) {
        PlyPacket* pkt = nullptr;
        {
            std::unique_lock<std::mutex> lck(a_mtx_);
            if(aac_queue_.empty() || !ply_buffer_) {
                a_cond_.wait_for(lck, std::chrono::milliseconds(10));
                continue;
            }
            pkt = aac_queue_.front();
            aac_queue_.pop();
            if(!pkt)
                continue;
        }

        // init aac decoder
        if (aac_decoder_ == NULL) {
            InitAACDecoder(pkt->_data, pkt->_data_len);
        }
        
        unsigned int decoded_out_len = 0;
        uint8_t decoded_out[4096] = {0};
       int ret =  aac_decoder_decode_frame(aac_decoder_, (unsigned char*)pkt->_data, pkt->_data_len, decoded_out, &decoded_out_len);

        if (decoded_out_len > 0) {
            DoSoundtouch(decoded_out, decoded_out_len);
            ChunkAndCacheAudioData(pkt->_pts, pkt->_sync_ts);
        } else {
            DII_LOG(LS_ERROR, stream_id_, 2002013) << "rtmp aac decode error with error code:"<<ret;
        }
    }
}

void DiiRtmpDecoder::InitAACDecoder(uint8_t*data, int32_t len) {
    aac_decoder_ = aac_decoder_open((unsigned char*)data,
                                              len,
                                              &encoded_audio_ch_nb_,
                                              &encoded_audio_sample_rate_);
              
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "Open aac codec decoder, aac channels: " << encoded_audio_ch_nb_
      << ", aac sample rate: " << encoded_audio_sample_rate_;

    if (encoded_audio_ch_nb_ == 0)
      encoded_audio_ch_nb_ = 1;
}

void DiiRtmpDecoder::InitSoundTouch(uint16_t sample_rate, uint8_t channel_count) {
    sound_touch_ = new dii_soundtouch::SoundTouch();
    sound_touch_->setSampleRate(sample_rate);
    sound_touch_->setChannels(channel_count);
    sound_touch_->setPitch(1.0);
    sound_touch_->setTempo(1.0);
}

void DiiRtmpDecoder::DoSoundtouch(uint8_t*data, int32_t len) {
    if(sound_touch_ == nullptr) {
        InitSoundTouch(encoded_audio_sample_rate_, encoded_audio_ch_nb_);
    }

    int32_t cache_time_len_ = GetCacheTime();
    if(cache_time_len_ < ply_buffer_->PlayReadyBufferLen() && current_audio_play_speed_ != 1) {
        current_audio_play_speed_ = 1;
        sound_touch_->setTempo(1.0);
        DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "audio play speed 100%, cache len: " << cache_time_len_ << " ms.";
    } else if (cache_time_len_ > (ply_buffer_->PlayReadyBufferLen() + 2000) && current_audio_play_speed_ != 2){
        current_audio_play_speed_ = 2;
        sound_touch_->setTempo(1.2);
        DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "audio play speed 120%, cache len: " << cache_time_len_ << " ms.";
    }

    // soundtouch process
    std::vector<uint8_t> soundtouch_buf;
    // if need speed cut, soundtouc_buf may > src audio data len.
    soundtouch_buf.resize(2*len);
    
    int out_sample_nb = len/encoded_audio_ch_nb_/2;
    sound_touch_->putSamples((dii_soundtouch::SAMPLETYPE *)data, out_sample_nb);

    int out_st_sample_cnt = sound_touch_->receiveSamples((dii_soundtouch::SAMPLETYPE *)soundtouch_buf.data(), out_sample_nb);
    if(out_st_sample_cnt > 0) {
       int out_len = out_st_sample_cnt*encoded_audio_ch_nb_*sizeof(int16_t);
       memcpy(audio_cache_ + a_cache_len_, soundtouch_buf.data(), out_len);
       a_cache_len_ += out_len;
    }
}

void DiiRtmpDecoder::ChunkAndCacheAudioData(uint32_t pts, uint64_t sync_ts) {
    // start chunk cache in 10ms len.
    int alen_10ms = encoded_audio_sample_rate_/100*2*encoded_audio_ch_nb_;
    const int max_pcm_10ms = 48000 / 100 * 2 * 2;    //采样率48000时10ms pcm数据的最大长度
    if (alen_10ms > max_pcm_10ms)  {
        DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "audio stream play speed pcm len (" << alen_10ms << ") max_len(" << max_pcm_10ms << ")";
        return;
    }
                  
    int ct = 0;
    while (a_cache_len_ > alen_10ms) {
       ply_buffer_->CachePcmData(audio_cache_ + ct * alen_10ms,
                                 alen_10ms,
                                 encoded_audio_sample_rate_,
                                 encoded_audio_ch_nb_,
                                 pts,
                                 sync_ts);
       a_cache_len_ -= alen_10ms;
       ct++;
    }
    memmove(audio_cache_, audio_cache_ + ct * alen_10ms, a_cache_len_);
}


void DiiRtmpDecoder::CacheAacData(const uint8_t*pdata, int len, uint32_t ts, uint64_t sync_ts) {
    audio_bitrate_ += len;
    // push packet
    PlyPacket* pkt = new PlyPacket(false);
    pkt->SetData(pdata, len, ts, sync_ts);
    std::unique_lock<std::mutex> lck(a_mtx_);
    aac_queue_.push(pkt);
    a_cond_.notify_one();
}

int DiiRtmpDecoder::GetMorePcmData(void *audioSamples,
                                 size_t samplesPerSec,
                                 size_t nChannels,
                                 uint64_t &sync_ts) {
    if (ply_buffer_ && ply_buffer_->PlayerStatus() != BufferReady) {
        return 0;
    }
 
    if(ply_buffer_) {
        DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Audio track need more audio sample data, samples per second: " << samplesPerSec <<  ", audio channels: " << nChannels;
        cur_sync_ts_ = sync_ts;
       
        if(_report){
            // callback to radar.
            dii_radar::DiiRadarCallback callback = dii_media_kit::DiiUtil::Instance()->GetRadarCallback();
            if(callback.RenderAudioCallback) {
                dii_radar::AudioFrameMatedata a_matedata;
                a_matedata.role = _role;
                if(_userId){
                    a_matedata.userid = std::string(_userId);
                }else{
                    a_matedata.userid = "";
                }
                a_matedata.bytesPerSample = 2;
                a_matedata.channels = nChannels;
                a_matedata.samplerate = samplesPerSec;
                a_matedata.streamid = "1000";//stream_id_;
                callback.RenderAudioCallback(a_matedata);
            }
        }
        
        return ply_buffer_->GetMorePcmData(audioSamples, samplesPerSec, nChannels, sync_ts);
    }
    return -1;
}

void DiiRtmpDecoder::ClearCache() {
    // clear play buffer
    //
    {
        std::unique_lock<std::mutex> vlck(v_mtx_);
        while (!h264_queue_.empty()) {
            auto it = h264_queue_.front();
            delete it;
            h264_queue_.pop();
        }
    }
    
    {
        std::unique_lock<std::mutex> alck(a_mtx_);
        while (!aac_queue_.empty()) {
            auto it = aac_queue_.front();
            delete it;
            aac_queue_.pop();
        }
    }
}

void DiiRtmpDecoder::VideoDecodeThread() {
    while(running_) {
        PlyPacket* pkt = nullptr;
        {
            std::unique_lock<std::mutex> lck(v_mtx_);
            if (h264_queue_.empty() || !h264_decoder_) {
                v_cond_.wait_for(lck, std::chrono::milliseconds(10));
                continue;
            }
            pkt = h264_queue_.front();
            h264_queue_.pop();
            if(!pkt)
                continue;
        }
     
        int frameType = pkt->_data[4] & 0x1f;
        dii_media_kit::EncodedImage encoded_image;
        encoded_image._buffer = (uint8_t*)pkt->_data;
        encoded_image._length = pkt->_data_len;
        encoded_image._size = pkt->_data_len + 8;
        encoded_image._timeStamp = pkt->_pts;
        if (frameType == 7) {
            encoded_image._frameType = dii_media_kit::kVideoFrameKey;
        }
        else {
            encoded_image._frameType = dii_media_kit::kVideoFrameDelta;
        }
        encoded_image._completeFrame = true;
        dii_media_kit::RTPFragmentationHeader frag_info;
     
        decode_fps_++;
        int ret = h264_decoder_->Decode(encoded_image, false, &frag_info);
        if (ret != 0) {
            DII_LOG(LS_INFO, stream_id_, 2002009) << "rtmp h264 decode error.with error code:"<<ret;
        }
        delete pkt;
	}
}

// decode video data
void DiiRtmpDecoder::OnNeedDecodeFrame(PlyPacket* pkt) {
    std::unique_lock<std::mutex> vlck(v_mtx_);
    h264_queue_.push(pkt);
}

// Got Decoded Frame Image
int32_t DiiRtmpDecoder::Decoded(dii_media_kit::VideoFrame& decodedImage) {
    render_fps_++;
    frame_width_ = decodedImage.width();
    frame_height_ = decodedImage.height();
    video_frame_callback_(decodedImage);
    
    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO)
        << "Got decoded frame image for video rending, width:"
        << decodedImage.width()
        << " height:" << decodedImage.height()
        <<" pts: " << decodedImage.timestamp();
    
    if(pre_pts_ == 0) {
        pre_pts_ =  decodedImage.timestamp();
    }
    
    int32_t dt = decodedImage.timestamp() - pre_pts_;
    if(dt > 200) {
        DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_INFO)
        << "video dealt pts: "
        << dt
        << " > 200ms"
        << " = (cur_pts: " << decodedImage.timestamp()
        << ") - (pre_pts: " << pre_pts_ << ")";
    }
    
    pre_pts_ =  decodedImage.timestamp();
    
    if(_report){
        // callback to radar.
        dii_radar::DiiRadarCallback callback = dii_media_kit::DiiUtil::Instance()->GetRadarCallback();
        if(callback.RenderVideoCallback) {
            dii_radar::VideoFrameMatedata v_matedata;
            v_matedata.role = _role;
            if(_userId){
                v_matedata.userid = std::string(_userId);
            }else{
                v_matedata.userid = "";
            }
            v_matedata.width = frame_width_;
            v_matedata.height = frame_height_;
            v_matedata.pts = pre_pts_;
            v_matedata.start_to_render_time = 0;
            v_matedata.render_time_flg = false;

            // Fix Me: 雷达根据stream id 判断不同的流间隔，stream_id 不能保证刷新后仍相同，此处写死 stream_id 为：1000
            v_matedata.streamid = "1000";
            callback.RenderVideoCallback(v_matedata);
        }
    }

	return 0;
}

void DiiRtmpDecoder::DoStatistics(DiiPlayerStatistics& statistics) {
    statistics.audio_bps_ = audio_bitrate_;
    statistics.video_bps_ = video_bitrate_;

    statistics.cache_len_               = GetCacheTime();
    statistics.video_decode_framerate   = decode_fps_;
    statistics.video_render_framerate   = render_fps_;
    statistics.audio_samplerate_        = encoded_audio_sample_rate_;
    statistics.video_width_             = frame_width_;
    statistics.video_height_            = frame_height_;

    statistics.sync_ts_ = cur_sync_ts_;
    
    audio_bitrate_ = 0;
    video_bitrate_ = 0;
    
    render_fps_ = 0;
    decode_fps_ = 0;
}
}

