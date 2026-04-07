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


/**
 * @brief Acceptor 工作线程, 用于处理监听套接字的连接
 */
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


    io_uring*
    uring() noexcept {
        return &uring_;
    }


    bool
    running() const noexcept {
        return state_ == STATE_RUNNING;
    }


    Session*
    get_session(SOCKET fd) noexcept {
        return sslots_[fd];
    }
    

    void
    run(std::vector<Listener*>& listeners) noexcept;


    void
    stop() noexcept {
        int state_running = STATE_RUNNING;
        state_.compare_exchange_strong(state_running, STATE_STOPPING);
    }


private:
    explicit Acceptor() noexcept {
        sslots_.resize(
            std::thread::hardware_concurrency() * Conf::instance()->per_max_conn() * 15 / 10,
            nullptr
        );
    }


    io_uring uring_ {};

    /** acceptor 状态 */
    std::atomic<int> state_ { STATE_STOPPED };

    /** 所有 session 槽 */
    std::vector<Session*> sslots_;
}; // class Acceptor;


} // namespace net
} // namespace xq


#endif // __XQ_NET_ACCEPTOR_HPP__