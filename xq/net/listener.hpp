#ifndef __XQ_NET_LISTENER_HPP_
#define __XQ_NET_LISTENER_HPP_


#include "xq/net/event.hpp"
#include "xq/utils/log.hpp"


namespace xq::net {


class Acceptor;


/** 监听器 */
class Listener {
public:
    /**
     * @brief 构造函数
     *
     * @param le       IListenerEvent 实例
     * @param endpoint 监听地址
     * @param port     监听端口
     */
    Listener(IListnerEvent* le, const char* endpoint, uint16_t port) noexcept
        : le_(le) {
        ASSERT(endpoint && port > 0, "params is invalid");
        host_ = std::format("{}:{}", endpoint, port);
        arg_.type = EpollArg::Type::Listener;
        arg_.data = this;
    }


    /**
     * @brief 析构函数
     */
    ~Listener() noexcept
    {}


    /**
     * @brief 格式化
     */
    std::string
    to_string() const noexcept {
        return host_;
    }


    /**
     * @brief socket fd
     */
    SOCKET
    fd() const noexcept {
        return fd_;
    }


    /**
     * @brief 事件对象
     */
    IListnerEvent*
    service() noexcept {
        return le_;
    }


    /**
     * @brief epoll 参数
     */
    EpollArg*
    arg() noexcept {
        return &arg_;
    }


    /**
     * @brief 所属的 Acceptor
     */
    Acceptor*
    acceptor() noexcept {
        return acceptor_;
    }


    /**
     * @brief 启动监听
     */
    void
    start(Acceptor* acceptor) noexcept;


    /**
     * @brief 停止监听
     */
    void
    stop() noexcept {
        if (fd_ != INVALID_SOCKET) {
            le_->on_stop(this);
            ::close(fd_);
            fd_ = INVALID_SOCKET;
        }
    }


    /**
     * @brief 接收连接 socket
     */
    SOCKET
    accept() noexcept {
        return ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    }


private:
    /** 监听 socket fd */
    SOCKET fd_ { INVALID_SOCKET };

    /** 事件实例 */
    IListnerEvent* le_ { nullptr };

    /** 所属 acceptor */
    Acceptor* acceptor_ { nullptr };

    /** 用于 epoll_event.data.ptr */
    EpollArg arg_ {};

    /** 监听地址 */
    std::string host_ {};
}; // class Listener;


} // namespace xq::net


#endif // __XQ_NET_LISTENER_HPP_