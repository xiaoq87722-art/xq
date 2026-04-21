#include "xq/net/conf.hpp"
#include "xq/net/connector.hpp"
#include "xq/net/sender.hpp"
#include "xq/utils/time.hpp"


void
xq::net::Connector::run(std::initializer_list<Conn::Ptr> conns) noexcept {
    int state_stopped = STATE_STOPPED;
    if (!state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
        return;
    }

    Sender sender;
    sender.run();

    int64_t nr = std::thread::hardware_concurrency() - 3;
    if (nr < 1) {
        nr = 1;
    }

    for (uint32_t i = 0; i < nr; ++i) {
        Processor* w = new Processor;
        w->run();
        workers_.emplace_back(w);
        while(!w->running()) {
            _mm_pause();
        }
    }

    const int MAX_EVENTS = Conf::instance()->per_max_conn();
    const int INTERVAL = Conf::instance()->hb_check_interval();
    ::epoll_event *events = (epoll_event*)xq::utils::malloc(sizeof(epoll_event) *MAX_EVENTS);

    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    for (auto& conn : conns) {
        if (conn) {
            add_conn(conn);
        }
    }

    int nfds, err;
    time_t last_check_time = 0;
    state_.store(STATE_RUNNING);

    while (1) {
        nfds = ::epoll_wait(epfd_, events, MAX_EVENTS, INTERVAL);
        if (nfds == -1) {
            err = errno;
            if (err == EINTR) {
                continue;
            }
            xERROR("epoll_wait failed, errno: [{}] {}", err, ::strerror(err));
            break;
        }

        tnow_ = xq::utils::systime();
        for (int i = 0; i < nfds; ++i) {
            auto& ev = events[i];
            auto ea = (EpollArg*)ev.data.ptr;

            switch (ea->type) {
                case EpollArg::Type::Conn: {
                    conn_handle(ea);
                } break;

                case EpollArg::Type::Event: {
                    event_handle(ea);
                } break;

                default: {
                    xFATAL("ConnRecver 不应处理 EpollArg::Type {}", (int)ea->type);
                } break;
            } // switch (ea->type);
        }

        if (!running()) {
            break;
        }

        if (last_check_time + 5 <= tnow_) {
            // TODO: 心跳
            last_check_time = tnow_;
        }
    }

    state_.store(STATE_STOPPING);
    for (auto& w: workers_) {
        w->stop();
    }

    for (auto& w: workers_) {
        w->join();
        delete w;
    }
    workers_.clear();

    release_epoll_event(&epfd_, &evfd_);
    xq::utils::free(events);

    sender.stop();
    sender.join();

    conns_.clear();

    state_.store(STATE_STOPPED);
}


void
xq::net::Connector::conn_handle(EpollArg* ea) noexcept {
    auto conn = (Conn*)ea->data;
    auto itr = conns_.find(conn->fd());
    if (itr == conns_.end()) {
        return;
    }

    void* buf = xq::utils::malloc(RBUF_MAX);
    int n = conn->read(buf, RBUF_MAX);
    if (n <= 0) {
        remove_conn(conn->fd());
        xq::utils::free(buf);
        return;
    }

    workers_[conn->fd() % workers_.size()]->post({itr->second, buf, n});
}


void
xq::net::Connector::event_handle(EpollArg* _) noexcept {
    int n, err = 0;
    uint64_t val;

    while (1) {
        n = ::read(evfd_, &val, sizeof(val));
        if (n < 0) {
            err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("read failed: [{}] {}", err, ::strerror(err));
            }
            break;
        }
    }
}


void
xq::net::Connector::add_conn(Conn::Ptr conn) noexcept {
    epoll_event ev;
    ev.data.ptr = conn->ea();
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, conn->fd(), &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    conns_.insert({ conn->fd(), conn });
}


void
xq::net::Connector::remove_conn(SOCKET fd) noexcept {
    auto itr = conns_.find(fd);
    if (itr != conns_.end()) {
        auto& conn = itr->second;
        ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
        conn->close();
        conns_.erase(itr);
    }
}