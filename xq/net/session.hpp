#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/net.in.h"
#include "xq/net/event.hpp"


namespace xq::net {


class Reactor;
class Listener;


class Session {
public:
    Session()
    {}


    ~Session()
    {}


    EpollArg*
    arg() noexcept {
        return &ea_;
    }


    void
    init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept {
        listener_ = listener;
        reactor_ = reactor;
        fd_ = fd;
        ea_ = { EA_TYPE_SESSION, this };

        socklen_t addrlen = sizeof(addr_);
        ::getpeername(fd_, (sockaddr*)&addr_, &addrlen);
    }


    void
    release() noexcept {
        if (fd_ != INVALID_SOCKET) {
            fd_ = INVALID_SOCKET;
        }

        if (listener_) {
            listener_ = nullptr;
        }

        if (reactor_) {
            reactor_ = nullptr;
        }
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


    char*
    rbuf() noexcept {
        return rbuf_;
    }


    std::string
    to_string() const noexcept {
        return std::format("[{}] {}", fd_, sockaddr_to_string((sockaddr*)&addr_));
    }


    int
    recv() noexcept;


    int
    send(const Reactor* r, const char* data, size_t len) noexcept;


private:
    EpollArg ea_ {};
    SOCKET fd_ { INVALID_SOCKET };
    Listener* listener_ { nullptr };
    Reactor* reactor_ { nullptr };
    sockaddr_storage addr_ {};
    char rbuf_[RBUF_MAX];
}; // class Session;

    
} // namespace xq::net


#endif // __XQ_NET_SESSION_HPP__