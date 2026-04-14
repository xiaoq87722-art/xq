#ifndef __XQ_NET_ACCEPTOR_HPP__
#define __XQ_NET_ACCEPTOR_HPP__


#include <vector>
#include <thread>
#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"
#include "xq/utils/memory.hpp"
#include "xq/net/conf.hpp"


namespace xq::net {


/**
 * @brief Acceptor 工作线程, 用于处理监听套接字的连接
 */
class Acceptor {
    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    Acceptor(Acceptor&&) = delete;
    Acceptor& operator=(Acceptor&&) = delete;


public:
    static constexpr int MAX_CONN = 100000;


    static Acceptor*
    instance() noexcept {
        static Acceptor acceptor;
        return &acceptor;
    }


    ~Acceptor() noexcept
    {}


    void
    run(const std::vector<Listener*>& listeners) noexcept;


    void
    stop() noexcept;


    bool
    running() const noexcept {
        return state_ == STATE_RUNNING;
    }


    uv_tcp_t*
    (&sessions() noexcept)[100000] {
        return sessions_;
    }


    uv_loop_t*
    loop() noexcept {
        return loop_;
    }


private:
    static void
    on_accept(uv_poll_t* server, int status, int events) noexcept;


    explicit Acceptor() noexcept {}


    uv_loop_t* loop_ { nullptr };
    std::atomic<int> state_ { STATE_STOPPED };
    std::vector<Reactor*> reactors_;
    std::vector<std::thread> threads_;
    uv_tcp_t* sessions_[MAX_CONN] { nullptr };
}; // class Acceptor;


} // namespace xq::net


#endif // __XQ_NET_ACCEPTOR_HPP__