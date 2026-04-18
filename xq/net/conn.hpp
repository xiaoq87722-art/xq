#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/net.in.hpp"


namespace xq::net {


class Conn {
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    Conn(Conn&&) = delete;
    Conn& operator=(Conn&&) = delete;


public:
    static Conn*
    create() noexcept {
        auto p = xq::utils::malloc(sizeof(Conn));
        return new (p) Conn;
    }


    ~Conn() noexcept {
        close();
    }


    int
    connect(const char* host) noexcept;


    void
    close() noexcept {
        if (fd_ != INVALID_SOCKET) {
            ::close(fd_);
            fd_ = INVALID_SOCKET;
        }
    }


private:
    Conn() {}


    SOCKET fd_ { INVALID_SOCKET };
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__