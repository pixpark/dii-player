//
//  rtmp_kit_utils.cpp
//  RtmpKitUtils
//
//  Created by devzhaoyou on 2019/9/17.
//  Copyright ? 2019 pixpark. All rights reserved.
//

#include "dii_media_utils.h"

#include <ctime>
#include <time.h>
#include <chrono>
#include <vector>

using namespace std;
using namespace std::chrono;

namespace dii_media_kit {

void DiiMediaKit::SetExternalStatisticsCallback(DiiPlayerStatisticsCallback callback) {
    DiiUtil::Instance()->SetExternalStatisticsCallback(callback);
}

void DiiMediaKit::SetEventTrackinglCallback(DiiEventTrackingCallback callback) {
    DiiUtil::Instance()->SetEventTrackinglCallback(callback);
}

int DiiMediaKit::SetRadarCallback(dii_radar::DiiRadarCallback callback) {
    return DiiUtil::Instance()->SetRadarCallback(callback);
}


DiiPlayerStatisticsCallback DiiUtil::external_statistics_callback_   = nullptr;
DiiEventTrackingCallback DiiUtil::event_tracking_callback_           = nullptr;
dii_radar::DiiRadarCallback DiiUtil::radar_callback_;

int32_t DiiUtil::stream_id_ = 1000;
std::mutex DiiUtil::mtx_;

DiiUtil* DiiUtil::dii_util_ins_ = nullptr;
std::mutex* DiiUtil::ins_mtx_ = new std::mutex();
DiiUtil* DiiUtil::Instance() {
    if (dii_util_ins_ == nullptr) {
        ins_mtx_->lock();
        if (dii_util_ins_ == nullptr) {
            dii_util_ins_ = new DiiUtil();
        }
        ins_mtx_->unlock();
    }
    return dii_util_ins_;
}

DiiUtil::DiiUtil() {
     
}

void DiiUtil::SetExternalStatisticsCallback(DiiPlayerStatisticsCallback callback) {
    std::unique_lock<std::mutex> lck(mtx_);
    external_statistics_callback_ = callback;
}

void DiiUtil::SetEventTrackinglCallback(DiiEventTrackingCallback callback) {
    std::unique_lock<std::mutex> lck(mtx_);
    event_tracking_callback_ = callback;
    //dii_rtc::LogMessage::AddLogToStream(this, dii_rtc::LS_INFO);//解决雷达数据量大的问题
}

int DiiUtil::SetRadarCallback(dii_radar::DiiRadarCallback callback) {
    std::unique_lock<std::mutex> lck(mtx_);
    radar_callback_ = callback;
    return 0;
}

dii_radar::DiiRadarCallback DiiUtil::GetRadarCallback() {
    std::unique_lock<std::mutex> lck(mtx_);
    return radar_callback_;
}

void DiiUtil::TraceEvent(int32_t code, std::string msg, std::string func, int32_t line) {
    DiiTrackEvent event;
    event.line = line;
    event.func = func.c_str();
    event.code = code;
    event.msgs = msg.c_str();
    std::unique_lock<std::mutex> lck(mtx_);
    if(event_tracking_callback_)
        event_tracking_callback_(event);
}

void DiiUtil::ExternalStatisticsCallback(DiiPlayerStatistics st) {
    std::unique_lock<std::mutex> lck(mtx_);
    if(external_statistics_callback_)
        external_statistics_callback_(st);
}

int32_t DiiUtil::CreateStreamId() {
    return stream_id_++;
}

void DiiUtil::OnLogMessage(int code, const std::string& message)  {
	if (code < 2000000) {
		return;
	}
	DiiTrackEvent event;
    event.line = 0;
    event.func = "";

    event.code = code;
    event.msgs = message.c_str();
    std::unique_lock<std::mutex> lck(mtx_);
    if(event_tracking_callback_)
        event_tracking_callback_(event);
}

// unix timestamp, millisec
int64_t DiiUnixTimestampMs() {
	milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
	int64_t milliseconds_since_epoch = ms.count();
	return milliseconds_since_epoch;
}
}
