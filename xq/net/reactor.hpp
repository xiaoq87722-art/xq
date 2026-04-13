#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include "xq/net/net.in.h"
#include "xq/net/event.hpp"
#include "xq/net/conf.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/spsc.hpp"
#include <atomic>
#include <uv.h>


namespace xq {
namespace net {


class Reactor {
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


public:
    static void
    on_write(uv_write_t* req, int status) noexcept;


    explicit Reactor() noexcept {}


    ~Reactor() noexcept {}


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == STATE_RUNNING;
    }

    
    void
    run();

    
    void
    stop();


    void
    post(Event ev);


private:
    static void
    event_handle(uv_async_t* handle) noexcept;


    static void
    on_read_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) noexcept;


    static void
    on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) noexcept;


    void
    add_session(SOCKET fd, Session* s) noexcept {
        sessions_[fd] = s;
    }


    Session*
    get_session(SOCKET fd) noexcept {
        return sessions_[fd];
    }


    void
    remove_session(SOCKET fd) noexcept {
        sessions_.erase(fd);
    }


    void
    on_accept(void* arg) noexcept;


    void
    on_stopped() noexcept;


    uv_loop_t* loop_ { nullptr };
    uv_async_t* async_ { nullptr };
    std::atomic<int> state_ { STATE_STOPPED };
    xq::utils::SPSC<Event, 1024> pending_fds_;
    std::map<SOCKET, Session*> sessions_;
}; // class Reactor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_REACTOR_HPP__