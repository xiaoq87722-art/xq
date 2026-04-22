#ifndef __XQ_NET_CONN_RECVER_HPP__
#define __XQ_NET_CONN_RECVER_HPP__


#include "xq/net/event.hpp"
#include "xq/net/processor.hpp"
#include "xq/net/sender.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


class Connector {
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;


public:
    Connector(IConnEvent* s) noexcept
        : service_(s)
    {}


    ~Connector() noexcept {}


    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    Sender*
    sender() noexcept {
        return &sender_;
    }


    IConnEvent*
    service() noexcept {
        return service_;
    }


    void
    run() noexcept;


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
    event_handle() noexcept;


    void
    add_conn(Conn* conn) noexcept;


    void
    remove_conn(SOCKET fd) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    time_t tnow_ { 0 };
    IConnEvent* service_ { nullptr };
    std::atomic<int> state_ { STATE_STOPPED };
    Sender sender_;
    std::vector<Processor*> procs_ {};
    Conn* conns_[1024];
}; // class Connector;


} // namespace xq::net


#endif // __XQ_NET_CONN_RECVER_HPP__