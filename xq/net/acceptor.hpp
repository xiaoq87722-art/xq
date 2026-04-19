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
    static constexpr int MAX_CONN = 100000;


    static Acceptor*
    instance() noexcept {
        static Acceptor acceptor;
        return &acceptor;
    }


    ~Acceptor() noexcept
    {}


    bool
    running() const noexcept {
        return state_ == STATE_RUNNING;
    }


    Session*
    (&sessions() noexcept)[MAX_CONN] {
        return sessions_;
    }


    SOCKET
    epfd() noexcept {
        return epfd_;
    }


    void
    run(const std::vector<Listener*>& listeners) noexcept;


    void
    stop() noexcept;


    int
    broadcast(const char* data, size_t len) noexcept;


private:
    explicit Acceptor() noexcept {}


    void
    event_handle(EpollArg* ea) noexcept;


    void
    listener_handle(EpollArg* ea) noexcept;


    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    std::atomic<int> state_ { STATE_STOPPED };
    std::vector<Reactor*> reactors_;
    std::vector<std::thread> threads_;
    Session* sessions_[MAX_CONN] { nullptr };
}; // class Acceptor;


} // namespace xq::net


#endif // __XQ_NET_ACCEPTOR_HPP__