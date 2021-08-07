//
//  dii_player.hpp
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#ifndef DII_MEDIA_PLAYER_KIT_DII_MEDIA_PLAYER
#define DII_MEDIA_PLAYER_KIT_DII_MEDIA_PLAYER

#include "dii_common.h"
#include <string>

namespace dii_media_kit  {
    class DiiMediaCore;
	class LIV_API DiiPlayer final {
	public:
		/**
		* @param render view handler.
		*/
        DiiPlayer(void* render, bool outputPcmForExternalMix = false);
        
        DiiPlayer(const DiiPlayer& ) = delete;
        DiiPlayer& operator=(const DiiPlayer& rhs) = delete;
		~DiiPlayer();

		/**
		* Open an input stream 
		*
		* @param url URL of the stream to open.
		* @param pos Time position(ms) to start.
		*
		* @return 0 on success < 0 on failure.
		*
		*/
		int32_t Start(const char* url, int64_t pos = 0, bool pause = false);
        int32_t Start(dii_radar::DiiRole role, const char * userId, const char* url, int64_t pos = 0, bool pause = false);

		int32_t Pause();
		int32_t Resume();
        int32_t SetLoop(bool loop);
		int32_t Stop();

		/**
		* Forward or back the current stream
		*
		* @param pos time position(ms) to seek.
		*
		* @return 0 on success < 0 on failure.
		*
		*/
		int32_t Seek(int64_t pos);

        void SetMute(const bool mute);
		int64_t Position();
		int64_t Duration();

        int32_t Get10msAudioData(uint8_t* buffer, int32_t sample_rate, int32_t channel_nb);
        int32_t SetPlayerCallback(DiiPlayerCallback* callback);
        int32_t ClearDisplayView(int32_t width = 640, int32_t height = 480, uint8_t r = 0, uint8_t g = 0, uint8_t b = 0);
    
        // support for windows & mac
        static int32_t SetPlayoutVolume(uint32_t vol);
		static int32_t SetPlayoutDevice(const char* deviceId);
	private:
		DiiMediaCore * dii_player_ = nullptr;
        int32_t stream_id_ = 0;
	};
}

#endif /* DII_MEDIA_PLAYER_KIT_DII_MEDIA_PLAYER */
