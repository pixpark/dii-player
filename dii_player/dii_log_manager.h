//
//
//  DiiRtmpLivekit
//
//  Created by devzhaoyou on 2019/9/7.
//  Copyright © 2019 pixpark. All rights reserved.
//

#ifndef rtmp_live_core_log_manager_h
#define rtmp_live_core_log_manager_h

#include "dii_common.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/stream.h"
#include "webrtc/system_wrappers/include/trace.h"
#include "webrtc/common_types.h"

namespace dii_media_kit {
    // Total buffer size is WEBRTC_TRACE_NUM_ARRAY (number of buffer partitions) *
    // WEBRTC_TRACE_MAX_QUEUE (number of lines per buffer partition) *
    // WEBRTC_TRACE_MAX_MESSAGE_SIZE (number of 1 byte charachters per line) =
    // 1 or 4 Mbyte.
    #define LIVE_KIT_TRACE_MAX_MESSAGE_SIZE 1024
    
    // log file size limit 1024*1024*25 -> 25M Bytes
    #define LIVE_KIT_TRACE_MAX_FILE_SIZE 26214400
    
    class DiiLogManager : public dii_rtc::LogSink, public TraceCallback {
    public:
        DiiLogManager();
        ~DiiLogManager();
        void SetLogToDebug(LogSeverity severity);
        int32_t SetTraceFileImpl(const char* file_name, LogSeverity severity);

        virtual void OnLogMessage(int code, const std::string& message) override;
        
        // Trace
        virtual void Print(TraceLevel level, const char* message, int length) override;
        void Flush();
    private:
        int32_t MoveLogFile();
        void WriteToFile(const char* msg, uint16_t length);
        
        size_t bytes_written_ ;
        std::unique_ptr<dii_rtc::FileStream> trace_file_;
        std::string trace_file_path_;
        dii_rtc::CriticalSection crit_;
        
    };
}
#endif /* log_to_file_hpp */
