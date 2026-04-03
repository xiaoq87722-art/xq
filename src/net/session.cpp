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
    inflight_.store(false, std::memory_order_relaxed);
    active_time_ = xq::utils::systime();
    remote_ = xq::net::sockaddr_to_string((sockaddr*)&addr_);

    int n = 0;
    Buffer* bs[10];

    while(n = wque_.try_dequeue_bulk(bs, 10), n > 0) {
        for (int i = 0; i < n; ++i) {
            delete bs[i];
        }
    }

    sending_.store(false, std::memory_order_release);
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
    ASSERT(::pthread_self() == reactor_->thread_id(), "Session::submit_recv 必须在 Reactor 线程调用");

    auto* sqe = acquire_sqe(reactor_->uring());
    if (!ev) {
        ev = RingEvent::create(xq::net::RingCommand::S_RECV, cfd_, this, generation_);
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
    Buffer* wbuf = new Buffer;
    wbuf->set_data(data, datalen);
    if (!wque_.enqueue(std::move(wbuf))) {
        delete wbuf;
        xERROR("{} 发送队列已满, 发送失败", to_string());
        return -1;
    }

    if (!sending_.exchange(true, std::memory_order_acq_rel)) {
        reactor_->notify(
            ctr->uring(), 
            RingEvent::create(RingCommand::R_SEND, cfd_, this, generation_),
            auto_submit
        );
    }

    return 0;
}


void
xq::net::Session::submit_send(RingEvent* ev, bool auto_submit) noexcept {
    ASSERT(::pthread_self() == reactor_->thread_id(), "只允许 session 的所属 reactor 线程调用 submit_send 函数");
    
    // 1. 如果是从 CQE 回调（S_SEND）进来的，说明上一次内核发送已完成
    if (ev && ev->cmd == RingCommand::S_SEND) {
        inflight_.store(false, std::memory_order_relaxed);
    }

    // 2. 检查是否有请求正在“飞行”中。如果有，必须退出，等待 CQE 触发下一次调用
    if (inflight_.load(std::memory_order_relaxed)) {
        return;
    }

    // 尝试复用旧的 Buffer，减少 new/delete 开销
    Buffer* wbuf = (ev && ev->cmd == RingCommand::S_SEND) ? (Buffer*)ev->ex : nullptr;

    while (true) {
        // 3. 尽力从队列中取出所有待发送数据
        Buffer* bs[16];
        int n;
        while ((n = wque_.try_dequeue_bulk(bs, 16)) > 0) {
            if (!wbuf) wbuf = new Buffer;
            for (int i = 0; i < n; ++i) {
                wbuf->append(bs[i]->data(), bs[i]->len());
                delete bs[i];
            }
        }

        // 4. 检查是否有数据需要提交
        if (!wbuf || wbuf->len() == 0) {
            // 真正没数据了，清理资源
            if (wbuf) { delete wbuf; wbuf = nullptr; }
            if (ev) { RingEvent::destroy(ev); ev = nullptr; }

            // 释放发送权限
            sending_.store(false, std::memory_order_release);

            // --- Double Check ---
            // 只要队列不为空，就尝试抢回发送权
            if (!wque_.empty()) {
                // 尝试把 false 还原为 true
                if (!sending_.exchange(true, std::memory_order_acquire)) {
                    // 成功抢回权限！继续循环去 try_dequeue 数据
                    continue; 
                }
                // 如果 exchange 返回 true，说明刚才那一瞬间有别的线程调用了 send()，
                // 它发现 sending 是 false，已经触发了新的 notify，我们只需安全退出。
            }
            return; 
        }

        // 5. 准备提交给 io_uring
        inflight_.store(true, std::memory_order_relaxed);

        if (!ev) {
            // 注意：一定要带上 generation_ 防止 cfd 重用导致的失效
            ev = RingEvent::create(RingCommand::S_SEND, cfd_, wbuf, generation_);
        } else {
            ev->cmd = RingCommand::S_SEND;
            ev->ex = wbuf;
            ev->gen = generation_; // 确保世代号始终是最新的
        }

        auto* sqe = acquire_sqe(reactor_->uring());
        // 如果 SQE 满了（极端情况），这里需要处理，此处简单 ASSERT
        ASSERT(sqe, "io_uring SQE pool is full"); 

        ::io_uring_sqe_set_data(sqe, ev);
        ::io_uring_prep_send(sqe, cfd_, wbuf->data(), wbuf->len(), MSG_NOSIGNAL);

        if (auto_submit) {
            int ret = ::io_uring_submit(reactor_->uring());
            ASSERT(ret >= 0, "::io_uring_submit failed");
        }
        
        // 任务已提交，直接退出。后续由 CQE 再次触发此函数
        return; 
    }
}