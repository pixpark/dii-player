#ifndef __DII_COMMON_H__
#define __DII_COMMON_H__

#include <stdint.h>
#include <functional>
#include <string>

namespace dii_radar {
typedef enum {
    _Role_Unknown = 0,//未设置role
    _Role_Student = 1,
    _Role_Teacher = 2,
    _Role_Assistant = 3,
}DiiRole;

    struct AudioFrameMatedata final {
        DiiRole role;
        std::string streamid;
        std::string userid;
        int64_t pts;
        int32_t samplerate;
        int32_t channels;
        int32_t bytesPerSample;
        int32_t volume;
    };

    struct VideoFrameMatedata final {
        DiiRole role;
        std::string streamid;
        std::string userid;
        int64_t pts;
        int32_t width;
        int32_t height;
        int32_t rotation;
		int64_t start_to_render_time;
		bool render_time_flg;
    };

    // 音频帧信息 callback
    typedef std::function<void (const AudioFrameMatedata& frame)> DiiAudioFrameMetadataCallback;
    // 视频帧信息 callback
    typedef std::function<void (const VideoFrameMatedata& frame)> DiiVideoFrameMetadataCallback;

 
    typedef struct DiiRadarCallback {
        DiiAudioFrameMetadataCallback         RenderAudioCallback; // 音频渲染帧信息callback
        DiiVideoFrameMetadataCallback         RenderVideoCallback; // 视频渲染帧信息callback

        DiiRadarCallback() {
            memset(this, 0, sizeof(DiiRadarCallback));
        }
    } DiiRadarCallback; // 播放器帧元数据 Callback
}

namespace dii_media_kit {

#define DII_MEDIA_KIT_VERSION "dii_media_version v0.1.7.1"

/*
#if defined(_WIN32)
#ifdef LIV_EXPORTS
#define LIV_API _declspec(dllexport)
#else
#define LIV_API _declspec(dllimport)
#endif
#else
#define LIV_API
#endif
*/

#ifdef LIV_EXPORTS
#define LIV_API _declspec(dllexport)
#elif LIV_DLL
#define LIV_API _declspec(dllimport)
#else
#define LIV_API
#endif


//
#define DII_RESOURCE_ERROR	-3
#define DII_PARAMETER_ERROR	-2
#define DII_ERROR				-1
#define DII_DONE				0
#define DII_ALREADY_DONE		1
#define DII_WARNING			2

   typedef enum {
        DII_STATE_ERROR = 0,
        DII_STATE_PLAYING,            // 正在播放
        DII_STATE_STOPPED,            // 停止
        DII_STATE_PAUSED,             // 暂停
        DII_STATE_SEEKING,            // 跳转
        DII_STATE_BUFFERING,          // 缓冲
        DII_STATE_STUCK,              // 卡顿（可在此状态切CND）
        DII_STATE_FINISH              // 播完
    } DiiPlayerState; // 播放器状态

    typedef enum {
        STUCK,      // 卡顿
        NORMAL,     // 一般
        FLUENCY     // 流畅
    } DiiFluency; // 流畅度(尚不支持)

	enum DiiAudioDevice {
		DEVICE_NONE = 0,
		DEVICE_MIC,
		DEVICE_SPEAKER,
		DEVICE_MIC_SPEAKER,
	};

    typedef struct DiiPlayerStatistics {
        int32_t stream_id;
        // video
        int32_t video_width_;
        int32_t video_height_;
        int32_t video_decode_framerate;
        int32_t video_render_framerate;

        // audio
        int32_t audio_samplerate_ = 0;

        // network
        int32_t cache_len_;
        int32_t audio_bps_;
        int32_t video_bps_;
        
        int64_t sync_ts_;


		int64_t start_to_render_time_;
        // 流畅度
        DiiFluency fluency;
        
        DiiPlayerStatistics() {
            memset(this, 0, sizeof(DiiPlayerStatistics));
        }
    } DiiPlayerStatistics; // 播放器状态统计

    enum DiiVideoFrameType {
        TYPE_YUV420 = 0,  // YUV 420 format
        TYPE_RGBA32 = 1,  // RGBA 8888 format
    };
    /** Video frame information. The video data format is YUV420. The buffer provides a pointer to a pointer. The interface cannot modify the pointer of the buffer, but can modify the content of the buffer only.
    */
    struct DiiVideoFrame {
        DiiVideoFrameType type;
        /** Video pixel width.
        */
        int width;  //width of video frame
        /** Video pixel height.
        */
        int height;  //height of video frame

        // -------- apply for yuv frame ---------
        /** Line span of the Y buffer within the YUV data.
        */
        int y_stride;  //stride of Y data buffer
        /** Line span of the U buffer within the YUV data.
        */
        int u_stride;  //stride of U data buffer
        /** Line span of the V buffer within the YUV data.
        */
        int v_stride;  //stride of V data buffer
        /** Pointer to the Y buffer pointer within the YUV data.
        */
        void* y_buffer;  //Y data buffer
        /** Pointer to the U buffer pointer within the YUV data.
        */
        void* u_buffer;  //U data buffer
        /** Pointer to the V buffer pointer within the YUV data.
        */
        void* v_buffer;  //V data buffer

        // -------- apply for rgba frame ---------

        /** Pointer to the rgba buffer pointer within the rgba data.
        */
        void* rgba_buffer;
        /** rgba data length
        */
        int rgba_buffer_len;


        /** Set the rotation of this frame before rendering the video. Supports 0, 90, 180, 270 degrees clockwise.
        */
        int rotation; // rotation of this frame (0, 90, 180, 270)

        /** Timestamp (ms) for the video stream render. Use this timestamp to synchronize the video stream render while rendering the video streams.
        @note This timestamp is for rendering the video stream, and not for capturing the video stream.
        */
        int64_t render_time_ms;
    };

    typedef std::function<void (DiiVideoFrame& frame, void* custom)> DiiVideoFrameCallback;
    typedef std::function<void (DiiPlayerStatistics& statistics)> DiiPlayerStatisticsCallback;
    typedef std::function<void (int32_t width, int32_t height)> DiiResolutionCallback;
    typedef std::function<void (uint64_t ts)> DiiSyncTimestampCallback;
    // state: 播放器状态，code: 状态码/错误码, msg: 状态信息, custom_data: 自定义信息
    typedef std::function<void (DiiPlayerState state, int32_t code, const char* msg, void* custom_data)> DiiPlayerStateCallback;
    
    typedef struct DiiPlayerCallback {
        DiiVideoFrameCallback         video_frame_callback;// 播放器视频帧回调
        DiiPlayerStateCallback        state_callback;      // 播放器状态回调
        DiiSyncTimestampCallback      sync_ts_callback;    // rtmp 同步时间戳回调
        DiiResolutionCallback         resolution_callback; // 视频分辨率变更回调
        DiiPlayerStatisticsCallback   statistics_callback; // 播放器统计回调
        void* custom_data;                                   // 自定义数据端，回调会原样带回该指针
        
        DiiPlayerCallback() {
            memset(this, 0, sizeof(DiiPlayerCallback));
        }
    } DiiPlayerCallback; // 播放器回调

    typedef std::function<void (const void* audioSamples,
                     const size_t nSamples,
                     const size_t nBytesPerSample,
                     const size_t nChannels,
                     const uint32_t samplesPerSec,
                     const uint32_t totalDelayMS)> DiiAudioRecorderCallback; //录音回调

    typedef struct DiiEventTracking {
        int line;
        const char* func;
        const char* msgs;
        int code;
        
        DiiEventTracking() {
            memset(this, 0, sizeof(DiiEventTracking));
        }
    } DiiTrackEvent;
    typedef std::function<void (DiiTrackEvent event)> DiiEventTrackingCallback; //打点回调

    typedef enum {
        LOG_NONE = 0,
        LOG_ERROR,
        LOG_WARNING,
        LOG_INFO,
        LOG_VERBOSE
    } LogSeverity; // 日志级别
    
    class LIV_API DiiMediaKit {
    public:
        // 获取库版本
        static const char* Version();
        // 写日志，路径和级别， 推荐 LOG_INFO
        static int32_t SetTraceLog(const char* path, LogSeverity severity);
        // 调试(终端打印)日志级别，推荐 LOG_INFO
        static void SetDebugLog(LogSeverity severity);
        static void SetExternalStatisticsCallback(DiiPlayerStatisticsCallback callback);
        static void SetEventTrackinglCallback(DiiEventTrackingCallback callback);
        
        // set radar callback
        static int SetRadarCallback(dii_radar::DiiRadarCallback callback);
    };
}
#endif    // __DII_COMMON_H__
