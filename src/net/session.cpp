#include "xq/net/acceptor.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/time.hpp"


void
xq::net::Session::init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept {
    listener_ = listener;
    reactor_ = reactor;
    fd_ = fd;
    ea_ = { EpollArg::Type::Session, this };
    
    if (sbuf_.capacity() < WBUF_MAX) {
        sbuf_.reset(WBUF_MAX);
    }

    sending_.store(false, std::memory_order_relaxed);
    cbs_ = wait_out_ = false;

    sbuf_.clear();
    rbuf_.clear();

    int n;
    xq::utils::SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            sbufs[i].release();
        }
    }

    socklen_t addrlen = sizeof(addr_);
    ASSERT(!::getpeername(fd_, (sockaddr*)&addr_, &addrlen), "getpeername failed: [{}] {}", errno, ::strerror(errno));

    last_active_ = reactor_->tnow();
}


void
xq::net::Session::release() noexcept {
    sbuf_.clear();
    rbuf_.clear();

    int n;
    xq::utils::SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            sbufs[i].release();
        }
    }

    if (fd_ != INVALID_SOCKET) {
        SOCKET fd = fd_;
        fd_ = INVALID_SOCKET;
        ::close(fd);
    }
}


int
xq::net::Session::recv() noexcept {
    ssize_t total = 0;

    while (1) {
        iovec iov[2];
        int niov = rbuf_.write_iov(iov);
        if (niov == 0) {
            Context ctx(reactor_, this);
            if (listener_->service()->on_data(&ctx, rbuf_) < 0) {
                cbs_ = true;
                return -1;
            }
            
            if (rbuf_.writable() == 0) {
                break;
            }

            continue;
        }

        ssize_t n = ::readv(fd_, iov, niov);
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                if (err == ECONNRESET || err == ETIMEDOUT || err == EPIPE) {
                    xINFO("readv: [{}] {} [{}]", err, ::strerror(err), to_string());
                } else {
                    xERROR("readv failed: [{}] {} [{}]", err, ::strerror(err), to_string());
                }
                return -err;
            }

            Context ctx(reactor_, this);
            if (listener_->service()->on_data(&ctx, rbuf_) < 0) {
                cbs_ = true;
                return -1;
            }
            break;
        } else if (n == 0) {
            rbuf_.clear();
            return EOF;
        }

        rbuf_.write_commit(n);
        total += n;
    }

    last_active_ = reactor_->tnow();
    return (int)total;
}


int
xq::net::Session::send(const Reactor* r, const char* data, size_t len) noexcept {
    if (!valid()) {
        return -1;
    }

    // TODO: 跨线程分支存在 Session 池复用 UAR 风险.
    //   业务线程 T 通过 valid() 后, Reactor 线程可能 release() 并被 Acceptor 重新 init()
    //   成新连接, T 后续的 sque_.enqueue 会污染新连接.
    //   修复方案: 加 std::atomic<uint64_t> gen_, 调用方持有 (Session*, gen) pair,
    //   enqueue 前后各 check 一次 gen_ 是否匹配; 或改用 refcount 延长对象生命周期.
    //   临时对策: 调用方保证只在 Session 所属 reactor 线程调用 send().
    if (r != reactor_) {
        xq::utils::SendBuf sb;
        sb.fill(data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满");

        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            reactor_->post({ Event::Type::Send, this });
        }

        return 0;
    }

    if (wait_out_ && data && len > 0) {
        ASSERT(sbuf_.write(data, len) == len, "RingBuf 写入失败，剩余空间不足");
        return 0;
    }

    sending_.store(false, std::memory_order_release);

    xq::utils::SendBuf sbufs[16];
    ssize_t n = sque_.try_dequeue_bulk(sbufs, 16);

    // 组 iovec: [sbuf_ 残留] + [sbufs] + [data]
    iovec iov[2 + 16 + 1];
    int niov = sbuf_.read_iov(iov);
    size_t sbuf_bytes = sbuf_.readable();

    for (ssize_t i = 0; i < n; ++i) {
        iov[niov].iov_base = sbufs[i].data();
        iov[niov].iov_len  = sbufs[i].len;
        niov++;
    }

    if (data && len > 0) {
        iov[niov].iov_base = (void*)data;
        iov[niov].iov_len  = len;
        niov++;
    }

    ssize_t bytes_sent = 0;
    ssize_t sent = 0;

    if (niov > 0) {
        sent = ::writev(fd_, iov, niov);
        if (sent < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                for (ssize_t i = 0; i < n; ++i) sbufs[i].release();
                xERROR("send failed: [{}] {}", err, ::strerror(err));
                return -err;
            }
            sent = 0;
        }
        bytes_sent = sent;
    }

    size_t rem = (size_t)sent;

    // 消费 sbuf_ 残留
    if (sbuf_bytes > 0) {
        size_t c = std::min(rem, sbuf_bytes);
        sbuf_.read_consume(c);
        rem -= c;
    }

    // 消费 sbufs; 未发完的尾巴吸回 sbuf_
    for (ssize_t i = 0; i < n; ++i) {
        size_t blen = (size_t)sbufs[i].len;
        if (rem >= blen) {
            rem -= blen;
        } else {
            size_t left = blen - rem;
            ASSERT(sbuf_.write(sbufs[i].data() + rem, left) == left, "RingBuf 写入失败，剩余空间不足");
            rem = 0;
        }
        sbufs[i].release();
    }

    // 消费 data; 未发完的尾巴吸回 sbuf_
    if (data && len > 0) {
        if (rem >= len) {
            rem -= len;
        } else {
            size_t left = len - rem;
            ASSERT(sbuf_.write(data + rem, left) == left, "RingBuf 写入失败，剩余空间不足");
            rem = 0;
        }
    }

    // 首批 16 条之外的 sque_ 条目, 防止唤醒丢失
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            size_t blen = (size_t)sbufs[i].len;
            ASSERT(sbuf_.write(sbufs[i].data(), blen) == blen, "RingBuf 写入失败，剩余空间不足");
            sbufs[i].release();
        }
    }

    // 继续尝试冲 sbuf_, 直到 EAGAIN 或清空
    while (sbuf_.readable() > 0) {
        iovec iov2[2];
        int niov2 = sbuf_.read_iov(iov2);
        ssize_t s = ::writev(fd_, iov2, niov2);
        if (s < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("send failed: [{}] {}", err, ::strerror(err));
                return -err;
            }
            break;
        }
        sbuf_.read_consume(s);
        bytes_sent += s;
    }

    if (sbuf_.readable() > 0) {
        wait_out_ = true;
        ::epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
        ev.data.ptr = &ea_;
        ASSERT(!::epoll_ctl(reactor_->epfd(), EPOLL_CTL_MOD, fd_, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    } else {
        wait_out_ = false;
    }

    return bytes_sent;
}


int
xq::net::Session::broadcast(const char* data, size_t len) noexcept {
    return listener_->acceptor()->broadcast(data, len);
}