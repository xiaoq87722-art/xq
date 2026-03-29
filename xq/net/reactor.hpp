#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include "xq/net/net.in.h"
#include "xq/net/session.hpp"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <liburing.h>


namespace xq {
namespace net {


class Reactor {
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


public:
    explicit Reactor() noexcept {}

    
    ~Reactor() noexcept {}


    io_uring*
    uring() noexcept {
        return &uring_;
    }


    bool
    running() const {
        return state_ == STATE_RUNNING;
    }


    uint64_t
    loaded() const {
        return loaded_;
    }


    void
    run() noexcept;


    pthread_t
    thread_id() const {
        return tid_;
    }


    RingEvent::Pool&
    ev_pool() noexcept {
        return ev_pool_;
    }


    /**
     * @brief 通知 Reactor 对象 RingEvent. 当前 Reactor/Acceptor 线程1 通知 Reactor 线程2
     * 
     * @param ct_uring: current thread io_uring 当前线程的 io_uring
     */
    void
    notify(io_uring* ct_uring, xq::net::RingEvent* ev, bool auto_submit = false) noexcept;


private:
    int on_s_accept(io_uring_cqe* cqe, RingEvent* ev) noexcept;
    int on_s_read(io_uring_cqe* cqe, RingEvent* ev) noexcept;
    int on_s_send(io_uring_cqe* cqe, RingEvent* ev) noexcept;
    int on_r_stop(io_uring_cqe* cqe, RingEvent* ev) noexcept;
    int on_r_timer(io_uring_cqe* cqe, RingEvent* ev) noexcept;
    int on_r_send(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    io_uring uring_ {};

    /** reactor 负载值 */
    std::atomic<uint64_t> loaded_ { 0 };

    /** 当前时间, 当前时间不是UNIX时间戳而是系统运行时间 */
    uint64_t tnow_ { 0 };

    /** reactor 线程ID */
    pthread_t tid_ { 0 };

    /** 状态值 */
    std::atomic<int> state_ { STATE_STOPPED };
    
    /** 会话 */
    std::unordered_map<SOCKET, Session*> sessions_;

    /** buf ring 缓冲区 */
    io_uring_buf_ring* br_ { nullptr };
    std::vector<uint8_t*> brbufs_;

    RingEvent::Pool ev_pool_;
}; // class Reactor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_REACTOR_HPP__