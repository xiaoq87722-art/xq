#include "xq/net/session.hpp"
#include "xq/net/event.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


int
xq::net::Session::recv() noexcept {
    char* p = rbuf_;
    ssize_t nleft = RBUF_MAX;

    while (1) {
        int n = ::recv(fd_, p, nleft, 0);
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("recv failed: [{}] {}", err, ::strerror(err));
                return -err;
            }

            return RBUF_MAX - nleft;
        } else if (n == 0) {
            return 0;
        } else {
            p += n;
            nleft -= n;
        }
    }
}


int
xq::net::Session::send(const Reactor* r, const char* data, size_t len) noexcept {
    if (r != reactor_) {
        SendBuf sb;
        sb.len = len;
        sb.data = (char*)xq::utils::malloc(len);
        ::memcpy(sb.data, data, len);
        sque_.enqueue(std::move(sb));
        reactor_->post({ EV_CMD_SEND, this });
        return 0;
    }

    // 将跨线程待发数据合并进 sbuf_
    ssize_t n;
    SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            sbuf_.write(sbufs[i].data, sbufs[i].len);
            xq::utils::free(sbufs[i].data);
        }
    }

    if (data && len > 0) {
        ASSERT(sbuf_.write(data, len) == len, "RingBuf 写入失败，剩余空间不足");
    }

    ssize_t total = (ssize_t)sbuf_.readable();

    while (sbuf_.readable() > 0) {
        iovec iov[2];
        int niov = sbuf_.read_iov(iov);
        ssize_t sent = ::writev(fd_, iov, niov);
        if (sent < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("send failed: [{}] {}", err, ::strerror(err));
                return -err;
            }
            break;
        }
        sbuf_.read_consume(sent);
    }

    if (sbuf_.readable() > 0) {
        ::epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
        ev.data.ptr = &ea_;
        ::epoll_ctl(reactor_->epfd(), EPOLL_CTL_MOD, fd_, &ev);
    }

    return total - (ssize_t)sbuf_.readable();
}