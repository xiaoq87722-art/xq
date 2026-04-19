#ifndef __XQ_NET_LISTENER_HPP_
#define __XQ_NET_LISTENER_HPP_


#include "xq/net/event.hpp"
#include "xq/utils/log.hpp"


namespace xq::net {


class Acceptor;


class Listener {
public:
    Listener(IService* service, const char* endpoint, uint16_t port)
        : service_(service) {
        ASSERT(endpoint && port > 0, "params is invalid");
        host_ = std::format("{}:{}", endpoint, port);
        arg_.type = EpollArg::Type::Listener;
        arg_.data = this;
    }


    ~Listener() {}


    std::string
    to_string() const noexcept {
        return host_;
    }


    SOCKET
    fd() const noexcept {
        return fd_;
    }


    IService*
    service() noexcept {
        return service_;
    }


    EpollArg*
    arg() noexcept {
        return &arg_;
    }

    Acceptor*
    acceptor() noexcept {
        return acceptor_;
    }


    void
    start(Acceptor* acceptor) noexcept;


    void
    stop() {
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
    IService* service_ { nullptr };
    Acceptor* acceptor_ { nullptr };
    EpollArg arg_ {};
    std::string host_ {};
}; // class Listener;


} // namespace xq::net


#endif // __XQ_NET_LISTENER_HPP_