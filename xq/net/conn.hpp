#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/event.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Connector;
class Processor;


/**
 * @brief 连接对象
 */
class Conn {
    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;
    Conn(Conn&&) = delete;
    Conn& operator=(Conn&&) = delete;

    friend class Processor;
    friend class Connector;


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
     * @param host 服务端地址, ip:port
     * @param r    所属 connector
     */
    void 
    init(const char* host, Connector* r) noexcept;


    /**
     * @brief 释放 conn 资源.
     *        流程: CAS 关门 -> 等 in-flight writer 归 0 -> 独占清理 (含 sque_.clear).
     */
    void
    release() noexcept {
        // Step 1, 关门
        bool expected = true;
        if (!valid_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
            return;  // 已 released, 幂等
        }

        // Step 2, 自旋等跨线程 writer 退出 (窗口极短: 几个原子操作)
        while (active_senders_.load(std::memory_order_acquire) != 0) {
            ::_mm_pause();
        }

        // Step 3, 独占清理
        sbuf_.clear();
        sque_.clear(xq::utils::SendBuf::clear);
        proc_ = nullptr;
        SOCKET fd = fd_;
        fd_ = INVALID_SOCKET;
        ::close(fd);
    }


    std::string
    to_string() const noexcept {
        return std::format("[{}]:{}", fd_, host_);
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


    /**
     * @brief 发送数据. 支持任意线程调用 (典型: Processor 线程).
     *        - 同线程 (Sender): 直接 drain + writev.
     *        - 跨线程: 由 active_senders_ + valid_ 保护, 无 UAR 风险.
     *        返回 >=0 已发送字节数; <0 表示错误或 conn 已失效.
     */
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

    Processor* proc_ { nullptr };

    /** 是否处理发送中 */
    std::atomic<bool> sending_ { false };

    /**
     * @brief conn 对象是否有效.
     *        init() 末尾 store(true) 发布, release() 开头 CAS true->false 关门.
     *        跨线程 send 必须在 active_senders_ 保护下再次校验此 flag.
     */
    std::atomic<bool> valid_ { false };

    /**
     * @brief 跨线程 send 的 in-flight 计数.
     *        writer 进入前 ++, 退出前 --. release 在 CAS valid_ 后自旋等归 0 再独占清理.
     *        配合 valid_ 解决池复用 UAR race.
     */
    std::atomic<uint32_t> active_senders_ { 0 };

    /** epoll 参数 */
    EpollArg ea_;

    std::string host_;

    /**
     * @brief 发送缓冲区.
     *        容量上限为性能优化的硬边界, 正常路径不应打满.
     *        若 sbuf_.write 触发 ASSERT, 说明业务写入速率 > kernel 消费速率且积压超过 WBUF_MAX, 需调大 WBUF_MAX.
     */
    xq::utils::RingBuf sbuf_ { WBUF_MAX };

    /**
     * @brief 跨线程发送队列 (MPSC). Processor 线程 send 走此通路, Sender 同线程 send 绕过.
     *        若 enqueue ASSERT 触发, 说明跨线程峰值突发超过容量, 需调大 per_shard_size.
     */
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 4096 };
}; // class Conn;


} // namespace xq::net


#endif // __XQ_NET_CONN_HPP__