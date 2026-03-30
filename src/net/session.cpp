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
xq::net::Session::submit_cancel() noexcept {
    if (::pthread_self() != reactor_->thread_id()) {
        xFATAL("Session 在错误的线程中被调用");
    }

    auto* sqe = acquire_sqe(reactor_->uring());
    ::io_uring_prep_cancel_fd(sqe, cfd_, 0);
    ::io_uring_sqe_set_data(sqe, nullptr);
    sqe->flags |= IOSQE_CQE_SKIP_SUCCESS;
}


void
xq::net::Session::submit_recv() noexcept {
    if (::pthread_self() != reactor_->thread_id()) {
        xFATAL("Session::submit_recv 在错误的线程中被调用");
    }

    auto* sqe = acquire_sqe(reactor_->uring());
    auto ev = reactor_->ev_pool().acquire_event();
    ev->init(xq::net::RingCommand::S_RECV, cfd_, this);
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_recv_multishot(sqe, cfd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = 1;
}


int
xq::net::Session::send(Reactor* ctr, const uint8_t* data, size_t datalen) noexcept {
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
            auto ev = ctr->ev_pool().acquire_event();
            ev->init(RingCommand::R_SEND, cfd_, this);
            ctr->notify(ctr->uring(), ev);
        }

        return 0;
    }

    drain_wque();
    cwbuf_.append(data, datalen);
    submit_send();
    return cwbuf_.len();
}


void
xq::net::Session::submit_send() noexcept {
    ASSERT(::pthread_self() == reactor_->thread_id(), "只允许 session 的所属 reactor 线程调用 submit_send 函数");

    if (cwbuf_.len() == 0) {
        sending_ = false;
        return;
    }

    sending_ = true;
    auto* sqe = acquire_sqe(reactor_->uring());
    auto ev = reactor_->ev_pool().acquire_event();
    ev->init(RingCommand::S_SEND, cfd_, this);
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_send(sqe, cfd_, cwbuf_.data(), cwbuf_.len(), MSG_NOSIGNAL);
}


void
xq::net::Session::drain_wque() noexcept {
    ASSERT(::pthread_self() == reactor_->thread_id(), "drain_wque 只能在 session 所属的 reactor 线程中调用");

    int n = 0;
    Buffer* bs[10];
    while (n = wque_.try_dequeue_bulk(bs, 10), n > 0) {
        for (int i = 0; i < n; ++i) {
            auto b = bs[i];
            cwbuf_.append(b->data(), b->len());
            delete b;
        }
    }
}