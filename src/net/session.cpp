#include "xq/net/session.hpp"
#include "xq/utils/log.hpp"
#include "xq/utils/time.hpp"
#include "xq/net/reactor.hpp"
#include <netinet/tcp.h>


void
xq::net::Session::init(SOCKET cfd, Listener* l, Reactor* r) noexcept {
    if (cfd_ != INVALID_SOCKET) {
        xFATAL("{} is not free", cfd_);
    }

    if (set_nonblocking(cfd)) {
        xFATAL("set_nonblocking failed: {}, {}", errno, ::strerror(errno));
    }

    static constexpr int nodelay_on = 1;
    if (::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay_on, sizeof(nodelay_on))) {
        xFATAL("setsockopt failed: [{}]{}", errno, ::strerror(errno));
    }

    socklen_t addrlen = sizeof(addr_);
    if (::getpeername(cfd, (sockaddr*)&addr_, &addrlen)) {
        xFATAL("getpeername failed: [{}]{}", errno, ::strerror(errno));
    }

    cfd_ = cfd;
    listener_ = l;
    reactor_ = r;
    generation_++; 
    active_time_ = xq::utils::systime();
    remote_ = xq::net::sockaddr_to_string((sockaddr*)&addr_);

    int n = 0;
    Buffer* bs[10];

    while(n = wque_.try_dequeue_bulk(bs, 10), n > 0) {
        for (int i = 0; i < n; ++i) {
            delete bs[i];
        }
    }

    sending_ = false;
    cwbuf_.reset();
    crbuf_.reset();

    xINFO("{} 连接成功", to_string());
}


void
xq::net::Session::release() noexcept {
    if (cfd_ == INVALID_SOCKET) {
        return;
    }

    xINFO("{} 断开连接", to_string());
    ::close(cfd_);
    cfd_ = INVALID_SOCKET;

    int n = 0;
    Buffer* bs[10];

    while(n = wque_.try_dequeue_bulk(bs, 10), n > 0) {
        for (int i = 0; i < n; ++i) {
            delete bs[i];
        }
    }
}


void
xq::net::Session::submit_cancel(bool auto_submit) noexcept {
    if (::pthread_self() != reactor_->thread_id()) {
        xFATAL("Session 在错误的线程中被调用");
    }

    auto* sqe = acquire_sqe(reactor_->uring());
    ::io_uring_prep_cancel_fd(sqe, cfd_, 0);
    ::io_uring_sqe_set_data(sqe, nullptr);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;

    if (auto_submit) {
        int ret = ::io_uring_submit(reactor_->uring());
        ASSERT(ret >= 0, "::io_uring_submit failed: [{}] {}", -ret, ::strerror(-ret));
    }
}


void
xq::net::Session::submit_recv(bool auto_submit, RingEvent* ev) noexcept {
    if (::pthread_self() != reactor_->thread_id()) {
        xFATAL("Session::submit_recv 在错误的线程中被调用");
    }

    auto* sqe = acquire_sqe(reactor_->uring());
    if (!ev) {
        ev = RingEvent::create();
        ev->init(xq::net::RingCommand::S_RECV, cfd_, this, generation_);
    }

    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_recv_multishot(sqe, cfd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = 1;

    if (auto_submit) {
        int ret = ::io_uring_submit(reactor_->uring());
        ASSERT(ret >= 0, "::io_uring_submit failed: [{}] {}", -ret, ::strerror(-ret));
    }
}


int
xq::net::Session::send(Reactor* ctr, const uint8_t* data, size_t datalen, bool auto_submit) noexcept {
    auto tid = ::pthread_self();
    if (tid != reactor_->thread_id()) {
        ASSERT(ctr->thread_id() == tid, "参数 r 实参必需为当前 reactor 工作线程");

        Buffer* wbuf = new Buffer;
        wbuf->set_data(data, datalen);
        if (!wque_.enqueue(std::move(wbuf))) {
            delete wbuf;
            xERROR("{} 发送队列已满, 发送失败", to_string());
            return -1;
        }

        if (!sending_.exchange(true)) {
            auto ev = RingEvent::create();
            ev->init(RingCommand::R_SEND, cfd_, this, generation_);
            ctr->notify(ctr->uring(), ev, auto_submit);
        }

        return 0;
    }

    if (sending_.exchange(true, std::memory_order_acq_rel)) {
        Buffer* wbuf = new Buffer;
        wbuf->set_data(data, datalen);
        if (!wque_.enqueue(std::move(wbuf))) {
            delete wbuf;
            xERROR("{} 发送队列已满, 发送失败", to_string());
            return -1;
        }

        return 0;
    }

    drain_wque();
    cwbuf_.append(data, datalen);
    submit_send(auto_submit);

    return cwbuf_.len();
}


void
xq::net::Session::submit_send(bool auto_submit) noexcept {
    ASSERT(::pthread_self() == reactor_->thread_id(), "只允许 session 的所属 reactor 线程调用 submit_send 函数");

    if (cwbuf_.len() == 0) {
        sending_.store(false, std::memory_order_release);
        // 双重检查：如果释放锁后发现又有新数据入队
        if (drain_wque() > 0) {
            // 尝试重新夺回发送权，如果抢占失败（说明业务线程已经抢先并发送了 R_SEND），则退出
            if (sending_.exchange(true, std::memory_order_acquire)) {
                return;
            }
        } else {
            return;
        }
    }

    auto* sqe = acquire_sqe(reactor_->uring());
    auto ev = RingEvent::create();
    ev->init(RingCommand::S_SEND, cfd_, this, generation_);
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_send(sqe, cfd_, cwbuf_.data(), cwbuf_.len(), MSG_NOSIGNAL);

    if (auto_submit) {
        int ret = ::io_uring_submit(reactor_->uring());
        ASSERT(ret >= 0, "::io_uring_submit failed: [{}] {}", -ret, ::strerror(-ret));
    }
}


uint32_t
xq::net::Session::drain_wque() noexcept {
    ASSERT(::pthread_self() == reactor_->thread_id(), "drain_wque 只能在 session 所属的 reactor 线程中调用");

    int n;
    Buffer* bs[10];
    while (n = wque_.try_dequeue_bulk(bs, 10), n > 0) {
        for (int i = 0; i < n; ++i) {
            auto b = bs[i];
            cwbuf_.append(b->data(), b->len());
            delete b;
        }
    }

    return cwbuf_.len();
}