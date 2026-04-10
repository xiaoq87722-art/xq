#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include "xq/net/net.in.h"
#include "xq/net/event.hpp"
#include "xq/net/conf.hpp"
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
    on_new_fd(uv_async_t* handle) noexcept;


    void
    add_session(SOCKET fd, uv_tcp_t* s) noexcept {
        xINFO("{} 连接成功", fd);
        sessions_[fd] = s;
    }


    uv_tcp_t*
    get_session(SOCKET fd) noexcept {
        return sessions_[fd];
    }


    void
    remove_session(SOCKET fd) noexcept {
        sessions_.erase(fd);
    }


    void
    on_accept(SOCKET fd) noexcept;


    void
    on_stopped() noexcept;


    uv_loop_t* loop_ { nullptr };
    uv_async_t* async_ { nullptr };
    std::atomic<int> state_ { STATE_STOPPED };
    xq::utils::SPSC<Event, 1024> pending_fds_;
    std::map<SOCKET, uv_tcp_t*> sessions_;
}; // class Reactor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_REACTOR_HPP__