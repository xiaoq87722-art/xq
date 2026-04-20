#ifndef __XQ_NET_CONN_WORKER_HPP__
#define __XQ_NET_CONN_WORKER_HPP__


#include <atomic>


#include "xq/net/conn.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


class ConnWorker {
    ConnWorker(const ConnWorker&) = delete;
    ConnWorker& operator=(const ConnWorker&) = delete;
    ConnWorker(ConnWorker&&) = delete;
    ConnWorker& operator=(ConnWorker&&) = delete;


    struct Element {
        Conn* conn;
        void* data;
        size_t len;
    };


public:
    ConnWorker() noexcept
    {}


    ~ConnWorker() noexcept
    {}


    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;
        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&ConnWorker::start, this);
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
        ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
    }


private:
    void
    start() noexcept;


    void
    event_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    time_t tnow_ { 0 };
    std::atomic<int> state_ { STATE_STOPPED };
    std::thread t_;
    xq::utils::SPSC<Element> evque_ {};
}; // class Worker;


} // namespace xq::net



#endif // __XQ_NET_CONN_WORKER_HPP__