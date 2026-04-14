#include "xq/net/session.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


void
xq::net::Session::on_write(uv_write_t* req, int status) noexcept {
    xq::utils::free(req->data);
    xq::utils::free(req);
}


void
xq::net::Session::send(const Reactor* r, char* data, size_t len) noexcept {
    auto buf = (char*)xq::utils::malloc(len);
    ::memcpy(buf, data, len);

    if (r != reactor_) {
        OnSendArg* arg = (OnSendArg*)xq::utils::malloc(sizeof(OnSendArg));
        arg->s = this;
        arg->data = buf;
        arg->len = len;

        reactor_->post({ EVENT_ON_SEND, arg });
        return;
    }

    uv_write_t* req = (uv_write_t*)xq::utils::malloc(sizeof(uv_write_t));
    req->data = buf;
    uv_buf_t wrbuf = uv_buf_init(buf, len);
    ::uv_write(req, (uv_stream_t*)uv_, &wrbuf, 1, on_write);
}