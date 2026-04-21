#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/event.hpp"


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
        return recver_;
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
    read(void* data, size_t dlen) noexcept;


    int
    write(const void* data, size_t dlen) noexcept;


private:
    Conn(Connector* r) noexcept
        : recver_(r) {
        ea_.type = EpollArg::Type::Conn;
        ea_.data = this;
    }


    SOCKET fd_ { INVALID_SOCKET };
    Connector* recver_ { nullptr };
    IConnEvent* service_ { nullptr };
    EpollArg ea_;
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__