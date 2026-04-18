#include <immintrin.h>


#include <csignal>
#include <cstring>


#include "xq/net/acceptor.hpp"


static void
signal_handle(int sig) {
    xq::net::Acceptor::instance()->stop();
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

    uint32_t nr = std::thread::hardware_concurrency();
    if (nr > 2) {
        nr -= 2;
    }

    static constexpr int MAX_EVENT = 10;
    ::epoll_event ev{}, events[MAX_EVENT];
    EpollArg ea;

    std::signal(SIGINT, signal_handle);
    std::signal(SIGTERM, signal_handle);

    for (uint32_t i = 0; i < nr; ++i) {
        Reactor* r = new Reactor();
        reactors_.emplace_back(r);
        threads_.emplace_back(std::thread(std::bind(&xq::net::Reactor::run, r)));

        while(!r->running()) {
            _mm_pause();
        }
    }

    epfd_ = ::epoll_create1(0);
    ASSERT(epfd_ != -1, "epoll_create1 failed: [{}] {}", errno, ::strerror(errno));

    evfd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    ASSERT(evfd_ != -1, "eventfd failed: [{}] {}", errno, ::strerror(errno));

    ea.type = EA_TYPE_QUEUE;
    ea.data = this;
    ev.data.ptr = &ea;
    ev.events = EPOLLIN | EPOLLET;
    ::epoll_ctl(epfd_, EPOLL_CTL_ADD, evfd_, &ev);

    for (auto l: listeners) {
        l->start(this);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = l->arg();
        ::epoll_ctl(epfd_, EPOLL_CTL_ADD, l->fd(), &ev);
    }

    int err = 0;
    state_.store(STATE_RUNNING);

    while (1) {
        int nfds = ::epoll_wait(epfd_, events, MAX_EVENT, -1);
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

            switch (ea->type) {
            case EA_TYPE_LISTENER:
                listener_handle(ea);
                break;
            
            case EA_TYPE_QUEUE:
                queue_handle(ea);
                break;
            }  
        }

        if (!running()) {
            break;
        }
    }

    for (auto l: listeners) {
        ::epoll_ctl(epfd_, EPOLL_CTL_DEL, l->fd(), &ev);
        l->stop();
    }

    for (auto& r: reactors_) {
        r->stop();
    }

    for (auto& t: threads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    for (auto r: reactors_) {
        delete r;
    }

    if (evfd_ != INVALID_SOCKET) {
        ::close(evfd_);
        evfd_ = INVALID_SOCKET;
    }
    
    if (epfd_ != INVALID_SOCKET) {
        ::close(epfd_);
        epfd_ = INVALID_SOCKET;
    }

    reactors_.clear();
    threads_.clear();

    for (int i = 0; i < MAX_CONN; ++i) {
        auto s = sessions_[i];
        if (s) {
            s->release();
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
        static constexpr uint64_t stop = 1;
        ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
    }
}


int
xq::net::Acceptor::broadcast(const char* data, size_t len) noexcept {
    for (auto& r: reactors_) {
        OnBroadcastArg* arg = (OnBroadcastArg*)xq::utils::malloc(sizeof(OnBroadcastArg) + len);
        ::memcpy(arg->data, data, len);
        arg->len = len;
        r->post({ EV_CMD_BROADCAST, arg });
    }

    return 0;
}


void
xq::net::Acceptor::queue_handle(EpollArg* ea) noexcept {
    int n;
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
}


void
xq::net::Acceptor::listener_handle(EpollArg* ea) noexcept {
    auto l = (Listener*)ea->data;

    while (1) {
        SOCKET cfd = l->accept();
        if (cfd == INVALID_SOCKET) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                stop();
            }
            break;
        }

        if (cfd >= MAX_CONN) {
            xERROR("too many connections, rejecting [{}]", cfd);
            ::close(cfd);
            continue;
        }

        OnAcceptArg* arg = (OnAcceptArg*)xq::utils::malloc(sizeof(OnAcceptArg));
        arg->fd = cfd;
        arg->l = l;

        auto r = next_reactor(reactors_);
        r->post(Event{ EV_CMD_ACCEPT, arg });
    }   
}