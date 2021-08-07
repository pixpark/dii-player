//
//  dii_player.cpp
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#include "dii_player.h"
#include "dii_media_core.h"
#include <list>

namespace dii_media_kit {
	DiiPlayer::DiiPlayer(void* render, bool outputPcmForExternalMix) {
		dii_player_ = new DiiMediaCore(render, outputPcmForExternalMix);
	}

	DiiPlayer::~DiiPlayer() {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "DiiPlayer destruct.";
		if (dii_player_) {
			delete dii_player_;
			dii_player_ = nullptr;
		}
	}

	int32_t DiiPlayer::Start(const char* url, int64_t pos, bool pause)
    {
        this->stream_id_ = DiiUtil::Instance()->CreateStreamId();
        if (!url) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "stream url is null.";
            return -1;
        }
                
		DII_LOG(LS_INFO, this->stream_id_, 0) << "start play"
                                                << ", url="     << url
                                                << ", pos="     << pos
                                                << ", pause="   << pause;
        
        int32_t ret = dii_player_->Start(this->stream_id_, url, pos, pause);
        if(ret < 0) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "play failed, ret=" << ret;
        }
		return ret;
	}

    int32_t DiiPlayer::Start(dii_radar::DiiRole role, const char * userId, const char* url, int64_t pos, bool pause)
    {
        return Start(url, pos, pause);
    }

	int32_t DiiPlayer::Pause() {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "pause.";
		int32_t ret = dii_player_->Pause();
        if(ret < 0) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "pause failed, ret=" << ret;
        }
		return ret;
	}

	int32_t DiiPlayer::Resume() {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "resume.";
		int32_t ret = dii_player_->Resume();
        if(ret < 0) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "resume failed, ret=" << ret;
        }
		return ret;
	}

    int32_t DiiPlayer::SetLoop(bool loop) {
        DII_LOG(LS_INFO, this->stream_id_, 0) << "setLoop, loop:" << loop;
        int32_t ret = dii_player_->SetLoop(loop);
        if(ret < 0) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "setLoop faild, ret:" << ret;
        }
        return ret;
    }

	int32_t DiiPlayer::Stop() {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "stop.";
		int32_t ret = dii_player_->StopPlay();
        if(ret < 0) {
            DII_LOG(LS_ERROR, this->stream_id_, 0) << "stop failed, ret=" << ret;
        }
		return ret;
	}

	int32_t DiiPlayer::Seek(int64_t pos) {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "seek, pos=" << pos;
		int32_t ret = dii_player_->Seek(pos);
        if(ret < 0 ) {
            DII_LOG(LS_INFO, this->stream_id_, 0) << "seek failed, ret=" << ret;
        }
		return ret;
	}

	int64_t DiiPlayer::Position() {
		int64_t pos = dii_player_->Position();
		DII_LOG(LS_VERBOSE, this->stream_id_, 0) << "positon: " << pos;
		return pos;
	}

	int64_t DiiPlayer::Duration() {
		int64_t dur = dii_player_->Duration();
        DII_LOG(LS_VERBOSE, this->stream_id_, 0) << "duration: " << dur;
		return dur;
	}

    void DiiPlayer::SetMute(const bool mute) {
        DII_LOG(LS_INFO, this->stream_id_, 0) << "SetMute: " << mute;
        dii_player_->SetMute(mute);
    }

    int32_t DiiPlayer::Get10msAudioData(uint8_t* buffer, int32_t sample_rate, int32_t channel_nb) {
        return dii_player_->OnNeedPlayAudio(buffer,  sample_rate, channel_nb);
    }

	int32_t DiiPlayer::SetPlayerCallback(DiiPlayerCallback* callback) {
		DII_LOG(LS_INFO, this->stream_id_, 0) << "SetPlayerCallback, callback=" << callback;
		int ret = dii_player_->SetPlayerCallback(callback);
        if(ret < 0) {
            DII_LOG(LS_INFO, this->stream_id_, 0) << "SetPlayerCallback failed, ret=" << ret;
        }
		return ret;
	}

	int32_t DiiPlayer::ClearDisplayView(int32_t width, int32_t height, uint8_t r, uint8_t g, uint8_t b) {
        return 0;
	}

    int32_t DiiPlayer::SetPlayoutVolume(uint32_t vol) {
		LOG(LS_INFO) << "Set playout volume volume=" << vol;
        int ret = DiiMediaCore::SetPlayoutVolume(vol);
        if(ret < 0) {
            LOG(LS_ERROR) << "SetPlayoutVolume failed ret=" << ret;
        }
		return DII_DONE;
    }

	int32_t DiiPlayer::SetPlayoutDevice(const char* deviceId) {
		LOG(LS_INFO) << "SetPlayoutDevice, deviceId=" << deviceId;
        int ret = DiiMediaCore::SetPlayoutDevice(deviceId);
        if(ret < 0) {
            LOG(LS_ERROR) << "SetPlayoutDevice failed, ret:" << ret;
        }
		return DII_DONE;
	}
}
