#ifndef __XQ_UTILS_LOG_HPP__
#define __XQ_UTILS_LOG_HPP__


#include <spdlog/spdlog.h>


namespace xq::utils {


void
init_log(const std::string& log_dir = "./logs/");


inline void
disable_log() noexcept {
    spdlog::set_level(spdlog::level::off);
}


inline void
set_log_level(spdlog::level::level_enum level) noexcept {
    spdlog::set_level(level);
}


} // namespace xq::utils


// 日志宏定义
#define xDEBUG(...)   SPDLOG_DEBUG(__VA_ARGS__)
#define xINFO(...)    SPDLOG_INFO(__VA_ARGS__)
#define xWARN(...)    SPDLOG_WARN(__VA_ARGS__)
#define xERROR(...)   SPDLOG_ERROR(__VA_ARGS__)
#define xFATAL(...)   do { SPDLOG_CRITICAL(__VA_ARGS__); std::abort(); } while(0)

#define ASSERT(expr, fmt, ...) \
    do { \
        if (!(expr)) { \
            xFATAL("Assertion failed: {} | " fmt, #expr, ##__VA_ARGS__); \
            std::abort(); \
        } \
    } while (0)


#endif // __XQ_UTILS_LOG_HPP__