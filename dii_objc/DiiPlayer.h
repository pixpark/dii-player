//
//  DiiMediaPlayer.h
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#import <Foundation/Foundation.h>

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#elif TARGET_OS_MAC
#include <Cocoa/Cocoa.h>
#endif

typedef enum {
    LS_NONE = 0,
    LS_VERBOSE,
    LS_INFO,
    LS_WARNING,
    LS_ERROR,
} DiiLogSeverity; // 日志级别

@interface DiiMediaKit : NSObject

+(NSString*)version;

/* 写日志
** @param path 日志完整路径
* @param severity 日志级别，建议 LS_INFO 级别 */
+(int)SetTraceLog:(NSString*) path Level:(DiiLogSeverity) severity;


/* 控制台打印调试日志级别
* @param severity 推荐 LS_INFO 级别 */
+(void)SetDebugLog:(DiiLogSeverity) severity;
+(void)SetPlayoutVolume:(int) vol;
@end

#pragma

typedef enum  {
    DII_STATE_ERROR = 0,
    DII_STATE_PLAYING,            // 正在播放
    DII_STATE_STOPPED,            // 停止
    DII_STATE_PAUSED,             // 暂停
    DII_STATE_SEEKING,            // SEEK
    DII_STATE_BUFFERING,          // 缓冲
    DII_STATE_STUCK,              // 卡顿（可在此状态切CND）
    DII_STATE_FINISH              // 播完
} DiiPlayerState; // 播放器回调状态

typedef enum {
    STUCK,      // 卡顿
    NORMAL,     // 一般
    FLUENCY     // 流畅
} DiiFluency; // 流畅度（尚不支持）

typedef struct {
    // video
    int32_t video_width_;
    int32_t video_height_;
    int32_t video_decode_framerate;
    int32_t video_render_framerate;

    // audio
    int32_t audio_samplerate_;
    
    // network
    int32_t cache_len_;
    int32_t audio_bps;
    int32_t video_bps;
      
    // 流畅度
    DiiFluency fluency;
} DiiPlayerStatistics; // 播放器统计项

typedef enum {
    _UserRole_Unknown = 0,//未设置role
    _UserRole_Student = 1,
    _UserRole_Teacher = 2,
    _UserRole_Assistant = 3
}DiiUserRole;

typedef void(^StateCallback)(DiiPlayerState state, int stateCode, NSString *msg);
typedef void(^StatisticsCallback)(DiiPlayerStatistics statistics);
typedef void(^SyncTimestampCallback)(uint64_t ts);
typedef void(^ResolutionCallback)(int32_t width, int32_t height);

typedef struct {
    StateCallback state_callback_;              // 播放器状态回调
    SyncTimestampCallback sync_ts_callback_;    // rtmp 同步时间戳回调
    StatisticsCallback statistics_callback_;    // 播放器统计回调
    ResolutionCallback resolution_callback_;    // 视频分辨率改变回调
} DiiPlayerCallback;

@interface DiiPlayer : NSObject
-(instancetype)init;



#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
-(instancetype)initWithRender:(UIView*)render;
-(instancetype)initWithRender:(UIView*)render externalmix:(bool)outputPcmForExternalMix;
#elif TARGET_OS_MAC
-(instancetype)initWithRender:(NSView*) render;
-(instancetype)initWithRender:(NSView*) render externalmix:(bool)outputPcmForExternalMix;
#endif

/**
 * 播放器回调设置
 @param callback See DiiPlayerCallback*/
-(void)setCallback:(DiiPlayerCallback) callback;


/* 开始播放，支持 rtmp，hls, http-flv, mp4本地文件
 * @param url 媒体链接地址或本地文件路径 */
-(int)start:(NSString*)url;


/// 从指定时间点开始播放
/// @param url 路径或链接
/// @param pos 开始播放的时间点，单位毫秒
-(int)start:(NSString*)url from:(int64_t)pos;

/// 暂停到某个时间点画面
/// @param url 路径或链接
/// @param pos 暂停的时间点
/// @param pause 是否暂停
-(int)start:(NSString*)url from:(int64_t)pos pause:(bool)pause;


-(int)start:(NSString*)url role:(DiiUserRole)role userid:(const char*)userid;
-(int)start:(NSString*)url from:(int64_t)pos role:(DiiUserRole)role userid:(const char*)userid;
-(int)start:(NSString*)url from:(int64_t)pos pause:(bool)pause role:(DiiUserRole)role userid:(const char*)userid;

/*
 *暂停*/
-(int)pause;

/*
 * 恢复
 */
-(int)resume;

/*
 * 停止 */
-(int)stop;

// pos 毫秒
-(int)seek:(int64_t)pos;

// 当前播放位置，毫秒
-(int64_t)position;

// 媒体时长，毫秒，不支持实时流，如rtmp, rtsp
-(int64_t)duration;

// 循环播放自
-(int)setLoop:(Boolean)loop;

//-(void)SetVolume:(int) vol;
-(void)SetMute:(bool)mute;

//声网主动调此接口完成混音
-(int32_t)Get10msAudioData:(uint8_t*)buffer sample_rate:(int32_t)sample_rate channel_nb:(int32_t)channel_nb;

/**
 * 清空view, 黑色
 */
-(int32_t)clearDisplay;

/**
* rgba 指定颜色清空view
*/
-(int32_t)clearDisplay:(int32_t)width height:(int32_t)height r:(uint8_t)r g:(uint8_t)g b:(uint8_t)b;
@end

