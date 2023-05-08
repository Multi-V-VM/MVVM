//
// Created by yiwei yang on 5/6/23.
//

#ifndef MVVM_LOGGING_H
#define MVVM_LOGGING_H

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/ostream.h>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include "wasm_runtime.h"

using std::list;
using std::string;
struct LocationInfo {
    LocationInfo(string file, int line, const char *func) : file_(std::move(file)), line_(line), func_(func) {}
    ~LocationInfo() = default;

    string file_;
    int line_;
    const char *func_;
};
class LogStream;
class LogWriter;

class LogWriter {
public:
    LogWriter(const LocationInfo &location, LogLevel loglevel) : location_(location), log_level_(loglevel) {
        char *logv = std::getenv("LOGV");
        if (logv) {
            string string_logv = logv;
            env_log_level = std::stoi(logv);
        } else {
            env_log_level = 4;
        }
    };

    void operator<(const LogStream &stream);

private:
    void output_log(const std::ostringstream &g);
    LocationInfo location_;
    LogLevel log_level_;
    int env_log_level;
};

class LogStream {
public:
    LogStream() { sstream_ = new std::stringstream(); }
    ~LogStream() = default;

    template <typename T> LogStream &operator<<(const T &val) noexcept {
        (*sstream_) << val;
        return *this;
    }

    friend class LogWriter;

private:
    std::stringstream *sstream_;
};

string level2string(LogLevel level);
fmt::color level2color(LogLevel level);

#define LOG_IF(level) LogWriter(LocationInfo(__FILE__, __LINE__, __FUNCTION__), level) < LogStream()
#define LOGV(level) LOGV_##level
#define LOGV_DEBUG LOG_IF(BH_LOG_LEVEL_DEBUG)
#define LOGV_INFO LOG_IF(BH_LOG_LEVEL_VERBOSE)
#define LOGV_WARNING LOG_IF(BH_LOG_LEVEL_WARNING)
#define LOGV_ERROR LOG_IF(BH_LOG_LEVEL_ERROR)
#define LOGV_FATAL LOG_IF(BH_LOG_LEVEL_FATAL)

#endif //MVVM_LOGGING_H
