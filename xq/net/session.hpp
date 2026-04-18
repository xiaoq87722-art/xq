#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/conf.hpp"
#include "xq/net/event.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


struct SendBuf {
    int len;
    char* data;
};


class Reactor;
class Listener;


class Session {
    friend class Reactor;
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
    init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept;


    void
    release() noexcept;


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


    int
    broadcast(const char* data, size_t len) noexcept;


private:
    bool
    is_timeout(time_t now) const noexcept {
        return now - last_active_ >= Conf::instance()->timeout();
    }


    EpollArg ea_ {};
    SOCKET fd_ { INVALID_SOCKET };
    time_t last_active_ { 0 };
    Listener* listener_ { nullptr };
    Reactor* reactor_ { nullptr };
    sockaddr_storage addr_ {};
    char rbuf_[RBUF_MAX];
    xq::utils::RingBuf sbuf_ { WBUF_MAX };
    std::atomic<bool> sending_ { false };
    bool can_send_ { true };
    xq::utils::MPSC<SendBuf> sque_ { 4, 32 };
}; // class Session;

    
} // namespace xq::net


#endif // __XQ_NET_SESSION_HPP__