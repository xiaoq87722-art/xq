#include "xq/net/conf.hpp"
#include "xq/net/connector.hpp"
#include "xq/net/processor.hpp"
#include "xq/net/event.hpp"
#include "xq/utils/signal.h"
#include "xq/utils/time.hpp"


void
xq::net::Processor::start() noexcept {
    // Step 1, 屏蔽信号
    xq::utils::block_signal({ SIGINT, SIGTERM });

    // Step 2, 初始化 epoll fd 和 event fd
    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    constexpr int MAX_EVENT = 16;
    ::epoll_event events[MAX_EVENT];
    const auto INTERVAL = Conf::instance()->hb_check_interval();
    int nfds, err, i;
    state_.store(STATE_RUNNING);

    // Step 3, IO LOOP
    while (running()) {
        err = 0;
        nfds = ::epoll_wait(epfd_, events, MAX_EVENT, INTERVAL);
        if (nfds < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: [{}] {}", err, ::strerror(err));
            break;
        }

        for (i = 0; i < nfds; ++i) {
            auto& ev = events[i];
            auto ea = (EpollArg*)ev.data.ptr;
            ASSERT(ea->type == EpollArg::Type::Event, "ea->type != EpollArg::Type::Event");
            msg_handle();
        }
    }

    // Step 4, 清空 message queue
    mque_.clear([](Message& m) {
       delete m.rb;
    });

    // Step 5, 释放 epoll 和 event
    release_epoll_event(&epfd_, &evfd_);
    state_.store(STATE_STOPPED);
}


void
xq::net::Processor::msg_handle() noexcept {
    int n, err = 0;
    uint64_t val;

    while (1) {
        n = ::read(evfd_, &val, sizeof(val));
        if (n < 0) {
            err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("read failed: [{}] {}", err, ::strerror(err));
                return;
            }
            break;
        }
    }

    processing_.store(false);

    Message es[16];
    while ((n = mque_.try_dequeue_bulk(es, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            auto& e = es[i];
            connector_->service()->on_data(e.conn, *e.rb);
            delete e.rb;
        }
    }
}