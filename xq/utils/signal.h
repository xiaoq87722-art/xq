#ifndef __XQ_UTILS_SIGNAL_HPP__
#define __XQ_UTILS_SIGNAL_HPP__


#include <signal.h>


#include <initializer_list>


namespace xq::utils {


/**
 * @brief 注册信号句柄
 */
void
regist_signal(__sighandler_t handle, const std::initializer_list<int>& siglist);


void
block_signal(const std::initializer_list<int>& siglist);




    
} // namespace xq::utils


#endif // __XQ_UTILS_SIGNAL_HPP__