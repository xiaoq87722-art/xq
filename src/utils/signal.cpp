#include "xq/utils/log.hpp"
#include "xq/utils/signal.h"


void
xq::utils::regist_signal(__sighandler_t handle, const std::initializer_list<int>& siglist) {
    struct sigaction sa{};

    sa.sa_handler = handle;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    for (auto sig: siglist) {
        ASSERT(!::sigaction(sig, &sa, nullptr), "[{}] {}", errno, ::strerror(errno));
    }
}


void
xq::utils::block_signal(const std::initializer_list<int>& siglist) {
    sigset_t mask;
    ::sigemptyset(&mask);

    for (auto sig: siglist) {
        ::sigaddset(&mask, sig);
    }

    ASSERT(!::pthread_sigmask(SIG_BLOCK, &mask, nullptr), "[{}] {}", errno, ::strerror(errno));
}