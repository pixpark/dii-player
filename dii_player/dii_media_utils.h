//
//  dii_media_utils.h
//  DiiRtmpLivekit
//
//  Created by devzhaoyou on 2019/9/17.
//  Copyright ? 2019 pixpark. All rights reserved.
//

#ifndef rtmp_kit_utils_hpp
#define rtmp_kit_utils_hpp

#include "dii_common.h"
#include "dii_com_def.h"
#include "webrtc/base/logging.h"

#include <list>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace dii_media_kit {

class DiiUtil : public dii_rtc::LogSink {
public:
    static DiiUtil* Instance();
    void SetExternalStatisticsCallback(DiiPlayerStatisticsCallback callback);
    void SetEventTrackinglCallback(DiiEventTrackingCallback callback);
    void TraceEvent(int32_t code, std::string msg, std::string func, int32_t line);
    void ExternalStatisticsCallback(DiiPlayerStatistics st);
    int SetRadarCallback(dii_radar::DiiRadarCallback callback);
    dii_radar::DiiRadarCallback GetRadarCallback();
    
    int32_t CreateStreamId();

    virtual void OnLogMessage(int code, const std::string& message) override;
private:
    DiiUtil();
    static std::mutex *ins_mtx_;
    static DiiUtil* dii_util_ins_;
    
    static DiiPlayerStatisticsCallback external_statistics_callback_;
    static DiiEventTrackingCallback event_tracking_callback_;
    static dii_radar::DiiRadarCallback radar_callback_;

    static std::mutex mtx_;
    static int32_t stream_id_;
};

/** func DiiUnixTimestampMs  **/
int64_t DiiUnixTimestampMs();
}

#endif /* rtmp_kit_utils_hpp */
