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
        
    int n;
    xq::utils::SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            const xq::utils::SendBuf& sbuf = sbufs[i];
            xq::utils::free(sbuf.data);
        }
    }

    socklen_t addrlen = sizeof(addr_);
    ASSERT(!::getpeername(fd_, (sockaddr*)&addr_, &addrlen), "getpeername failed: [{}] {}", errno, ::strerror(errno));

    last_active_ = reactor_->tnow();
}


void
xq::net::Session::release() noexcept {
    if (listener_) {
        listener_ = nullptr;
    }

    if (reactor_) {
        reactor_ = nullptr;
    }

    sbuf_.clear();

    int n;
    xq::utils::SendBuf sbufs[16];
    while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
        for (int i = 0; i < n; ++i) {
            auto& sbuf = sbufs[i];
            xq::utils::free(sbuf.data);
        }
    }

    if (fd_ != INVALID_SOCKET) {
        ::close(fd_);
        fd_ = INVALID_SOCKET;
    }
}


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
            break;
        } else if (n == 0) {
            return 0;
        } else {
            p += n;
            nleft -= n;
        }
    }

    last_active_ = reactor_->tnow();
    return RBUF_MAX - nleft;
}


int
xq::net::Session::send(const Reactor* r, const char* data, size_t len) noexcept {
    if (!valid()) {
        return -1;
    }

    if (r != reactor_) {
        xq::utils::SendBuf sb;
        sb.len = len;
        sb.data = (char*)xq::utils::malloc(len);
        ::memcpy(sb.data, data, len);
        ASSERT(sque_.enqueue(std::move(sb)), "sque_ 队列已满");

        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            reactor_->post({ Event::Type::Send, this });
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

    // 将跨线程待发数据合并进 sbuf_
    sending_.store(false, std::memory_order_release);

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
        iovec iov[2];
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
        ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
        ev.data.ptr = &ea_;
        ASSERT(!::epoll_ctl(reactor_->epfd(), EPOLL_CTL_MOD, fd_, &ev), "epoll_ctl failed: [{}] {}", errno, ::strerror(errno));
    } else {
        wait_out_ = false;
    }

    return total - (ssize_t)sbuf_.readable();
}


int
xq::net::Session::broadcast(const char* data, size_t len) noexcept {
    return listener_->acceptor()->broadcast(data, len);
}