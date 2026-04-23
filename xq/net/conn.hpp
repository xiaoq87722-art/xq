#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/event.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Connector;


/**
 * @brief 连接对象
 */
class Conn {
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    Conn(Conn&&) = delete;
    Conn& operator=(Conn&&) = delete;


public:
    /** 构造函数 */
    Conn() noexcept 
        : ea_(EpollArg::Type::Conn, this) 
    {}


    /** 析构函数 */
    ~Conn() noexcept;


    /** 
     * @brief 初始化 conn
     * 
     * @param host: 服务端地址, ip:port
     *        r:    所属 connector
     */
    void 
    init(const char* host, Connector* r) noexcept;


    /**
     * @brief 释放 conn 资源
     */
    void
    release() noexcept {
        bool expected = true;
        if (valid_.compare_exchange_strong(expected, false)) {
            sbuf_.clear();
            SOCKET fd = fd_;
            fd_ = INVALID_SOCKET;
            ::close(fd);
        }
    }


    bool
    valid() const noexcept {
        return valid_.load(std::memory_order_acquire);
    }


    SOCKET
    fd() const noexcept {
        return fd_;
    }


    Connector*
    recver() noexcept {
        return connector_;
    }


    EpollArg*
    ea() noexcept {
        return &ea_;
    }


    int
    recv(void* data, size_t dlen) noexcept;


    int
    send(const char* data, size_t dlen) noexcept;


private:
    /** 
     * @brief 是否处理等待发送状态.
     *        当writev 方法返回 EAGAIN | EWOULDBLOCK 的时候, 需要等带 epoll EPOLLOUT 事件
     */
    bool wait_out_ { false };

    /** socket fd */
    SOCKET fd_ { INVALID_SOCKET };

    /** 所属 connector */
    Connector* connector_ { nullptr };

    /** 是否处理发送中 */
    std::atomic<bool> sending_ { false };

    /** conn 对象是否有效 */
    std::atomic<bool> valid_ { false };

    /** epoll 参数 */
    EpollArg ea_;

    /** 发送缓冲区 */
    xq::utils::RingBuf sbuf_ { WBUF_MAX };

    /** 发送队列 */
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 4096 };
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__