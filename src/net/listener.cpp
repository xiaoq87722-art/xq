#include "xq/net/acceptor.hpp"
#include "xq/net/listener.hpp"


static constexpr int BUF_SIZE = 1024 * 256;


void
xq::net::Listener::start(Acceptor* acceptor) noexcept {
    acceptor_ = acceptor;
    fd_ = xq::net::tcp_listen(host_.c_str(), BUF_SIZE, BUF_SIZE);
    ASSERT(fd_ != INVALID_SOCKET, "tcp_listen failed: [{}] {}", errno, ::strerror(errno));

    int r = ::listen(fd_, SOMAXCONN);
    ASSERT(r == 0, "listen failed: [{}] {}", errno, ::strerror(errno));

    service_->on_start(this);
}