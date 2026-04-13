#include "xq/net/session.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


static void
on_write(uv_write_t* req, int status) noexcept {
    xq::utils::free(req);
}


void
xq::net::Session::send(char* data, size_t len) noexcept {
    uv_write_t* req = (uv_write_t*)xq::utils::malloc(sizeof(uv_write_t));
    uv_buf_t wrbuf = uv_buf_init(data, len);
    ::uv_write(req, (uv_stream_t*)uv_, &wrbuf, 1, on_write);
}