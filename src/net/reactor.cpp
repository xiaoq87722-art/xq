#include "xq/net/reactor.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/acceptor.hpp"
#include "xq/net/conf.hpp"
#include "xq/utils/time.hpp"
#include "xq/utils/memory.hpp"
#include <sys/timerfd.h>
#include <list>


static inline void
block_signal() {
    sigset_t set{};

    ASSERT(!::sigemptyset(&set), "[{}] {}", errno, ::strerror(errno));
    ASSERT(!::sigaddset(&set, SIGINT), "[{}] {}", errno, ::strerror(errno));
    ASSERT(!::sigaddset(&set, SIGTERM), "[{}] {}", errno, ::strerror(errno));
    ASSERT(!::pthread_sigmask(SIG_BLOCK, &set, nullptr), "[{}] {}", errno, ::strerror(errno));
}


static inline xq::net::RingEvent*
setup_timer(xq::net::Reactor* reactor, xq::net::RingEvent* ev) {
    int ret = 0;

    if (!ev) {
        const time_t interval = (time_t)xq::net::Conf::instance()->hb_check_interval();
        SOCKET tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        struct itimerspec its{
            .it_interval = { .tv_sec = interval, .tv_nsec = 0 },
            .it_value    = { .tv_sec = interval, .tv_nsec = 0 },
        };

        ASSERT(!::timerfd_settime(tfd, 0, &its, nullptr), "[{}] {}", errno, ::strerror(errno));

        uint64_t* counter = new uint64_t(0);
        ev = xq::net::RingEvent::create();
        ev->init(xq::net::RingCommand::R_TIMER, tfd, (void*)(uintptr_t)counter);
    }

    auto *sqe = xq::net::acquire_sqe(reactor->uring());
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_read(sqe, ev->fd, ev->ex, sizeof(uint64_t), 0);
    return ev;
}


static inline void
release_timer(xq::net::Reactor* reactor, xq::net::RingEvent *ev) {
    ::close(ev->fd);
    uint64_t* counter = (uint64_t*)ev->ex;
    delete counter;
    xq::net::RingEvent::destroy(ev);
}


void
xq::net::Reactor::notify(io_uring* ct_uring, xq::net::RingEvent* ev, bool auto_submit) noexcept {
    auto* sqe = acquire_sqe(ct_uring);
    ::io_uring_prep_msg_ring(sqe, uring_.ring_fd, 0, (uint64_t)(uintptr_t)ev, 0);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
    ::io_uring_sqe_set_data(sqe, nullptr);
    
    if (auto_submit) {
        int ret = ::io_uring_submit(ct_uring);
        ASSERT(ret >= 0, "::io_uring_submit failed: [{}] {}", -ret, ::strerror(-ret));
    }
}


void
xq::net::Reactor::run() noexcept {
    int state_stopped = STATE_STOPPED;
    if (!state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
        return;
    }

    // Step 1, 屏蔽 SIGINT 和 SIGTERM
    block_signal();

    // Step 2, 初始化io_uring 和 buf_ring
    br_ = init_io_uring_with_br(&uring_, brbufs_);
    
    // Step 3, 注册定时器
    auto* timer = setup_timer(this, nullptr);

    uint32_t head, count;
    io_uring_cqe* cqe;
    tid_ = ::pthread_self();
    int ret;
    bool should_stop = false;

    // Step 4, IO LOOP
    state_ = STATE_RUNNING;
    while (1) {
        ret = ::io_uring_submit_and_wait(&uring_, 1);
        if (ret < 0) {
            xERROR("io_uring_submit_and_wait failed: {}, {}", -ret, ::strerror(-ret));
            continue;
        }

        tnow_ = xq::utils::systime();
        count = 0;
        io_uring_for_each_cqe(&uring_, head, cqe) {
            count++;
            auto* ev = (RingEvent*)::io_uring_cqe_get_data(cqe);
            ASSERT(ev != nullptr, 
                "代码不应该走到这里, 因为每个 sqe 都设置了 user_data, 而没有设置 user_data 的sqe 也设置了 IOSQE_CQE_SKIP_SUCCESS 标识");

            switch (ev->cmd) {
                case RingCommand::R_STOP:
                    on_r_stop(cqe, ev);
                    should_stop = true;
                    break;

                case RingCommand::R_TIMER:
                    on_r_timer(cqe, ev);
                    break;

                case RingCommand::R_SEND:
                    on_r_send(cqe, ev);
                    break;

                case RingCommand::R_ACCEPT:
                    on_r_accept(cqe, ev);
                    break;

                case RingCommand::S_RECV:
                    on_s_recv(cqe, ev);
                    break;

                case RingCommand::S_SEND:
                    on_s_send(cqe, ev);
                    break;

                default:
                    ASSERT(0, "代码不应该走到这里来, 不应该有已处理的其他cmd");
                    break;
            } // switch(ev->cmd);
        }

        if (count > 0) {
            ::io_uring_cq_advance(&uring_, count);
        }

        if (should_stop) {
            break;
        }
    }

    state_ = STATE_STOPPING;

    // Step 5, 停止定时器
    release_timer(this, timer);

    // Step 6, 释放 io_uring 和 buf_ring
    release_io_uring_with_br(&uring_, br_, brbufs_);

    for (auto& s: sessions_) {
        s.second->release();
    }
    sessions_.clear();

    state_ = STATE_STOPPED;
}


void
xq::net::Reactor::on_r_stop(io_uring_cqe*, RingEvent* ev) noexcept {
    RingEvent::destroy(ev);
}


void
xq::net::Reactor::on_r_accept(io_uring_cqe*, RingEvent* ev) noexcept {
    Session* s = (Session*)ev->ex;
    sessions_.insert(std::make_pair(ev->fd, s));
    s->submit_recv();
    RingEvent::destroy(ev);
}


void
xq::net::Reactor::on_r_timer(io_uring_cqe*, RingEvent* ev) noexcept {
    std::vector<Session*> rmlist;
    const auto timeout = Conf::instance()->timeout();

    for (auto &itr: sessions_) {
        auto *s = itr.second;
        if (s->active_time() + timeout < tnow_) {
            rmlist.emplace_back(s);
        }
    }

    for (auto* s: rmlist) {
        s->submit_cancel();
    }

    setup_timer(this, ev);
}


void
xq::net::Reactor::on_s_recv(io_uring_cqe* cqe, RingEvent* ev) noexcept {
    auto res = cqe->res;
    auto sess = (Session*)ev->ex;

    auto it = sessions_.find(ev->fd);
    if (it == sessions_.end() || it->second != sess || ev->gen != sess->gen()) {
        // 如果世代号不匹配，说明是上一个连接遗留的延迟 CQE
        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            RingEvent::destroy(ev);
        }
        return;
    }

    if (res > 0) {
        // 正常读取到数据的情况
        auto bid = (uint16_t)(cqe->flags >> IORING_CQE_BUFFER_SHIFT);
        auto buf = brbufs_[bid];

        sess->send(this, buf, res);
        sess->set_active_time(tnow_);
        loaded_.fetch_add(res, std::memory_order_relaxed);

        recycle_buf_ring(br_, buf, bid);

        if (!(cqe->flags & IORING_CQE_F_MORE)) {
            if (sess->valid()) {
                sess->submit_recv(false, ev);
            } else {
                RingEvent::destroy(ev);
                sessions_.erase(sess->fd());
                sess->release();
            }
        }
    } else {
        // 出现 recv 错误的情况

        if (res < 0) {
            if (res == -EMFILE || res == -ENFILE) {
                xFATAL("进程或系统的文件描述符(FD)配额用尽");
            } else if (res == -ENOBUFS || res == -ENOMEM) {
                xFATAL("内核内存不足");
            } else if (res == -EINVAL) {
                xFATAL("不支持 multishot 的旧版本内核上运行");
            } else if (res == -ECANCELED) {
                xINFO("服务端 {} 主动断开连接 {}", sess->listener()->to_string(), sess->to_string());
            } else {
                xERROR("{} recv error: [{}]{}", sess->to_string(), -res, ::strerror(-res));
            }
        } else {
            xINFO("EOF: {}", sess->to_string());
        }

        sessions_.erase(ev->fd);
        RingEvent::destroy(ev);
        sess->release();
    }
}


void
xq::net::Reactor::on_s_send(io_uring_cqe* cqe, RingEvent* ev) noexcept {
    auto res = cqe->res;
    auto wbuf = (Buffer*)ev->ex;

    auto it = sessions_.find(ev->fd);
    if (it != sessions_.end() && it->second->gen() == ev->gen) {
        auto* sess = it->second;
        if (res >= 0) {
            wbuf->consume(res);
            loaded_.fetch_add(res, std::memory_order_relaxed);
            sess->submit_send(ev);
            return; 
        } else if (res != -ECANCELED) {
            xERROR("{} send failed: [{}]{}", sess->to_string(), -res, ::strerror(-res));
            sess->submit_cancel();
        }
    }

    delete wbuf;
    RingEvent::destroy(ev);
}


void
xq::net::Reactor::on_r_send(io_uring_cqe*, RingEvent* ev) noexcept {
    auto sess = (Session*)ev->ex;

    auto it = sessions_.find(ev->fd);
    if (it != sessions_.end() && it->second == sess && ev->gen == sess->gen()) {
        sess->submit_send();
    }

    RingEvent::destroy(ev);
}