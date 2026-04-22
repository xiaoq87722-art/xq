#ifndef __XQ_NET_CONN_SENDER_HPP__
#define __XQ_NET_CONN_SENDER_HPP__


#include "xq/net/event.hpp"
#include "xq/utils/mpsc.hpp"


namespace xq::net {


class Sender {


public:
    Sender() noexcept
    {}


    ~Sender() noexcept
    {}


    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    SOCKET
    epfd() const noexcept {
        return epfd_;
    }

    std::thread::id
    tid() const noexcept {
        return t_.get_id();
    }


    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;
        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&Sender::start, this);
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
    post(Event ev) noexcept {
        if (running()) {
            ASSERT(evque_.enqueue(std::move(ev)), "队列已满");

            bool expected = false;
            if (processing_.compare_exchange_strong(expected, true)) {
                constexpr uint64_t event = 1;
                ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
            }
        }
    }


private:
    void
    start() noexcept;


    void
    event_handle() noexcept;


    void
    conn_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    std::atomic<int> state_ { STATE_STOPPED };
    std::atomic<bool> processing_ { false };
    std::thread t_;
    xq::utils::MPSC<Event> evque_ { 4, 4096 };
}; // class ConnSender;


} // namespace xq::net


#endif // __XQ_NET_CONN_SENDER_HPP__