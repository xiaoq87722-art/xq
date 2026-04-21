#include "xq/net/conf.hpp"
#include "xq/net/processor.hpp"
#include "xq/net/event.hpp"
#include "xq/utils/signal.h"
#include "xq/utils/time.hpp"


void
xq::net::Processor::start() noexcept {
    xq::utils::block_signal({ SIGINT, SIGTERM });

    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    constexpr int MAX_EVENT = 16;
    ::epoll_event events[MAX_EVENT];

    const auto INTERVAL = Conf::instance()->hb_check_interval();
    int nfds, err, i;
    state_.store(STATE_RUNNING);

    while (1) {
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
            event_handle(ea);
        }

        if (!running()) {
            break;
        }
    }

    int n;
    Element es[16];
    while (n = evque_.try_dequeue_bulk(es, 16), n > 0) {
        for (i = 0; i < n; ++i) {
            auto& e = es[i];
            xq::utils::free(e.data);
        }
    }

    release_epoll_event(&epfd_, &evfd_);
    state_.store(STATE_STOPPED);
}


void
xq::net::Processor::event_handle(EpollArg* _) noexcept {
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

    Element es[16];
    while (n = evque_.try_dequeue_bulk(es, 16), n > 0) {
        for (int i = 0; i < n; ++i) {
            auto& e = es[i];
            e.conn->service()->on_data(e.conn.get(), (char*)e.data, e.len);
            xq::utils::free(e.data);
        }
    }
}