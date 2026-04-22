#include "xq/net/conf.hpp"
#include "xq/net/conn.hpp"
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

    while (running()) {
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
            switch (ea->type) {
                case EpollArg::Type::Event: {
                    event_handle();
                } break;

                case EpollArg::Type::Conn: {
                    conn_handle(ea);
                } break;
             
                default: {
                    xFATAL("Sender 不应处理 EpollArg::Type {}", (int)ea->type);
                } break;
            }
        }
    }

    release_epoll_event(&epfd_, &evfd_);
    state_.store(STATE_STOPPED);
}


void
xq::net::Sender::event_handle() noexcept {
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
            ASSERT(ev.type == Event::Type::Send, "e.cmd != Event::Command::Send");
            auto c = (Conn*)ev.data;
            c->send(nullptr, 0);
        }
    }
}


void
xq::net::Sender::conn_handle(EpollArg* ea) noexcept {
    auto c = (Conn*)ea->data;
    c->send(nullptr, 0);
}