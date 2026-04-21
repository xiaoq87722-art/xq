#ifndef __XQ_NET_CONN_RECVER_HPP__
#define __XQ_NET_CONN_RECVER_HPP__


#include "xq/net/event.hpp"
#include "xq/net/processor.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


class Connector {
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;


public:
    Connector() noexcept {}


    ~Connector() noexcept {}


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
    std::vector<Processor*> workers_ {};
    std::unordered_map<SOCKET, Conn::Ptr> conns_;
}; // class Connector;


} // namespace xq::net


#endif // __XQ_NET_CONN_RECVER_HPP__