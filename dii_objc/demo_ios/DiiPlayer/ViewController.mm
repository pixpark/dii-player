//
//  ViewController.m
//  DiiRtmpLiveDemo
//
//  Created by pixpark on 16/9/16.
//  Copyright © 2016年 pixpark. All rights reserved.
//

#import "ViewController.h"
#import "DiiPlayerViewController.h"

#define BaiduURL @"https://www.baidu.com"

@interface ViewController ()
@property (nonatomic, strong) UIButton *playerBtn;
@property (nonatomic, strong) UIButton *shortVideoBtn;
@property (nonatomic, strong) UILabel *powerLabel;

@end

@implementation ViewController
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.navigationController setNavigationBarHidden:YES];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    [self requestNetworkAccess];
    [self.view addSubview:self.playerBtn];
    [self.view addSubview:self.shortVideoBtn];
    [self.view addSubview:self.powerLabel];
}

-(void)requestNetworkAccess {
    NSURLRequest *request = [NSURLRequest requestWithURL:[NSURL URLWithString:BaiduURL]];
    NSURLSession *session = [NSURLSession sharedSession];
    NSURLSessionDataTask *dataTask = [session dataTaskWithRequest:request completionHandler:^(NSData * _Nullable data, NSURLResponse * _Nullable response, NSError * _Nullable error) {
        //do nothing
    }];
    
    [dataTask resume];
}
 
- (void)onBtnClicked:(UIButton*)sender {
    DiiPlayerViewController *vc  = [DiiPlayerViewController new];
    [self.navigationController pushViewController:vc animated:YES];
}
 
- (UIButton*)playerBtn {
    if (!_playerBtn) {
        _playerBtn = [UIButton buttonWithType:UIButtonTypeSystem];
        [_playerBtn addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _playerBtn.frame = CGRectMake((CGRectGetWidth(self.view.frame) - 200)/2, (CGRectGetHeight(self.view.frame))/2 - 50, 200, 45);
        _playerBtn.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _playerBtn.layer.borderWidth = .5;
        [_playerBtn setTitle:@"直播/点播" forState:UIControlStateNormal];
        [_playerBtn.layer setCornerRadius:8.0];
    }
    return _playerBtn;
}

- (UIButton*)shortVideoBtn {
    if (!_shortVideoBtn) {
        _shortVideoBtn = [UIButton buttonWithType:UIButtonTypeSystem];
        [_shortVideoBtn addTarget:self action:@selector(onBtnClicked:) forControlEvents:UIControlEventTouchUpInside];
        _shortVideoBtn.frame = CGRectMake((CGRectGetWidth(self.view.frame) - 200)/2, (CGRectGetHeight(self.view.frame))/2 + 50, 200, 45);
        _shortVideoBtn.layer.borderColor = [UIColor systemBlueColor].CGColor;
        _shortVideoBtn.layer.borderWidth = .5;
        [_shortVideoBtn setTitle:@"短视频" forState:UIControlStateNormal];
        [_shortVideoBtn.layer setCornerRadius:8.0];
    }
    return _shortVideoBtn;
}

- (UILabel*)powerLabel {
    if (!_powerLabel) {
        _powerLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, CGRectGetHeight(self.view.frame)- 40, CGRectGetWidth(self.view.frame), 35)];
        _powerLabel.text = @"@pixpark";
        _powerLabel.numberOfLines = 0;
        _powerLabel.textAlignment = NSTextAlignmentCenter;
        _powerLabel.font = [UIFont systemFontOfSize:12];
        _powerLabel.textColor = [UIColor blackColor];
    }
    return _powerLabel;
}
- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
