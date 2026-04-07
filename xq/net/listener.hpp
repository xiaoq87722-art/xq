#ifndef __XQ_NET_LISTENER_HPP__
#define __XQ_NET_LISTENER_HPP__


#include "xq/net/net.in.h"
#include "xq/net/buffer.hpp"
#include <unistd.h>
#include <sys/socket.h>
#include <string>
#include <atomic>


namespace xq {
namespace net {


class Session;
class Listener;


class ListenerEvent {
public:
    virtual void on_init(Listener* listener) {}
    virtual void on_stopped(Listener* listener) {}
    virtual int on_connected(Session* sess) { return 0; }
    virtual void on_disconnected(Session* sess) {}
    virtual int on_data(Session* sess, const Buffer& buf) = 0;
}; // class ListenerEvent;


class Listener {
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;


public:
    Listener(ListenerEvent* ev, const char* endpoint) noexcept;


    ~Listener() noexcept {
        if (lfd_ != INVALID_SOCKET) {
            ::close(lfd_);
            lfd_ = INVALID_SOCKET;
        }

        ev_->on_stopped(this);
    }


    const char*
    host() const {
        return host_.c_str();
    }


    SOCKET
    fd() const {
        return lfd_;
    }


    std::string
    to_string() const {
        return std::format("[{}]{}", lfd_, host_);
    }


    ListenerEvent* event() {
        return ev_;
    }


    const ListenerEvent* event() const {
        return ev_;
    }


    void
    submit_accept(io_uring* uring, bool auto_submit = false) noexcept {
        auto *sqe = acquire_sqe(uring);
        ::io_uring_sqe_set_data(sqe, (void*)this);
        ::io_uring_prep_multishot_accept(sqe, lfd_, nullptr, nullptr, 0);

        if (auto_submit) {
            int ret = ::io_uring_submit(uring);
            ASSERT(ret >= 0, "::io_uring_submit(uring) failed: [{}] {}", -ret, ::strerror(-ret));
        }
    }


private:
    /** 监听套接字 */
    SOCKET lfd_ { INVALID_SOCKET };

    /** 监听事件 */
    ListenerEvent* ev_ { nullptr };

    /** 监听地址 */
    std::string host_;
}; // class Listener;


} // namespace net
} // namespace xq


#endif // __XQ_NET_LISTENER_HPP__