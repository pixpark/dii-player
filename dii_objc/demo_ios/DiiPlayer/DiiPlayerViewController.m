//
//  RtmpPullViewController.m
//  DiiRtmpLiveDemo
//
//  Created by pixpark on 16/9/19.
//  Copyright © 2016年 Dii. All rights reserved.
//

#import "DiiPlayerViewController.h"
#import <DiiMediaKit/DiiPlayer.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <sys/utsname.h>


@interface DiiPlayerViewController() {
    int64_t media_dur ;
    bool stop_timer_update;
    bool started;
}

@property (nonatomic, strong) UIButton *btnStart;
@property (nonatomic, strong) UIButton *btnPause;
@property (nonatomic, strong) UIButton *btnResume;
@property (nonatomic, strong) UIButton *btnStop;
@property (nonatomic, strong) UISlider* sliderBar;
@property (nonatomic, strong) UITextView* curPosLabel;
@property (nonatomic, strong) NSTimer *timer;

@property (nonatomic, strong) UIView *renderView;
@property (nonatomic, strong) DiiPlayer* dii_player;
@property (nonatomic, strong) NSString* fileOrUrl;
@property (nonatomic, strong) NSMutableArray* urlList;
@property (nonatomic) int urlIndex;

@end

@implementation DiiPlayerViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.urlList = [[NSMutableArray alloc] initWithObjects:
                    @"http://vfx.mtime.cn/Video/2019/03/14/mp4/190314223540373995.mp4", //vod
                    @"https://v-cdn.zjol.com.cn/276982.mp4", // vod
                    @"rtmp://ali.fifo.site/live/stream", // rtmp live
                    @"http://ivi.bupt.edu.cn/hls/cctv1hd.m3u8", // cctv1, hls live
                    nil];
    
    // UI
    self.view.backgroundColor = [UIColor whiteColor];
    [self.navigationController setNavigationBarHidden:YES];
    [self.view addSubview:self.renderView]; //video render View
    
    // Button
    [self.view addSubview:self.btnStart];
    [self.view addSubview:self.btnPause];
    [self.view addSubview:self.btnResume];
    [self.view addSubview:self.btnStop];
    [self.view addSubview:self.sliderBar];
    [self.view addSubview:self.curPosLabel];
    
    NSArray *logpaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask, YES);
    NSString *documentsDirectory = [logpaths objectAtIndex:0];
    NSString* ocLogFilePath = [documentsDirectory stringByAppendingPathComponent:@"dii_player.log"];
    // 写日志
    [DiiMediaKit  SetTraceLog:ocLogFilePath Level:LS_INFO];
    // 终端日志
    [DiiMediaKit SetDebugLog:LS_INFO];
    
    // 开启定时器，更新进度条
    self.timer = [NSTimer timerWithTimeInterval:0.2 target:self selector:@selector(timerRun) userInfo:nil repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:self.timer forMode:NSDefaultRunLoopMode];
}

#pragma mark - button event -
- (void)onBtnClicked:(UIButton*)sender {
    if(_btnStart == sender) {
        [self.dii_player start:self.urlList[self.urlIndex]];
    } else if(_btnPause == sender) {
        [self.dii_player pause];
    } else if(_btnResume == sender) {
        [self.dii_player resume];
    } else if(_btnStop == sender) {
        [self.dii_player stop];
    }
}

-(void)timerRun {
    int64_t pos = [self.dii_player position];
    media_dur = [self.dii_player duration];
    if(media_dur > 0) {
        if(!stop_timer_update)
            self.sliderBar.value = 100*(float)pos/media_dur;
        
        // update lebel time
        char str_pos[20] = {0};
        int64_t sec = pos/1000;
        int64_t msec = pos%1000;
        sprintf(str_pos,"%02lld:%02lld:%02lld", sec/60, sec%60, msec/10);
        _curPosLabel.text = [[NSString alloc] initWithCString:(const char*)str_pos encoding:NSASCIIStringEncoding];
    }
}

-(void)updateValue:(id)sender {
    [self.dii_player seek:self.sliderBar.value/100.0*media_dur];
}

-(void)sliderTouchDown: (id)sender {
    stop_timer_update = true;
}

-(void)sliderTouchUpInSide: (id)sender {
    stop_timer_update = false;
}

-(DiiPlayer*)dii_player {
    if(!_dii_player) {
        _dii_player = [[DiiPlayer alloc] initWithRender:self.renderView externalmix:false];
        
        DiiPlayerCallback callback;
        callback.state_callback_ = ^(DiiPlayerState state, int stateCode, NSString *msg) {
            NSLog(@"state: %d, code: %d, msg: %@", state, stateCode, msg);
            if(DII_STATE_PLAYING == state){
                
            }
        };
        [_dii_player setCallback:callback];
    }
    return _dii_player;
}

- (UIView*)renderView {
    if (!_renderView) {
        _renderView = [[UIView alloc] initWithFrame:CGRectMake(0, 200, CGRectGetWidth(self.view.frame), 300)];
    }
    return _renderView;
}

- (UIButton*)btnStart {
    if(!_btnStart) {
        _btnStart = [UIButton buttonWithType:UIButtonTypeSystem];
        [_btnStart setTitle:@"开始" forState:UIControlStateNormal];
        [_btnStart.layer setCornerRadius:5.0];
        _btnStart.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _btnStart.layer.borderWidth = .5;
        [_btnStart addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _btnStart.frame = CGRectMake(40, 100, 70, 40);
    }
    
    return _btnStart;
}

- (UIButton*)btnPause {
    if(!_btnPause) {
        _btnPause = [UIButton buttonWithType:UIButtonTypeSystem];
        [_btnPause setTitle:@"暂停" forState:UIControlStateNormal];
        [_btnPause.layer setCornerRadius:5.0];
        _btnPause.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _btnPause.layer.borderWidth = .5;
        [_btnPause addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _btnPause.frame = CGRectMake(130, 100, 70,40);
    }
    return _btnPause;
}

- (UIButton*)btnResume {
    if(!_btnResume) {
        _btnResume = [UIButton buttonWithType:UIButtonTypeSystem];
        [_btnResume setTitle:@"恢复" forState:UIControlStateNormal];
        [_btnResume addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _btnResume.frame = CGRectMake(220, 100, 70,40);
        [_btnResume.layer setCornerRadius:5.0];
        _btnResume.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _btnResume.layer.borderWidth = .5;
    }
    return _btnResume;
}

- (UIButton*)btnStop {
    if(!_btnStop) {
        _btnStop = [UIButton buttonWithType:UIButtonTypeSystem];
        [_btnStop setTitle:@"停止" forState:UIControlStateNormal];
        [_btnStop addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _btnStop.frame = CGRectMake(310, 100, 70,40);
        [_btnStop.layer setCornerRadius:5.0];
        _btnStop.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _btnStop.layer.borderWidth = .5;
    }
    return _btnStop;
}

-(UISlider*)sliderBar {
    if (!_sliderBar) {
        _sliderBar = [[UISlider alloc] initWithFrame:CGRectMake(10, self.view.frame.size.height - 100, self.view.frame.size.width-20, 40)];
        _sliderBar.continuous = NO;
        _sliderBar.minimumValue = 0;
        _sliderBar.maximumValue = 100;
        [_sliderBar addTarget:self action:@selector(updateValue:) forControlEvents:UIControlEventValueChanged];
        [_sliderBar addTarget:self action:@selector(sliderTouchDown:) forControlEvents:UIControlEventTouchDown];
        [_sliderBar addTarget:self action:@selector(sliderTouchUpInSide:) forControlEvents:UIControlEventTouchUpInside];
    }
    return _sliderBar;
}

-(UITextView*)curPosLabel {
    if(!_curPosLabel) {
        _curPosLabel = [[UITextView alloc] initWithFrame:CGRectMake(10, self.view.frame.size.height - 125, 100, 20)];
        _curPosLabel.text = @"00:00:00";
    }
    return _curPosLabel;
}

- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
