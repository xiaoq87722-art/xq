#include "xq/net/reactor.hpp"
#include "xq/net/session.hpp"
#include "xq/net/acceptor.hpp"


void
xq::net::Reactor::run() {
    int stopped_state = STATE_STOPPED;
    if (!state_.compare_exchange_strong(stopped_state, STATE_STARTING)) {
        return;
    }

    loop_ = (uv_loop_t*)xq::utils::malloc(sizeof(uv_loop_t), true);
    loop_->data = this;
    int r = ::uv_loop_init(loop_);
    ASSERT(r == 0, "uv_loop_init failed: [{}] {}", r, ::uv_strerror(r));

    async_ = (uv_async_t*)xq::utils::malloc(sizeof(uv_async_t), true);
    ::uv_async_init(loop_, async_, event_handle);

    state_.exchange(STATE_RUNNING);
    r = ::uv_run(loop_, UV_RUN_DEFAULT);
    ASSERT(r == 0, "uv_run has {} active left", r);

    r = ::uv_loop_close(loop_);
    ASSERT(r == 0, "uv_loop_close failed: [{}] {}", r, ::uv_strerror(r));

    xq::utils::free(loop_);
    state_.exchange(STATE_STOPPED);
}


void
xq::net::Reactor::stop() {
    ASSERT(pending_fds_.enqueue({ EVENT_ON_STOP, 0 }), "pending_fds_ 队列已满");
    ::uv_async_send(async_);
}


void
xq::net::Reactor::post(Event ev) {
    ASSERT(pending_fds_.enqueue(std::move(ev)), "pending_fds_ 队列已满");
    ::uv_async_send(async_);
}


void
xq::net::Reactor::event_handle(uv_async_t* handle) noexcept {
    auto reactor = (xq::net::Reactor*)handle->loop->data;

    Event ev;
    while (reactor->pending_fds_.dequeue(ev)) {
        switch (ev.first) {
            case EVENT_ON_ACCEPT:
                reactor->on_accept(ev.second);
                break;

            case EVENT_ON_STOP:
                reactor->on_stopped();
                break;

            case EVENT_ON_SEND:
                reactor->on_send(ev.second);
                break;

            default:
                break;
        }
    }
}


void
xq::net::Reactor::on_accept(void* arg) noexcept {
    auto params = (OnAcceptArg*)arg;
    auto fd = params->fd;
    uv_tcp_t* c = Acceptor::instance()->sessions()[fd];
    if (!c) {
        Acceptor::instance()->sessions()[fd] = c = new uv_tcp_t;
    }

    int r = ::uv_tcp_init(loop_, c);
    ASSERT(r == 0, "uv_tcp_init failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_tcp_open(c, fd);
    ASSERT(r == 0, "uv_tcp_open failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_read_start((uv_stream_t*)c, on_read_alloc, on_read);
    ASSERT(r == 0, "uv_read_start failed: [{}] {}", r, ::uv_strerror(r));

    Session* s = new Session(c, params->l, this);
    c->data = s;
    add_session(fd, s);
    params->l->service()->on_connected(s);
    delete params;
}


void
xq::net::Reactor::on_stopped() noexcept {
    int state_running = STATE_RUNNING;
    if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
        ::uv_walk(loop_, [](uv_handle_t* handle, void*) {
            if (!::uv_is_closing(handle)) {
                ::uv_close(handle, [](uv_handle_t* handle) {
                    xq::utils::free(handle);
                });
            }
        }, nullptr);
    }
}


void
xq::net::Reactor::on_send(void* arg) noexcept {
    auto params = (OnSendArg*)arg;
    params->s->send(this, params->data, params->len);
    delete params;
}


void
xq::net::Reactor::on_read_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) noexcept {
    buf->base = (char*)xq::utils::malloc(suggested_size);
    buf->len = suggested_size;
}


void
xq::net::Reactor::on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) noexcept {
    Session* s = (Session*)stream->data;
    
    if (nread > 0) {
        Context ctx { .reactor = s->reactor(), .session = s };
        s->listener()->service()->on_data(&ctx, buf->base, nread);
    } else if (nread == UV_EOF) {
        ::uv_close((uv_handle_t*)stream, [](uv_handle_t* handle) {
            Session* s = (Session*)handle->data;
            s->listener()->service()->on_disconnected(s);
            s->reactor()->remove_session(s->fd());
            xq::utils::free(s->uv());
            delete s;
        });
    } else if (nread < 0) {
        xINFO("[{}] error: [{}] {}", s->fd(), (int)nread, ::uv_strerror((int)nread));
        ::uv_close((uv_handle_t*)stream, [](uv_handle_t* handle) {
            Session* s = (Session*)handle->data;
            s->reactor()->remove_session(s->fd());
            xq::utils::free(s->uv());
            delete s;
        });
    }

    if (buf->base) {
        xq::utils::free(buf->base);
    }
}