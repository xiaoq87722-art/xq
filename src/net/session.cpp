#include "xq/net/session.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


static void
on_write(uv_write_t* req, int status) noexcept {
    xq::utils::free(req);
}


void
xq::net::Session::send(const Reactor* r, char* data, size_t len) noexcept {
    if (r != reactor_) {
        OnSendArg* arg = new OnSendArg{
            .s = this,
            .data = data,
            .len = len
        };

        arg->s = this;
        arg->data = data;
        reactor_->post({ EVENT_ON_SEND, arg });
        return;
    }

    uv_write_t* req = (uv_write_t*)xq::utils::malloc(sizeof(uv_write_t));
    uv_buf_t wrbuf = uv_buf_init(data, len);
    ::uv_write(req, (uv_stream_t*)uv_, &wrbuf, 1, on_write);
}