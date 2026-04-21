#ifndef __XQ_NET_CONN_WORKER_HPP__
#define __XQ_NET_CONN_WORKER_HPP__


#include <atomic>


#include "xq/net/conn.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


class Processor {
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(Processor&&) = delete;


    struct Element {
        Conn::Ptr conn;
        void* data;
        int len;
    };


public:
    Processor() noexcept
    {}


    ~Processor() noexcept
    {}


    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    bool
    processing() const noexcept {
        return processing_.load();
    }


    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;
        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&Processor::start, this);
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
    post(Element e) noexcept {
        if (!running()) {
            return;
        }

        constexpr uint64_t event = 1;
        ASSERT(evque_.enqueue(std::move(e)), "队列已满");

        bool expected = false;
        if (processing_.compare_exchange_strong(expected, true)) {
            ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
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
    std::atomic<bool> processing_ { false };
    std::thread t_;
    xq::utils::SPSC<Element> evque_ {};
}; // class Worker;


} // namespace xq::net


#endif // __XQ_NET_CONN_WORKER_HPP__