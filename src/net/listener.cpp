#include "xq/net/listener.hpp"
#include "xq/net/acceptor.hpp"


void
xq::net::Listener::start(Acceptor* acceptor, uv_poll_cb cb) noexcept {
    service_->on_start(this);
    acceptor_ = acceptor;

    fd_ = xq::net::tcp_listen(host_.c_str(), 256*1024, 256*1024);
    ASSERT(fd_ != INVALID_SOCKET, "tcp_listen failed: [{}] {}", errno, ::strerror(errno));

    int r = ::listen(fd_, SOMAXCONN);
    ASSERT(r == 0, "listen failed: [{}] {}", errno, ::strerror(errno));

    poll_handle_ = (uv_poll_t*)xq::utils::malloc(sizeof(uv_poll_t), true);
    uv_poll_init(acceptor->loop(), poll_handle_, fd_);
    poll_handle_->data = this;
    uv_poll_start(poll_handle_, UV_READABLE, cb);
}