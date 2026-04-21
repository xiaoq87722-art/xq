#include "xq/net/conn.hpp"
#include "xq/net/connector.hpp"


int
xq::net::Conn::recv(void* data, size_t dlen) noexcept {
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
    if (std::this_thread::get_id() != connector_->sender()->tid()) {
        xq::utils::SendBuf sb;
        sb.len = len;
        sb.data = (char*)xq::utils::malloc(len);
        ::memcpy(sb.data, data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满");

        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            connector_->sender()->post({ Event::Command::Send, this });
        }

        return 0;
    }

    if (wait_out_ && data && len > 0) {
        xq::utils::SendBuf sb;
        sb.len = len;
        sb.data = (char*)xq::utils::malloc(len);
        ::memcpy(sb.data, data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满");

        return 0;
    }

    ssize_t n;
    xq::utils::SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            ASSERT(sbuf_.write(sbufs[i].data, sbufs[i].len) > 0, "RingBuf 写入失败，剩余空间不足");
            xq::utils::free(sbufs[i].data);
        }
    }

    if (data && len > 0) {
        ASSERT(sbuf_.write(data, len) == len, "RingBuf 写入失败，剩余空间不足");
    }

    ssize_t total = (ssize_t)sbuf_.readable();

    while (sbuf_.readable() > 0) {
        ::iovec iov[2];
        int niov = sbuf_.read_iov(iov);
        ssize_t sent = ::writev(fd_, iov, niov);
        if (sent < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("send failed: [{}] {}", err, ::strerror(err));
                return -err;
            }
            wait_out_ = true;
            break;
        }
        sbuf_.read_consume(sent);
    }

    if (sbuf_.readable() > 0) {
        ::epoll_event ev;
        ev.events = EPOLLET | EPOLLOUT;
        ev.data.ptr = &ea_;
        ASSERT(!::epoll_ctl(connector_->sender()->epfd(), EPOLL_CTL_MOD, fd_, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    } else {
        wait_out_ = false;
    }

    return total - (ssize_t)sbuf_.readable();
}