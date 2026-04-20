#ifndef __XQ_NET_CONN_RECVER_HPP__
#define __XQ_NET_CONN_RECVER_HPP__


#include "xq/net/conn_worker.hpp"
#include "xq/net/event.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


class ConnRecver {
    ConnRecver(const ConnRecver&) = delete;
    ConnRecver& operator=(const ConnRecver&) = delete;
    ConnRecver(ConnRecver&&) = delete;
    ConnRecver& operator=(ConnRecver&&) = delete;


public:
    ConnRecver() noexcept {}


    ~ConnRecver() noexcept {}


    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }

    void
    run(std::initializer_list<Conn::Ptr> conns) noexcept;


    void
    stop() noexcept {
        int state_running = STATE_RUNNING;
        if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
            constexpr uint64_t stop = 1;
            ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
        }
    }
    

private:
    void
    conn_handle(EpollArg* ea) noexcept;


    void
    event_handle(EpollArg* _) noexcept;


    void
    add_conn(Conn::Ptr conn) noexcept;


    void
    remove_conn(SOCKET fd) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    time_t tnow_ { 0 };
    std::atomic<int> state_ { STATE_STOPPED };
    std::vector<ConnWorker*> workers_ {};
    std::unordered_map<SOCKET, Conn::Ptr> conns_;
}; // class Connector;


} // namespace xq::net


#endif // __XQ_NET_CONN_RECVER_HPP__