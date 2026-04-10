#include "xq/net/reactor.hpp"
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
    ::uv_async_init(loop_, async_, on_new_fd);

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
xq::net::Reactor::on_new_fd(uv_async_t* handle) noexcept {
    auto reactor = (xq::net::Reactor*)handle->loop->data;

    SOCKET fd;
    int r;
    Event ev;
    while (reactor->pending_fds_.dequeue(ev)) {
        switch (ev.first) {
            case EVENT_ON_ACCEPT:
                reactor->on_accept((SOCKET)(uintptr_t)ev.second);
                break;

            case EVENT_ON_STOP:
                reactor->on_stopped();
                break;
        }
    }
}


void
xq::net::Reactor::on_accept(SOCKET fd) noexcept {
    xINFO("{} 连接成功", fd);
    uv_tcp_t* s = Acceptor::instance()->sessions()[fd];
    if (!s) {
        Acceptor::instance()->sessions()[fd] = s = new uv_tcp_t;
    }

    s->data = (void*)(uintptr_t)fd;

    int r = ::uv_tcp_init(loop_, s);
    ASSERT(r == 0, "uv_tcp_init failed: [{}] {}", r, ::uv_strerror(r));

    r = ::uv_tcp_open(s, fd);
    ASSERT(r == 0, "uv_tcp_open failed: [{}] {}", r, ::uv_strerror(r));

    ::uv_close((uv_handle_t*)s, [](uv_handle_t* handle) {
        uv_os_sock_t cfd = (uv_os_sock_t)(uintptr_t)handle->data;
        xINFO("{} 连接断开", cfd);
        ::memset(handle, 0, sizeof(uv_tcp_t));
    });
    // add_session(fd, s);
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