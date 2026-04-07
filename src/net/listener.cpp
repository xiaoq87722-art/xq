#include "xq/net/listener.hpp"
#include "xq/utils/log.hpp"
#include "xq/net/conf.hpp"


xq::net::Listener::Listener(ListenerEvent* ev, const char* endpoint) noexcept
    : ev_(ev)
    , host_(endpoint) {
    auto* conf = Conf::instance();
    lfd_ = tcp_listen(endpoint, conf->rcv_buf(), conf->snd_buf());
    ASSERT(lfd_ != INVALID_SOCKET && lfd_ > 0, "Failed to create listener for {}: {}, {}", endpoint, errno, ::strerror(errno));
    ASSERT(::listen(lfd_, SOMAXCONN) == 0, "Failed to listen on {}: {}, {}", endpoint, errno, ::strerror(errno));
    ev_->on_init(this);
}