#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/event.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Connector;


class Conn {
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    Conn(Conn&&) = delete;
    Conn& operator=(Conn&&) = delete;


public:
    Conn() noexcept {
        ea_.type = EpollArg::Type::Conn;
        ea_.data = this;
    }


    ~Conn() noexcept {
        release();
    }


    void 
    init(const char* host, Connector* r) noexcept {
        fd_ = tcp_connect(host);
        ASSERT(fd_ != INVALID_SOCKET, "tcp_connect failed");
        connector_ = r;
    }


    void
    release() noexcept {
        if (connector_) {
            connector_ = nullptr;
        }

        sbuf_.clear();

        int n;
        xq::utils::SendBuf sbufs[16];
        while ((n = sque_.try_dequeue_bulk(sbufs, 16)) > 0) {
            for (int i = 0; i < n; ++i) {
                xq::utils::free(sbufs[i].data);
            }
        }

        if (fd_ != INVALID_SOCKET) {
            SOCKET fd = fd_;
            fd_ = INVALID_SOCKET;
            ::close(fd);
        }
    }


    bool
    valid() const noexcept {
        return fd_ != INVALID_SOCKET;
    }


    SOCKET
    fd() const noexcept {
        return fd_;
    }


    Connector*
    recver() noexcept {
        return connector_;
    }


    EpollArg*
    ea() noexcept {
        return &ea_;
    }


    void
    close() noexcept {
        if (fd_ != INVALID_SOCKET) {
            ::close(fd_);
            fd_ = INVALID_SOCKET;
        }
    }


    int
    recv(void* data, size_t dlen) noexcept;


    int
    send(const char* data, size_t dlen) noexcept;


private:
    bool wait_out_ { false };
    SOCKET fd_ { INVALID_SOCKET };
    Connector* connector_ { nullptr };
    std::atomic<bool> sending_ { false };
    EpollArg ea_;
    xq::utils::RingBuf sbuf_ { WBUF_MAX };
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 4096 };
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__