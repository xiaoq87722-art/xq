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

        ::uv_ip4_addr(endpoint, port, &addr_);
    }


    ~Listener() {}


    uv_tcp_t*
    get_listener() {
        return listener_;
    }


    std::string
    to_string() const noexcept {
        return sockaddr_to_string((sockaddr*)&addr_);
    }


    void
    start(Acceptor* acceptor, uv_connection_cb cb) noexcept;


    void
    stop() {
        if (!::uv_is_closing((uv_handle_t*)listener_)) {
            ::uv_close((uv_handle_t*)listener_, [](uv_handle_t* handle) {
                auto l = (Listener*)handle->data;
                xINFO("{} closed .........", l->to_string());
                xq::utils::free(handle);
            });
        }
    }


    SOCKET
    accept() noexcept;


private:

    SOCKET fd_ { INVALID_SOCKET };
    uv_tcp_t* listener_ { nullptr };
    sockaddr_in addr_ {};
    Acceptor* acceptor_ { nullptr };
}; // class Listener;


} // namespace xq::net



#endif // __XQ_NET_LISTENER_HPP_