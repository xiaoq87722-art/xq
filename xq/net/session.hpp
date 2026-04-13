#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/net.in.h"


namespace xq::net {


class Reactor;
class Listener;


class Session {
public:
    Session(uv_tcp_t* uv, Listener* listener, Reactor* reactor)
        : uv_(uv)
        , listener_(listener)
        , reactor_(reactor) {
            ::uv_fileno((const uv_handle_t*)uv_, &fd_);
            socklen_t addrlen = sizeof(addr_);
            ::getpeername(fd_, (sockaddr*)&addr_, &addrlen);
        }


    ~Session()
    {}


    uv_tcp_t*
    uv() noexcept {
        return uv_;
    }


    Listener*
    listener() noexcept {
        return listener_;
    }


    Reactor*
    reactor() noexcept {
        return reactor_;
    }


    SOCKET
    fd() noexcept {
        return fd_;
    }


    std::string
    to_string() const noexcept {
        return std::format("[{}] {}", fd_, sockaddr_to_string((sockaddr*)&addr_));
    }


    void
    send(char* data, size_t len) noexcept;


private:
    SOCKET fd_ { INVALID_SOCKET };
    uv_tcp_t* uv_ { nullptr };
    Listener* listener_ { nullptr };
    Reactor* reactor_ { nullptr };
    sockaddr_storage addr_ {};
}; // class Session;

    
} // namespace xq::net


#endif // __XQ_NET_SESSION_HPP__