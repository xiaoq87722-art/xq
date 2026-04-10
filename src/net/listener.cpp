#include "xq/net/listener.hpp"
#include "xq/net/acceptor.hpp"


void
xq::net::Listener::start(Acceptor* acceptor, uv_connection_cb cb) noexcept {
    listener_ = (uv_tcp_t*)xq::utils::malloc(sizeof(uv_tcp_t), true);

    int r = ::uv_tcp_init(acceptor->loop(), listener_);
    ASSERT(r == 0, "uv_tcp_init failed: [{}] {}", r, ::uv_strerror(r));

    listener_->data = this;
    r = ::uv_tcp_bind(listener_, (const sockaddr*)&addr_, 0);
    ASSERT(r == 0, "uv_tcp_bind failed: [{}] {}", r, ::uv_strerror(r));
        
    r = ::uv_fileno((uv_handle_t*)listener_, &fd_);
    ASSERT(r == 0 && fd_ >= 0, "uv_fileno failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_listen((uv_stream_t*)listener_, SOMAXCONN, cb);
    ASSERT(r == 0, "uv_listen failed: [{}] {}", r, ::uv_strerror(r));

    xINFO("start listener: {}", to_string());
    acceptor_ = acceptor;
}



SOCKET
xq::net::Listener::accept() noexcept {
    uv_tcp_t* s = (uv_tcp_t*)xq::utils::malloc(sizeof(uv_tcp_t), true);
    ::uv_tcp_init(acceptor_->loop(), s);

    int r = ::uv_accept((uv_stream_t*)listener_, (uv_stream_t*)s);
    if (r != 0) {
        ::uv_close((uv_handle_t*)s, [](uv_handle_t* handle) {
            xq::utils::free(handle);
        });
        return INVALID_SOCKET;
    }

    uv_os_fd_t fd;
    ::uv_fileno((uv_handle_t*)s, &fd);
    SOCKET cfd = ::dup(fd);

    ::uv_close((uv_handle_t*)s, [](uv_handle_t* handle) {
        xq::utils::free(handle);
    });

    return cfd;
}