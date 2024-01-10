//
// Created by victoryang00 on 5/6/23.
//

#ifndef MVVM_LOGGING_H
#define MVVM_LOGGING_H

#include "wamr_export.h"
#include "wasm_runtime.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/ostream.h>
#include <fmt/printf.h>
#include <fstream>
#include <iostream>
#include <list>
#include <ranges>
#include <sstream>
#include <string>
#if !defined(_WIN32)
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <winsock2.h>
#endif
#ifndef __APPLE__
/** Barry's work*/
struct Enumerate : std::ranges::range_adaptor_closure<Enumerate> {
    template <std::ranges::viewable_range R> constexpr auto operator()(R &&r) const {
        return std::views::zip(std::views::iota(0), (R &&)r);
    }
};
#else
struct Enumerate : std::__range_adaptor_closure<Enumerate> {
    template <std::ranges::viewable_range R> constexpr auto operator()(R &&r) const {
        return std::views::zip(std::views::iota(0), (R &&)r);
    }
};
#endif
inline constexpr Enumerate enumerate;
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
    LogStream() {
#ifdef _WIN32
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE), hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode;
        GetConsoleMode(hOutput, &dwMode);
        dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOutput, dwMode);
#endif
        sstream_ = new std::stringstream();
    }
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

#define MVVM_SOCK_ADDR "172.17.0.1"
#define MVVM_SOCK_ADDR6 "fe80::42:aeff:fe1f:b579"
#define MVVM_SOCK_MASK 24
#define MVVM_SOCK_MASK6 48
#define MVVM_SOCK_PORT 1235
#define MVVM_MAX_ADDR 5
#define MVVM_SOCK_INTERFACE "docker0"
#define LOG_IF(level) LogWriter(LocationInfo(__FILE__, __LINE__, __FUNCTION__), level) < LogStream()
#define LOGV(level) LOGV_##level
#define LOGV_DEBUG LOG_IF(BH_LOG_LEVEL_DEBUG)
#define LOGV_INFO LOG_IF(BH_LOG_LEVEL_VERBOSE)
#define LOGV_WARNING LOG_IF(BH_LOG_LEVEL_WARNING)
#define LOGV_ERROR LOG_IF(BH_LOG_LEVEL_ERROR)
#define LOGV_FATAL LOG_IF(BH_LOG_LEVEL_FATAL)

enum opcode {
    MVVM_SOCK_SUSPEND = 0,
    MVVM_SOCK_RESUME = 1,
    MVVM_SOCK_ACK = 2,
};
struct mvvm_op_data {
    enum opcode op;
    bool is_tcp;
    int size;
    SocketAddrPool addr[MVVM_MAX_ADDR][2];
};
bool is_ip_in_cidr (const char *base_ip, int subnet_mask_len, uint32_t ip);
bool is_ipv6_in_cidr(const char *base_ip_str, int subnet_mask_len, struct in6_addr *ip);

#endif // MVVM_LOGGING_H
