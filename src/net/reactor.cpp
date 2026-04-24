#include <pthread.h>


#include <csignal>


#include "xq/net/acceptor.hpp"
#include "xq/net/reactor.hpp"
#include "xq/utils/signal.h"
#include "xq/utils/time.hpp"


void
xq::net::Reactor::post(Event ev) noexcept {
    if (running()) {
        ASSERT(evque_.enqueue(std::move(ev)), "evque_ 队列已满");

        bool processing = false;
        if (processing_.compare_exchange_strong(processing, true)) {
            static constexpr uint64_t event = 1;
            ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
        } 
    } 
}


void
xq::net::Reactor::start() noexcept {
    // Step 1, 屏蔽信号
    xq::utils::block_signal({ SIGINT, SIGTERM });

    // Step 2, 初始化 epoll 和 eventfd
    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    const int MAX_EVENT = Conf::instance()->per_max_conn();
    ::epoll_event* events = (epoll_event*)xq::utils::malloc(sizeof(epoll_event) * MAX_EVENT, true);

    int nfds, err, i;
    time_t last_check_time = 0;
    const int INTERVAL = Conf::instance()->hb_check_interval();
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

        tnow_ = xq::utils::systime();
        for (i = 0; i < nfds; ++i) {
            auto& ev = events[i];
            auto ea = (EpollArg*)ev.data.ptr;

            switch (ea->type) {
                case EpollArg::Type::Event: {
                    event_handle(ea);
                } break;

                case EpollArg::Type::Session: {
                    if ((ev.events & (EPOLLERR | EPOLLHUP)) || ev.events & EPOLLIN) {
                        session_recv_handle(ea);
                    }

                    auto s = (Session*)ea->data;
                    if ((ev.events & EPOLLOUT) && s->valid()) {
                        session_send_handle(ea);
                    }
                } break;

                default: {
                    xFATAL("Reactor 不应处理 EpollArg::Type {}", (int)ea->type);
                } break;
            } // switch (ea->type);
        }

        if (last_check_time + 5 <= tnow_) {
            timer_handle();
            last_check_time = tnow_;
        }
    }

    // Step 4, 清空会话
    while (!sessions_.empty()) {
        auto itr = sessions_.begin();
        itr->second->cbs_ = true;
        remove_session(itr->first);
    }

    //  Step 5, 释放 epoll 和 eventfd
    release_epoll_event(&epfd_, &evfd_);
    
    xq::utils::free(events);
    state_.store(STATE_STOPPED);
}


void
xq::net::Reactor::event_handle(EpollArg* ea) noexcept {
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
    while ((n = evque_.try_dequeue_bulk(evs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            switch (evs[i].type) {
                case Event::Type::Accept: {
                    on_accept(evs[i].param);
                } break;

                case Event::Type::Send: {
                    on_send(evs[i].param);
                } break;

                case Event::Type::Broadcast: {
                    on_broadcast(evs[i].param);
                } break;

                default: {
                    xFATAL("Reactor 不应处理 Event::Command {}", (int)evs[i].type);
                } break;
            } // switch (evs[i].cmd);
        }
    } // while (!evque_.empty());
}


void
xq::net::Reactor::session_recv_handle(EpollArg* ea) noexcept {
    auto s = (Session*)ea->data;

    int n = s->recv();
    if (n <= 0) {
        if (n != EOF) {
            xINFO("出现错误，关闭连接 [{}]", s->to_string());
        }
        remove_session(s->fd());
    }
}


void
xq::net::Reactor::session_send_handle(EpollArg* ea) noexcept {
    auto s = (Session*)ea->data;
    int res;

    if ((res = s->send(nullptr, 0)) < 0) {
        xERROR("send failed for session [{}]: {}", s->to_string(), -res);
    }
}


void
xq::net::Reactor::on_accept(void* params) noexcept {
    auto arg = (EAcceptParam*)params;
    auto fd = arg->fd;

    if (!Acceptor::instance()->sessions()[fd]) {
        Acceptor::instance()->sessions()[fd] = Session::create();
    }
    auto s = Acceptor::instance()->sessions()[fd];
    s->init(fd, arg->l, this);

    ::epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    ev.data.ptr = s->arg();
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));

    add_session(fd, s);
    if (arg->l->service()->on_connected(s)) {
        s->cbs_ = true;
        remove_session(fd);
    }
    xq::utils::free(params);
}


void
xq::net::Reactor::on_send(void* arg) noexcept {
    auto s = (Session*)arg;
    if (s->send(nullptr, 0)) {
        xERROR("send failed for session [{}]", s->to_string());
    }
}


void
xq::net::Reactor::on_broadcast(void* params) noexcept {
    auto arg = (EBroadcastParam*)params;

    for (auto itr = sessions_.begin(); itr != sessions_.end();) {
        auto s = itr->second;
        if (s->send(arg->data, arg->len) < 0) {
            s->cbs_ = true;
            ++itr;
            remove_session(s->fd());
        } else {
            ++itr;
        }
    }
    xq::utils::free(params);
}


void
xq::net::Reactor::timer_handle() noexcept {
    for (auto itr = sessions_.begin(); itr != sessions_.end();) {
        auto s = itr->second;
        if (s->is_timeout(tnow_)) {
            SOCKET fd = itr->first;
            s->cbs_ = true;
            ++itr;
            remove_session(fd);
        } else {
            ++itr;
        }
    }
}