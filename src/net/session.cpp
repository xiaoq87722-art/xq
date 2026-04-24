#include "xq/net/acceptor.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/time.hpp"


void
xq::net::Session::init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept {
    // 前置条件: 处于 released 状态, active_senders_ 已被 release 排空.
    ASSERT(!valid_.load(std::memory_order_relaxed), "Session::init called on valid session");

    // Step 1, 初始化所有字段 (此时 valid_=false, 跨线程 writer 无法进入, 独占写入安全)
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

    // sque_ 由 release 负责清空, 此处兜底以防未经 release 的首次 init.
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

    // Step 2, 发布新 identity. 此后跨线程 writer 可进入.
    valid_.store(true, std::memory_order_release);
}


void
xq::net::Session::release() noexcept {
    // Step 1, 关门: CAS valid_ true->false, 阻止新 writer 进入.
    bool expected = true;
    if (!valid_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // 已 released, 幂等
    }

    // Step 2, 自旋等 in-flight writer 退出 (窗口极短: 几个原子操作)
    while (active_senders_.load(std::memory_order_acquire) != 0) {
        ::_mm_pause();
    }

    // Step 3, 独占清理
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
            return total;
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
xq::net::Session::send(const char* data, size_t len) noexcept {
    // 跨线程分支: 由 active_senders_ + valid_ 双原子保护, 杜绝池复用 UAR race.
    //   1) fetch_add 宣告 "我正在使用这个 Session"
    //   2) 在保护区内再次 check valid_ (seq_cst 配对保证 StoreLoad 顺序)
    //   3) 整个操作期间 release 会 spin 等 active_senders_ 归零, 不会清掉本 writer 的数据
    if (std::this_thread::get_id() != reactor_->tid()) {
        active_senders_.fetch_add(1, std::memory_order_seq_cst);
        if (!valid_.load(std::memory_order_seq_cst)) {
            active_senders_.fetch_sub(1, std::memory_order_release);
            return -1;
        }

        xq::utils::SendBuf sb;
        sb.fill(data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满, 调大 MPSC 容量");

        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            reactor_->post({ Event::Type::Send, this });
        }

        active_senders_.fetch_sub(1, std::memory_order_release);
        return 0;
    }

    // 同线程分支: 本线程即 owner, valid_ 只会被本线程修改, relaxed 读即可.
    if (!valid_.load(std::memory_order_relaxed)) {
        return -1;
    }

    if (wait_out_ && data && len > 0) {
        ASSERT(sbuf_.write(data, len) == len, "sbuf_ 写入失败 (积压超过 WBUF_MAX), 调大 WBUF_MAX");
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
            ASSERT(sbuf_.write(sbufs[i].data() + rem, left) == left, "sbuf_ 写入失败 (积压超过 WBUF_MAX), 调大 WBUF_MAX");
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
            ASSERT(sbuf_.write(data + rem, left) == left, "sbuf_ 写入失败 (积压超过 WBUF_MAX), 调大 WBUF_MAX");
            rem = 0;
        }
    }

    // 首批 16 条之外的 sque_ 条目, 防止唤醒丢失
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            size_t blen = (size_t)sbufs[i].len;
            ASSERT(sbuf_.write(sbufs[i].data(), blen) == blen, "sbuf_ 写入失败 (积压超过 WBUF_MAX), 调大 WBUF_MAX");
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

    wait_out_ = sbuf_.readable() > 0;
    return bytes_sent;
}


int
xq::net::Session::broadcast(const char* data, size_t len) noexcept {
    return listener_->acceptor()->broadcast(data, len);
}