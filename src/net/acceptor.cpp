#include "xq/net/acceptor.hpp"
#include "xq/net/reactor.hpp"
#include "xq/utils/signal.h"
#include "xq/net/net.in.h"
#include <immintrin.h>
#include <cstring>
#include "xq/net/conf.hpp"


static void
signal_handle(uv_signal_t* handle, int) {
    auto* a = (xq::net::Acceptor*)handle->loop->data;
    a->stop();
}


static xq::net::Reactor*
next_reactor(std::vector<xq::net::Reactor*>& reactors) noexcept {
    static size_t index = 0;
    return reactors[index++ % reactors.size()];
}


void
xq::net::Acceptor::on_new_connection(uv_stream_t* server, int status) noexcept {
    if (status < 0) {
        xERROR("on_new_connection failed: [{}] {}", status, ::uv_strerror(status));
        return;
    }

    auto l = (xq::net::Listener*)server->data;
    xINFO("{} on_new_connection", l->to_string());

    auto cfd = l->accept();
    if (cfd == INVALID_SOCKET) {
        return;
    }

    next_reactor(Acceptor::instance()->reactors_)->post({ EVENT_ON_ACCEPT, cfd });
}


void
xq::net::Acceptor::run(const std::vector<Listener*>& listeners) noexcept {
    int stopped_state = STATE_STOPPED;

    do {
        if (!state_.compare_exchange_strong(stopped_state, STATE_STARTING)) {
            break;
        }

        loop_ = (uv_loop_t*)xq::utils::malloc(sizeof(uv_loop_t), true);
        int r = ::uv_loop_init(loop_);
        ASSERT(r == 0, "uv_loop_init failed: [{}] {}", r, ::uv_strerror(r));
        loop_->data = this;

        uv_signal_t sigint, sigterm;
        ::uv_signal_init(loop_, &sigint);
        ::uv_signal_init(loop_, &sigterm);

        ::uv_signal_start(&sigint, signal_handle, SIGINT);
        ::uv_signal_start(&sigterm, signal_handle, SIGTERM);

        uint32_t nr = std::thread::hardware_concurrency();
        if (nr > 2) {
            nr -= 2;
        }

        for (uint32_t i = 0; i < nr; ++i) {
            Reactor* r = new Reactor();
            reactors_.emplace_back(r);
            threads_.emplace_back(std::thread(std::bind(&xq::net::Reactor::run, r)));

            while(!r->running()) {
                _mm_pause();
            }
        }

        for (auto l: listeners) {
            l->start(this, on_new_connection);
            listeners_.emplace_back(l->get_listener());
        }

        state_.exchange(STATE_RUNNING);
        r = ::uv_run(loop_, UV_RUN_DEFAULT);
        ASSERT(r == 0, "uv_run has {} active left", r);

        for (auto& l: listeners) {
            l->stop();
        }

        for (uint32_t i = 0; i < nr; ++i) {
            Reactor* r = reactors_[i];
            r->stop();
            auto& t = threads_[i];
            if (t.joinable()) {
                t.join();
            }
            delete r;
        }

        r = ::uv_loop_close(loop_);
        ASSERT(r == 0, "uv_loop_close failed: [{}] {}", r, ::uv_strerror(r));
        xq::utils::free(loop_);

        reactors_.clear();
        listeners_.clear();
        threads_.clear();
    } while(0);

    int state_stopping = STATE_STOPPING;
    state_.compare_exchange_strong(state_stopping, STATE_STOPPED);
}


void
xq::net::Acceptor::stop() noexcept {
    int state_running = STATE_RUNNING;
    if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
        ::uv_walk(loop_, [](uv_handle_t* handle, void*) {
            if (!::uv_is_closing(handle)) {
                ::uv_close(handle, nullptr);
            }
        }, nullptr);
    }
}