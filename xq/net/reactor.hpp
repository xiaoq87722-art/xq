#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include "xq/net/write_buf.hpp"
#include "xq/net/event.hpp"
#include "xq/net/conf.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/mpsc.hpp"
#include <atomic>


namespace xq::net {


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


    void
    add_session(SOCKET fd, Session* s) noexcept {
        sessions_[fd] = s;
    }


    Session*
    get_session(SOCKET fd) noexcept {
        return sessions_[fd];
    }


    WriteBuf::Pool&
    write_buf_pool() noexcept {
        return wbuf_pool_;
    }


    void
    remove_session(SOCKET fd) noexcept {
        sessions_.erase(fd);
    }


private:
    static void
    event_handle(uv_async_t* handle) noexcept;


    static void
    on_read_alloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) noexcept;


    static void
    on_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) noexcept;


    void
    on_accept(void* arg) noexcept;


    void
    on_stopped() noexcept;


    void
    on_send(void* arg) noexcept;


    uv_loop_t* loop_ { nullptr };
    uv_async_t* async_ { nullptr };
    std::atomic<int> state_ { STATE_STOPPED };
    xq::utils::MPSC<Event> pending_fds_ { 8, 1024 };
    std::unordered_map<SOCKET, Session*> sessions_;
    WriteBuf::Pool wbuf_pool_;
}; // class Reactor;


} // namespace xq::net


#endif // __XQ_NET_REACTOR_HPP__