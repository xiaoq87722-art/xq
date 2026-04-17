#include "xq/net/session.hpp"
#include "xq/net/event.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


int
xq::net::Session::recv() noexcept {
    char* p = rbuf_;
    ssize_t nleft = RBUF_MAX;

    while (1) {
        int n = ::recv(fd_, p, nleft, 0);
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("recv failed: [{}] {}", err, ::strerror(err));
                return -err;
            }

            return RBUF_MAX - nleft;
        } else if (n == 0) {
            return 0;
        } else {
            p += n;
            nleft -= n;
        }
    }
}


void
xq::net::Session::send(const Reactor* r, char* data, size_t len) noexcept {
    xINFO("{} => {}", to_string(), std::string_view(data, len));
    // if (r != reactor_) {
    //     OnSendArg* arg = (OnSendArg*)xq::utils::malloc(sizeof(OnSendArg) + len);
    //     ::memcpy(arg->data, data, len);
    //     arg->s = this;
    //     arg->len = len;

    //     reactor_->post({ EVENT_ON_SEND, arg });
    //     return;
    // }

    // auto wb = reactor_->write_buf_pool().get();
    // ::memcpy(wb->data, data, len);
    // wb->reactor = reactor_;

    // wb->req.data = wb;
    // uv_buf_t wrbuf = uv_buf_init(wb->data, len);
    // ::uv_write(&wb->req, (uv_stream_t*)uv_, &wrbuf, 1, on_write);
}