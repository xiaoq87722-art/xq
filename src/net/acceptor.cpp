#include <immintrin.h>
#include <netinet/tcp.h>


#include <cstring>


#include "xq/net/acceptor.hpp"
#include "xq/utils/signal.h"


static void
signal_handle(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        xq::net::Acceptor::instance()->stop();
    }
}


static xq::net::Reactor*
next_reactor(std::vector<xq::net::Reactor*>& reactors) noexcept {
    static size_t index = 0;
    return reactors[index++ % reactors.size()];
}


void
xq::net::Acceptor::run(const std::vector<Listener*>& listeners) noexcept {
    int stopped_state = STATE_STOPPED;
    if (!state_.compare_exchange_strong(stopped_state, STATE_STARTING)) {
        return;
    }

    int64_t nr = std::thread::hardware_concurrency() - 2;
    if (nr < 1) {
        nr = 1;
    }

    // Step 1, 注册信号事件
    xq::utils::regist_signal(signal_handle, { SIGINT, SIGTERM });

    // Step 2, 启动 reactor 线程
    for (uint32_t i = 0; i < nr; ++i) {
        Reactor* r = new Reactor();
        r->run();
        reactors_.emplace_back(r);
        while(!r->running()) {
            _mm_pause();
        }
    }

    // Step 3, 初始化 epoll 和 eventfd
    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    // Step 4, 开启监听
    constexpr int MAX_EVENT = 16;
    ::epoll_event ev, events[MAX_EVENT];

    for (auto l: listeners) {
        l->start(this);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = l->arg();
        ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, l->fd(), &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    }

    int err, nfds, i;
    state_.store(STATE_RUNNING);

    // Step 5， IO LOOP
    while (running()) {
        err = 0;
        nfds = ::epoll_wait(epfd_, events, MAX_EVENT, -1);
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

            // ---------------------------
            // 在 acceptor 的 epoll 中 EpollArg 只有两种类型
            //  * Listener: 用于监听
            //  * Event: 自定义事件
            // ---------------------------
            switch (ea->type) {
                case EpollArg::Type::Listener: {
                    listener_handle(ea);
                } break;
            
                case EpollArg::Type::Event: {
                    event_handle(ea);
                } break;

                default: {
                    xFATAL("Acceptor 不应处理 EpollArg::Type {}", (int)ea->type);
                } break;
            } // switch (ea->type);
        }
    }

    // Step 6, 停止监听
    for (auto l: listeners) {
        l->stop();
    }

    // Step 7, 释放 epoll 和 eventfd
    release_epoll_event(&epfd_, &evfd_);

    // Step 8, 停止 reactor
    state_.store(STATE_STOPPING);
    for (auto& r: reactors_) {
        r->stop();
    }

    for (auto r: reactors_) {
        r->join();
        delete r;
    }

    reactors_.clear();

    // Step 9: 清理 sessions
    for (i = 0; i < MAX_CONN; ++i) {
        auto s = sessions_[i];
        if (s) {
            xq::utils::free(s);
            sessions_[i] = nullptr;
        }
    }

    state_.store(STATE_STOPPED);
}


void
xq::net::Acceptor::stop() noexcept {
    int state_running = STATE_RUNNING;
    if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
        constexpr uint64_t stop = 1;
        ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
    }
}


int
xq::net::Acceptor::broadcast(const char* data, size_t len) noexcept {
    for (auto& r: reactors_) {
        EventBroadcastParam* arg = (EventBroadcastParam*)xq::utils::malloc(sizeof(EventBroadcastParam) + len);
        ::memcpy(arg->data, data, len);
        arg->len = len;
        r->post({ Event::Type::Broadcast, arg });
    }

    return 0;
}


void
xq::net::Acceptor::event_handle(EpollArg* ea) noexcept {
    // -------------------------------------------------------
    //  Acceptor::event_handle 只处理停止消息.
    //    在停止的时候已经修改了 state_ 字段, 所以在这里只需要从 eventfd 中读完数据即可.
    // -------------------------------------------------------
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
}


void
xq::net::Acceptor::listener_handle(EpollArg* ea) noexcept {
    auto l = (Listener*)ea->data;

    while (1) {
        SOCKET cfd = l->accept();
        if (cfd == INVALID_SOCKET) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                stop();
            }
            break;
        }

        if (cfd >= MAX_CONN) {
            xERROR("too many connections, rejecting [{}]", cfd);
            ::close(cfd);
            continue;
        }

        constexpr int nodelay = 1;
        ASSERT(!::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)), "setsockopt failed: [{}] {}", errno, ::strerror(errno));

        EventAcceptParam* arg = (EventAcceptParam*)xq::utils::malloc(sizeof(EventAcceptParam));
        arg->fd = cfd;
        arg->l = l;

        next_reactor(reactors_)->post(Event{ Event::Type::Accept, arg });
    }   
}