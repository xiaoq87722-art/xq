#include "xq/net/connector.hpp"
#include "xq/net/conf.hpp"
#include "xq/utils/memory.hpp"
#include "xq/utils/signal.h"
#include "xq/utils/time.hpp"
#include <sys/mman.h>
#include <sys/timerfd.h>


static void
signal_handle(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        xq::net::Connector::instance()->stop();
    }
}


static xq::net::RingEvent*
setup_timer(xq::net::Connector* connector, xq::net::RingEvent* ev) {
    int ret = 0;

    if (!ev) {
        const time_t interval = (time_t)xq::net::Conf::instance()->timeout() / 2;

        SOCKET tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        struct itimerspec its{
            .it_interval = { .tv_sec = interval, .tv_nsec = 0 },
            .it_value    = { .tv_sec = interval, .tv_nsec = 0 },
        };

        ret = ::timerfd_settime(tfd, 0, &its, nullptr);
        ASSERT(ret == 0, "timerfd_settime failed: {}, {}", errno, ::strerror(errno));

        uint64_t* counter = new uint64_t(0);
        ev = xq::net::RingEvent::create();
        ev->init(xq::net::RingCommand::C_TIMER, tfd, (void*)(uintptr_t)counter);
    }

    auto *sqe = xq::net::acquire_sqe(connector->uring());
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_read(sqe, ev->fd, ev->ex, sizeof(uint64_t), 0);
    return ev;
}


static inline void
release_timer(xq::net::Connector* connector, xq::net::RingEvent *ev) {
    ::close(ev->fd);
    uint64_t* counter = (uint64_t*)ev->ex;
    delete counter;
    xq::net::RingEvent::destroy(ev);
}


void
xq::net::Connector::run(const std::initializer_list<const char*>& hosts) noexcept {
    int state_stopped = STATE_STOPPED;
    if (!state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
        return;
    }

    xq::utils::regist_signal(signal_handle, { SIGINT, SIGTERM });

    // Step 1, 初始化 io_uring
    br_ = init_io_uring_with_br(&uring_, brbufs_);


    // Step 2, 设置定时器
    auto* timer = setup_timer(this, nullptr);

    for (auto& host: hosts) {
        auto* conn = new Conn(this);
        conn->submit_connect(host);
    }

    // Step 3, IO LOOP
    uint32_t head, count;
    io_uring_cqe* cqe;
    int ret = 0;
    state_ = STATE_RUNNING;

    while(1) {
        ret = ::io_uring_submit_and_wait(&uring_, 1);
        if (ret < 0) {
            if (ret == -EINTR && state_ != STATE_RUNNING) {
                break;
            }

            xERROR("io_uring_submit_and_wait failed: {}, {}", -ret, ::strerror(-ret));
            continue;
        }

        count = 0;
        io_uring_for_each_cqe(&uring_, head, cqe) {
            count++;
            auto* ev = (RingEvent*)::io_uring_cqe_get_data(cqe);
            if (!ev) {
                continue;
            }

            switch (ev->cmd) {
            case RingCommand::C_CONN:
                ret = on_conn(cqe, ev);
                break;
            
            case RingCommand::C_TIMER:
                ret = on_timer(cqe, ev);
                break;

            case RingCommand::C_RECV:
                ret = on_recv(cqe, ev);
                break;
            
            default:
                break;
            }
        }

        if (count > 0) {
            ::io_uring_cq_advance(&uring_, count);
        }

        if (state_ != STATE_RUNNING) {
            break;
        }
    }
    
    release_timer(this, timer);

    for (auto& itr: conns_) {
        delete itr.second;
    }
    conns_.clear();

    release_io_uring_with_br(&uring_, br_, brbufs_);

    xINFO("正常退出");
    state_ = STATE_STOPPED;
}


int
xq::net::Connector::on_conn(io_uring_cqe* cqe, RingEvent* ev) {
    auto res = cqe->res;
    auto* conn = (Conn*)ev->ex;

    RingEvent::destroy(ev);

    if (res < 0) {
        xERROR("{} 连接失败: [{}]{}", conn->host(), -res, ::strerror(-res));
        delete conn;
        return 0;
    }

    xINFO("{} 连接成功", conn->host());
    conn->submit_recv();
    conns_.insert(std::make_pair(conn->host(), conn));
    return 0;
}


int
xq::net::Connector::on_timer(io_uring_cqe* cqe, RingEvent* ev) {
    xINFO("------------------------ {}", xq::utils::systime());
    setup_timer(this, ev);
    return 0;
}


int
xq::net::Connector::on_recv(io_uring_cqe* cqe, RingEvent* ev) {
    auto res = cqe->res;
    auto conn = (Conn*)ev->ex;

    bool should_cleanup = (res <= 0);
    
    if (res > 0) {
        auto bid = (uint16_t)(cqe->flags >> IORING_CQE_BUFFER_SHIFT);
        auto buf = brbufs_[bid];
        
        recycle_buf_ring(br_, buf, bid);

        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            should_cleanup = true;
        }
    }

    if (should_cleanup) {
        if (res == 0) xINFO("{} has lost connections", conn->host());
        else if (res < 0) xERROR("{} recv error: {}", conn->host(), ::strerror(-res));

        conns_.erase(conn->host());
        RingEvent::destroy(ev);
        delete conn;
    }

    return 0;
}