#include "xq/net/conn.hpp"
#include "xq/net/connector.hpp"


xq::net::Conn::~Conn() noexcept {
    release();
    sque_.clear(xq::utils::SendBuf::clear);
}


void
xq::net::Conn::init(const char* host, Connector* r) noexcept {
    bool expected = false;
    if (!valid_.compare_exchange_strong(expected, true)) {
        return;
    }

    fd_ = tcp_connect(host);
    ASSERT(fd_ != INVALID_SOCKET, "tcp_connect failed");
    connector_ = r;
    wait_out_ = false;
    sending_.store(false, std::memory_order_relaxed);

    sbuf_.clear();
    sque_.clear(xq::utils::SendBuf::clear);
    host_ = host;
}


int
xq::net::Conn::recv(void* data, size_t dlen) noexcept {
    if (!valid()) {
        return -1;
    }

    char *p = (char*)data;
    size_t nleft = dlen;

    int n, err = 0;
    while (nleft > 0) {
        n = ::recv(fd_, p, nleft, 0);
        if (n < 0) {
            err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("recv failed: [{}] {}", err, ::strerror(err));
                return -1;
            }

            return dlen - nleft;
        } else if (n == 0) {
            return 0;
        } else {
            p += n;
            nleft -= n;
        }
    }

    return dlen - nleft;
}


int
xq::net::Conn::send(const char* data, size_t len) noexcept {
    if (!valid()) {
        return -1;
    }

    // TODO: 跨线程 send 仍有 UAR race. valid_ 原子化解决了 release 幂等性,
    //   但 send() 入口的 valid() check 与末尾 sque_.enqueue 之间存在 TOCTOU 窗口:
    //   Conn 可能在此期间被 release + 重新 init 成新连接, 新 identity 下的
    //   enqueue 会污染新连接.
    //   彻底修复需要 gen/epoch 或 refcount.
    //   临时对策: 调用方保证只在 Sender 线程调用 send().
    if (std::this_thread::get_id() != connector_->sender()->tid()) {
        xq::utils::SendBuf sb;
        sb.fill(data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满");

        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            connector_->sender()->post({ Event::Type::Send, this });
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
    ::iovec iov[2 + 16 + 1];
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

    // 首批 16 条之外的 sque_ 条目也要处理, 防止唤醒丢失
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            size_t blen = (size_t)sbufs[i].len;
            ASSERT(sbuf_.write(sbufs[i].data(), blen) == blen, "RingBuf 写入失败，剩余空间不足");
            sbufs[i].release();
        }
    }

    // 继续尝试冲 sbuf_, 直到 EAGAIN 或清空
    while (sbuf_.readable() > 0) {
        ::iovec iov2[2];
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