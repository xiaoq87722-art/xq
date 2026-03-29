#ifndef __XQ_NET_ACCEPTOR_HPP__
#define __XQ_NET_ACCEPTOR_HPP__


#include <vector>
#include <thread>
#include "xq/utils/memory.hpp"
#include "xq/net/session.hpp"
#include "xq/net/listener.hpp"
#include "xq/net/conf.hpp"


namespace xq {
namespace net {


class Acceptor {
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    Acceptor(Acceptor&&) = delete;
    Acceptor& operator=(Acceptor&&) = delete;


public:
    static Acceptor*
    instance() noexcept {
        static Acceptor acceptor;
        return &acceptor;
    }


    ~Acceptor() noexcept;


    io_uring* uring() noexcept {
        return &uring_;
    }


    // RingEvent::Pool&
    // ev_pool() noexcept {
    //     return ev_pool_;
    // }


    void
    run(const std::initializer_list<const char*>& endpoints) noexcept;


    void
    stop() noexcept {
        int state_running = STATE_RUNNING;
        state_.compare_exchange_strong(state_running, STATE_STOPPING);
    }


private:
    explicit Acceptor() noexcept {
        sess_slots_.resize(
            std::thread::hardware_concurrency() * Conf::instance()->per_max_conn() * 15 / 10
        );
    }


    io_uring uring_ {};

    /** acceptor 状态 */
    std::atomic<int> state_ { STATE_STOPPED };

    /** 所有 session 槽 */
    std::vector<Session*> sess_slots_;

    // RingEvent::Pool ev_pool_;
}; // class Acceptor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_ACCEPTOR_HPP__