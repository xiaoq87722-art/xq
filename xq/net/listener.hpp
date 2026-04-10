#ifndef __XQ_NET_LISTENER_HPP_
#define __XQ_NET_LISTENER_HPP_


#include <uv.h>
#include "xq/net/net.in.h"
#include "xq/utils/log.hpp"


namespace xq::net {


class Acceptor;


class Listener {
public:
    Listener(const char* endpoint, uint16_t port) {
        ASSERT(endpoint && port > 0, "params is invalid");
        host_ = std::format("{}:{}", endpoint, port);
    }


    ~Listener() {}


    std::string
    to_string() const noexcept {
        return host_;
    }


    void
    start(Acceptor* acceptor, uv_poll_cb cb) noexcept;


    void
    stop() {
        if (poll_handle_) {
            ::uv_close((uv_handle_t*)poll_handle_, [](uv_handle_t* handle) {
                xq::utils::free(handle);
            });
            poll_handle_ = nullptr;
        }

        if (fd_ != INVALID_SOCKET) {
            xINFO("{} 停止监听", host_);
            ::close(fd_);
            fd_ = INVALID_SOCKET;
        }
    }


    SOCKET
    accept() noexcept {
        return ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK);
    }


private:

    SOCKET fd_ { INVALID_SOCKET };
    std::string host_ {};
    uv_poll_t* poll_handle_ { nullptr };
    Acceptor* acceptor_ { nullptr };
}; // class Listener;


} // namespace xq::net



#endif // __XQ_NET_LISTENER_HPP_