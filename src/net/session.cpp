#include "xq/net/session.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


void
xq::net::Session::on_write(uv_write_t* req, int status) noexcept {
    auto wb = (WriteBuf*)req->data;
    wb->session->reactor()->write_buf_pool().put(wb);
}


void
xq::net::Session::send(const Reactor* r, char* data, size_t len) noexcept {
    if (r != reactor_) {
        OnSendArg* arg = (OnSendArg*)xq::utils::malloc(sizeof(OnSendArg));
        ::memcpy(arg->data, data, len);
        arg->s = this;
        arg->len = len;

        reactor_->post({ EVENT_ON_SEND, arg });
        return;
    }

    auto wb = reactor_->write_buf_pool().get();
    ::memcpy(wb->data, data, len);
    wb->session = this;

    wb->req.data = wb;
    uv_buf_t wrbuf = uv_buf_init(wb->data, len);
    ::uv_write(&wb->req, (uv_stream_t*)uv_, &wrbuf, 1, on_write);
}