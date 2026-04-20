#include "xq/net/conf.hpp"
#include "xq/net/conn_recver.hpp"
#include "xq/net/conn_sender.hpp"
#include "xq/utils/time.hpp"


void
xq::net::ConnRecver::run(std::initializer_list<Conn*> conns) noexcept {
    int state_stopped = STATE_STOPPED;
    if (!state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
        return;
    }

    ConnSender sender;
    sender.run();

    int64_t nr = std::thread::hardware_concurrency() - 3;
    if (nr < 1) {
        nr = 1;
    }

    for (uint32_t i = 0; i < nr; ++i) {
        ConnWorker* w = new ConnWorker;
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
            conn->init(this);
            add_conn(conn);
        }
    }

    int nfds, err;
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
            auto arg = (EpollArg*)ev.data.ptr;

            switch (arg->type) {
            case EpollArg::Type::Conn:
                conn_handle(arg);
                break;

            case EpollArg::Type::Event:
                event_handle(arg);
                break;
            }
        }

        if (!running()) {
            break;
        }
    }

    state_.store(STATE_STOPPING);

    for (auto itr = conns_.begin(); itr != conns_.end(); ++itr) {
        Conn* conn = itr->second;
        if (conn) {
            conn->close();
            xq::utils::free(conn);
        }
    }
    conns_.clear();

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

    state_.store(STATE_STOPPED);
}


void
xq::net::ConnRecver::conn_handle(EpollArg* ea) noexcept {
    auto conn = (Conn*)ea->data;
    // TODO: conn->read()
}


void
xq::net::ConnRecver::event_handle(EpollArg* _) noexcept {
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
xq::net::ConnRecver::add_conn(Conn* conn) noexcept {
    epoll_event ev;
    ev.data.ptr = conn->ea();
    ev.events = EPOLLIN | EPOLLET;
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, conn->fd(), &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    conns_.insert({ conn->fd(), conn });
}


void
xq::net::ConnRecver::remove_conn(SOCKET fd) noexcept {
    auto itr = conns_.find(fd);
    if (itr != conns_.end()) {
        auto& conn = itr->second;
        ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
        conn->close();
        conns_.erase(itr);
        xq::utils::free(conn);
    }
}