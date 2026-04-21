#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/conf.hpp"
#include "xq/net/event.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Reactor;
class Listener;


class Session {
    friend class Reactor;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


public:
    static Session*
    create() {
        auto p = xq::utils::malloc(sizeof(Session));
        return new(p) Session;
    }


    ~Session() noexcept
    {}


    void
    init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept;


    void
    release() noexcept;


    bool
    valid() const noexcept {
        return fd_ != INVALID_SOCKET;
    }


    SOCKET
    fd() noexcept {
        return fd_;
    }


    bool
    closed_by_server() const noexcept {
        return cbs_;
    }


    Listener*
    listener() noexcept {
        return listener_;
    }


    Reactor*
    reactor() noexcept {
        return reactor_;
    }


    EpollArg*
    arg() noexcept {
        return &ea_;
    }


    std::string
    to_string() const noexcept {
        return std::format("[{}] {}", fd_, sockaddr_to_string((sockaddr*)&addr_));
    }


    char*
    rbuf() noexcept {
        return rbuf_;
    }


    int
    recv() noexcept;


    int
    send(const Reactor* r, const char* data, size_t len) noexcept;


    int
    broadcast(const char* data, size_t len) noexcept;


private:
    Session() noexcept
    {}


    bool
    is_timeout(time_t now) const noexcept {
        return now - last_active_ >= Conf::instance()->timeout();
    }


    bool cbs_ { false };
    SOCKET fd_ { INVALID_SOCKET };
    time_t last_active_ { 0 };
    Listener* listener_ { nullptr };
    Reactor* reactor_ { nullptr };
    EpollArg ea_ {};
    sockaddr_storage addr_ {};

    bool wait_out_ { false };
    std::atomic<bool> sending_ { false };
    xq::utils::RingBuf sbuf_ { WBUF_MAX };
    
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 32 };
    char rbuf_[RBUF_MAX] { 0 };
}; // class Session;

    
} // namespace xq::net


#endif // __XQ_NET_SESSION_HPP__