//
//  ViewController.m
//  LiveDemoMac
//
//  Created by devzhaoyou on 2019/8/1.
//  Copyright © 2019 devzhaoyou. All rights reserved.
//

#import "ViewController.h"
#import <DiiMediaKit_Mac/DiiPlayer.h>
#import <DiiMediaKit_Mac/dii_audio_device_interface.h>




class AudioRecordCallback:public dii_media_kit::DiiAudioRecordCallback {
    void OnRecordAudio(const void* audioSamples,
                       const size_t nSamples,
                       const size_t nBytesPerSample,
                       const size_t nChannels,
                       const uint32_t samplesPerSec,
                       const uint32_t totalDelayMS) {
        printf("Gezhaoyou hahahhaha\n");
    }};



@interface ViewController ()
{
    
}
@property (nonatomic, strong) NSTextField *urlTextField;
@property (nonatomic, strong) NSTextField *stateLabel;
@property (nonatomic, strong) NSButton *pullBtn;
@property (nonatomic, strong) NSButton *pushBtn;
@property (nonatomic, strong) NSButton *stopBtn;
@property (nonatomic, strong) NSView *renderView;
@property (nonatomic, strong) DiiPlayer* dii_player;
//@property (nonatomic, strong) RTMPGuestKit *guestKit;
//@property (nonatomic, strong) RTMPHosterKit *hosterKit;
@end

@implementation ViewController
{
    
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [self.view setFrame:CGRectMake(0, 0, 1280, 670)];

    [self.view addSubview:self.pullBtn];
    [self.view addSubview:self.pushBtn];
    [self.view addSubview:self.stopBtn];
    [self.view addSubview:self.urlTextField];
    [self.view addSubview:self.stateLabel];
    [self.view addSubview:self.renderView];
    
    [DiiMediaKit SetTraceLog:@"/Users/pixpark/repos/dii_media.log" Level:LS_INFO];
    
}

- (void)onPushBtnClicked {
    // push rtmp stream
//    self.hosterKit = [[RTMPHosterKit alloc] initWithDelegate:nil];
//    [self.hosterKit SetVideoMode:RTMP_Video_SD];
//    [self.hosterKit SetVideoCapturer:self.renderView andUseFront:YES];
//    [self.hosterKit StartPushRtmpStream:[self.urlTextField stringValue]];
//
    //
    [self.pushBtn setEnabled:NO];
    [self.pullBtn setEnabled:NO];
    [self.stopBtn setEnabled:YES];
}

- (void)onPullBtnClicked {
    // play rtmp stream
    [self.dii_player start:self.urlTextField.stringValue];
 
    [self.pushBtn setEnabled:NO];
    [self.pullBtn setEnabled:NO];
    [self.stopBtn setEnabled:YES];
}

- (void)onStopBtnClicked {
//    [self.guestKit StopRtmpPlay];
//    [self.guestKit clear];
//
//    [self.hosterKit StopRtmpStream];
//    [self.hosterKit clear];
    
    [self.pushBtn setEnabled:YES];
    [self.pullBtn setEnabled:YES];
    [self.stopBtn setEnabled:NO];
}


#pragma mark - RTMPGuestRtmpDelegate
- (void)OnRtmplayerOK {
    NSLog(@"OnRtmpStreamOK");
    dispatch_async(dispatch_get_main_queue(), ^{
//        self.stateRTMPLabel.text = @"连接RTMP服务成功";
    });
    
}
- (void)OnRtmplayerStatus:(int) cacheTime withBitrate:(int) curBitrate {
    //    NSLog(@"OnRtmplayerStatus:%d withBitrate:%d",cacheTime,curBitrate);
    dispatch_async(dispatch_get_main_queue(), ^{
        self.stateLabel.stringValue = [NSString stringWithFormat:@"RTMP缓存区:%d 码率:%d",cacheTime,curBitrate];
    });
}
- (void)OnRtmplayerCache:(int) time waitTime:(int)wait_time {
    NSLog(@"OnRtmplayerCache:%d wait time:%d",time, wait_time);
    dispatch_async(dispatch_get_main_queue(), ^{
//        self.stateRTMPLabel.text = [NSString stringWithFormat:@"RTMP正在缓存:%d, wait:%d秒", time, wait_time];
    });
}

- (void)OnRtmplayerClosed:(int) errcode {
    
}

- (void)OnGotRtmpSyncTime:(uint64_t) ts {
    //    NSLog(@"OnGotRtmpSyncTime:%llu", ts);
}

- (void)OnVideoResolutionChange: (int32_t) height Width:(int32_t) width {
    NSLog(@"OnVideoResolutionChange height:%d width:%d", height, width);
}


- (void)setRepresentedObject:(id)representedObject {
    [super setRepresentedObject:representedObject];

    // Update the view, if already loaded.
}

- (NSView*) renderView {
    if(!_renderView) {
        _renderView = [[NSView alloc] initWithFrame:CGRectMake(0, 0, CGRectGetWidth(self.view.frame), CGRectGetMinY(self.urlTextField.frame) - 10)];
    }
    return _renderView;
}

//
- (NSTextField*)urlTextField {
    if (!_urlTextField) {
        _urlTextField = [[NSTextField alloc] initWithFrame:CGRectMake(10, CGRectGetHeight(self.view.frame), CGRectGetWidth(self.view.frame) - 450, 20)];
        _urlTextField.stringValue  = @"https://outin-cf17324cfc8311e9a7ef00163e1c7426.oss-cn-shanghai.aliyuncs.com/b64f0e22802d431da55d0f237c2eaee4/307d36174e4840aba962919f298d4734-00c54aa955da342ff52675c5ea73e548-ld.m3u8";
    }
    return _urlTextField;
}

- (NSTextField*)stateLabel {
    if (!_stateLabel) {
        _stateLabel = [[NSTextField alloc] initWithFrame:CGRectMake(CGRectGetMaxX(self.urlTextField.frame) + 10, CGRectGetHeight(self.view.frame), 200, 20)];
        _stateLabel.editable = NO;
        _stateLabel.bordered = NO;
        _stateLabel.stringValue = @"测试";
    }
    return _stateLabel;
}

- (NSButton*)pushBtn {
    if (!_pushBtn) {
        _pushBtn = [[NSButton alloc] initWithFrame: CGRectMake(CGRectGetMaxX(self.stateLabel.frame) + 10, CGRectGetMinY(self.urlTextField.frame), 60, 20)];
        [_pushBtn setTarget:self];
        [_pushBtn setAction:@selector(onPushBtnClicked)];
        [_pushBtn setTitle:@"推流"];
    }
    return _pushBtn;
}

- (NSButton*)pullBtn {
    if (!_pullBtn) {
        _pullBtn = [[NSButton alloc] initWithFrame: CGRectMake(CGRectGetMaxX(self.stateLabel.frame) + 80, CGRectGetMinY(self.urlTextField.frame), 60, 20)];
        [_pullBtn setTarget:self];
        [_pullBtn setAction:@selector(onPullBtnClicked)];
        [_pullBtn setTitle:@"播放"];
    }
    return _pullBtn;
}

- (NSButton*)stopBtn {
    if (!_stopBtn) {
        _stopBtn = [[NSButton alloc] initWithFrame: CGRectMake(CGRectGetMaxX(self.stateLabel.frame) + 150, CGRectGetMinY(self.urlTextField.frame), 60, 20)];
        [_stopBtn setTarget:self];
        [_stopBtn setAction:@selector(onStopBtnClicked)];
        [_stopBtn setTitle:@"停止"];
        [_stopBtn setEnabled:NO];
    }
    return _stopBtn;
}

-(DiiPlayer*)dii_player {
   if(!_dii_player) {
        _dii_player = [[DiiPlayer alloc] initWithRender:self.renderView];
       [_dii_player setUserInfo:_UserRole_Teacher userid:"userid"];
       
       
       DiiPlayerCallback callback;
       callback.state_callback_ = ^(DiiPlayerState state, int stateCode, NSString *msg) {
//            NSLog(@"state: %d, code: %d, msg: %@", state, stateCode, msg);
       };

//       [_dii_player setCallback:callback];
    }
    return _dii_player;
}

@end
