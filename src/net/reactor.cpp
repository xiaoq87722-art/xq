#include <pthread.h>


#include <csignal>


#include "xq/net/acceptor.hpp"
#include "xq/net/reactor.hpp"
#include "xq/utils/signal.h"
#include "xq/utils/time.hpp"


void
xq::net::Reactor::run() noexcept {
    int stopped_state = STATE_STOPPED;
    if (!state_.compare_exchange_strong(stopped_state, STATE_STARTING)) {
        return;
    }

    xq::utils::block_signal({ SIGINT, SIGTERM });

    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != -1, "epoll_create1 failed: [{}] {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != -1, "eventfd failed: [{}] {}", errno, ::strerror(errno));

    const int MAX_EVENT = Conf::instance()->per_max_conn();
    ::epoll_event ev{};
    ::epoll_event* events = (epoll_event*)xq::utils::malloc(sizeof(epoll_event) * MAX_EVENT, true);
    EpollArg ea;

    ea.type = EpollArg::Type::Event;
    ea.data = this;
    ev.data.ptr = &ea;
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));

    int err = 0;
    time_t last_check_time = 0;
    const int INTERVAL = Conf::instance()->hb_check_interval();
    state_.store(STATE_RUNNING);

    while (1) {
        int nfds = ::epoll_wait(epfd_, events, MAX_EVENT, INTERVAL);

        if (nfds < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed: [{}] {}", err, ::strerror(err));
            break;
        }

        tnow_ = xq::utils::systime();

        for (int i = 0; i < nfds; ++i) {
            auto& ev = events[i];
            auto ea = (EpollArg*)ev.data.ptr;

            if (ea->type == EpollArg::Type::Event) {
                event_handle(ea);
            } else {
                if ((ev.events & (EPOLLERR | EPOLLHUP)) || ev.events & EPOLLIN) {
                    session_recv_handle(ea);
                }

                auto s = (Session*)ea->data;
                if ((ev.events & EPOLLOUT) && s->fd() != INVALID_SOCKET) {
                    session_send_handle(ea);
                }
            }
        }

        if (!running()) {
            break;
        }

        if (last_check_time + 5 <= tnow_) {
            timer_handle();
            last_check_time = tnow_;
        }
    }

    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, evfd_, nullptr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    ::close(evfd_);
    evfd_ = INVALID_SOCKET;

    ::close(epfd_);
    epfd_ = INVALID_SOCKET;

    sessions_.clear();
    xq::utils::free(events);
    state_.store(STATE_STOPPED);
}


void
xq::net::Reactor::stop() noexcept {
    int state_running = STATE_RUNNING;
    if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
        constexpr uint64_t stop = 1;
        ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
    }
}


void
xq::net::Reactor::post(Event ev) noexcept {
    if (!running()) {
        return;
    }

    constexpr uint64_t event = 1;
    ASSERT(evque_.enqueue(std::move(ev)), "evque_ 队列已满");
    ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
}


void
xq::net::Reactor::event_handle(EpollArg* ea) noexcept {
    int n;
    Event evs[16];
    uint64_t val;

    while (1) {
        n = ::read(evfd_, &val, sizeof(val));
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("read failed: [{}] {}", err, ::strerror(err));
            }
            break;
        }
    }

    while (!evque_.empty()) {
        n = evque_.try_dequeue_bulk(evs, 16);

        for (int i = 0; i < n; ++i) {
            switch (evs[i].cmd) {
            case Event::Command::Accept:
                on_accept(evs[i].data);
                break;

            case Event::Command::Send:
                on_send(evs[i].data);
                break;

            case Event::Command::Broadcast:
                on_broadcast(evs[i].data);
                break;
            }
        }
    } // while (!evque_.empty());
}


void
xq::net::Reactor::session_recv_handle(EpollArg* ea) noexcept {
    auto s = (Session*)ea->data;

    int n = s->recv();
    if (n > 0) {
        Context ctx(this, s);

        if (!s->listener()->service()->on_data(&ctx, s->rbuf(), n)) {
            return;
        }
    }

    if (n < 0) {
        xERROR("recv failed for session [{}]: {}", s->to_string(), -n);
    }

    remove_session(s->fd());
}


void
xq::net::Reactor::session_send_handle(EpollArg* ea) noexcept {
    auto s = (Session*)ea->data;
    int res;
    s->can_send_ = true;
    if (res = s->send(this, nullptr, 0), res < 0) {
        xERROR("send failed for session [{}]: {}", s->to_string(), -res);
    }
}


void
xq::net::Reactor::on_accept(void* params) noexcept {
    auto arg = (EventAcceptParam*)params;
    auto fd = arg->fd;

    Session* s = Acceptor::instance()->sessions()[fd];
    if (!s) {
        Acceptor::instance()->sessions()[fd] = s = Session::create();
    }
    s->init(fd, arg->l, this);

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = s->arg();
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));

    add_session(fd, s);
    arg->l->service()->on_connected(s);
    xq::utils::free(params);
}


void
xq::net::Reactor::on_send(void* arg) noexcept {
    auto s = (Session*)arg;
    s->send(this, nullptr, 0);
}


void
xq::net::Reactor::on_broadcast(void* params) noexcept {
    auto arg = (EventBroadcastParam*)params;

    for (auto& [fd, s] : sessions_) {
        if (s->reactor() != this) {
            continue;
        }

        s->send(this, arg->data, arg->len);
    }

    xq::utils::free(params);
}


void
xq::net::Reactor::timer_handle() noexcept {
    for (auto itr = sessions_.begin(); itr != sessions_.end();) {
        auto s = itr->second;
        if (s->reactor() != this) {
            ++itr;
            continue;
        }

        if (s->is_timeout(tnow_)) {
            SOCKET fd = itr->first;
            ++itr;
            remove_session(fd);
        } else {
            ++itr;
        }
    }
}
