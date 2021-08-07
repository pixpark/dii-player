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
#include "dii_rtmp_puller.h"
#include "srs_librtmp.h"
#include "dii_media_utils.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/thread.h"

#ifndef _WIN32
#define ERROR_SUCCESS   0
#endif

#define RTMP_READ_TIME_OUT    10000  //ms
#define RTMP_WRITE_TIME_OUT   10000  //ms


#define ERR_CODE_UNKNOW                     99
#define ERR_CODE_HANDSHARK                  100
#define ERR_CODE_CONNECT                    101
#define ERR_CODE_PLAY_STREAM                102
#define ERR_CODE_READ_TIME_OUT              103

static u_int8_t fresh_nalu_header[] = { 0x00, 0x00, 0x00, 0x01 };
static u_int8_t cont_nalu_header[] = { 0x00, 0x00, 0x01 };

DiiRtmpPuller::DiiRtmpPuller(int32_t stream_id, DiiPullerCallback&callback, bool report)
	: callback_(callback)
	, srs_codec_(NULL)
	, running_(false)
	, rtmp_status_(RS_PLY_Init)
	, rtmp_(NULL)
	, audio_payload_(NULL)
	, video_payload_(NULL)
    , _role(dii_radar::_Role_Unknown)
    , _userId(NULL)
    , _report(report)
{
    this->stream_id_ = stream_id;
	
    srs_codec_ = new SrsAvcAacCodec();
	audio_payload_ = new DemuxData(1024);
	video_payload_ = new DemuxData(384 * 1024);
}

DiiRtmpPuller::~DiiRtmpPuller(void)
{
    this->Shutdown();
	if (srs_codec_) {
		delete srs_codec_;
		srs_codec_ = NULL;
	}
	if (audio_payload_) {
		delete audio_payload_;
		audio_payload_ = NULL;
	}
	if (video_payload_) {
		delete video_payload_;
		video_payload_ = NULL;
	}
    if(_userId){
        free(_userId);
        _userId = NULL;
    }
}

void DiiRtmpPuller::StartPull(const std::string& url, bool report) {
    if(running_) {
        return;
    }
    _report = report;
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "rtmp puller: start pull rtmp url: " << url;;
    str_url_ = url;
    running_ = true;
    rtmp_status_ = RS_PLY_Init;
    rtmp_ = srs_rtmp_create(str_url_.c_str());
    srs_rtmp_set_timeout(rtmp_, RTMP_READ_TIME_OUT, RTMP_WRITE_TIME_OUT);
     dii_rtc::Thread::Start();
}

void DiiRtmpPuller::Shutdown() {
    if(!running_) {
        return;
    }

    running_ = false;
    rtmp_status_ = RS_PLY_Closed;
    
    // 防止socket read 函数长时间读不退出，主动 disconnect
    srs_rtmp_disconnect_server(rtmp_);
   
    dii_rtc::Thread::Stop();
    srs_rtmp_destroy(rtmp_);
    rtmp_ = nullptr;
    DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "rtmp puller, stop pull: " << str_url_;
}

//* For Thread
void DiiRtmpPuller::Run()
{
	bool connect_ok = false;
	unsigned long long connect_error_count = 0;
	unsigned long long timestamp_delay_log = 0;
	unsigned int error_code = 0;
	char * error_info = NULL;

	while (running_) {
		bool need_sleep = true;
		unsigned long long sleeptimestamp_ms = 10;
        int ret = 0;
		error_code = 0;
		error_info = NULL;
		bool need_reconnect = false;
        if (rtmp_ != NULL) {
			if (RS_PLY_Init == rtmp_status_) {
			    ret = srs_rtmp_handshake(rtmp_);
				if (ret == 0) {
					error_code = 0;
					error_info = (char *)"rtmp simple handshake ok.";
					//DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << error_info;
					rtmp_status_ = RS_PLY_Handshaked;
					need_sleep = false;
					sleeptimestamp_ms = 5;
				} else {
					connect_error_count++;
					error_code = 2002003;
					error_info = (char *)"rtmp simple handshake failed";
                    //DII_LOG(LS_ERROR, stream_id_, error_code) << error_info;
					//callback_.OnPullFailed(ret, error_code, error_info);
                    rtmp_status_ = RS_PLY_Closed;
					need_sleep = true;
					sleeptimestamp_ms = 200;
                }
			}else if(RS_PLY_Handshaked == rtmp_status_){
                ret = srs_rtmp_connect_app(rtmp_);
				if (ret == 0) {
					error_code = 0;
					error_info = (char *)"rtmp connect vhost/app ok.";
                    //DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << error_info;
					rtmp_status_ = RS_PLY_Connected;
					need_sleep = false;
					sleeptimestamp_ms = 5;
				} else {
					connect_error_count++;
					error_code = 2002004;
					error_info = (char *)"rtmp connect vhost / app faild.";
                   //DII_LOG(LS_ERROR, stream_id_, error_code) << error_info;
					//callback_.OnPullFailed(ret, error_code, error_info);
                    rtmp_status_ = RS_PLY_Closed;
					need_sleep = true;
					sleeptimestamp_ms = 200;
                }
			}else if(RS_PLY_Connected == rtmp_status_){
                ret = srs_rtmp_play_stream(rtmp_);
				if (ret == 0) {
					error_code = 0;
					error_info = (char *)"rtmp play stream command ok.";
					//DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << error_info;
					rtmp_status_ = RS_PLY_Played;
					//CallConnect();
					callback_.OnServerConnected();
					need_sleep = false;
					sleeptimestamp_ms = 5;
				}
                else {
					connect_error_count++;
					error_code = 2002005;
					error_info = (char *)"rtmp play stream command failed.";
                   // DII_LOG(LS_INFO, stream_id_, error_code) << error_info;
					//callback_.OnPullFailed(ret, error_code, error_info);
                    rtmp_status_ = RS_PLY_Closed;
					need_sleep = true;
					sleeptimestamp_ms = 200;
                }
			}else if(RS_PLY_Played == rtmp_status_){
                ret = DoReadData();
				if (ret == 0) {
					need_sleep = false;
				}
				else {
					connect_error_count++;
					need_sleep = true;
				}
			}
		}

		if (error_info) {
			if (error_code == 0) {
				DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << error_info;
			}
			else {
				callback_.OnPullFailed(ret, error_code, error_info);
			}
		}

		if (need_sleep) {
			dii_rtc::Thread::SleepMs(sleeptimestamp_ms);
		}

        if(rtmp_status_ == RS_PLY_Closed) {
            DII_LOG(LS_INFO, stream_id_, DII_CODE_COMMON_INFO) << "shutdown rtmp puller thread done.";
            break;
        }
	}
}

int32_t DiiRtmpPuller::DoReadData()
{
	int size;
	char pkt_type;
	char* data;
	u_int32_t timestamp;
    
    int ret = srs_rtmp_read_packet(rtmp_, &pkt_type, &timestamp, &data, &size);
    if(ret != 0) {
        rtmp_status_ = RS_PLY_Closed;
        if(running_) {
            //DII_LOG(LS_ERROR, stream_id_, 2002006) << "Srs rtmp read packet faild, with error code:" << ret << ", url: " << str_url_;
            callback_.OnPullFailed(ret,2002006, "Srs rtmp read packet faild");
        }
        free(data);
        return ret;
    }
    
    // check if timestamp jump, try to fix it.
    if(timestamp != 0) {
        int32_t dt = timestamp - pre_pkt_ts_;
        if(dt < -3600) {
            if(error_ts_pkt_count_++ %30 == 0)
                DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "rtmp pkt timestamp jump times: " << error_ts_pkt_count_
                                << ", dt: " << dt
                                << " pre packet ts: " << pre_pkt_ts_
                                << ", current packet ts: "
                                << timestamp;
        }
        pre_pkt_ts_ = timestamp;
    }

    DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Incoming rtmp packet, type: "
                 << static_cast<int>(pkt_type)
                 << " size: " << size
                 << " timestamp: " << timestamp << " this:" << this;

    if (pkt_type == SRS_RTMP_TYPE_VIDEO) {
		SrsCodecSample sample;
		if (srs_codec_->video_avc_demux(data, size, &sample) == ERROR_SUCCESS) {
			if (srs_codec_->video_codec_id == SrsCodecVideoAVC) {	// Jus support H264
                GotVideoSample(timestamp, &sample);
			}
			else {
                DII_LOG(LS_ERROR, stream_id_, 2002007) << "Don't support video format, video codec id: " << srs_codec_->video_codec_id;
			}
		}
        video_bitrate_ += size;
	} else if (pkt_type == SRS_RTMP_TYPE_AUDIO) {
		SrsCodecSample sample;
		int retcode = srs_codec_->audio_aac_demux(data, size, &sample);
		if (retcode != ERROR_SUCCESS) {
//			if (sample.acodec == SrsCodecAudioMP3 && srs_codec_->audio_mp3_demux(data, size, &sample) != ERROR_SUCCESS) {
//				DII_LOG(LS_ERROR, stream_id_) << "Don't support audio format, audio codec id: " << static_cast<int>(SrsCodecAudioMP3);
//                free(data);
//                ret = -1;
//				return ret;
//			}
            DII_LOG(LS_ERROR, stream_id_, 2002014) << "Audio packet data demux format error, only support AAC.with error code:"<< retcode;
			free(data);
            ret = -1;
			return ret;	// Just support AAC.
		}
		SrsCodecAudio acodec = (SrsCodecAudio)srs_codec_->audio_codec_id;

		// ts support audio codec: aac/mp3
		if (acodec != SrsCodecAudioAAC && acodec != SrsCodecAudioMP3) {
            DII_LOG(LS_ERROR, stream_id_, 2002014) << "Rtmp audio packet format error, only support AAC.";
			free(data);
            ret = -1;
			return ret;
		}
		// for aac: ignore sequence header
		if (acodec == SrsCodecAudioAAC && sample.aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
            DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "receive AAC sequence header, timestamp: " << timestamp;
        } else if (srs_codec_->aac_object == SrsAacObjectTypeReserved) {
            DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "receive AAC sequence header error, aac_object: SrsAacObjectTypeReserved.";
            ret = -1;
            return ret;
        }
        
        DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Got rtmp audio sample data, timestamp: " << timestamp;
        
        //
        if(rtmp_metadata_packet_ts_ == 0) {
            rtmp_metadata_packet_ts_ = timestamp;
        }
        
        uint64_t sync_ts = 0;
        if(metadata_sync_ts_ > 0) {
            uint64_t delta_ts = timestamp - rtmp_metadata_packet_ts_;
            sync_ts = metadata_sync_ts_ + delta_ts;
        }
        lastest_audio_ts_ = timestamp;
		GotAudioSample(timestamp, &sample, sync_ts);
        audio_bitrate_ += size;
	} else if (pkt_type == SRS_RTMP_TYPE_SCRIPT) {
		if (srs_rtmp_is_onMetaData(pkt_type, data, size)) {
            double sync_ts = -1;
            if(srs_rtmp_get_custom_metadata_sync_ts(data, size, sync_ts) != 0) {
                rtmp_status_ = RS_PLY_Closed;
                if(running_) {
                    //DII_LOG(LS_ERROR, stream_id_, 2002007) << "Decode rtmp metadata packet error, url: " << str_url_;
                    callback_.OnPullFailed(ret, 2002007,"Decode rtmp metadata packet error");
                }
                free(data);
                return -1;
            }
           
            if(sync_ts > 0) {
                metadata_sync_ts_ = static_cast<uint64_t>(sync_ts);
                rtmp_metadata_packet_ts_ = lastest_audio_ts_;
                DII_LOG(LS_VERBOSE, stream_id_, DII_CODE_COMMON_INFO) << "Got rtmp metadata synchronized timestamp: " << metadata_sync_ts_
                             << " current rtmp packet timestamp: " << rtmp_metadata_packet_ts_;
            } else {
                DII_LOG(LS_WARNING, stream_id_, DII_CODE_COMMON_WARN) << "Not got rmtp metadata synchronized timestamp.";
            }
		}
	}
    free(data);
    return 0;
}

int DiiRtmpPuller::GotVideoSample(uint32_t timestamp, SrsCodecSample *sample)
{
	int ret = ERROR_SUCCESS;
	// ignore info frame,
	// @see https://github.com/simple-rtmp-server/srs/issues/288#issuecomment-69863909
	if (sample->frame_type == SrsCodecVideoAVCFrameVideoInfoFrame) {
		return ret;
	}

	// ignore sequence header
	if (sample->frame_type == SrsCodecVideoAVCFrameKeyFrame
		&& sample->avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
		return ret;
	}

	// when ts message(samples) contains IDR, insert sps+pps.
	if (sample->has_idr) {
		// fresh nalu header before sps.
		if (srs_codec_->sequenceParameterSetLength > 0) {
			video_payload_->append((const char*)fresh_nalu_header, 4);
			// sps
			video_payload_->append(srs_codec_->sequenceParameterSetNALUnit, srs_codec_->sequenceParameterSetLength);
		}
		// cont nalu header before pps.
		if (srs_codec_->pictureParameterSetLength > 0) {
			video_payload_->append((const char*)fresh_nalu_header, 4);
			// pps
			video_payload_->append(srs_codec_->pictureParameterSetNALUnit, srs_codec_->pictureParameterSetLength);
		}
	}

	// all sample use cont nalu header, except the sps-pps before IDR frame.
	for (int i = 0; i < sample->nb_sample_units; i++) {
		SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
		int32_t size = sample_unit->size;

		if (!sample_unit->bytes || size <= 0) {
			ret = -1;
			return ret;
		}
        

		// 5bits, 7.3.1 NAL unit syntax,
		// H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
		SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(sample_unit->bytes[0] & 0x1f);

		// ignore SPS/PPS/AUD
		switch (nal_unit_type) {
		case SrsAvcNaluTypeSPS:
		case SrsAvcNaluTypePPS:
		case SrsAvcNaluTypeSEI:
		case SrsAvcNaluTypeAccessUnitDelimiter:
			continue;
		default: {
            if (nal_unit_type == SrsAvcNaluTypeReserved) {
                RescanVideoframe(sample_unit->bytes, sample_unit->size, timestamp);
                continue;
            }
        }
			break;
		}

		if (nal_unit_type == SrsAvcNaluTypeIDR) {
            // DII_LOG(LS_INFO, stream_id_) << "Got H264 IDR Frame.";
			// insert cont nalu header before frame.
#ifdef WEBRTC_IOS
            video_payload_->append((const char*)fresh_nalu_header, 4);
#else
			video_payload_->append((const char*)cont_nalu_header, 3);
#endif
		}
		else {
			video_payload_->append((const char*)fresh_nalu_header, 4);
		}
		// sample data
        // DII_LOG(LS_VERBOSE, stream_id_) << "Got H264 Sample Frame Data.";
		video_payload_->append(sample_unit->bytes, sample_unit->size);
	}
	//* Fix for mutil nalu.
	if (video_payload_->_data_len != 0) {
        callback_.OnPullVideoData((uint8_t *) video_payload_->_data, video_payload_->_data_len, timestamp);
	}
	video_payload_->reset();

	return ret;
}
int DiiRtmpPuller::GotAudioSample(uint32_t timestamp, SrsCodecSample *sample, uint64_t sync_ts)
{
    int ret = ERROR_SUCCESS;
	for (int i = 0; i < sample->nb_sample_units; i++) {
		SrsCodecSampleUnit* sample_unit = &sample->sample_units[i];
		int32_t size = sample_unit->size;

		if (!sample_unit->bytes || size <= 0 || size > 0x1fff) {
			ret = -1;
			return ret;
		}

		// the frame length is the AAC raw data plus the adts header size.
		int32_t frame_length = size + 7;

		// AAC-ADTS
		// 6.2 Audio Data Transport Stream, ADTS
		// in aac-iso-13818-7.pdf, page 26.
		// fixed 7bytes header
		u_int8_t adts_header[7] = { 0xff, 0xf9, 0x00, 0x00, 0x00, 0x0f, 0xfc };
		/*
		// adts_fixed_header
		// 2B, 16bits
		int16_t syncword; //12bits, '1111 1111 1111'
		int8_t ID; //1bit, '1'
		int8_t layer; //2bits, '00'
		int8_t protection_absent; //1bit, can be '1'
		// 12bits
		int8_t profile; //2bit, 7.1 Profiles, page 40
		TSAacSampleFrequency sampling_frequency_index; //4bits, Table 35, page 46
		int8_t private_bit; //1bit, can be '0'
		int8_t channel_configuration; //3bits, Table 8
		int8_t original_or_copy; //1bit, can be '0'
		int8_t home; //1bit, can be '0'

		// adts_variable_header
		// 28bits
		int8_t copyright_identification_bit; //1bit, can be '0'
		int8_t copyright_identification_start; //1bit, can be '0'
		int16_t frame_length; //13bits
		int16_t adts_buffer_fullness; //11bits, 7FF signals that the bitstream is a variable rate bitstream.
		int8_t number_of_raw_data_blocks_in_frame; //2bits, 0 indicating 1 raw_data_block()
		*/
		// profile, 2bits
		SrsAacProfile aac_profile = srs_codec_aac_rtmp2ts(srs_codec_->aac_object);
		adts_header[2] = (aac_profile << 6) & 0xc0;
		// sampling_frequency_index 4bits
		adts_header[2] |= (srs_codec_->aac_sample_rate << 2) & 0x3c;
		// channel_configuration 3bits
		adts_header[2] |= (srs_codec_->aac_channels >> 2) & 0x01;
		adts_header[3] = (srs_codec_->aac_channels << 6) & 0xc0;
		// frame_length 13bits
		adts_header[3] |= (frame_length >> 11) & 0x03;
		adts_header[4] = (frame_length >> 3) & 0xff;
		adts_header[5] = ((frame_length << 5) & 0xe0);
		// adts_buffer_fullness; //11bits
		adts_header[5] |= 0x1f;

		// copy to audio buffer
		audio_payload_->append((const char*)adts_header, sizeof(adts_header));
		audio_payload_->append(sample_unit->bytes, sample_unit->size);

        callback_.OnPullAudioData((uint8_t *) audio_payload_->_data, audio_payload_->_data_len, timestamp, sync_ts);
		audio_payload_->reset();
	}

	return ret;
}

void DiiRtmpPuller::RescanVideoframe(const char*pdata, int len, uint32_t timestamp)
{
    int nal_type = pdata[4] & 0x1f;
    const char *p = pdata;
    if (nal_type == 7)
    {// keyframe
        int find7 = 0;
        const char* ptr7 = NULL;
        int size7 = 0;
        int find8 = 0;
        const char* ptr8 = NULL;
        int size8 = 0;
        const char* ptr5 = NULL;
        int size5 = 0;
        int head01 = 4;
        for (int i = 4; i < len - 4; i++)
        {
            if ((p[i] == 0x0 && p[i + 1] == 0x0 && p[i + 2] == 0x0 && p[i + 3] == 0x1) || (p[i] == 0x0 && p[i + 1] == 0x0 && p[i + 2] == 0x1))
            {
                if (p[i + 2] == 0x01)
                    head01 = 3;
                else
                    head01 = 4;
                if (find7 == 0)
                {
                    find7 = i;
                    ptr7 = p;
                    size7 = find7;
                    i++;
                }
                else if (find8 == 0)
                {
                    find8 = i;
                    ptr8 = p + find7 ;
                    size8 = find8 - find7;
                    const char* ptr = p + i;
                    if ((ptr[head01] & 0x1f) == 5)
                    {
                        ptr5 = p + find8 + head01;
                        size5 = len - find8 - head01;
                        break;
                    }
                }
                else
                {
                    ptr5 = p + i + head01;
                    size5 = len - i - head01;
                    break;
                }
            }
        }
        video_payload_->append(ptr7, size7);
        video_payload_->append(ptr8, size8);
        video_payload_->append((const char*)fresh_nalu_header, 4);
        video_payload_->append(ptr5, size5);
        callback_.OnPullVideoData((uint8_t *) video_payload_->_data, video_payload_->_data_len, timestamp);
        video_payload_->reset();
    }
    else 
    {
        video_payload_->append(pdata, len);
        callback_.OnPullVideoData((uint8_t *) video_payload_->_data, video_payload_->_data_len, timestamp);
        video_payload_->reset();
    }
}


void DiiRtmpPuller::CallConnect()
{
    callback_.OnServerConnected();
}
