#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


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
    explicit Reactor() noexcept {}


    ~Reactor() noexcept {}


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == STATE_RUNNING;
    }


    SOCKET
    epfd() const noexcept {
        return epfd_;
    }

    
    void
    run();

    
    void
    stop();


    void
    post(Event ev) noexcept;


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


private:
    void
    on_accept(void* arg) noexcept;


    void
    on_stopped() noexcept;


    void
    on_send(void* arg) noexcept;


    void
    custom_handle(EpollArg* ea) noexcept;


    void
    session_recv_handle(EpollArg* ea) noexcept;


    void
    session_send_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    std::atomic<int> state_ { STATE_STOPPED };
    xq::utils::MPSC<Event> evque_ { 8, 1024 };
    std::unordered_map<SOCKET, Session*> sessions_;
}; // class Reactor;


} // namespace xq::net


#endif // __XQ_NET_REACTOR_HPP__