#include "xq/net/listener.hpp"
#include "xq/utils/log.hpp"
#include "xq/net/conf.hpp"


std::vector<xq::net::Listener*>
xq::net::Listener::build_listeners(io_uring* uring, const std::initializer_list<const char*>& endpoints) {
    std::vector<Listener*> list;

    for (auto* ep: endpoints) {
        auto *l = new Listener(ep);
        l->submit_accept(uring);
        list.emplace_back(l);
        xINFO("✅ {} 开启监听 ✅", l->host());
    }

    return std::move(list);
}


void
xq::net::Listener::release_listeners(std::vector<Listener*>& list) {
    for (auto l: list) {
        xINFO("❎ {} 停止监听 ❎", l->host());
        delete l;
    }

    list.clear();
}


xq::net::Listener::Listener(const char* endpoint) noexcept
 : host_(endpoint) {
    auto* conf = Conf::instance();
    lfd_ = tcp_listen(endpoint, conf->rcv_buf(), conf->snd_buf());
    ASSERT(lfd_ != INVALID_SOCKET && lfd_ > 0, "Failed to create listener for {}: {}, {}", endpoint, errno, ::strerror(errno));
    ASSERT(::listen(lfd_, SOMAXCONN) == 0, "Failed to listen on {}: {}, {}", endpoint, errno, ::strerror(errno));
}