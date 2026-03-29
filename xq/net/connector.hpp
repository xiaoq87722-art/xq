#ifndef __XQ_NET_CONNECTOR_HPP__
#define __XQ_NET_CONNECTOR_HPP__


#include "xq/net/conn.hpp"
#include <map>


namespace xq {
namespace net {


class Connector {


public:
    static Connector*
    instance() {
        static Connector c;
        return &c;
    }


    ~Connector() {}


    io_uring* uring() {
        return &uring_;
    }


    void run(const std::initializer_list<const char*>& hosts) noexcept;


    void stop() noexcept {
        int state_running = STATE_RUNNING;
        state_.compare_exchange_strong(state_running, STATE_STOPPING);
    }


    RingEvent::Pool& ev_pool() noexcept {
        return ev_pool_;
    }


private:
    explicit Connector() {}


    int on_conn(io_uring_cqe* cqe, RingEvent* ev);
    int on_timer(io_uring_cqe* cqe, RingEvent* ev);
    int on_recv(io_uring_cqe* cqe, RingEvent* ev);


    std::atomic<int>             state_ { STATE_STOPPED };
    io_uring                     uring_ {};
    std::vector<uint8_t*>        brbufs_;
    std::map<std::string, Conn*> conns_;
    RingEvent::Pool              ev_pool_;
}; // class Connector;


} // namespace net
} // namespace xq


#endif // __XQ_NET_CONNECTOR_HPP__