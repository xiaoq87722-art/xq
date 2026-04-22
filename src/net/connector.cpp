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

    // Step 1, 开启发送线程
    sender_.run();

    int64_t nr = std::thread::hardware_concurrency() - 3;
    if (nr < 1) {
        nr = 1;
    }

    // Step 2, 开启业务线程
    for (uint32_t i = 0; i < nr; ++i) {
        Processor* proc = new Processor;
        proc->run();
        procs_.emplace_back(proc);
        while(!proc->running()) {
            _mm_pause();
        }
    }

    const int MAX_EVENTS = Conf::instance()->per_max_conn();
    const int INTERVAL = Conf::instance()->hb_check_interval();
    ::epoll_event *events = (epoll_event*)xq::utils::malloc(sizeof(epoll_event) *MAX_EVENTS);

    // Step 3, 初始化 epoll 和 eventfd
    EpollArg ea { EpollArg::Type::Event, this };
    init_epoll_event(&epfd_, &evfd_, &ea);

    // TODO: 添加连接对象, 该地方要删除, 后期直接访问ETCD去发现网关
    for (auto& conn : conns) {
        if (conn) {
            add_conn(conn);
        }
    }

    int nfds, err;
    time_t last_check_time = 0;
    state_.store(STATE_RUNNING);

    while (running()) {
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
                    event_handle();
                } break;

                default: {
                    xFATAL("ConnRecver 不应处理 EpollArg::Type {}", (int)ea->type);
                } break;
            } // switch (ea->type);
        }

        if (last_check_time + 5 <= tnow_) {
            // TODO: 发送心跳
            last_check_time = tnow_;
        }
    }

    for (auto& p: procs_) {
        p->stop();
    }

    for (auto& p: procs_) {
        p->join();
        delete p;
    }
    procs_.clear();

    sender_.stop();
    sender_.join();
    conns_.clear();

    release_epoll_event(&epfd_, &evfd_);
    xq::utils::free(events);
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
    int n = conn->recv(buf, RBUF_MAX);
    if (n <= 0) {
        remove_conn(conn->fd());
        xq::utils::free(buf);
        return;
    }

    // TODO 不能取模, 这样的话只会跑同一个 processor
    procs_[conn->fd() % procs_.size()]->post({itr->second, buf, n});
}


void
xq::net::Connector::event_handle() noexcept {
    // event handle 只处理 stop 事件, 所以这里只需要读空数据就可以
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
    ::epoll_event evr;
    evr.data.ptr = conn->ea();
    evr.events = EPOLLIN | EPOLLET;
    ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_ADD, conn->fd(), &evr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));

    ::epoll_event evw;
    evw.data.ptr = conn->ea();
    evw.events = EPOLLET | EPOLLOUT;
    ASSERT(!::epoll_ctl(sender_.epfd(), EPOLL_CTL_ADD, conn->fd(), &evw), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    conns_.insert({ conn->fd(), conn });
}


void
xq::net::Connector::remove_conn(SOCKET fd) noexcept {
    auto itr = conns_.find(fd);
    if (itr != conns_.end()) {
        auto& conn = itr->second;
        ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
        ASSERT(!::epoll_ctl(sender_.epfd(), EPOLL_CTL_DEL, fd, nullptr), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
        conn->close();
        conns_.erase(itr);
    }
}