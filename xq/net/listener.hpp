#ifndef __XQ_NET_LISTENER_HPP_
#define __XQ_NET_LISTENER_HPP_

#include "xq/net/session.hpp"
#include "xq/net/net.in.h"
#include "xq/utils/log.hpp"


namespace xq::net {


class Acceptor;
class Listener;


class IService {
public:
    virtual void on_start(Listener* l) = 0;
    virtual void on_stop(Listener* l) = 0;
    virtual void on_connected(Session* s) = 0;
    virtual void on_disconnected(Session* s) = 0;
    virtual void on_data(Session* s, const char* data, size_t len) = 0;
}; // class IService;


class Listener {
public:
    Listener(IService* service, const char* endpoint, uint16_t port)
        : service_(service) {
        ASSERT(endpoint && port > 0, "params is invalid");
        host_ = std::format("{}:{}", endpoint, port);
    }


    ~Listener() {}


    std::string
    to_string() const noexcept {
        return host_;
    }


    IService*
    service() noexcept {
        return service_;
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
            service_->on_stop(this);
            ::close(fd_);
            fd_ = INVALID_SOCKET;
        }
    }


    SOCKET
    accept() noexcept {
        return ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    }


private:

    SOCKET fd_ { INVALID_SOCKET };
    std::string host_ {};
    uv_poll_t* poll_handle_ { nullptr };
    Acceptor* acceptor_ { nullptr };
    IService* service_ { nullptr };
}; // class Listener;


} // namespace xq::net



#endif // __XQ_NET_LISTENER_HPP_