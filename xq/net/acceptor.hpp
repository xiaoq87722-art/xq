#ifndef __XQ_NET_ACCEPTOR_HPP__
#define __XQ_NET_ACCEPTOR_HPP__


#include <thread>
#include <vector>


#include "xq/net/listener.hpp"
#include "xq/net/reactor.hpp"


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
    /** 最大连接数 */
    static constexpr int MAX_CONN = 100000;


    static Acceptor*
    instance() noexcept {
        static Acceptor acceptor;
        return &acceptor;
    }


    ~Acceptor() noexcept
    {}


    /** 是否处于运行中 */
    bool
    running() const noexcept {
        return state_ == STATE_RUNNING;
    }


    /** 获取 session */
    Session*
    (&sessions() noexcept)[MAX_CONN] {
        return sessions_;
    }


    /** 运行 */
    void
    run(const std::vector<Listener*>& listeners) noexcept;


    /** 停止 */
    void
    stop() noexcept;


    /** 广播 */
    int
    broadcast(const char* data, size_t len) noexcept;


private:
    explicit Acceptor() noexcept
    {}


    void
    event_handle(EpollArg* ea) noexcept;


    void
    listener_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    std::atomic<int> state_ { STATE_STOPPED };
    std::vector<Reactor*> reactors_;
    Session* sessions_[MAX_CONN] { nullptr };
}; // class Acceptor;


} // namespace xq::net


#endif // __XQ_NET_ACCEPTOR_HPP__