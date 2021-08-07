//
//  log_to_file.cpp
//  DiiRtmpLivekit
//
//  Created by devzhaoyou on 2019/9/7.
//  Copyright © 2019 pixpark. All rights reserved.
//

#include "dii_log_manager.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/fileutils.h"
#include "webrtc/base/pathutils.h"
#include "dii_media_utils.h"

#include <time.h>
#ifndef WIN32
#include <sys/time.h>
#endif
namespace dii_media_kit {
static DiiLogManager* manager_ = nullptr;

int32_t DiiMediaKit::SetTraceLog(const char* path, LogSeverity severity) {
    if(!manager_) {
        manager_ = new dii_media_kit::DiiLogManager();
    }
    
    if(path == nullptr) {
        LOG(LS_WARNING) << "setting trace log file is null";
        return -1;
    }
    
    LOG(LS_INFO) << "setting trace log file:" << path << "level: " << severity;
    return  manager_->SetTraceFileImpl(path, severity);
}

void DiiMediaKit::SetDebugLog(LogSeverity severity) {
    LOG(LS_INFO) << "Set log to stderr, log level: " << severity;
    if(!manager_) {
        manager_ = new dii_media_kit::DiiLogManager();
    }
    manager_->SetLogToDebug(severity);
}

const char* DiiMediaKit::Version() {
    return DII_MEDIA_KIT_VERSION;
}

#pragma class DiiLogManager
DiiLogManager::DiiLogManager() {
    trace_file_.reset(new dii_rtc::FileStream());
}

DiiLogManager::~DiiLogManager() {
    dii_rtc::LogMessage::RemoveLogToStream(this);
    trace_file_->Flush();
    trace_file_->Close();
}

void DiiLogManager::SetLogToDebug(LogSeverity severity) {
    // setting log level
    dii_rtc::LoggingSeverity ls = dii_rtc::LS_NONE;
    switch (severity) {
        case LOG_VERBOSE:
            ls = dii_rtc::LS_VERBOSE;
            break;
        case LOG_INFO:
            ls = dii_rtc::LS_INFO;
            break;
        case LOG_WARNING:
            ls = dii_rtc::LS_WARNING;
            break;
        case LOG_ERROR:
            ls = dii_rtc::LS_ERROR;
            break;
        case LOG_NONE:
            ls = dii_rtc::LS_NONE;
            break;
        default:
            break;
    }
    
    // setting log level
    dii_rtc::LogMessage::LogToDebug(ls);
    dii_rtc::LogMessage::LogToConsole(true);
}

int32_t DiiLogManager::SetTraceFileImpl(const char* path, LogSeverity severity) {
    dii_rtc::CritScope lock(&crit_);
    LOG(LS_VERBOSE) << "setting trace log file:" << path << "level: " << severity;
    if(path == nullptr) {
        // log
        return -1;
    }
    
    trace_file_->Close();
    trace_file_path_ = path;
    
    int error = -1;
    if (!trace_file_->Open(trace_file_path_, "ab", &error)) {
        return error;
    }
    
    // check if need move logfile
    trace_file_->GetSize(&bytes_written_);
    if(bytes_written_ > LIVE_KIT_TRACE_MAX_FILE_SIZE) {
         trace_file_->Close();
         this->MoveLogFile();
        
        if (!trace_file_->Open(trace_file_path_, "ab", &error)) {
            return error;
        }
    }
    
    // setting log level
    dii_rtc::LoggingSeverity ls = dii_rtc::LS_NONE;
    switch (severity) {
        case LOG_VERBOSE:
            ls = dii_rtc::LS_VERBOSE;
            break;
        case LOG_INFO:
            ls = dii_rtc::LS_INFO;
            break;
        case LOG_WARNING:
            ls = dii_rtc::LS_WARNING;
            break;
        case LOG_ERROR:
            ls = dii_rtc::LS_ERROR;
            break;
        case LOG_NONE:
            ls = dii_rtc::LS_NONE;
            break;
        default:
            break;
    }
    // setting write log level
    dii_rtc::LogMessage::AddLogToStream(this, ls);
    
    Trace::CreateTrace();
    Trace::SetTraceCallback(this);
    return 0;
}

void DiiLogManager::OnLogMessage(int code, const std::string& message)  {
    this->WriteToFile(message.data(), message.length());
}

void DiiLogManager::Print(TraceLevel level, const char* message, int length) {
     this->WriteToFile(message, length);
}

int32_t DiiLogManager::MoveLogFile() {
    char new_file_name[1024] = {0};
#ifndef WEBRTC_WIN
    struct timeval system_time_high_res;
    if (gettimeofday(&system_time_high_res, 0) == -1) {
        return -1;
    }
    struct tm buffer;
    const struct tm* system_time =
    localtime_r(&system_time_high_res.tv_sec, &buffer);
    sprintf(new_file_name, "%s_%4u%02u%02u_%02u%02u",
            trace_file_path_.data(),
            system_time->tm_year + 1900,
            system_time->tm_mon + 1,
            system_time->tm_mday,
            system_time->tm_hour,
            system_time->tm_min);
#else
    SYSTEMTIME system_time;
    GetLocalTime(&system_time);
    sprintf(new_file_name, "%s_%4u%02u%02u_%02u%02u",
            trace_file_path_.data(),
            system_time.wYear,
            system_time.wMonth,
            system_time.wDay,
            system_time.wHour,
            system_time.wMinute);
#endif
    
    if (!dii_rtc::Filesystem::MoveFile(trace_file_path_, std::string(new_file_name))){
        return -1;
    }
    return 0;
}
        
void DiiLogManager::WriteToFile(const char* msg, uint16_t length) {
    if (!trace_file_->GetState())
        return;
    
    if (bytes_written_ > LIVE_KIT_TRACE_MAX_FILE_SIZE) {
        bytes_written_ = 0;
        trace_file_->Flush();
        trace_file_->Close();
        // move log file
        this->MoveLogFile();
        
        // reopen log file
        int error = 0;
        if (!trace_file_->Open(trace_file_path_, "ab", &error)) {
            return;
        }
    }

    char trace_message[LIVE_KIT_TRACE_MAX_MESSAGE_SIZE];
    memcpy(trace_message, msg, length);
    trace_message[length] = 0;
    trace_message[length - 1] = '\n';
    size_t written = -1;
    int ret = -1;
    trace_file_->Write(trace_message, length, &written, &ret);
    trace_file_->Flush();
    bytes_written_ += written;
}

void DiiLogManager::Flush() {
    if (!trace_file_->GetState())
        return;
    trace_file_->Flush();
}
} // namespace dii_media_kit
