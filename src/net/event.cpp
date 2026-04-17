#include "xq/net/event.hpp"
#include "xq/net/session.hpp"


int
xq::net::Context::send(const char* data, size_t len) noexcept {
    return session->send(reactor, data, len);
}