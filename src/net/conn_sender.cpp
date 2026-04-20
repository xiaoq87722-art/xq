#include "xq/net/conn_sender.hpp"
#include "xq/utils/signal.h"


void
xq::net::ConnSender::start() noexcept {
    xq::utils::block_signal({ SIGINT, SIGTERM });

    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    constexpr int MAX_EVENT = 16;
    ::epoll_event events[MAX_EVENT];

    int nfds, err, i;
    state_.store(STATE_RUNNING);

    while (1) {
    }

    release_epoll_event(&epfd_, &evfd_);
    state_.store(STATE_STOPPED);
}