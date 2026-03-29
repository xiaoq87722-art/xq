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