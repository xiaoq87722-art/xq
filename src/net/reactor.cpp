#include "xq/net/reactor.hpp"
#include "xq/net/session.hpp"
#include "xq/net/acceptor.hpp"
#include <csignal>
#include <pthread.h>


void
xq::net::Reactor::run() {
    int stopped_state = STATE_STOPPED;
    if (!state_.compare_exchange_strong(stopped_state, STATE_STARTING)) {
        return;
    }

    ::sigset_t mask;
    ::sigemptyset(&mask);
    ::sigaddset(&mask, SIGINT);
    ::sigaddset(&mask, SIGTERM);
    ::pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != -1, "epoll_create1 failed: [{}] {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != -1, "eventfd failed: [{}] {}", errno, ::strerror(errno));

    static constexpr int MAX_EVENT = 4096;
    ::epoll_event ev{}, events[MAX_EVENT];
    EpollArg ea;

    ea.type = EA_TYPE_QUEUE;
    ea.data = this;
    ev.data.ptr = &ea;
    ev.events = EPOLLIN | EPOLLET;
    ::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev);

    int err = 0;
    state_.store(STATE_RUNNING);

    while (1) {
        int nfds = ::epoll_wait(epfd_, events, MAX_EVENT, 5000);

        if (nfds < 0) {
            err = errno;
            if (err == EINTR) {
                continue;
            }

            xERROR("epoll_wait failed: [{}] {}", err, ::strerror(err));
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            auto& ev = events[i];
            auto ea = (EpollArg*)ev.data.ptr;

            if (ea->type == EA_TYPE_QUEUE) {
                custom_handle(ea);
            } else {
                session_handle(ea);
            }
        }

        if (!running()) {
            break;
        }
    }

    if (evfd_ != INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = INVALID_SOCKET;
    }

    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    sessions_.clear();

    state_.store(STATE_STOPPED);
}


void
xq::net::Reactor::stop() {
    static constexpr uint64_t stop = 1;
    ASSERT(evque_.enqueue(std::move(Event{ EV_CMD_STOP, nullptr })), "evque_ 队列已满");
    ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
}


void
xq::net::Reactor::post(Event ev) noexcept {
    static constexpr uint64_t event = 1;
    ASSERT(evque_.enqueue(std::move(ev)), "evque_ 队列已满");
    ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
}


void
xq::net::Reactor::on_accept(void* params) noexcept {
    auto arg = (OnAcceptArg*)params;
    auto fd = arg->fd;

    Session* s = Acceptor::instance()->sessions()[fd];
    if (!s) {
        Acceptor::instance()->sessions()[fd] = s = (Session*)xq::utils::malloc(sizeof(Session));
    }
    s->init(fd, arg->l, this);

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = s->arg();
    ::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev);

    add_session(fd, s);
    arg->l->service()->on_connected(s);
    xq::utils::free(params);
}


void
xq::net::Reactor::on_send(void* arg) noexcept {
    // TODO
}


void
xq::net::Reactor::custom_handle(EpollArg* ea) noexcept {
    int n;
    Event evs[16];
    uint64_t val;

    while (1) {
        n = ::read(evfd_, &val, sizeof(val));
        if (n < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                xERROR("read failed: [{}] {}", errno, ::strerror(errno));
            }
            break;
        }
    }

    while (!evque_.empty()) {
        n = evque_.try_dequeue_bulk(evs, 16);

        for (int i = 0; i < n; ++i) {
            switch (evs[i].cmd) {
            case EV_CMD_STOP:
                state_.store(STATE_STOPPING);
                break;

            case EV_CMD_ACCEPT:
                on_accept(evs[i].data);
                break;

            case EV_CMD_SEND:
                on_send(evs[i].data);
                break;
            }
        }
    } // while (!evque_.empty());
}


void
xq::net::Reactor::session_handle(EpollArg* ea) noexcept {
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

    s->listener()->service()->on_disconnected(s);
    remove_session(s->fd());
    s->release();
}