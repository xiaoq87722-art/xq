#include "xq/net/conn.hpp"


int
xq::net::Conn::connect(const char* host) noexcept {
    fd_ = xq::net::tcp_connect(host);
    if (fd_ == INVALID_SOCKET) {
        return -1;
    }

    return 0;
}