#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include <atomic>
#include <unordered_map>


#include "xq/net/event.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/mpsc.hpp"


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
        return state_.load() == STATE_RUNNING;
    }


    SOCKET
    epfd() const noexcept {
        return epfd_;
    }


    time_t
    tnow() const noexcept {
        return tnow_;
    }

    
    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;

        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&Reactor::start, this);
        }
    }

    
    void
    stop() noexcept {
        int state_running = STATE_RUNNING;
        if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
            constexpr uint64_t stop = 1;
            ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
        }
    }


    void
    join() noexcept {
        if (t_.joinable()) {
            t_.join();
        }
    }


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
        auto itr = sessions_.find(fd);
        if (itr != sessions_.end()) {
            auto s = itr->second;
            s->listener()->service()->on_disconnected(s);
            ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, s->fd(), nullptr), "epoll_ctl failed");
            sessions_.erase(itr);
            s->release();
        }
    }


private:
    void
    start() noexcept;


    void
    event_handle(EpollArg* ea) noexcept;


    void
    session_recv_handle(EpollArg* ea) noexcept;


    void
    session_send_handle(EpollArg* ea) noexcept;


    void
    on_accept(void* params) noexcept;


    void
    on_stopped() noexcept;


    void
    on_send(void* params) noexcept;


    void
    on_broadcast(void* params) noexcept;


    void
    timer_handle() noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    time_t tnow_ { 0 };
    std::atomic<int> state_ { STATE_STOPPED };
    std::atomic<bool> processing_ { false };
    std::thread t_ {};
    xq::utils::MPSC<Event> evque_ { 8, 1024 };
    std::unordered_map<SOCKET, Session*> sessions_;
}; // class Reactor;


} // namespace xq::net


#endif // __XQ_NET_REACTOR_HPP__