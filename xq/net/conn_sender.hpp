#ifndef __XQ_NET_CONN_SENDER_HPP__
#define __XQ_NET_CONN_SENDER_HPP__


#include "xq/net/event.hpp"
#include "xq/utils/mpsc.hpp"


namespace xq::net {


class ConnSender {


public:
    ConnSender() noexcept
    {}


    ~ConnSender() noexcept
    {}


    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;
        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&ConnSender::start, this);
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


private:
    void
    start() noexcept;


    void
    event_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    std::atomic<int> state_ { STATE_STOPPED };
    std::thread t_;
    xq::utils::MPSC<Event> evque_ { 4, 4096 };
}; // class ConnSender;


} // namespace xq::net


#endif // __XQ_NET_CONN_SENDER_HPP__