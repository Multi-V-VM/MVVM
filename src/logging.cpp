//
// Created by victoryang00 on 5/6/23.
//

#include "logging.h"

void LogWriter::operator<(const LogStream &stream) {
    std::ostringstream msg;
    msg << stream.sstream_->rdbuf();
    output_log(msg);
}

void LogWriter::output_log(const std::ostringstream &msg) {
    if (log_level_ >= env_log_level)
        std::cout << fmt::format(fmt::emphasis::bold | fg(level2color(log_level_)), "[{}] ({}:{} L {}) ",
                                 level2string(log_level_), location_.file_, location_.line_, location_.func_)
                  << fmt::format(fg(level2color(log_level_)), "{}", msg.str()) << std::endl;
}
std::string level2string(LogLevel level) {
    switch (level) {
    case BH_LOG_LEVEL_DEBUG:
        return "DEBUG";
    case BH_LOG_LEVEL_VERBOSE:
        return "INFO";
    case BH_LOG_LEVEL_FATAL:
        return "FATAL";
    case BH_LOG_LEVEL_WARNING:
        return "WARNING";
    case BH_LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "";
    }
}
fmt::color level2color(LogLevel level) {
    switch (level) {
    case BH_LOG_LEVEL_DEBUG:
        return fmt::color::alice_blue;
    case BH_LOG_LEVEL_VERBOSE:
        return fmt::color::magenta;
    case BH_LOG_LEVEL_FATAL:
        return fmt::color::rebecca_purple;
    case BH_LOG_LEVEL_WARNING:
        return fmt::color::yellow;
    case BH_LOG_LEVEL_ERROR:
        return fmt::color::red;
    default:
        return fmt::color::white;
    }
}