#ifndef __XQ_UTILS_TIME_HPP__
#define __XQ_UTILS_TIME_HPP__


#include <time.h>
#include "xq/utils/log.hpp"


namespace xq::utils {


/**
 * @brief 获取系统启动时间(秒)
 */
inline uint64_t
systime() noexcept{
    struct timespec ts{};
    ASSERT(::clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "[{}] {}", errno, ::strerror(errno));
    return ts.tv_sec;
}


} // namespace xq::utils


#endif // __XQ_UTILS_TIME_HPP__