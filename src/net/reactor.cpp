#include "xq/net/reactor.hpp"
#include "xq/net/session.hpp"
#include "xq/net/acceptor.hpp"


static void
on_reactor_stopped(uv_handle_t* handle) noexcept {
    if (handle->type == UV_TCP && handle->data) {
        xq::net::Session* s = (xq::net::Session*)handle->data;
        s->listener()->service()->on_disconnected(s);
        s->reactor()->remove_session(s->fd());
        s->release();
    } else {
        xq::utils::free(handle);
    }
}


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
    sessions_.clear();
    wbuf_pool_.clear();

    Event evs[64];
    size_t n;
    while ((n = pending_fds_.try_dequeue_bulk(evs, 64)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            auto cmd = evs[i].first;
            if (cmd == EVENT_ON_SEND || cmd == EVENT_ON_STOP) {
                xq::utils::free(evs[i].second);
            }
        }
    }

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

    Event evs[64];
    size_t n;
    while ((n = reactor->pending_fds_.try_dequeue_bulk(evs, 64)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            auto& ev = evs[i];
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
}


void
xq::net::Reactor::on_accept(void* params) noexcept {
    auto arg = (OnAcceptArg*)params;
    auto fd = arg->fd;
    uv_tcp_t* c = Acceptor::instance()->sessions()[fd];
    if (!c) {
        Acceptor::instance()->sessions()[fd] = c = (uv_tcp_t*)xq::utils::malloc(sizeof(uv_tcp_t), true);
        c->data = (Session*)xq::utils::malloc(sizeof(Session), true);
    }

    auto s = (Session*)(c->data);
    s->init(c, arg->l, this);

    int r = ::uv_tcp_init(loop_, c);
    ASSERT(r == 0, "uv_tcp_init failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_tcp_open(c, fd);
    ASSERT(r == 0, "uv_tcp_open failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_read_start((uv_stream_t*)c, on_read_alloc, on_read);
    ASSERT(r == 0, "uv_read_start failed: [{}] {}", r, ::uv_strerror(r));

    add_session(fd, s);
    arg->l->service()->on_connected(s);
    xq::utils::free(arg);
}


void
xq::net::Reactor::on_stopped() noexcept {
    int state_running = STATE_RUNNING;
    if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
        ::uv_walk(loop_, [](uv_handle_t* handle, void*) {
            if (!::uv_is_closing(handle)) {
                ::uv_close(handle, on_reactor_stopped);
            }
        }, nullptr);
    }
}


void
xq::net::Reactor::on_send(void* arg) noexcept {
    auto params = (OnSendArg*)arg;
    auto uv = params->s->uv();

    if (uv && !::uv_is_closing((uv_handle_t*)uv)) {
        auto wb = write_buf_pool().get();
        ::memcpy(wb->data, params->data, params->len);
        wb->reactor = this;

        wb->req.data = wb;
        uv_buf_t wrbuf = uv_buf_init(wb->data, params->len);
        ::uv_write(&wb->req, (uv_stream_t*)params->s->uv(), &wrbuf, 1, Session::on_write);
    }
    
    xq::utils::free(params);
}


void
xq::net::Reactor::on_read_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) noexcept {
    Session* s = (Session*)handle->data;
    buf->base = s->rbuf();
    buf->len = RBUF_MAX;
}


void
xq::net::Reactor::on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) noexcept {
    Session* s = (Session*)stream->data;
    
    if (nread > 0) {
        Context ctx { .reactor = s->reactor(), .session = s };
        s->listener()->service()->on_data(&ctx, buf->base, nread);
    } else if (nread < 0) {
        if (nread != UV_EOF) {
            xERROR("on_read failed: [{}] {}", nread, ::uv_strerror(nread));
        }

        ::uv_close((uv_handle_t*)stream, [](uv_handle_t* handle) {
            Session* s = (Session*)handle->data;
            s->listener()->service()->on_disconnected(s);
            s->reactor()->remove_session(s->fd());
            s->release();
        });
    }
}