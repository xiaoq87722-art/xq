#include "xq/net/conf.hpp"
#include "xq/net/sender.hpp"
#include "xq/utils/signal.h"


void
xq::net::Sender::start() noexcept {
    xq::utils::block_signal({ SIGINT, SIGTERM });

    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    constexpr int MAX_EVENT = 16;
    ::epoll_event events[MAX_EVENT];

    const auto INTERVAL = Conf::instance()->hb_check_interval();
    int nfds, err, i;
    state_.store(STATE_RUNNING);

    while (1) {
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

    release_epoll_event(&epfd_, &evfd_);
    state_.store(STATE_STOPPED);
}


void
xq::net::Sender::event_handle(EpollArg* _) noexcept {
    int n;
    uint64_t val;

    while (1) {
        n = ::read(evfd_, &val, sizeof(val));
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("read failed: [{}] {}", err, ::strerror(err));
                return;
            }
            break;
        }
    }

    processing_.store(false);

    Event evs[16];
    while (n = evque_.try_dequeue_bulk(evs, 16), n > 0) {
        for (int i = 0; i < n; ++i) {
            auto& ev = evs[i];
            ASSERT(ev.cmd == Event::Command::Send, "e.cmd != Event::Command::Send");
            // conn->send(e.data, e.len);
        }
    }
}