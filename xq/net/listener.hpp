#ifndef __XQ_NET_LISTENER_HPP__
#define __XQ_NET_LISTENER_HPP__


#include "xq/net/net.in.h"
#include <unistd.h>
#include <sys/socket.h>
#include <string>
#include <atomic>


namespace xq {
namespace net {


class Listener {
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;


public:
    static std::vector<Listener*>
    build_listeners(io_uring* uring, const std::initializer_list<const char*>& endpoints);


    static void
    release_listeners(std::vector<Listener*>& list);


    ~Listener() noexcept {
        if (lfd_ != INVALID_SOCKET) {
            ::close(lfd_);
            lfd_ = INVALID_SOCKET;
        }
    }


    const char*
    host() const {
        return host_.c_str();
    }


    SOCKET
    fd() const {
        return lfd_;
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
    Listener(const char* endpoint) noexcept;


    /** 监听套接字 */
    SOCKET lfd_ { INVALID_SOCKET };

    /** 监听地址 */
    std::string host_;
}; // class Listener;


} // namespace net
} // namespace xq


#endif // __XQ_NET_LISTENER_HPP__