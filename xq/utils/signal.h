#ifndef __XQ_UTILS_SIGNAL_HPP__
#define __XQ_UTILS_SIGNAL_HPP__


#include <signal.h>
#include <initializer_list>
#include <functional>
#include "xq/utils/log.hpp"


namespace xq {
namespace utils {


/**
 * @brief 注册信号句柄
 */
void
regist_signal(__sighandler_t handle, const std::initializer_list<int>& siglist);

    
} // namespace utils
} // namespace xq


#endif // __XQ_UTILS_SIGNAL_HPP__