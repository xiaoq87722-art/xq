#ifndef __XQ_NET_LISTENER_HPP_
#define __XQ_NET_LISTENER_HPP_


#include "xq/net/session.hpp"
#include "xq/net/event.hpp"
#include "xq/utils/log.hpp"


namespace xq::net {


class Acceptor;
class Listener;
class Reactor;


struct Context {
    Reactor* reactor { nullptr };
    Session* session { nullptr };

    void
    send(const char* data, size_t len) noexcept {
        session->send(reactor, const_cast<char*>(data), len);
    }
};


class IService {
public:
    virtual void on_start(Listener* l) = 0;
    virtual void on_stop(Listener* l) = 0;
    virtual void on_connected(Session* s) = 0;
    virtual void on_disconnected(Session* s) = 0;
    virtual void on_data(Context* ctx, const char* data, size_t len) = 0;
}; // class IService;


class Listener {
public:
    Listener(IService* service, const char* endpoint, uint16_t port)
        : service_(service) {
        ASSERT(endpoint && port > 0, "params is invalid");
        host_ = std::format("{}:{}", endpoint, port);
        arg_.type = EE_TYPE_LISTENER;
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


    void
    start() noexcept;


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
    EpollArg arg_{};
    std::string host_ {};
    IService* service_ { nullptr };
}; // class Listener;


} // namespace xq::net



#endif // __XQ_NET_LISTENER_HPP_