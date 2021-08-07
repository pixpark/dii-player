//
//  DiiMediaPlayer.m
//  DiiPlayerKit
//
//  Created by devzhaoyou on 2019/10/26.
//  Copyright © 2019 pixpark. All rights reserved.
//

#import "DiiPlayer.h"
#include "dii_common.h"
#include "dii_player.h"


#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#import "WebRTC/RTCEAGLVideoView.h"
#else
#import "WebRTC/RTCNSGLVideoView.h"
#endif

#pragma mark - DiiMediaPlayer
@interface DiiPlayer() {
    dii_media_kit::DiiPlayer *dii_player;
}

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
@property (nonatomic, strong)  RTCEAGLVideoView *renderView;
#else
@property (nonatomic, strong)  RTCNSGLVideoView *renderView;
#endif
//
//#ifdef DII_MAC
//@property (nonatomic,weak) NSView *parView;
//@property (nonatomic, strong)  RTCNSGLVideoView *videoShowView;
//#endif
//
//@property (nonatomic, assign) CGSize showVideoSize; // 显示视频的窗口大小
@end

@implementation DiiPlayer

-(instancetype)init {
    return [self initWithRender:nil];
}

#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
-(instancetype)initWithRender:(UIView*)render {
#else
-(instancetype)initWithRender:(NSView*) render{
#endif
    return [self initWithRender:render externalmix:false];
    }


#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
-(instancetype)initWithRender:(UIView*)render externalmix:(bool)outputPcmForExternalMix {
#else
-(instancetype)initWithRender:(NSView*) render externalmix:(bool)outputPcmForExternalMix{
#endif
        if(self = [super init]) {
            if(render) {
                dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
                    self.renderView = [[RTCEAGLVideoView alloc] initWithFrame:render.bounds];
                    self.renderView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
#else
                    NSOpenGLPixelFormatAttribute attributes[] = {
                        NSOpenGLPFADoubleBuffer,
                        NSOpenGLPFADepthSize, 24,
                        NSOpenGLPFAOpenGLProfile,
                        NSOpenGLProfileVersion3_2Core,
                        0
                    };
                    NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
                    self.renderView =  [[RTCNSGLVideoView alloc] initWithFrame:render.bounds pixelFormat:pixelFormat];
                    [self.renderView setAutoresizingMask:NSViewHeightSizable | NSViewWidthSizable];
#endif
                    [render addSubview:self.renderView];
                    self->dii_player = new dii_media_kit::DiiPlayer((__bridge void*)self.renderView, outputPcmForExternalMix);
                });
            } else {
                dii_player = new dii_media_kit::DiiPlayer(nullptr, outputPcmForExternalMix);
            }
        }
        return self;
    }
    
    
    
    -(void)unint {
   
    }
    
    -(void)dealloc {
        if(self.renderView) {
            [self.renderView performSelectorOnMainThread:@selector(removeFromSuperview) withObject:nil waitUntilDone:YES];
            self.renderView = nil;
        }
        delete self->dii_player;
        self->dii_player = nullptr;
    }
    
    -(void)setCallback:(DiiPlayerCallback)callback {
        dispatch_async(dispatch_get_main_queue(), ^{
            dii_media_kit::DiiPlayerCallback cb;
            if(callback.state_callback_) {
                cb.state_callback = [callback](dii_media_kit::DiiPlayerState state, int32_t code, const char* msg, void* custom_data) {
                    callback.state_callback_(DiiPlayerState(state), code, [NSString stringWithUTF8String:msg]);
                };
            }
            
            if(callback.sync_ts_callback_) {
                cb.sync_ts_callback = [callback](uint64_t ts) {
                    callback.sync_ts_callback_(ts);
                };
            }
            
            if(callback.resolution_callback_) {
                cb.resolution_callback = [callback](int32_t width, int32_t height) {
                    callback.resolution_callback_(width, height);
                };
            }
            
            if(callback.statistics_callback_) {
                cb.statistics_callback = [callback](dii_media_kit::DiiPlayerStatistics& statistics) {
                    DiiPlayerStatistics st;
                    st.audio_samplerate_        = statistics.audio_samplerate_;
                    st.video_render_framerate   = statistics.video_render_framerate;
                    st.video_decode_framerate   = statistics.video_decode_framerate;
                    st.video_height_            = statistics.video_height_;
                    st.video_width_             = statistics.video_width_;
                    st.cache_len_               = statistics.cache_len_;
                    st.audio_bps                = statistics.audio_bps_;
                    st.video_bps                = statistics.video_bps_;
                    st.fluency                  = (DiiFluency)statistics.fluency;
                    
                    callback.statistics_callback_(st);
                };
            }
            
            self->dii_player->SetPlayerCallback(&cb);
        });
    }
    
    -(int)start:(NSString*)url {
        dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
            [UIApplication sharedApplication].idleTimerDisabled = YES;
#endif
            self->dii_player->Start([url UTF8String]);
        });
        return 0;
    }
    
    -(int)start:(NSString*)url from:(int64_t)pos {
        dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
            [UIApplication sharedApplication].idleTimerDisabled = YES;
#endif
            self->dii_player->Start([url UTF8String], pos);
        });
        return 0;
    }
    
    -(int)start:(NSString*)url from:(int64_t)pos pause:(bool)pause {
        dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
            [UIApplication sharedApplication].idleTimerDisabled = YES;
#endif
            self->dii_player->Start([url UTF8String], pos, pause);
        });
        return 0;
    }
    
    -(int)start:(NSString*)url role:(DiiUserRole)role userid:(const char*)userid{
        return [self start:url from:0 role:role userid:userid];
    }
    
    -(int)start:(NSString*)url from:(int64_t)pos role:(DiiUserRole)role userid:(const char*)userid{
        return [self start:url from:pos pause:false role:role userid:userid];
    }
    
    -(int)start:(NSString*)url from:(int64_t)pos pause:(bool)pause role:(DiiUserRole)role userid:(const char*)userid{
        dii_radar::DiiRole dii_role = dii_radar::_Role_Unknown;
        if(role == _UserRole_Teacher){
            dii_role = dii_radar::_Role_Teacher;
        }else if(role == _UserRole_Student){
            dii_role = dii_radar::_Role_Student;
        }else if(role == _UserRole_Assistant){
            dii_role = dii_radar::_Role_Assistant;
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
#if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
            [UIApplication sharedApplication].idleTimerDisabled = YES;
#endif
            self->dii_player->Start(dii_role, userid, [url UTF8String], pos, pause);
        });
        return 0;
    }
    
    
    -(int)pause {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->Pause();
        });
        return 0;
    }
    
    -(int)resume {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->Resume();
        });
        return 0;
    }
    
    -(int)setLoop:(Boolean)loop {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->SetLoop(loop);
        });
        return 0;
    }
    
    -(int)stop {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->Stop();
        });
        return 0;
    }
    
    -(int)seek:(int64_t)pos {
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->Seek(pos);
        });
        return 0;
    }
    
    -(int64_t)position {
        if(dii_player)
            return dii_player->Position();
        else
            return 0;
    }
    
    -(int64_t)duration {
        if(dii_player)
            return dii_player->Duration();
        else
            return 0;
    }
    
    -(int32_t)Get10msAudioData:(uint8_t*)buffer sample_rate:(int32_t)sample_rate channel_nb:(int32_t)channel_nb {
        if(dii_player) {
            return dii_player->Get10msAudioData(buffer, sample_rate, channel_nb);
        }
        return 0;
    }
    
    -(int32_t)clearDisplay {
        return 0;
    }
    
    -(int32_t)clearDisplay:(int32_t)width height:(int32_t)height r:(uint8_t)r g:(uint8_t)g b:(uint8_t)b {
        return 0;
    }
    
    -(void)SetVolume:(int) vol{
        dispatch_async(dispatch_get_main_queue(), ^{
            dii_media_kit::DiiPlayer::SetPlayoutVolume(vol);
        });
        
    }
    
    -(void)SetMute:(bool)mute{
        dispatch_async(dispatch_get_main_queue(), ^{
            self->dii_player->SetMute(mute);
        });
    }
    
    @end
    
    @implementation DiiMediaKit
    +(NSString*)version {
        NSString *ver = [NSString stringWithUTF8String:dii_media_kit::DiiMediaKit::Version()];
        return ver;
    }
    
    +(int)SetTraceLog:(NSString*) path Level:(DiiLogSeverity) severity {
        dii_media_kit::LogSeverity lv;
        switch (severity) {
            case LS_NONE:
                lv = dii_media_kit::LOG_NONE;
                break;
            case LS_VERBOSE:
                lv = dii_media_kit::LOG_VERBOSE;
                break;
            case LS_INFO:
                lv = dii_media_kit::LOG_INFO;
                break;
            case LS_WARNING:
                lv = dii_media_kit::LOG_WARNING;
                break;
            case LS_ERROR:
                lv = dii_media_kit::LOG_ERROR;
                break;
            default:
                lv = dii_media_kit::LOG_NONE;
                break;
        }
        return dii_media_kit::DiiMediaKit::SetTraceLog([path UTF8String], lv);
    }
    
    +(void)SetDebugLog:(DiiLogSeverity) severity {
        dii_media_kit::LogSeverity lv;
        switch (severity) {
            case LS_NONE:
                lv = dii_media_kit::LOG_NONE;
                break;
            case LS_VERBOSE:
                lv = dii_media_kit::LOG_VERBOSE;
                break;
            case LS_INFO:
                lv = dii_media_kit::LOG_INFO;
                break;
            case LS_WARNING:
                lv = dii_media_kit::LOG_WARNING;
                break;
            case LS_ERROR:
                lv = dii_media_kit::LOG_ERROR;
                break;
            default:
                lv = dii_media_kit::LOG_NONE;
                break;
        }
        dii_media_kit::DiiMediaKit::SetDebugLog(lv);
    }
    @end
