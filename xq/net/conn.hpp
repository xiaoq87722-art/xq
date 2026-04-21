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
    typedef std::shared_ptr<Conn> Ptr;


    static Conn::Ptr
    create(Connector* r) noexcept {
        return Ptr(new Conn(r));
    }


    ~Conn() noexcept {
        close();
    }


    SOCKET
    fd() const noexcept {
        return fd_;
    }


    Connector*
    recver() noexcept {
        return connector_;
    }


    IConnEvent*
    service() noexcept {
        return service_;
    }


    void
    set_service(IConnEvent* service) noexcept {
        service_ = service;
    }


    EpollArg*
    ea() noexcept {
        return &ea_;
    }


    int
    connect(const char* host) noexcept {
        fd_ = xq::net::tcp_connect(host);
        return fd_ == INVALID_SOCKET ? -1 : 0;
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
    Conn(Connector* r) noexcept
        : connector_(r) {
        ea_.type = EpollArg::Type::Conn;
        ea_.data = this;
    }


    bool wait_out_ { false };
    SOCKET fd_ { INVALID_SOCKET };
    Connector* connector_ { nullptr };
    IConnEvent* service_ { nullptr };
    std::atomic<bool> sending_ { false };
    EpollArg ea_;
    xq::utils::RingBuf sbuf_ { RBUF_MAX };
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 4096 };
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__