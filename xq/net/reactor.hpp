#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include "xq/net/net.in.h"
#include "xq/net/session.hpp"
#include "xq/net/conf.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <liburing.h>


namespace xq {
namespace net {


class Reactor {
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


public:
    typedef Reactor* Ptr;


    static Ptr create() noexcept {
        return new Reactor;
    }


    ~Reactor() noexcept {}


    io_uring*
    uring() noexcept {
        return &uring_;
    }


    bool
    running() const {
        return state_ == STATE_RUNNING;
    }


    void
    run() noexcept;


    pthread_t
    thread_id() const {
        return tid_;
    }


    /**
     * @brief 通知 Reactor 对象 RingEvent. 当前 Reactor/Acceptor 线程1 通知 Reactor 线程2
     * 
     * @param ct_uring: current thread io_uring 当前线程的 io_uring
     */
    void
    notify(io_uring* ct_uring, xq::net::RingEvent* ev, bool auto_submit = false) noexcept;


    void
    add_pending_notif() noexcept {
        ++pending_notifs_;
    }


private:
    explicit Reactor() noexcept {
        sessions_.resize(
            std::thread::hardware_concurrency() * Conf::instance()->per_max_conn() * 15 / 10,
            nullptr
        );
    }


    void
    on_r_accept(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    on_s_recv(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    on_s_send(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    on_r_stop(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    on_r_timer(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    on_r_send(io_uring_cqe* cqe, RingEvent* ev) noexcept;


    void
    add_session(Session* s) noexcept;


    void
    remove_session(Session* s) noexcept;


    /** io_uring */
    io_uring uring_ {};

    /** 当前时间, 当前时间不是UNIX时间戳而是系统运行时间 */
    uint64_t tnow_ { 0 };

    /** reactor 线程ID */
    pthread_t tid_ { 0 };

    /** 状态值 */
    std::atomic<int> state_ { STATE_STOPPED };
    
    /** 会话 */
    std::vector<Session*> sessions_;

    /** 停止阶段: 仍存活的 session 数 */
    int pending_sessions_ { 0 };

    /** 停止阶段: 已提交 sendmsg_zc 但 NOTIF CQE 尚未到达的数量 */
    int pending_notifs_ { 0 };

    /** buf ring 缓冲区 */
    io_uring_buf_ring* br_ { nullptr };
    std::vector<uint8_t*> brbufs_;
}; // class Reactor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_REACTOR_HPP__